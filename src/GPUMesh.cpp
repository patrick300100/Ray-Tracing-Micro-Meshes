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

GPUMesh::GPUMesh(const Mesh& cpuMesh, const ComPtr<ID3D12Device5>& device): cpuMesh(cpuMesh) {
    /**
     * This code checks whether projected points lie inside the triangle.
     * So if we have a micro vertex, displace it, and project the displaced micro vertex onto the triangle's plane,
     * we check whether the projected point is still inside the triangle
     *
     * In generally not, but maybe it doesn't happen so often that artifacts are barely visible?
     */
    // for(const auto& t : cpuMesh.triangles) {
    //     const auto v0 = cpuMesh.vertices[t.baseVertexIndices.x];
    //     const auto v1 = cpuMesh.vertices[t.baseVertexIndices.y];
    //     const auto v2 = cpuMesh.vertices[t.baseVertexIndices.z];
    //
    //     // const auto v0D = v0.position + v0.displacement;
    //     // const auto v1D = v1.position + v1.displacement;
    //     // const auto v2D = v2.position + v2.displacement;
    //
    //     const auto v0D = v0.position + t.uVertices[0].displacement;
    //     const auto v1D = v1.position + t.uVertices[36].displacement;
    //     const auto v2D = v2.position + t.uVertices[44].displacement;
    //
    //     for(const auto uv : t.uVertices) {
    //         const auto bc = Triangle::computeBaryCoords(v0D, v1D, v2D, uv.position + uv.displacement);
    //
    //         if(isPointInTriangle(uv.position + uv.displacement, v0D, v1D, v2D)) {
    //             int tsdf = 5;
    //         } else {
    //             std::cout << "Base position\n";
    //             printVertex(v0.position);
    //             printVertex(v1.position);
    //             printVertex(v2.position);
    //             std::cout << "---\n";
    //
    //             printVertex(bc);
    //             printVertex(v0D);
    //             printVertex(v1D);
    //             printVertex(v2D);
    //
    //             int red = 3;
    //         }
    //     }
    // }







    // int zeroCount = 0, notZeroCount = 0;
    // for(const auto t1 : cpuMesh.triangles) {
    //     const auto v0 = cpuMesh.vertices[t1.baseVertexIndices.x];
    //     const auto v1 = cpuMesh.vertices[t1.baseVertexIndices.y];
    //     const auto v2 = cpuMesh.vertices[t1.baseVertexIndices.z];
    //
    //     glm::vec3 e1 = v1.position - v0.position;
    //     glm::vec3 e2 = v2.position - v0.position;
    //     glm::vec3 N = normalize(cross(e1, e2)); // plane normal
    //
    //     glm::vec3 T = normalize(e1);
    //     glm::vec3 B = normalize(cross(N, T));
    //
    //     Plane p = {T, B, N};
    //
    //     Vertex2D v0Proj = projectVertexTo2D(v0.position + v0.displacement, T, B, N, v0.position);
    //     Vertex2D v1Proj = projectVertexTo2D(v1.position + v1.displacement, T, B, N, v0.position);
    //     Vertex2D v2Proj = projectVertexTo2D(v2.position + v2.displacement, T, B, N, v0.position);
    //     Triangle2D tr = {v0Proj, v1Proj, v2Proj};
    //
    //     /*
    //      * Compute triangular prism (early opt-out). First get max and min displacement
    //      */
    //     //TODO do not hardcode the '+ 45'. Only for testing purposes
    //     float maxDisplacement = -100;
    //     float minDisplacement = 100;
    //     for(const auto [p, d] : t1.uVertices) {
    //         float displ = glm::dot(d, N);
    //         maxDisplacement = std::max(maxDisplacement, displ);
    //         minDisplacement = std::min(minDisplacement, displ);
    //     }
    //
    //     if(minDisplacement == 0.0f) {
    //         std::cout << "is 0\n";
    //         zeroCount++;
    //     } else notZeroCount++;
    // }

    for(const auto& triangle : cpuMesh.triangles) {
        std::ranges::transform(triangle.uVertices, std::back_inserter(displacements), [](const uVertex& uv) { return uv.displacement; });

        const auto v0 = cpuMesh.vertices[triangle.baseVertexIndices.x];
        const auto v1 = cpuMesh.vertices[triangle.baseVertexIndices.y];
        const auto v2 = cpuMesh.vertices[triangle.baseVertexIndices.z];

        glm::vec3 e1 = v1.position - v0.position;
        glm::vec3 e2 = v2.position - v0.position;
        glm::vec3 N = glm::normalize(cross(e1, e2)); // plane normal

        glm::vec3 T = normalize(e1);
        glm::vec3 B = glm::normalize(cross(N, T));

        std::ranges::transform(triangle.uVertices, std::back_inserter(planePositions), [&](const uVertex& uv) { return projectTo2DNumber2(uv.position + uv.displacement, T, B, N, v0.position); });
    }

    std::cout << "ray = Ray(Point({" << O.x << ", " << O.y << ", " << O.z << "}), Vector(Point({" << D.x << ", " << D.y << ", " << D.z << "})))" << std::endl;

    const auto tr = cpuMesh.triangles[7380];

    int nRows = 5;
    int displacementOffset = 110700;

    const auto v0 = cpuMesh.vertices[tr.baseVertexIndices.x];
    const auto v1 = cpuMesh.vertices[tr.baseVertexIndices.y];
    const auto v2 = cpuMesh.vertices[tr.baseVertexIndices.z];
    glm::vec2 v0GridCoordinate = glm::vec2(0,0);
    glm::vec2 v1GridCoordinate = glm::vec2(nRows - 1, 0);
    glm::vec2 v2GridCoordinate = glm::vec2(nRows - 1, nRows - 1);

    theV0Pos = v0.position;
    std::cout << "v0 = Point({" << v0.position.x << ", " << v0.position.y << ", " << v0.position.z << "})" << std::endl;
    std::cout << "v1 = Point({" << v1.position.x << ", " << v1.position.y << ", " << v1.position.z << "})" << std::endl;
    std::cout << "v2 = Point({" << v2.position.x << ", " << v2.position.y << ", " << v2.position.z << "})" << std::endl;

    /*
     * Creation of 2D triangle where vertices are displaced
     */
    glm::vec3 e1 = v1.position - v0.position;
    glm::vec3 e2 = v2.position - v0.position;
    glm::vec3 N = normalize(cross(e1, e2)); // plane normal

    glm::vec3 T = normalize(e1);
    glm::vec3 B = normalize(cross(N, T));

    Plane p = {T, B, N};

    Vertex2D v0Proj = projectVertexTo2D(v0.position + getDisplacement(v0GridCoordinate, displacementOffset), T, B, N, v0.position, v0GridCoordinate);
    Vertex2D v1Proj = projectVertexTo2D(v1.position + getDisplacement(v1GridCoordinate, displacementOffset), T, B, N, v0.position, v1GridCoordinate);
    Vertex2D v2Proj = projectVertexTo2D(v2.position + getDisplacement(v2GridCoordinate, displacementOffset), T, B, N, v0.position, v2GridCoordinate);
    Triangle2D t = {v0Proj, v1Proj, v2Proj};

    /*
     * Creation of 2D ray
     */
    glm::vec3 O_proj = O - dot(O - v0.position, N) * N;
    glm::vec3 D_proj = normalize(D - dot(D, N) * N);

    glm::vec2 rayOrigin2D = projectTo2D(O_proj, T, B, v0.position);
    glm::vec2 rayDir2D = normalize(projectTo2D(O_proj + D_proj, T, B, v0.position) - rayOrigin2D);
    Ray2D ray = {rayOrigin2D, rayDir2D};

    std::cout << "ray2D = Ray(Point({" << O_proj.x << ", " << O_proj.y << ", " << O_proj.z << "}), Vector(Point({" << D_proj.x << ", " << D_proj.y << ", " << D_proj.z << "})))" << std::endl;

    /*
     * Creation of 2D triangle where vertices are NOT displaced
     */
    Triangle2D tUndisplaced = {
        projectVertexTo2D(v0.position, T, B, N, v0.position, v0GridCoordinate),
        projectVertexTo2D(v1.position, T, B, N, v0.position, v1GridCoordinate),
        projectVertexTo2D(v2.position, T, B, N, v0.position, v2GridCoordinate)
    };

    TriangleDAUD ts = {t, tUndisplaced};

    std::cout << "Execute({" << std::endl;
    int counter = 0;
    for(int i = 0; i < tr.uVertices.size(); i++) {
        const auto uv = tr.uVertices[i];
        const auto disPos = uv.position + uv.displacement;
        std::cout << "\"uv" << counter << "D = Point({" << disPos.x << ", " << disPos.y << ", " << disPos.z << "})\"," << std::endl;
        counter++;
    }
    for(int i = 0; i < tr.uFaces.size(); i++) {
        const auto ut = tr.uFaces[i];
        std::cout << "\"Polygon(uv" << ut.x << "D, uv" << ut.y << "D, uv" << ut.z << "D)\"";

        if(i < tr.uFaces.size() - 1) std::cout << "," << std::endl;
        else std::cout << "\n";
    }
    std::cout << "})" << std::endl;

    //rayTraceMMTriangle(ts, ray, p, v0.position, displacementOffset);








    //Prepare buffer data for use in compute shader
    std::vector<SimpleTriangle> triangles;
    std::vector<uVertex> uVertices;
    for(const auto& t : cpuMesh.triangles) {
        SimpleTriangle st{};

        st.uVerticesStart = uVertices.size();
        st.uVerticesCount = t.uVertices.size();

        triangles.push_back(st);
        uVertices.insert(uVertices.end(), t.uVertices.begin(), t.uVertices.end());
    }

    CommandSender cw(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
    cw.reset();

    //Create buffers for on the GPU and upload data to those buffers
    DefaultBuffer<uVertex> microVertexBuffer(device, uVertices.size(), D3D12_RESOURCE_STATE_COPY_DEST);
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
    createBLAS(device, cw.getCommandList(), triangles.size(), outputBuffer.getBuffer(), scratchBufferBLAS);

    DefaultBuffer<void> scratchBufferTLAS;
    UploadBuffer<D3D12_RAYTRACING_INSTANCE_DESC> instanceBuffer(device, 1);
    createTLAS(device, cw.getCommandList(), scratchBufferTLAS, instanceBuffer);

    cw.execute(device);







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
    AABBs(std::move(other.AABBs)),
    blasBuffer(std::move(other.blasBuffer)),
    tlasBuffer(std::move(other.tlasBuffer)),
    cpuMesh(std::move(other.cpuMesh))
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
        AABBs = std::move(other.AABBs);
        nIndices = other.nIndices;
        blasBuffer = std::move(other.blasBuffer);
        tlasBuffer = std::move(other.tlasBuffer);
    }

    return *this;
}

std::vector<GPUMesh> GPUMesh::loadGLTFMeshGPU(const std::filesystem::path& animFilePath, const std::filesystem::path& umeshFilePath, const Microsoft::WRL::ComPtr<ID3D12Device5>& device) {
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

ComPtr<ID3D12Resource> GPUMesh::getTLASBuffer() const {
    return tlasBuffer.getBuffer();
}

std::vector<D3D12_RAYTRACING_AABB> GPUMesh::getAABBs() const {
    return AABBs;
}

