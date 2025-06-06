#pragma once

#include "shader.h"

using namespace Microsoft::WRL;

class RasterizationShader : public Shader {

public:
    RasterizationShader() = default;
    RasterizationShader(const LPCWSTR& vertexFile, const LPCWSTR& pixelFile, int nConstantBuffers, const ComPtr<ID3D12Device>& device);

    [[nodiscard]] ComPtr<ID3DBlob> getVertexShader() const;
    [[nodiscard]] ComPtr<ID3DBlob> getPixelShader() const;
};
