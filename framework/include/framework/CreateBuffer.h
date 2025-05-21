#pragma once

#include <d3dx12.h>
#include <d3d12.h>
#include <wrl/client.h>

template<typename T>
class CreateBuffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

public:
    CreateBuffer() = default;

    CreateBuffer(const Microsoft::WRL::ComPtr<ID3D12Device>& device, const int elementCount, const bool isConstantBuffer = false) {
        auto size = sizeof(T) * elementCount;
        if(isConstantBuffer) size = (size + 255) & ~255;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer)
        );
    }

    void copyDataIntoBuffer(const std::vector<T>& data) {
        void* dataPtr = nullptr;
        buffer->Map(0, nullptr, &dataPtr);
        memcpy(dataPtr, data.data(), data.size() * sizeof(T));
        buffer->Unmap(0, nullptr);

        dataPtr = nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> getBuffer() {
        return buffer;
    }
};
