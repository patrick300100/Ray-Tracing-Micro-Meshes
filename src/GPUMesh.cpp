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
    unsigned int uVerticesStart;
    unsigned int uVerticesCount;
};

struct SimpleVertex {
    glm::vec3 position;
    glm::vec3 displacement;
};

GPUMesh::GPUMesh(const Mesh& cpuMesh, const ComPtr<ID3D12Device5>& device, bool runTessellated): cpuMesh(cpuMesh) {
    if(runTessellated) {
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

        nVertices = vData.size();
        nIndices = 3 * iData.size();
    }


    //Prepare buffer data for use in compute shader
    std::vector<SimpleTriangle> triangles;
    std::vector<SimpleVertex> uVertices;
    for(const auto& t : cpuMesh.triangles) {
        SimpleTriangle st{};

        st.uVerticesStart = uVertices.size();
        st.uVerticesCount = t.uVertices.size();

        triangles.push_back(st);
        std::ranges::transform(t.uVertices, std::back_inserter(uVertices), [&](const uVertex& uv) { return SimpleVertex{uv.position, uv.displacement}; });
    }

    CommandSender cw(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
    cw.reset();

    //Create buffers for on the GPU and upload data to those buffers
    DefaultBuffer<SimpleVertex> microVertexBuffer(device, uVertices.size(), D3D12_RESOURCE_STATE_COPY_DEST);
    microVertexBuffer.upload(uVertices, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    DefaultBuffer<SimpleTriangle> triangleBuffer(device, triangles.size(), D3D12_RESOURCE_STATE_COPY_DEST);
    triangleBuffer.upload(triangles, cw.getCommandList(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    DefaultBuffer<D3D12_RAYTRACING_AABB> outputBuffer(device, triangles.size(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    //Execute, wait and reset for later use
    cw.execute(device);
    cw.reset();

    //Create our compute shader and execute it (computing an AABB around each triangle)
    ComputeShader cs(RESOURCE_ROOT L"shaders/createAABBs.hlsl", device, {{SRV, 2}, {UAV, 1}}, sizeof(D3D12_RAYTRACING_AABB) * triangles.size());
    cs.createSRV<uVertex>(microVertexBuffer.getBuffer());
    cs.createSRV<SimpleTriangle>(triangleBuffer.getBuffer());
    cs.createUAV<D3D12_RAYTRACING_AABB>(outputBuffer.getBuffer());

    AABBs = cs.execute<D3D12_RAYTRACING_AABB>(outputBuffer.getBuffer(), triangles.size());

    //Create BLAS AND TLAS
    DefaultBuffer<void> scratchBufferBLAS;
    if(runTessellated) createTriangleBLAS(device, cw.getCommandList(), scratchBufferBLAS);
    else createBLAS(device, cw.getCommandList(), triangles.size(), outputBuffer.getBuffer(), scratchBufferBLAS);

    DefaultBuffer<void> scratchBufferTLAS;
    UploadBuffer<D3D12_RAYTRACING_INSTANCE_DESC> instanceBuffer(device, 1);
    createTLAS(device, cw.getCommandList(), scratchBufferTLAS, instanceBuffer);

    cw.execute(device);
}

GPUMesh::GPUMesh(GPUMesh&& other) noexcept:
    vertexBufferView(other.vertexBufferView),
    indexBufferView(other.indexBufferView),
    nVertices(other.nVertices),
    nIndices(other.nIndices),
    vertexBuffer(std::move(other.vertexBuffer)),
    indexBuffer(std::move(other.indexBuffer)),
    AABBs(std::move(other.AABBs)),
    blasBuffer(std::move(other.blasBuffer)),
    tlasBuffer(std::move(other.tlasBuffer)),
    cpuMesh(std::move(other.cpuMesh))
{
}

GPUMesh& GPUMesh::operator=(GPUMesh&& other) noexcept {
    if(this != &other) {
        cpuMesh = std::move(other.cpuMesh);
        vertexBufferView = other.vertexBufferView;
        indexBufferView = other.indexBufferView;
        vertexBuffer = std::move(other.vertexBuffer);
        indexBuffer = std::move(other.indexBuffer);
        AABBs = std::move(other.AABBs);
        nVertices = other.nVertices;
        nIndices = other.nIndices;
        blasBuffer = std::move(other.blasBuffer);
        tlasBuffer = std::move(other.tlasBuffer);
    }

    return *this;
}

GPUMesh GPUMesh::loadGLTFMeshGPU(const std::filesystem::path& umeshFilePath, const ComPtr<ID3D12Device5>& device, const bool runTessellated) {
    GLTFReadInfo read_micromesh;
    if (!read_gltf(umeshFilePath.string(), read_micromesh)) {
        std::cerr << "Error reading gltf file" << std::endl;
    }

    if (!read_micromesh.has_subdivision_mesh()) {
        std::cerr << "gltf file does not contain micromesh data" << std::endl;
    }

    const Mesh m = TinyGLTFLoader(umeshFilePath, read_micromesh).toMesh();

    return {m, device, runTessellated};
}

void GPUMesh::createBLAS(
    const ComPtr<ID3D12Device5>& device5,
    const ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
    UINT nAABB,
    const ComPtr<ID3D12Resource>& AABBBuffer,
    DefaultBuffer<void>& scratchBuffer)
{
    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometryDesc.AABBs.AABBCount = nAABB;
    geometryDesc.AABBs.AABBs.StartAddress = AABBBuffer->GetGPUVirtualAddress();
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

void GPUMesh::createTriangleBLAS(const ComPtr<ID3D12Device5>& device, const ComPtr<ID3D12GraphicsCommandList4>& cmdList, DefaultBuffer<void>& scratchBuffer) {
    const D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {
        .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
        .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
        .Triangles = {
            .Transform3x4 = 0,
            .IndexFormat = DXGI_FORMAT_R32_UINT,
            .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
            .IndexCount = nIndices,
            .VertexCount = nVertices,
            .IndexBuffer = indexBuffer->GetGPUVirtualAddress(),
            .VertexBuffer = {
                .StartAddress = vertexBuffer->GetGPUVirtualAddress() + offsetof(Vertex, position),
                .StrideInBytes = sizeof(Vertex)
            },
        }
    };

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geometryDesc;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

    scratchBuffer = DefaultBuffer<void>(prebuildInfo.ScratchDataSizeInBytes, device, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    blasBuffer = DefaultBuffer<void>(prebuildInfo.ResultDataMaxSizeInBytes, device, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratchBuffer.getBuffer()->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = blasBuffer.getBuffer()->GetGPUVirtualAddress();

    cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = blasBuffer.getBuffer().Get();
    cmdList->ResourceBarrier(1, &uavBarrier);
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

ComPtr<ID3D12Resource> GPUMesh::getTLASBuffer() const {
    return tlasBuffer.getBuffer();
}

ComPtr<ID3D12Resource> GPUMesh::getVertexBuffer() const {
    return vertexBuffer;
}

ComPtr<ID3D12Resource> GPUMesh::getIndexBuffer() const {
    return indexBuffer;
}
