#pragma once

#include <d3dcommon.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>

class Shader {
protected:
    std::vector<Microsoft::WRL::ComPtr<ID3DBlob>> shaders;
    Microsoft::WRL::ComPtr<ID3DBlob> signature;

    void addStage(const LPCWSTR& shaderFile, const LPCSTR& entryFunction, const LPCSTR& shaderModel);

public:
    Shader() = default;

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3DBlob> getSignature() const;
};
