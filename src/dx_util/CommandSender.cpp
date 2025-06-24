#include "CommandSender.h"

#include <iostream>

CommandSender::CommandSender(const Microsoft::WRL::ComPtr<ID3D12Device5>& device, const D3D12_COMMAND_LIST_TYPE cmdListType) {
    device->CreateCommandAllocator(cmdListType, IID_PPV_ARGS(&allocator));
    device->CreateCommandList1(0, cmdListType, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdList));

    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = cmdListType;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 1;
    device->CreateCommandQueue(&desc, IID_PPV_ARGS(&cmdQueue));
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> CommandSender::getCommandList() const {
    return cmdList;
}

void CommandSender::execute(const Microsoft::WRL::ComPtr<ID3D12Device>& device) const {
    cmdList->Close();

    ID3D12CommandList* lists[] = { cmdList.Get() };
    cmdQueue->ExecuteCommandLists(_countof(lists), lists);

    waitOnGPU(device);
}

void CommandSender::reset() const {
    allocator->Reset();
    cmdList->Reset(allocator.Get(), nullptr);
}

Microsoft::WRL::ComPtr<ID3D12CommandQueue> CommandSender::getCommandQueue() const {
    return cmdQueue;
}

void CommandSender::waitOnGPU(const Microsoft::WRL::ComPtr<ID3D12Device>& device) const {
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 1;
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

    cmdQueue->Signal(fence.Get(), fenceValue);
    if(fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);
}
