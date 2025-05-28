#pragma once

#include <d3dx12.h>
#include "Buffer.h"
#include "UploadBuffer.h"

using namespace Microsoft::WRL;

template<typename T>
class DefaultBuffer final : public Buffer {
    UploadBuffer<T> uploadBuffer;
    ComPtr<ID3D12GraphicsCommandList> cmdList;

public:
    DefaultBuffer() = default;

    DefaultBuffer(const ComPtr<ID3D12Device>& device, const int elementCount, const ComPtr<ID3D12GraphicsCommandList>& cmdl, const bool isConstantBuffer = false, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE):
        uploadBuffer(device, elementCount, isConstantBuffer),
        cmdList(cmdl)
    {
        this->size = sizeof(T) * elementCount;
        if(isConstantBuffer) this->size = (this->size + 255) & ~255;

        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(this->size, flags);

        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&this->buffer)
        );
    }

    void upload(const std::vector<T>& data) {
        uploadBuffer.upload(data);

        const auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
            this->buffer.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST
        );
        cmdList->ResourceBarrier(1, &barrier1);

        cmdList->CopyResource(this->buffer.Get(), uploadBuffer.getBuffer().Get());

        const auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
            this->buffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
        );
        cmdList->ResourceBarrier(1, &barrier2);
    }
};