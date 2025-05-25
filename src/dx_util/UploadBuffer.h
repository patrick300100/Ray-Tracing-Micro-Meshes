#pragma once

#include <d3dx12.h>
#include "Buffer.h"

template<typename T>
class UploadBuffer final : public Buffer<T> {
    void* mappedPtr = nullptr;

public:
    UploadBuffer() = default;

    UploadBuffer(const Microsoft::WRL::ComPtr<ID3D12Device>& device, const int elementCount, const bool isConstantBuffer = false) {
        this->size = sizeof(T) * elementCount;
        if(isConstantBuffer) this->size = (this->size + 255) & ~255;

        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(this->size);

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

    void upload(const std::vector<T>& data) override {
        memcpy(mappedPtr, data.data(), data.size() * sizeof(T));
    }

    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;

    UploadBuffer(UploadBuffer&& other) noexcept: mappedPtr(other.mappedPtr) {
        this->buffer = std::move(other.buffer);

        this->size = other.size;
        other.size = 0;

        other.mappedPtr = nullptr;
    }

    UploadBuffer& operator=(UploadBuffer&& other) noexcept {
        if (this != &other) {
            this->buffer = std::move(other.buffer);

            this->size = other.size;
            other.size = 0;

            mappedPtr = other.mappedPtr;
            other.mappedPtr = nullptr;
        }
        return *this;
    }
};