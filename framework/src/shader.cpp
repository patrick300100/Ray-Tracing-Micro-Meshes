#include "shader.h"

#include <d3dx12.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <cassert>
#include <string>
#include <utility>

Shader::Shader(Microsoft::WRL::ComPtr<ID3DBlob> vBlob, Microsoft::WRL::ComPtr<ID3DBlob> pBlob, Microsoft::WRL::ComPtr<ID3DBlob> sig):
    vertexShaderBlob(std::move(vBlob)),
    pixelShaderBlob(std::move(pBlob)),
    signature(std::move(sig))
{
}

Shader::Shader(Shader&& other) noexcept :
    vertexShaderBlob(std::move(other.vertexShaderBlob)),
    pixelShaderBlob(std::move(other.pixelShaderBlob)),
    signature(std::move(other.signature))
{
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if(this != &other) {
        vertexShaderBlob = std::move(other.vertexShaderBlob);
        pixelShaderBlob = std::move(other.pixelShaderBlob);
        signature = std::move(other.signature);
    }
    return *this;
}

ShaderBuilder& ShaderBuilder::addStage(const LPCWSTR& shaderFile, const LPCSTR& entryFunction, const LPCSTR& shaderModel) {
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    const HRESULT hr = D3DCompileFromFile(
        shaderFile,
        nullptr,
        nullptr,
        entryFunction,
        shaderModel,
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &shaderBlob,
        &errorBlob
    );

    if(FAILED(hr)) {
        if(errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }

        throw ShaderLoadingException("Failed to compile shader.");
    }

    shaders.push_back(shaderBlob);

    return *this;
}

ShaderBuilder& ShaderBuilder::addVS(const LPCWSTR& shaderFile) {
    return addStage(shaderFile, "VSMain", "vs_5_0");
}

ShaderBuilder& ShaderBuilder::addPS(const LPCWSTR& shaderFile) {
    return addStage(shaderFile, "PSMain", "ps_5_0");
}

Shader ShaderBuilder::build() {
    std::vector<CD3DX12_ROOT_PARAMETER> rootParameters(nConstBuffers);

    for(int i = 0; i < nConstBuffers; i++) {
        rootParameters[i].InitAsConstantBufferView(i);
    }

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    const auto hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if(FAILED(hr)) OutputDebugStringA(static_cast<char*>(error->GetBufferPointer()));

    return {shaders[0], shaders[1], signature};
}

Microsoft::WRL::ComPtr<ID3DBlob> Shader::getVertexShaderBlob() const {
    return vertexShaderBlob;
}

Microsoft::WRL::ComPtr<ID3DBlob> Shader::getPixelShaderBlob() const {
    return pixelShaderBlob;
}

ShaderBuilder& ShaderBuilder::addConstantBuffers(const int nBuffers) {
    nConstBuffers = nBuffers;
    return *this;
}

Microsoft::WRL::ComPtr<ID3DBlob> Shader::getSignature() const {
    return signature;
}

