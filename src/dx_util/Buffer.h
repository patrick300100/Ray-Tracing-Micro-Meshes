#pragma once

#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

class Buffer {
protected:
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    unsigned long long size = 0;

public:
    virtual ~Buffer() = default;

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> getBuffer() const { return buffer; }
};
