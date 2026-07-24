#include "Assets/AssetImporter.h"
#include "Renderer/ShaderCooker.h"

#include <algorithm>
#include <cctype>

namespace {
std::filesystem::path FindShaderContentRoot(std::filesystem::path sourcePath) {
    std::error_code ec;
    sourcePath = std::filesystem::absolute(std::move(sourcePath), ec).lexically_normal();
    std::filesystem::path cursor = sourcePath.parent_path();
    while (!cursor.empty()) {
        const std::string name = cursor.filename().string();
        if (name == "Content" || name == "EngineContent")
            return cursor;
        const auto parent = cursor.parent_path();
        if (parent == cursor)
            break;
        cursor = parent;
    }
    return sourcePath.parent_path();
}

class ShaderAssetImporter final : public IAssetImporter {
public:
    const char* GetName() const override { return "shader"; }
    uint32_t GetVersion() const override { return 1; }

    bool Supports(const std::filesystem::path& sourcePath) const override {
        std::string extension = sourcePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return extension == ".shader";
    }

    ImportResult Import(const ImportRequest& request) const override {
        ImportResult result;
        ShaderCookRequest cookRequest;
        cookRequest.sourcePath = request.sourcePath;
        cookRequest.artifactPath = request.artifactPath;
        cookRequest.allowedRoot = FindShaderContentRoot(request.sourcePath);
        cookRequest.targetPlatform = request.targetPlatform;
        cookRequest.settingsJson = request.settingsJson;

        std::string error;
        ShaderCookResult cooked = ShaderCooker::Cook(cookRequest, &error);
        result.succeeded = cooked.succeeded;
        result.type = "shader";
        result.dependencies = std::move(cooked.dependencies);
        result.diagnostics = std::move(cooked.diagnostics);
        if (!result.succeeded && result.diagnostics.empty())
            result.diagnostics.push_back({"error", error.empty() ? "shader cook failed" : error});
        return result;
    }
};
} // namespace

std::unique_ptr<IAssetImporter> CreateShaderAssetImporter() {
    return std::make_unique<ShaderAssetImporter>();
}
