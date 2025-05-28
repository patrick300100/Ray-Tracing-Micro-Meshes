#include "CommandListWrapper.h"

CommandListWrapper::CommandListWrapper(const Microsoft::WRL::ComPtr<ID3D12Device>& device) {
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList));
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandListWrapper::getCommandList() const {
    return cmdList;
}

void CommandListWrapper::execute(const Microsoft::WRL::ComPtr<ID3D12CommandQueue>& cmdQueue) const {
    cmdList->Close();

    ID3D12CommandList* lists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(_countof(lists), lists);
}

void CommandListWrapper::reset() const {
    allocator->Reset();
    cmdList->Reset(allocator.Get(), nullptr);
}
