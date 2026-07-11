#pragma once

#include "Assets/AssetDatabase.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ImportRequest {
    std::filesystem::path sourcePath;
    std::filesystem::path artifactPath;
    std::string uuid;
    std::string settingsJson = "{}";
    std::string targetPlatform = "windows-x64";
};

struct ImportResult {
    bool succeeded = false;
    std::string type;
    std::vector<std::string> dependencies;
    std::vector<AssetDiagnostic> diagnostics;
};

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;
    virtual const char* GetName() const = 0;
    virtual uint32_t GetVersion() const = 0;
    virtual bool Supports(const std::filesystem::path& sourcePath) const = 0;
    virtual std::string GetArtifactExtension(const std::filesystem::path& sourcePath) const;
    virtual ImportResult Import(const ImportRequest& request) const = 0;
};

std::unique_ptr<IAssetImporter> CreatePassthroughAssetImporter();
std::unique_ptr<IAssetImporter> CreateGltfModelAssetImporter();
std::unique_ptr<IAssetImporter> CreateShaderAssetImporter();
