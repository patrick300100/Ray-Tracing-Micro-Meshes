#pragma once

#include <d3dx12.h>
#include "Buffer.h"

class ReadbackBuffer final : public Buffer {
public:
    ReadbackBuffer() = default;

    ReadbackBuffer(const ComPtr<ID3D12Device>& device, const unsigned long long sizeInBytes) {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_READBACK);
        const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);

        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&this->buffer)
        );
    }
};