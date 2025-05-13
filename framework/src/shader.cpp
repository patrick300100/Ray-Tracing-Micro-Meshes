#include "shader.h"

#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <cassert>
#include <string>
#include <utility>

Shader::Shader(Microsoft::WRL::ComPtr<ID3DBlob> vBlob, Microsoft::WRL::ComPtr<ID3DBlob> pBlob): vertexShaderBlob(std::move(vBlob)), pixelShaderBlob(std::move(pBlob)) {
}

Shader::Shader(Shader&& other) noexcept : vertexShaderBlob(std::move(other.vertexShaderBlob)), pixelShaderBlob(std::move(other.pixelShaderBlob)) {
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if(this != &other) {
        vertexShaderBlob = std::move(other.vertexShaderBlob);
        pixelShaderBlob = std::move(other.pixelShaderBlob);
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
    return {shaders[0], shaders[1]};
}

Microsoft::WRL::ComPtr<ID3DBlob> Shader::getVertexShaderBlob() const {
    return vertexShaderBlob;
}

Microsoft::WRL::ComPtr<ID3DBlob> Shader::getPixelShaderBlob() const {
    return pixelShaderBlob;
}
