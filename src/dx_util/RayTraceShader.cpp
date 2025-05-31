#include "RayTraceShader.h"

RayTraceShader::RayTraceShader(const LPCWSTR& rayGenFile, const LPCWSTR& missFile, const LPCWSTR& closestHitFile, const LPCWSTR& intersectionFile) {
    ComPtr<IDxcCompiler3> dxcCompiler;
    ComPtr<IDxcUtils> dxcUtils;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));

    compileShader(dxcCompiler, dxcUtils, rayGenFile);
    compileShader(dxcCompiler, dxcUtils, missFile);
    compileShader(dxcCompiler, dxcUtils, closestHitFile);
    compileShader(dxcCompiler, dxcUtils, intersectionFile);
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

