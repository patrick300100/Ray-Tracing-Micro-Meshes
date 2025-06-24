#include "shader.h"

#include <../../framework/third_party/d3dx12/d3dx12.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <cassert>
#include <string>

void Shader::addStage(const LPCWSTR& shaderFile, const LPCSTR& entryFunction, const LPCSTR& shaderModel) {
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

        exit(1);
    }

    shaders.push_back(shaderBlob);
}

Microsoft::WRL::ComPtr<ID3D12RootSignature> Shader::getRootSignature() const {
    return rootSignature;
}
