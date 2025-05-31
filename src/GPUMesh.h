#pragma once

#include <d3d12.h>
#include <framework/mesh.h>
#include "WireframeDraw.h"
#include <filesystem>
#include <wrl/client.h>

#include "DefaultBuffer.h"

using namespace Microsoft::WRL;

struct MeshLoadingException final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class GPUMesh {
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    UINT nIndices;

    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;

    DefaultBuffer<void> blasBuffer;
    DefaultBuffer<void> tlasBuffer;

    //WireframeDraw wfDraw;

    static void waitOnGPU(const ComPtr<ID3D12Device>& device, const ComPtr<ID3D12CommandQueue>& commandQueue);

    void createBLAS(
        const ComPtr<ID3D12Device5>& device5,
        const ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
        UINT nAABB,
        const ComPtr<ID3D12Resource>& outputBuffer,
        DefaultBuffer<void>& scratchBuffer
    );
    void createTLAS(
        const ComPtr<ID3D12Device5>& device5,
        const ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
        DefaultBuffer<void>& scratchBuffer,
        UploadBuffer<D3D12_RAYTRACING_INSTANCE_DESC>& instanceBuffer
    );

public:
    Mesh cpuMesh;

    GPUMesh(const Mesh& cpuMesh, const ComPtr<ID3D12Device>& device);
    GPUMesh(const GPUMesh&) = delete;
    GPUMesh(GPUMesh&& other) noexcept;

    GPUMesh& operator=(const GPUMesh&) = delete;
    GPUMesh& operator=(GPUMesh&& other) noexcept;

    static std::vector<GPUMesh> loadGLTFMeshGPU(const std::filesystem::path& animFilePath, const std::filesystem::path& umeshFilePath, const ComPtr<ID3D12Device>& device);

    void drawWireframe(const glm::mat4& mvp, float displacementScale) const;

    [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW getVertexBufferView() const;
    [[nodiscard]] D3D12_INDEX_BUFFER_VIEW getIndexBufferView() const;
    [[nodiscard]] UINT getIndexCount() const;
};
