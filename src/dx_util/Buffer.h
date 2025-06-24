#pragma once

#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

class Buffer {
protected:
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;

public:
    virtual ~Buffer() = default;

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> getBuffer() const { return buffer; }
};
