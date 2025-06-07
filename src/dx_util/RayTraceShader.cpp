#include "RayTraceShader.h"

#include <algorithm>

RayTraceShader::RayTraceShader(
    const LPCWSTR& rayGenFile,
    const LPCWSTR& missFile,
    const LPCWSTR& closestHitFile,
    const LPCWSTR& intersectionFile,
    const std::vector<std::pair<BufferType, int>>& buffersLocal,
    const std::vector<std::pair<BufferType, int>>& buffersGlobal,
    const ComPtr<ID3D12Device>& device
):
    device(device)
{
    ComPtr<IDxcCompiler3> dxcCompiler;
    ComPtr<IDxcUtils> dxcUtils;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));

    compileShader(dxcCompiler, dxcUtils, rayGenFile);
    compileShader(dxcCompiler, dxcUtils, missFile);
    compileShader(dxcCompiler, dxcUtils, closestHitFile);
    compileShader(dxcCompiler, dxcUtils, intersectionFile);

    if(!buffersLocal.empty()) initBuffers(buffersLocal, LOCAL);
    if(!buffersGlobal.empty()) initBuffers(buffersGlobal, GLOBAL);

    //Create descriptor heap
    {
        const auto totalDescriptors = std::ranges::fold_left(buffersLocal, 0, [](int acc, const auto& p) { return acc + p.second; }) +
            std::ranges::fold_left(buffersGlobal, 0, [](int acc, const auto& p) { return acc + p.second; });

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = totalDescriptors;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
    }

    cpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void RayTraceShader::compileShader(const ComPtr<IDxcCompiler3>& compiler, const ComPtr<IDxcUtils>& utils, const LPCWSTR& shaderFile) {
    ComPtr<IDxcBlobEncoding> shaderSource;
    utils->LoadFile(shaderFile, nullptr, &shaderSource);

    LPCWSTR arguments[] = {
        L"-T", L"lib_6_6",
        L"-E", L"main",
        L"-Zi",
        L"-Qembed_debug",
    };

    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = shaderSource->GetBufferPointer();
    sourceBuffer.Size = shaderSource->GetBufferSize();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    ComPtr<IDxcResult> result;
    compiler->Compile(&sourceBuffer, arguments, _countof(arguments), nullptr, IID_PPV_ARGS(&result));

    ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if(errors && errors->GetStringLength() > 0) OutputDebugStringA(errors->GetStringPointer());

    ComPtr<IDxcBlob> shader;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr);

    shaders.push_back(shader);
}

void RayTraceShader::initBuffers(const std::vector<std::pair<BufferType, int>>& buffers, const RootSignatureType type) {
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

    device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(type == GLOBAL ? &globalRootSignature : &localRootSignature)
    );
}

void RayTraceShader::setDescriptorHeap(const ComPtr<ID3D12GraphicsCommandList>& cmdList) {
    const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();

    cmdList->SetDescriptorHeaps(1, descriptorHeap.GetAddressOf());
    cmdList->SetComputeRootSignature(globalRootSignature.Get());
    cmdList->SetComputeRootDescriptorTable(0, gpuHandle);
}
