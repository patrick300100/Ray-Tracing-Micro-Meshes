#pragma once

#include <d3d12.h>
#include <d3dcommon.h>
#include <wrl/client.h>
#include <vector>

class Shader {
protected:
    std::vector<Microsoft::WRL::ComPtr<ID3DBlob>> shaders;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;

    void addStage(const LPCWSTR& shaderFile, const LPCSTR& entryFunction, const LPCSTR& shaderModel);

public:
    Shader() = default;

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12RootSignature> getRootSignature() const;
};
