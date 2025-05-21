#include "GPUMesh.h"

#include <d3dx12.h>
#include <framework/disable_all_warnings.h>
#include <framework/TinyGLTFLoader.h>
#include "framework/CreateBuffer.h"

DISABLE_WARNINGS_PUSH()
#include <glm/gtc/type_ptr.inl>
#include "mesh_io_gltf.h"
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <iostream>
#include <vector>

GPUMesh::GPUMesh(const Mesh& cpuMesh, const Microsoft::WRL::ComPtr<ID3D12Device>& device): cpuMesh(cpuMesh) {
    const auto [vData, iData] = cpuMesh.allTriangles();

    //Create vertex buffer
    {
        CreateBuffer<Vertex> vBuffer(device, vData.size());
        vBuffer.copyDataIntoBuffer(vData);

        vertexBuffer = std::move(vBuffer.getBuffer());
        vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(Vertex);
        vertexBufferView.SizeInBytes = vData.size() * sizeof(Vertex);
    }

    //Create index buffer
    {
        CreateBuffer<glm::uvec3> iBuffer(device, iData.size());
        iBuffer.copyDataIntoBuffer(iData);

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
    cpuMesh(std::move(other.cpuMesh))
{
}

GPUMesh& GPUMesh::operator=(GPUMesh&& other) noexcept {
    if(this != &other) {
        //wfDraw = std::move(other.wfDraw);
        cpuMesh = std::move(other.cpuMesh);
        vertexBufferView = other.vertexBufferView;
        indexBufferView = other.indexBufferView;
        nIndices = other.nIndices;
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
