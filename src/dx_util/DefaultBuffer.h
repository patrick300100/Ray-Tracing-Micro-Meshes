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

    /**
     * Creates a default buffer.
     *
     * @param device the device
     * @param elementCount the number of elements in the buffer
     * @param flags buffer flags
     */
    DefaultBuffer(const ComPtr<ID3D12Device>& device, const int elementCount, const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE):
        DefaultBuffer(sizeof(T) * elementCount, device, flags)
    {
    }

    /**
     * Creates a default buffer.
     *
     * @param sizeInBytes the full size of the buffer in bytes
     * @param device the device
     * @param flags buffer flags
     */
    DefaultBuffer(const unsigned long long sizeInBytes, const ComPtr<ID3D12Device>& device, const D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE):
        uploadBuffer(sizeInBytes, device)
    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes, flags);

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