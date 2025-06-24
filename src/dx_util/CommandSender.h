#pragma once

#include <d3d12.h>
#include <wrl/client.h>

/**
 * Can be used to send commands to the GPU.
 */
class CommandSender {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmdQueue;

    void waitOnGPU(const Microsoft::WRL::ComPtr<ID3D12Device>& device) const;
public:
    CommandSender() = default;
    CommandSender(const Microsoft::WRL::ComPtr<ID3D12Device5>& device, D3D12_COMMAND_LIST_TYPE cmdListType);

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> getCommandList() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12CommandQueue> getCommandQueue() const;

    void execute(const Microsoft::WRL::ComPtr<ID3D12Device>& device) const;
    void reset() const;
};
