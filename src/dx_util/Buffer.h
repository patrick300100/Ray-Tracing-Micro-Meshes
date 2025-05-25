#pragma once

#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

template<typename T>
class Buffer {
protected:
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    unsigned long long size = 0;

public:
    virtual ~Buffer() = default;

    virtual void upload(const std::vector<T>& data) = 0;
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> getBuffer() const { return buffer; }

    // Buffer(Buffer&& other) noexcept: buffer(std::move(other.buffer)), size(other.size) {
    //     other.size = 0;
    // }
    //
    // Buffer& operator=(Buffer&& other) noexcept {
    //     if(this != &other) {
    //         buffer = std::move(other.buffer);
    //
    //         size = other.size;
    //         other.size = 0;
    //     }
    //
    //     return *this;
    // }
};
