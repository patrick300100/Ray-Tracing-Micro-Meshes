#pragma once

#include <d3dx12.h>
#include <d3d12.h>

#include "shader.h"

using namespace Microsoft::WRL;

enum BufferType {
    SRV, //Shader Resource View
    UAV, //Unordered Access View
    CBV //Constant Buffer view
};

class ComputeShader : public Shader {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12PipelineState> pipelineState;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    UINT descriptorSize{};
    ComPtr<ID3D12CommandAllocator> cmdAllocator;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    ComPtr<ID3D12Resource> readbackBuffer;

    void initBuffers(const std::vector<std::pair<BufferType, int>>& buffers);
    void waitOnGPU() const;

public:
    ComputeShader() = default;

    /**
     * Creates a compute shader object.
     *
     * @param shaderFile the path to the shader file
     * @param device a pointer to the device
     * @param buffers a list of buffers that your shader needs, along with the number of buffers (for example, 2 SRVs and 1 UAV).
     * It is important that the order of buffers in this list is the same as the order in which you create the buffers with
     * createSRV(...), createUAV(...) and createCBV(...). So if you pass in this list 2 SRVs and 1 UAV, then you should
     * call createSRV(...), createSRV(...) and then createUAV(...) exactly in that order.
     * @param outputSizeInBytes the size in bytes of the buffer where the GPU should write the results to
     */
    ComputeShader(const LPCWSTR& shaderFile, const ComPtr<ID3D12Device>& device, const std::vector<std::pair<BufferType, int>>& buffers, unsigned long long outputSizeInBytes);

    /**
     * Executes the compute shader. When calling this function, it is assumed that the number of elements to process
     * is the same as the number of outputted elements.
     *
     * So if you want to process n elements, we assume that the compute shader outputs n elements back.
     *
     * @tparam T the type of the result of the compute shader.
     * @param outputBuffer the buffer where the compute shader copies the results into (this is typically the UAV buffer)
     * @param elementsToProcess the total number of elements that the GPU has to compute for you (and returns).
     * @return a vector containing the output data of the compute shader
     */
    template<typename T>
    std::vector<T> execute(const ComPtr<ID3D12Resource>& outputBuffer, const UINT elementsToProcess) {
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();

        cmdList->SetDescriptorHeaps(1, descriptorHeap.GetAddressOf());
        cmdList->SetComputeRootSignature(rootSignature.Get());
        cmdList->SetComputeRootDescriptorTable(0, gpuHandle);

        constexpr UINT threadsPerGroup = 64; //Must match the number of threads per group in the compute shader.
        const UINT threadGroupCount = (elementsToProcess + threadsPerGroup - 1) / threadsPerGroup;
        cmdList->Dispatch(threadGroupCount, 1, 1);

        const auto transition1 = CD3DX12_RESOURCE_BARRIER::Transition(
        outputBuffer.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->ResourceBarrier(1, &transition1);

        cmdList->CopyResource(readbackBuffer.Get(), outputBuffer.Get());

        const auto transition2 = CD3DX12_RESOURCE_BARRIER::Transition(
        outputBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->ResourceBarrier(1, &transition2);

        cmdList->Close();

        ID3D12CommandList* listsToExecute[] = { cmdList.Get() };
        commandQueue->ExecuteCommandLists(1, listsToExecute);

        waitOnGPU();

        void* mappedData = nullptr;
        readbackBuffer->Map(0, nullptr, &mappedData);

        auto result = reinterpret_cast<T*>(mappedData);
        std::vector<T> resultVector(result, result + elementsToProcess);

        readbackBuffer->Unmap(0, nullptr);

        return resultVector;
    }

    /**
     * Creates an SRV for a buffer that should be used in this compute shader.
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
     * Creates a UAV for a buffer that should be used in this compute shader.
     *
     * It is important that the order in which you call these create*(...) methods are in line with how you passed them
     * via the constructor. Please see the constructor documentation.
     *
     * @tparam T the type of the buffer
     * @param buffer the buffer
     */
    template<typename T>
    void createUAV(const ComPtr<ID3D12Resource>& buffer) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.NumElements = buffer->GetDesc().Width / sizeof(T);
        uavDesc.Buffer.StructureByteStride = sizeof(T);
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

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
