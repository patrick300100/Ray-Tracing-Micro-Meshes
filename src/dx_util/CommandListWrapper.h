#pragma once

#include <d3d12.h>
#include <wrl/client.h>

/**
 * Can be used to send commands to the GPU.
 */
class CommandListWrapper {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;

public:
    explicit CommandListWrapper(const Microsoft::WRL::ComPtr<ID3D12Device>& device);

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> getCommandList() const;

    void execute(const Microsoft::WRL::ComPtr<ID3D12CommandQueue>& cmdQueue) const;
    void reset() const;
};
