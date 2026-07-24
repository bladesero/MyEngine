#include "Renderer/RHI/PlatformShaderCompiler.h"

#include "D3DShaderCompilerBackend.h"
#include "Renderer/Backends/D3DCommon/ShaderCompilerD3D11.h"
#include "Renderer/Backends/D3DCommon/ShaderCompilerD3D12.h"

namespace {
bool CompileD3DShaderStageBackend(bool d3d12, const std::string& filePath, const std::string& entry,
                                  const char* profile, std::vector<unsigned char>& outBytecode,
                                  const std::vector<std::string>& defines) {
    return d3d12 ? ShaderCompilerD3D12::CompileStageFromFile(filePath, entry, profile, outBytecode, defines)
                 : ShaderCompilerD3D11::CompileStageFromFile(filePath, entry, profile, outBytecode, defines);
}
}

bool RegisterD3DShaderCompilerBackend() {
    return RegisterD3DShaderStageCompiler(&CompileD3DShaderStageBackend);
}
