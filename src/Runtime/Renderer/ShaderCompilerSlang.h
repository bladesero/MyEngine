#pragma once

#include "Assets/ShaderAsset.h"

#include <filesystem>
#include <string>
#include <vector>

class ShaderCompilerSlang {
public:
    static bool IsAvailable();
    static std::string GetVersionString();

    static bool CompileStageFromFile(const std::filesystem::path& filePath, const std::string& entry, ShaderStage stage,
                                     ShaderBackend backend, std::vector<uint8_t>& outBlob,
                                     const std::vector<std::string>& defines = {}, std::string* error = nullptr,
                                     CookedShaderStageReflection* reflection = nullptr);
};
