#include "RayTraceShader.h"

#include <algorithm>
#include <iostream>

#include "UploadBuffer.h"

RayTraceShader::RayTraceShader(
    const LPCWSTR& rayGenFile,
    const LPCWSTR& missFile,
    const LPCWSTR& closestHitFile,
    const LPCWSTR& intersectionFile,
    const std::vector<std::pair<BufferType, int>>& buffersLocal,
    const std::vector<std::pair<BufferType, int>>& buffersGlobal,
    const ComPtr<ID3D12Device>& d
) {
    d->QueryInterface(IID_PPV_ARGS(&device));

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
        d->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
    }

    cpuHandle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    descriptorSize = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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

ComPtr<IDxcBlob> RayTraceShader::getRayGenShader() const {
    return shaders[0];
}

ComPtr<IDxcBlob> RayTraceShader::getMissShader() const {
    return shaders[1];
}

ComPtr<IDxcBlob> RayTraceShader::getClosestHitShader() const {
    return shaders[2];
}

ComPtr<IDxcBlob> RayTraceShader::getIntersectionShader() const {
    return shaders[3];
}

void RayTraceShader::createPipeline() {
    // Raytracing pipeline subobjects
    std::vector<D3D12_STATE_SUBOBJECT> subobjects;
    subobjects.reserve(8);

    // Raygen
    auto raygenShader = getRayGenShader();
    D3D12_DXIL_LIBRARY_DESC raygenLibDesc = {};
    D3D12_SHADER_BYTECODE raygenBytecode = { raygenShader->GetBufferPointer(), raygenShader->GetBufferSize() };
    D3D12_EXPORT_DESC raygenExport = { L"RGMain", L"main", D3D12_EXPORT_FLAG_NONE };
    raygenLibDesc.DXILLibrary = raygenBytecode;
    raygenLibDesc.NumExports = 1;
    raygenLibDesc.pExports = &raygenExport;

    D3D12_STATE_SUBOBJECT raygenLib = {};
    raygenLib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    raygenLib.pDesc = &raygenLibDesc;
    subobjects.push_back(raygenLib);

    // Miss
    auto missShader = getMissShader();
    D3D12_DXIL_LIBRARY_DESC missLibDesc = {};
    D3D12_SHADER_BYTECODE missBytecode = { missShader->GetBufferPointer(), missShader->GetBufferSize() };
    D3D12_EXPORT_DESC missExport = { L"MissMain", L"main", D3D12_EXPORT_FLAG_NONE };
    missLibDesc.DXILLibrary = missBytecode;
    missLibDesc.NumExports = 1;
    missLibDesc.pExports = &missExport;

    D3D12_STATE_SUBOBJECT missLib = {};
    missLib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    missLib.pDesc = &missLibDesc;
    subobjects.push_back(missLib);

    // ClosestHit
    auto closestHitShader = getClosestHitShader();
    D3D12_DXIL_LIBRARY_DESC closestHitLibDesc = {};
    D3D12_SHADER_BYTECODE closestHitBytecode = { closestHitShader->GetBufferPointer(), closestHitShader->GetBufferSize() };
    D3D12_EXPORT_DESC closestHitExport = { L"CHMain", L"main", D3D12_EXPORT_FLAG_NONE };
    closestHitLibDesc.DXILLibrary = closestHitBytecode;
    closestHitLibDesc.NumExports = 1;
    closestHitLibDesc.pExports = &closestHitExport;

    D3D12_STATE_SUBOBJECT closestHitLib = {};
    closestHitLib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    closestHitLib.pDesc = &closestHitLibDesc;
    subobjects.push_back(closestHitLib);

    // Intersection
    auto intersectionShader = getIntersectionShader();
    D3D12_DXIL_LIBRARY_DESC intersectionLibDesc = {};
    D3D12_SHADER_BYTECODE intersectionBytecode = { intersectionShader->GetBufferPointer(), intersectionShader->GetBufferSize() };
    D3D12_EXPORT_DESC intersectionExport = { L"IMain", L"main", D3D12_EXPORT_FLAG_NONE };
    intersectionLibDesc.DXILLibrary = intersectionBytecode;
    intersectionLibDesc.NumExports = 1;
    intersectionLibDesc.pExports = &intersectionExport;

    D3D12_STATE_SUBOBJECT intersectionLib = {};
    intersectionLib.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    intersectionLib.pDesc = &intersectionLibDesc;
    subobjects.push_back(intersectionLib);

    // Hit Group: combine closest hit + intersection
    D3D12_HIT_GROUP_DESC hitGroupDesc = {};
    hitGroupDesc.ClosestHitShaderImport = L"CHMain";
    hitGroupDesc.IntersectionShaderImport = L"IMain";
    hitGroupDesc.HitGroupExport = L"MyHitGroup"; // Named export for SBT
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;

    D3D12_STATE_SUBOBJECT hitGroupSubobject = {};
    hitGroupSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    hitGroupSubobject.pDesc = &hitGroupDesc;
    subobjects.push_back(hitGroupSubobject);

    // Global Root Signature
    D3D12_STATE_SUBOBJECT globalRootSigSubobject = {};
    globalRootSigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    globalRootSigSubobject.pDesc = globalRootSignature.GetAddressOf();
    subobjects.push_back(globalRootSigSubobject);

    // Shader config
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 3;
    shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 3 * 2;

    D3D12_STATE_SUBOBJECT shaderConfigSubobject = {};
    shaderConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    shaderConfigSubobject.pDesc = &shaderConfig;
    subobjects.push_back(shaderConfigSubobject);

    // Pipeline config
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    D3D12_STATE_SUBOBJECT pipelineConfigSubobject = {};
    pipelineConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    pipelineConfigSubobject.pDesc = &pipelineConfig;
    subobjects.push_back(pipelineConfigSubobject);

    // Assemble state object
    D3D12_STATE_OBJECT_DESC pipelineDesc = {};
    pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    pipelineDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
    pipelineDesc.pSubobjects = subobjects.data();

    HRESULT hr = device->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&pipelineStateObject));
    if(FAILED(hr)) {
        std::cout << "Uh-oh...\n";
    }

    hr = pipelineStateObject.As(&pipelineProps);
    if(FAILED(hr)) {
        std::cout << "Uh-oh...\n";
    }
}

