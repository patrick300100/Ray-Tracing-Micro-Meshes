#pragma once

#include <d3d12.h>
#include <wrl/client.h>

/**
 * Can be used to send commands to the GPU.
 */
class CommandSender {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> cmdQueue;

public:
    CommandSender(const Microsoft::WRL::ComPtr<ID3D12Device>& device, D3D12_COMMAND_LIST_TYPE cmdListType);

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> getCommandList() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12CommandQueue> getCommandQueue() const;

    void execute() const;
    void reset() const;
};
