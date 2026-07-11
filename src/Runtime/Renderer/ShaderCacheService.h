#pragma once

#include "Assets/ShaderAsset.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

enum class ShaderCacheMode {
    EditorOnDemandCompile,
    RuntimeCookedOnly
};

struct ShaderCacheRequest {
    std::filesystem::path sourcePath;
    std::vector<ShaderBackend> backends;
    bool allowCompile = false;
};

struct ShaderCacheResult {
    bool succeeded = false;
    bool cacheHit = false;
    std::filesystem::path artifactPath;
    std::string diagnostic;
};

class ShaderCacheService {
public:
    using Resolver = std::function<ShaderCacheResult(const ShaderCacheRequest&)>;

    static ShaderCacheService& Get();

    void SetResolver(Resolver resolver);
    void ClearResolver();
    ShaderCacheResult EnsureShaderArtifact(const ShaderCacheRequest& request) const;

private:
    Resolver m_Resolver;
};
