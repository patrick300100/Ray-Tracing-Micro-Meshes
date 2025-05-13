#pragma once

#include <d3dcommon.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>

struct ShaderLoadingException : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Shader {
    Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;

    friend class ShaderBuilder;
    Shader(Microsoft::WRL::ComPtr<ID3DBlob> vBlob, Microsoft::WRL::ComPtr<ID3DBlob> pBlob);

public:
    Shader() = default;
    Shader(const Shader&) = delete;
    Shader(Shader&&) noexcept;
    ~Shader() = default;

    Shader& operator=(Shader&&) noexcept;

    [[nodiscard]] Microsoft::WRL::ComPtr<ID3DBlob> getVertexShaderBlob() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3DBlob> getPixelShaderBlob() const;
};

class ShaderBuilder {
    std::vector<Microsoft::WRL::ComPtr<ID3DBlob>> shaders;

    ShaderBuilder& addStage(const LPCWSTR& shaderFile, const LPCSTR& entryFunction, const LPCSTR& shaderModel);

public:
    ShaderBuilder() = default;
    ShaderBuilder(const ShaderBuilder&) = delete;
    ShaderBuilder(ShaderBuilder&&) = default;
    ~ShaderBuilder() = default;

    ShaderBuilder& addVS(const LPCWSTR& shaderFile);
    ShaderBuilder& addPS(const LPCWSTR& shaderFile);
    Shader build();
};
