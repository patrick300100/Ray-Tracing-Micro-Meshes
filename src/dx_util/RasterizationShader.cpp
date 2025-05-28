#include "RasterizationShader.h"

#include <d3dx12.h>
#include <d3d12.h>

RasterizationShader::RasterizationShader(const LPCWSTR& vertexFile, const LPCWSTR& pixelFile, const int nConstantBuffers) {
    addStage(vertexFile, "VSMain", "vs_5_0");
    addStage(pixelFile, "PSMain", "ps_5_0");

    std::vector<CD3DX12_ROOT_PARAMETER> rootParameters(nConstantBuffers);

    for(int i = 0; i < nConstantBuffers; i++) {
        rootParameters[i].InitAsConstantBufferView(i);
    }

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> error;
    const auto hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if(FAILED(hr)) OutputDebugStringA(static_cast<char*>(error->GetBufferPointer()));
}

ComPtr<ID3DBlob> RasterizationShader::getVertexShader() const {
    return shaders[0];
}


ComPtr<ID3DBlob> RasterizationShader::getPixelShader() const {
    return shaders[1];
}
