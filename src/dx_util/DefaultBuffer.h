#pragma once

#include <d3dx12.h>
#include "Buffer.h"
#include "UploadBuffer.h"

using namespace Microsoft::WRL;

template<typename T>
class DefaultBuffer final : public Buffer {
    UploadBuffer<T> uploadBuffer;

public:
    DefaultBuffer() = default;

    DefaultBuffer(const ComPtr<ID3D12Device>& device, const int elementCount, const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE):
        uploadBuffer(device, elementCount)
    {
        this->size = sizeof(T) * elementCount;

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

    void upload(const std::vector<T>& data, const ComPtr<ID3D12GraphicsCommandList>& cmdList, const D3D12_RESOURCE_STATES afterState) {
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
            afterState
        );
        cmdList->ResourceBarrier(1, &barrier2);
    }
};