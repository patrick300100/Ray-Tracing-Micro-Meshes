#pragma once

#include <wrl/client.h>
#include <dxcapi.h>
#include <vector>
#pragma comment(lib, "dxcompiler.lib")

using namespace Microsoft::WRL;

class RayTraceShader {
    std::vector<ComPtr<IDxcBlob>> shaders;

    void compileShader(const ComPtr<IDxcCompiler3>& compiler, const ComPtr<IDxcUtils>& utils, const LPCWSTR& shaderFile);

public:
    RayTraceShader() = default;
    RayTraceShader(const LPCWSTR& rayGenFile, const LPCWSTR& missFile, const LPCWSTR& closestHitFile, const LPCWSTR& intersectionFile);
};
