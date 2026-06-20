#include "Renderer/ShaderCompilerD3D12.h"

#ifdef MYENGINE_PLATFORM_WINDOWS

#include "Core/Logger.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (need <= 1) return {};
    std::vector<wchar_t> buf(static_cast<size_t>(need));
    const int written = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf.data(), need);
    if (written <= 1) return {};
    return std::wstring(buf.data());
}
}

static bool CompileOneStageFromFile(
    const std::wstring& widePath,
    const std::string& filePath,
    const std::string& entry,
    const char* target,
    std::vector<unsigned char>& outBytecode,
    const std::vector<std::string>& defines = {}) {
    std::vector<std::string> names, values;
    std::vector<D3D_SHADER_MACRO> macros;
    names.reserve(defines.size()); values.reserve(defines.size()); macros.reserve(defines.size() + 1);
    for (const auto& define : defines) {
        const size_t equals = define.find('=');
        names.push_back(define.substr(0, equals));
        values.push_back(equals == std::string::npos ? "1" : define.substr(equals + 1));
    }
    for (size_t i = 0; i < names.size(); ++i) macros.push_back({names[i].c_str(), values[i].c_str()});
    macros.push_back({nullptr, nullptr});
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errBlob;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    const HRESULT hr = D3DCompileFromFile(
        widePath.c_str(),
        defines.empty() ? nullptr : macros.data(),
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry.c_str(),
        target,
        flags,
        0,
        &shaderBlob,
        &errBlob);
    if (FAILED(hr) || !shaderBlob) {
        if (errBlob) {
            Logger::Error("[ShaderCompileError] file=", filePath, " entry=", entry, " profile=", target,
                " | ", static_cast<const char*>(errBlob->GetBufferPointer()));
        } else {
            Logger::Error("[ShaderCompileError] file=", filePath, " entry=", entry, " profile=", target,
                " | no diagnostic blob");
        }
        return false;
    }
    const size_t n = shaderBlob->GetBufferSize();
    outBytecode.resize(n);
    std::memcpy(outBytecode.data(), shaderBlob->GetBufferPointer(), n);
    return true;
}

bool ShaderCompilerD3D12::CompileProgramFromFile(
    const std::string& filePath,
    const std::string& vsEntry,
    const std::string& psEntry,
    D3D12CompiledShaderProgram& outProgram) {
    const std::wstring widePath = Utf8ToWide(filePath);
    if (widePath.empty()) {
        Logger::Error("[ShaderCompileError] file=", filePath,
            " message=invalid utf8 path for D3DCompileFromFile");
        return false;
    }

    outProgram = {};
    if (!CompileOneStageFromFile(widePath, filePath, vsEntry, "vs_5_1", outProgram.vsBytecode)) {
        return false;
    }
    if (!CompileOneStageFromFile(widePath, filePath, psEntry, "ps_5_1", outProgram.psBytecode)) {
        return false;
    }
    return true;
}

bool ShaderCompilerD3D12::CompileStageFromFile(
    const std::string& filePath, const std::string& entry, const char* profile,
    std::vector<unsigned char>& outBytecode,
    const std::vector<std::string>& defines) {
    const std::wstring widePath = Utf8ToWide(filePath);
    return !widePath.empty() && CompileOneStageFromFile(
        widePath, filePath, entry, profile, outBytecode, defines);
}

#endif
