#include "ComputeShader.h"

#include "ReadbackBuffer.h"

ComputeShader::ComputeShader(const LPCWSTR& shaderFile, const ComPtr<ID3D12Device>& device, const std::vector<BufferType>& buffers, const int outputSizeInBytes):
    device(device),
    nBuffers(buffers.size())
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
        heapDesc.NumDescriptors = buffers.size();
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
    }

    readbackBuffer = ReadbackBuffer(device, outputSizeInBytes).getBuffer();
    cpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
}

void ComputeShader::initBuffers(const std::vector<BufferType>& buffers) {
    std::vector<CD3DX12_ROOT_PARAMETER> rootParams;
    rootParams.reserve(buffers.size());

    std::vector<CD3DX12_DESCRIPTOR_RANGE> descriptorRanges;
    descriptorRanges.reserve(buffers.size());

    UINT srvRegister = 0, uavRegister = 0, cbvRegister = 0;

    for(const BufferType& b : buffers) {
        CD3DX12_ROOT_PARAMETER param{};
        CD3DX12_DESCRIPTOR_RANGE range{};

        switch(b) {
            case SRV:
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, srvRegister++);
                descriptorRanges.push_back(range);
                param.InitAsDescriptorTable(1, &descriptorRanges.back());
                rootParams.push_back(param);
                break;
            case UAV:
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, uavRegister++);
                descriptorRanges.push_back(range);
                param.InitAsDescriptorTable(1, &descriptorRanges.back());
                rootParams.push_back(param);
                break;
            case CBV:
                range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, cbvRegister++);
                descriptorRanges.push_back(range);
                param.InitAsDescriptorTable(1, &descriptorRanges.back());
                rootParams.push_back(param);
                break;
        }
    }

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(
        static_cast<UINT>(rootParams.size()),
        rootParams.data(),
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE
    );

    ComPtr<ID3DBlob> error;
    const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

    if(FAILED(hr) && error) {
        OutputDebugStringA(static_cast<char*>(error->GetBufferPointer()));
        error->Release();
    }

    device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
}
