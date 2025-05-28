#pragma once

#include <d3d12.h>
#include <framework/mesh.h>
#include "WireframeDraw.h"
#include <filesystem>
#include <wrl/client.h>

struct MeshLoadingException final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class GPUMesh {
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    UINT nIndices;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

    //WireframeDraw wfDraw;

public:
    Mesh cpuMesh;

    GPUMesh(const Mesh& cpuMesh, const Microsoft::WRL::ComPtr<ID3D12Device>& device);
    GPUMesh(const GPUMesh&) = delete;
    GPUMesh(GPUMesh&& other) noexcept;

    GPUMesh& operator=(const GPUMesh&) = delete;
    GPUMesh& operator=(GPUMesh&& other) noexcept;

    static std::vector<GPUMesh> loadGLTFMeshGPU(const std::filesystem::path& animFilePath, const std::filesystem::path& umeshFilePath, const Microsoft::WRL::ComPtr<ID3D12Device>& device);

    void drawWireframe(const glm::mat4& mvp, float displacementScale) const;

    [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW getVertexBufferView() const;
    [[nodiscard]] D3D12_INDEX_BUFFER_VIEW getIndexBufferView() const;
    [[nodiscard]] UINT getIndexCount() const;
};
