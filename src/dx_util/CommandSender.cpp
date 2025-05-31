#include "CommandSender.h"

CommandSender::CommandSender(const Microsoft::WRL::ComPtr<ID3D12Device>& device, const D3D12_COMMAND_LIST_TYPE cmdListType) {
    device->CreateCommandAllocator(cmdListType, IID_PPV_ARGS(&allocator));
    device->CreateCommandList(0, cmdListType, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList));

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = cmdListType;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 1;
    device->CreateCommandQueue(&desc, IID_PPV_ARGS(&cmdQueue));
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandSender::getCommandList() const {
    return cmdList;
}

void CommandSender::execute() const {
    cmdList->Close();

    ID3D12CommandList* lists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(_countof(lists), lists);
}

void CommandSender::reset() const {
    allocator->Reset();
    cmdList->Reset(allocator.Get(), nullptr);
}

Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandSender::getCommandQueue() const {
    return cmdQueue;
}
