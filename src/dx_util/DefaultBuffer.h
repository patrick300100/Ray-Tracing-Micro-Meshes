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
     * @param state buffer state
     * @param flag buffer flag
     */
    DefaultBuffer(const ComPtr<ID3D12Device>& device, const size_t elementCount, const D3D12_RESOURCE_STATES state, const D3D12_RESOURCE_FLAGS flag = D3D12_RESOURCE_FLAG_NONE):
        DefaultBuffer(sizeof(T) * elementCount, device, state, flag)
    {
    }

    /**
     * Creates a default buffer.
     *
     * @param sizeInBytes the full size of the buffer in bytes
     * @param device the device
     * @param state buffer state
     * @param flag buffer flag
     */
    DefaultBuffer(const unsigned long long sizeInBytes, const ComPtr<ID3D12Device>& device, const D3D12_RESOURCE_STATES state, const D3D12_RESOURCE_FLAGS flag = D3D12_RESOURCE_FLAG_NONE):
        uploadBuffer(sizeInBytes, device)
    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes, flag);

        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            state,
            nullptr,
            IID_PPV_ARGS(&this->buffer)
        );
    }

    /**
     * Uploads data to this default buffer.
     *
     * @param data the data
     * @param cmdList a command list
     * @param afterState the state of this buffer after it copied the elements
     */
    void upload(const std::vector<T>& data, const ComPtr<ID3D12GraphicsCommandList>& cmdList, const D3D12_RESOURCE_STATES afterState) {
        upload(data.data(), sizeof(T) * data.size(), cmdList, afterState);
    }

    /**
     * Uploads data to this default buffer.
     *
     * @param data a pointer to the data
     * @param size the size of data in bytes
     * @param cmdList a command list
     * @param afterState the state of this buffer after it copied the elements
     */
    void upload(const void* data, const size_t size, const ComPtr<ID3D12GraphicsCommandList>& cmdList, const D3D12_RESOURCE_STATES afterState) {
        uploadBuffer.upload(data, size);

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