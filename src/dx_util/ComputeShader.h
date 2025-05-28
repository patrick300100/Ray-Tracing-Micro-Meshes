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
    ComPtr<ID3D12RootSignature> rootSignature;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    UINT descriptorSize{};
    ComPtr<ID3D12CommandAllocator> cmdAllocator;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    int nBuffers{};
    ComPtr<ID3D12Resource> readbackBuffer;

    void initBuffers(const std::vector<BufferType>& buffers);

public:
    ComputeShader() = default;
    ComputeShader(const LPCWSTR& shaderFile, const ComPtr<ID3D12Device>& device, const std::vector<BufferType>& buffers, int outputSizeInBytes);

    template<typename T>
    void execute(const ComPtr<ID3D12Resource>& outputBuffer, const UINT elementsToProcess) {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();

        cmdList->SetDescriptorHeaps(1, descriptorHeap.GetAddressOf());
        cmdList->SetComputeRootSignature(rootSignature.Get());

        for(int i = 0; i < nBuffers; i++) {
            cmdList->SetComputeRootDescriptorTable(i, gpuHandle);
            gpuHandle.ptr += descriptorSize;
        }

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

        void* mappedData = nullptr;
        readbackBuffer->Map(0, nullptr, &mappedData);

        auto result = reinterpret_cast<T*>(mappedData);

        readbackBuffer->Unmap(0, nullptr);
    }

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

    void createCBV(const ComPtr<ID3D12Resource>& buffer) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};

        UINT64 bufferSize = buffer->GetDesc().Width;
        cbvDesc.BufferLocation = buffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = static_cast<UINT>((bufferSize + 255) & ~255); // Align to 256 bytes

        device->CreateConstantBufferView(&cbvDesc, cpuHandle);
        cpuHandle.ptr += descriptorSize;
    }
};
