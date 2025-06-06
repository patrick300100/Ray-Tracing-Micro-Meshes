#include "ComputeShader.h"

#include <algorithm>
#include "ReadbackBuffer.h"

ComputeShader::ComputeShader(const LPCWSTR& shaderFile, const ComPtr<ID3D12Device>& device, const std::vector<std::pair<BufferType, int>>& buffers, const unsigned long long outputSizeInBytes):
    device(device)
{
    addStage(shaderFile, "main", "cs_5_0");

    initBuffers(buffers);

    //Create command queue
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue));
    }

    //Create pipeline
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = rootSignature.Get();
        psoDesc.CS = { shaders[0]->GetBufferPointer(), shaders[0]->GetBufferSize() };
        device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    }

    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&cmdAllocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, cmdAllocator.Get(), pipelineState.Get(), IID_PPV_ARGS(&cmdList));

    //Create descriptor heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = std::ranges::fold_left(buffers, 0, [](int acc, const auto& p) { return acc + p.second; });;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
    }

    readbackBuffer = ReadbackBuffer(device, outputSizeInBytes).getBuffer();
    cpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void ComputeShader::initBuffers(const std::vector<std::pair<BufferType, int>>& buffers) {
    std::vector<CD3DX12_DESCRIPTOR_RANGE> ranges;
    UINT srvRegister = 0, uavRegister = 0, cbvRegister = 0;

    for(const auto& b : buffers) {
        CD3DX12_DESCRIPTOR_RANGE range;

        switch(b.first) {
            case SRV:
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, b.second, srvRegister);
                srvRegister += b.second;

                ranges.push_back(range);
                break;
            case UAV:
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, b.second, uavRegister);
                uavRegister += b.second;

                ranges.push_back(range);
                break;
            case CBV:
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, b.second, cbvRegister);
                cbvRegister += b.second;

                ranges.push_back(range);
                break;
        }
    }

    CD3DX12_ROOT_PARAMETER rootParam;
    rootParam.InitAsDescriptorTable(ranges.size(), ranges.data(), D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(1, &rootParam, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> signature, error;
    const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

    if(FAILED(hr) && error) {
        OutputDebugStringA(static_cast<char*>(error->GetBufferPointer()));
        error->Release();
    }

    device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
}

void ComputeShader::waitOnGPU() const {
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 1;
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

    commandQueue->Signal(fence.Get(), fenceValue);

    if(fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    CloseHandle(fenceEvent);
}

