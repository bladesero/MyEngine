#pragma once

#include "API/RuntimeApi.h"

#include <string>
#include <vector>

// Platform-neutral boundary used by Renderer and Cooking. Native compiler
// headers and implementations remain inside the owning backend module.
using D3DShaderStageCompiler = bool (*)(bool d3d12, const std::string& filePath,
                                        const std::string& entry, const char* profile,
                                        std::vector<unsigned char>& outBytecode,
                                        const std::vector<std::string>& defines);

bool RegisterD3DShaderStageCompiler(D3DShaderStageCompiler compiler);
MYENGINE_RUNTIME_API bool HasD3DShaderStageCompiler();
bool CompileD3DShaderStage(bool d3d12, const std::string& filePath, const std::string& entry,
                           const char* profile, std::vector<unsigned char>& outBytecode,
                           const std::vector<std::string>& defines = {});
