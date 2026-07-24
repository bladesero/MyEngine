#pragma once

#include "API/RuntimeApi.h"

#include "Assets/AssetDatabase.h"
#include "Assets/ShaderAsset.h"

#include <filesystem>
#include <string>
#include <vector>

struct ShaderCookRequest {
    std::filesystem::path sourcePath;
    std::filesystem::path artifactPath;
    std::filesystem::path allowedRoot;
    std::vector<ShaderBackend> backends;
    std::string targetPlatform = "windows-x64";
    std::string settingsJson = "{}";
};

struct ShaderCookResult {
    bool succeeded = false;
    bool cacheHit = false;
    std::filesystem::path artifactPath;
    std::vector<std::string> dependencies;
    std::vector<AssetDiagnostic> diagnostics;
    std::string cacheKey;
};

namespace ShaderCooker {
MYENGINE_RUNTIME_API const char* BackendName(ShaderBackend backend);
MYENGINE_RUNTIME_API std::vector<ShaderBackend> BackendsForTargetPlatform(const std::string& targetPlatform);
MYENGINE_RUNTIME_API bool CollectDependencies(const std::filesystem::path& source, const std::filesystem::path& allowedRoot,
                         std::vector<std::string>& outDependencies, std::string* error = nullptr);
MYENGINE_RUNTIME_API std::string BuildCacheKey(const std::filesystem::path& source, const std::filesystem::path& allowedRoot,
                          const std::vector<ShaderBackend>& backends, const std::string& targetPlatform,
                          const std::string& settingsJson, std::vector<std::string>* outDependencies = nullptr,
                          std::string* error = nullptr);
MYENGINE_RUNTIME_API ShaderCookResult Cook(const ShaderCookRequest& request, std::string* error = nullptr);
} // namespace ShaderCooker
