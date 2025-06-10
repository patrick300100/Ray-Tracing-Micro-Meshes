#pragma once

#include <d3dx12.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <dxcapi.h>
#include <vector>

#include "UploadBuffer.h"
#pragma comment(lib, "dxcompiler.lib")

using namespace Microsoft::WRL;

enum BufferType {
    SRV, //Shader Resource View
    UAV, //Unordered Access View
    CBV //Constant Buffer view
};

class RayTraceShader {
    std::vector<ComPtr<IDxcBlob>> shaders;
    ComPtr<ID3D12Device5> device;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    UINT descriptorSize{};
    ComPtr<ID3D12RootSignature> localRootSignature;

    ComPtr<ID3D12StateObjectProperties> pipelineProps;

    UploadBuffer<void*> rayGenSBTBuffer, missSBTBuffer, hitGroupSBTBuffer;

    enum RootSignatureType {
        GLOBAL,
        LOCAL
    };

    void compileShader(const ComPtr<IDxcCompiler3>& compiler, const ComPtr<IDxcUtils>& utils, const LPCWSTR& shaderFile);
    void initBuffers(const std::vector<std::pair<BufferType, int>>& buffers, RootSignatureType type);

public:
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    ComPtr<ID3D12StateObject> pipelineStateObject;
    ComPtr<ID3D12RootSignature> globalRootSignature;
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;

    RayTraceShader() = default;
    RayTraceShader(
        const LPCWSTR& rayGenFile,
        const LPCWSTR& missFile,
        const LPCWSTR& closestHitFile,
        const LPCWSTR& intersectionFile,
        const std::vector<std::pair<BufferType, int>>& buffersLocal,
        const std::vector<std::pair<BufferType, int>>& buffersGlobal,
        const ComPtr<ID3D12Device>& d
    );

    void createPipeline();
    void createSBT(UINT w, UINT h); //swapchain width and height

    [[nodiscard]] ComPtr<IDxcBlob> getRayGenShader() const;
    [[nodiscard]] ComPtr<IDxcBlob> getMissShader() const;
    [[nodiscard]] ComPtr<IDxcBlob> getClosestHitShader() const;
    [[nodiscard]] ComPtr<IDxcBlob> getIntersectionShader() const;

    /**
     * Creates an SRV for the TLAS buffer.
     *
     * @param buffer the TLAS buffer
     */
    void createAccStrucSRV(const ComPtr<ID3D12Resource>& buffer) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure.Location = buffer->GetGPUVirtualAddress();

        device->CreateShaderResourceView(nullptr, &srvDesc, cpuHandle);
        cpuHandle.ptr += descriptorSize;
    }

    /**
     * Creates an SRV for a buffer that should be used in this shader.
     *
     * It is important that the order in which you call these create*(...) methods are in line with how you passed them
     * via the constructor. Please see the constructor documentation.
     *
     * @tparam T the type of the buffer
     * @param buffer the buffer
     */
    template<typename T>
    void createSRV(const ComPtr<ID3D12Resource>& buffer) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = buffer->GetDesc().Width / sizeof(T);
        srvDesc.Buffer.StructureByteStride = sizeof(T);
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        device->CreateShaderResourceView(buffer.Get(), &srvDesc, cpuHandle);
        cpuHandle.ptr += descriptorSize;
    }

    /**
     * Creates a UAV for the output texture
     * @param buffer
     */
    void createOutputUAV(const ComPtr<ID3D12Resource>& buffer) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        device->CreateUnorderedAccessView(buffer.Get(), nullptr, &uavDesc, cpuHandle);
        cpuHandle.ptr += descriptorSize;
    }

    /**
     * Creates a CBV for a buffer that should be used in this compute shader.
     *
     * It is important that the order in which you call these create*(...) methods are in line with how you passed them
     * via the constructor. Please see the constructor documentation.
     *
     * @param buffer the buffer
     */
    void createCBV(const ComPtr<ID3D12Resource>& buffer) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};

        UINT64 bufferSize = buffer->GetDesc().Width;
        cbvDesc.BufferLocation = buffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = static_cast<UINT>((bufferSize + 255) & ~255); // Align to 256 bytes

        device->CreateConstantBufferView(&cbvDesc, cpuHandle);
        cpuHandle.ptr += descriptorSize;
    }
};
