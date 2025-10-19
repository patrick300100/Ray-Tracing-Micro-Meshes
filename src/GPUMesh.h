#pragma once

#include <d3d12.h>
#include <framework/mesh.h>
#include <filesystem>
#include <wrl/client.h>

#include "DefaultBuffer.h"

using namespace Microsoft::WRL;

class GPUMesh {
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    UINT nVertices, nIndices;

    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;

    std::vector<D3D12_RAYTRACING_AABB> AABBs;
    DefaultBuffer<void> blasBuffer;
    DefaultBuffer<void> tlasBuffer;

    //Create BLAS with AABBs
    void createBLAS(
        const ComPtr<ID3D12Device5>& device5,
        const ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
        UINT nAABB,
        const ComPtr<ID3D12Resource>& AABBBuffer,
        DefaultBuffer<void>& scratchBuffer
    );

    //Create BLAS with triangles
    void createTriangleBLAS(
        const ComPtr<ID3D12Device5>& device5,
        const ComPtr<ID3D12GraphicsCommandList4>& cmdList4,
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

    GPUMesh(const Mesh& cpuMesh, const ComPtr<ID3D12Device5>& device, bool runTessellated);
    GPUMesh(const GPUMesh&) = delete;
    GPUMesh(GPUMesh&& other) noexcept;

    GPUMesh& operator=(const GPUMesh&) = delete;
    GPUMesh& operator=(GPUMesh&& other) noexcept;

    static std::vector<GPUMesh> loadGLTFMeshGPU(const std::filesystem::path& umeshFilePath, const ComPtr<ID3D12Device5>& device, bool runTessellated);

    [[nodiscard]] ComPtr<ID3D12Resource> getTLASBuffer() const;
    [[nodiscard]]ComPtr<ID3D12Resource> getVertexBuffer() const;
    [[nodiscard]] ComPtr<ID3D12Resource> getIndexBuffer() const;
};
