#include "GPUMesh.h"

#include <windows.h>
#include <framework/disable_all_warnings.h>
#include <framework/TinyGLTFLoader.h>

#include "CommandSender.h"
#include "ComputeShader.h"
#include "DefaultBuffer.h"
#include "ReadbackBuffer.h"
#include "UploadBuffer.h"

DISABLE_WARNINGS_PUSH()
#include <glm/gtc/type_ptr.inl>
#include "mesh_io_gltf.h"
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <iostream>
#include <vector>
#include <algorithm>

struct SimpleTriangle {
    glm::uvec3 baseVertexIndices;

    unsigned int uVerticesStart;
    unsigned int uVerticesCount;
};

struct SimpleVertex {
    glm::vec3 position;
    glm::vec3 displacement;
};

GPUMesh::GPUMesh(const Mesh& cpuMesh, const ComPtr<ID3D12Device>& device): cpuMesh(cpuMesh) {
    //Convert mesh vertices to SimpleVertex
    std::vector<SimpleVertex> vertices;
    vertices.reserve(cpuMesh.vertices.size());
    std::ranges::transform(cpuMesh.vertices, std::back_inserter(vertices), [](const Vertex& v) { return SimpleVertex{v.position, v.displacement}; });

    //Prepare other data for use in compute shader
    std::vector<SimpleTriangle> triangles;
    std::vector<uVertex> uVertices;
    for(const auto& t : cpuMesh.triangles) {
        SimpleTriangle st{};

        st.baseVertexIndices = t.baseVertexIndices;
        st.uVerticesStart = uVertices.size();
        st.uVerticesCount = t.uVertices.size();

        triangles.push_back(st);
        uVertices.insert(uVertices.end(), t.uVertices.begin(), t.uVertices.end());
    }

    CommandSender cw(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);

    //Create buffers for on the GPU and upload data to those buffers
    DefaultBuffer<SimpleVertex> baseVertexBuffer(device, vertices.size(), D3D12_RESOURCE_STATE_COPY_DEST);
    baseVertexBuffer.upload(vertices, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    DefaultBuffer<uVertex> microVertexBuffer(device, uVertices.size(), D3D12_RESOURCE_STATE_COPY_DEST);
    microVertexBuffer.upload(uVertices, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    DefaultBuffer<SimpleTriangle> triangleBuffer(device, triangles.size(), D3D12_RESOURCE_STATE_COPY_DEST);
    triangleBuffer.upload(triangles, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    DefaultBuffer<D3D12_RAYTRACING_AABB> outputBuffer(device, triangles.size(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    //Execute, wait and reset for later use
    cw.execute();
    waitOnGPU(device, cw.getCommandQueue());
    cw.reset();

    //Create our compute shader and execute it (computing an AABB around each triangle)
    ComputeShader cs(RESOURCE_ROOT L"shaders/skinning.hlsl", device, {{SRV, 3}, {UAV, 1}}, sizeof(D3D12_RAYTRACING_AABB) * triangles.size());
    cs.createSRV<SimpleVertex>(baseVertexBuffer.getBuffer());
    cs.createSRV<uVertex>(microVertexBuffer.getBuffer());
    cs.createSRV<SimpleTriangle>(triangleBuffer.getBuffer());
    cs.createUAV<D3D12_RAYTRACING_AABB>(outputBuffer.getBuffer());

    auto AABBs = cs.execute<D3D12_RAYTRACING_AABB>(outputBuffer.getBuffer(), triangles.size());

    //Get an 'upgraded' version of our device and command list
    ComPtr<ID3D12Device5> device5;
    device->QueryInterface(IID_PPV_ARGS(&device5));

    ComPtr<ID3D12GraphicsCommandList4> cmdList4;
    cw.getCommandList()->QueryInterface(IID_PPV_ARGS(&cmdList4));

    //Create BLAS AND TLAS
    DefaultBuffer<void> scratchBufferBLAS;
    createBLAS(device5, cmdList4, triangles.size(), outputBuffer.getBuffer(), scratchBufferBLAS);

    DefaultBuffer<void> scratchBufferTLAS;
    UploadBuffer<D3D12_RAYTRACING_INSTANCE_DESC> instanceBuffer(device, 1);
    createTLAS(device5, cmdList4, scratchBufferTLAS, instanceBuffer);

    cw.execute();
    waitOnGPU(device, cw.getCommandQueue());







    const auto [vData, iData] = cpuMesh.allTriangles();

    //Create vertex buffer
    {
        UploadBuffer<Vertex> vBuffer(device, vData.size());
        vBuffer.upload(vData);

        vertexBuffer = std::move(vBuffer.getBuffer());
        vertexBufferView.BufferLocation = vBuffer.getBuffer()->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(Vertex);
        vertexBufferView.SizeInBytes = vData.size() * sizeof(Vertex);
    }

    //Create index buffer
    {
        UploadBuffer<glm::uvec3> iBuffer(device, iData.size());
        iBuffer.upload(iData);

        indexBuffer = std::move(iBuffer.getBuffer());
        indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
        indexBufferView.Format = DXGI_FORMAT_R32_UINT;
        indexBufferView.SizeInBytes = iData.size() * sizeof(glm::uvec3);
    }

    nIndices = 3 * iData.size();
}

GPUMesh::GPUMesh(GPUMesh&& other) noexcept:
    //wfDraw(std::move(other.wfDraw)),
    vertexBufferView(other.vertexBufferView),
    indexBufferView(other.indexBufferView),
    nIndices(other.nIndices),
    vertexBuffer(std::move(other.vertexBuffer)),
    indexBuffer(std::move(other.indexBuffer)),
    cpuMesh(std::move(other.cpuMesh)),
    blasBuffer(std::move(other.blasBuffer)),
    tlasBuffer(std::move(other.tlasBuffer))
{
}

GPUMesh& GPUMesh::operator=(GPUMesh&& other) noexcept {
    if(this != &other) {
        //wfDraw = std::move(other.wfDraw);
        cpuMesh = std::move(other.cpuMesh);
        vertexBufferView = other.vertexBufferView;
        indexBufferView = other.indexBufferView;
        vertexBuffer = std::move(other.vertexBuffer);
        indexBuffer = std::move(other.indexBuffer);
        nIndices = other.nIndices;
        blasBuffer = std::move(other.blasBuffer);
        tlasBuffer = std::move(other.tlasBuffer);
    }

    return *this;
}

std::vector<GPUMesh> GPUMesh::loadGLTFMeshGPU(const std::filesystem::path& animFilePath, const std::filesystem::path& umeshFilePath, const Microsoft::WRL::ComPtr<ID3D12Device>& device) {
    if(!std::filesystem::exists(animFilePath)) throw MeshLoadingException(fmt::format("File {} does not exist", animFilePath.string().c_str()));

    GLTFReadInfo read_micromesh;
    if (!read_gltf(umeshFilePath.string(), read_micromesh)) {
        std::cerr << "Error reading gltf file" << std::endl;
    }

    if (!read_micromesh.has_subdivision_mesh()) {
        std::cerr << "gltf file does not contain micromesh data" << std::endl;
    }

    std::vector<Mesh> subMeshes = TinyGLTFLoader(animFilePath, read_micromesh).toMesh();
    std::vector<GPUMesh> gpuMeshes;
    for (const Mesh& mesh : subMeshes) { gpuMeshes.emplace_back(mesh, device); }

    return gpuMeshes;
}

void GPUMesh::drawWireframe(const glm::mat4& mvp, const float displacementScale) const {
    glUniformMatrix4fv(0, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(1, displacementScale);
    glUniform4fv(2, 1, glm::value_ptr(glm::vec4(0.235f, 0.235f, 0.235f, 1.0f)));

    //wfDraw.drawBaseEdges();

    glUniform4fv(2, 1, glm::value_ptr(glm::vec4(0.435f, 0.435f, 0.435f, 0.5f)));

    //wfDraw.drawMicroEdges();
}

D3D12_VERTEX_BUFFER_VIEW GPUMesh::getVertexBufferView() const {
    return vertexBufferView;
}

D3D12_INDEX_BUFFER_VIEW GPUMesh::getIndexBufferView() const {
    return indexBufferView;
}

UINT GPUMesh::getIndexCount() const {
    return nIndices;
}

void GPUMesh::waitOnGPU(const ComPtr<ID3D12Device>& device, const ComPtr<ID3D12CommandQueue>& commandQueue) {
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 1;
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

    commandQueue->Signal(fence.Get(), fenceValue);
    if(fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);
}

void GPUMesh::createBLAS(
    const ComPtr<ID3D12Device5>& device5,
    const ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
    UINT nAABB,
    const ComPtr<ID3D12Resource>& outputBuffer,
    DefaultBuffer<void>& scratchBuffer)
{
    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometryDesc.AABBs.AABBCount = nAABB;
    geometryDesc.AABBs.AABBs.StartAddress = outputBuffer->GetGPUVirtualAddress();
    geometryDesc.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geometryDesc;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

    scratchBuffer = DefaultBuffer<void>(prebuildInfo.ScratchDataSizeInBytes, device5, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    blasBuffer = DefaultBuffer<void>(prebuildInfo.ResultDataMaxSizeInBytes, device5, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratchBuffer.getBuffer()->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = blasBuffer.getBuffer()->GetGPUVirtualAddress();

    cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = blasBuffer.getBuffer().Get();
    cmdList4->ResourceBarrier(1, &uavBarrier);
}

void GPUMesh::createTLAS(
    const ComPtr<ID3D12Device5>& device5,
    const ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
    DefaultBuffer<void>& scratchBuffer,
    UploadBuffer<D3D12_RAYTRACING_INSTANCE_DESC>& instanceBuffer)
{
    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
    instanceDesc.InstanceID = 0;
    instanceDesc.InstanceContributionToHitGroupIndex = 0;
    instanceDesc.InstanceMask = 0xFF;
    instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1; //Identity matrix for Transform attribute
    instanceDesc.AccelerationStructure = blasBuffer.getBuffer()->GetGPUVirtualAddress();

    instanceBuffer.upload({instanceDesc});

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = 1;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.InstanceDescs = instanceBuffer.getBuffer()->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

    scratchBuffer = DefaultBuffer<void>(prebuildInfo.ScratchDataSizeInBytes, device5, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    tlasBuffer = DefaultBuffer<void>(prebuildInfo.ResultDataMaxSizeInBytes, device5, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratchBuffer.getBuffer()->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = tlasBuffer.getBuffer()->GetGPUVirtualAddress();

    cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = tlasBuffer.getBuffer().Get();
    cmdList4->ResourceBarrier(1, &barrier);
}
