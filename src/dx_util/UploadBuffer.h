#pragma once

#include <d3dx12.h>
#include "Buffer.h"

using namespace Microsoft::WRL;

template<typename T>
class UploadBuffer final : public Buffer {
    void* mappedPtr = nullptr;

public:
    UploadBuffer() = default;

    /**
     * Creates an upload buffer.
     *
     * @param device the device
     * @param elementCount the number of elements in the buffer
     * @param isConstantBuffer whether the buffer is a constant buffer or not
     */
    UploadBuffer(const ComPtr<ID3D12Device>& device, const int elementCount, const bool isConstantBuffer = false): UploadBuffer(sizeof(T) * elementCount, device, isConstantBuffer) {
    }

    /**
     * Creates an upload buffer.
     *
     * @param sizeInBytes the full size of the buffer in bytes
     * @param device the device
     * @param isConstantBuffer whether the buffer is a constant buffer or not
     */
    UploadBuffer(unsigned long long sizeInBytes, const ComPtr<ID3D12Device>& device, const bool isConstantBuffer = false) {
        if(isConstantBuffer) sizeInBytes = (sizeInBytes + 255) & ~255;

        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);

        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&this->buffer)
        );

        this->buffer->Map(0, nullptr, &mappedPtr);
    }

    ~UploadBuffer() override {
        if(mappedPtr != nullptr) this->buffer->Unmap(0, nullptr);
        mappedPtr = nullptr;
    }

    /**
     * Upload data to this upload buffer.
     *
     * @param data the data
     */
    void upload(const std::vector<T>& data) {
        upload(data, data.size() * sizeof(T));
    }

    /**
     * Uploads data to this upload buffer.
     *
     * @param data the data
     * @param size the size of the data in bytes
     */
    void upload(const std::vector<T>& data, const size_t size) {
        upload(data.data(), size);
    }

    /**
     * Uploads data to this upload buffer.
     *
     * @param data a pointer to the data
     * @param size the size of the data in bytes
     */
    void upload(const void* data, const size_t size) const {
        memcpy(mappedPtr, data, size);
    }

    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;

    UploadBuffer(UploadBuffer&& other) noexcept: mappedPtr(other.mappedPtr) {
        this->buffer = std::move(other.buffer);

        other.mappedPtr = nullptr;
    }

    UploadBuffer& operator=(UploadBuffer&& other) noexcept {
        if (this != &other) {
            this->buffer = std::move(other.buffer);

            mappedPtr = other.mappedPtr;
            other.mappedPtr = nullptr;
        }
        return *this;
    }
};