template<typename T>
T alignUp(T value, size_t alignment) {
    return static_cast<T>((static_cast<size_t>(value) + (alignment - 1)) & ~(alignment - 1));
}

void RayTraceShader::createSBT(const UINT w, const UINT h) {
    ComPtr<ID3D12StateObjectProperties> stateObjectProps;
    HRESULT hr = pipelineStateObject->QueryInterface(IID_PPV_ARGS(&stateObjectProps));
    if(FAILED(hr)) {
        std::cout << "Uh-oh...\n";
    }

    void* rayGenId = stateObjectProps->GetShaderIdentifier(L"RGMain");
    void* missId = stateObjectProps->GetShaderIdentifier(L"MissMain");
    void* hitGroupId = stateObjectProps->GetShaderIdentifier(L"MyHitGroup");

    UINT shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    UINT shaderRecordAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

    UINT64 recordSize = alignUp(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, shaderRecordAlignment);

    rayGenSBTBuffer = UploadBuffer<void*>(recordSize, device);
    rayGenSBTBuffer.upload({rayGenId}, shaderIdSize);

    missSBTBuffer = UploadBuffer<void*>(recordSize, device);
    missSBTBuffer.upload({missId}, shaderIdSize);

    hitGroupSBTBuffer = UploadBuffer<void*>(recordSize, device);
    hitGroupSBTBuffer.upload({hitGroupId}, shaderIdSize);

    dispatchDesc.RayGenerationShaderRecord.StartAddress = rayGenSBTBuffer.getBuffer()->GetGPUVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = recordSize;

    dispatchDesc.MissShaderTable.StartAddress = missSBTBuffer.getBuffer()->GetGPUVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = recordSize;
    dispatchDesc.MissShaderTable.StrideInBytes = recordSize;

    dispatchDesc.HitGroupTable.StartAddress = hitGroupSBTBuffer.getBuffer()->GetGPUVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = recordSize;
    dispatchDesc.HitGroupTable.StrideInBytes = recordSize;

    dispatchDesc.Width = w;
    dispatchDesc.Height = h;
    dispatchDesc.Depth = 1;
}
