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

class PassthroughAssetImporter final : public IAssetImporter {
public:
    const char* GetName() const override { return "passthrough"; }
    uint32_t GetVersion() const override { return 1; }

    bool Supports(const std::filesystem::path& sourcePath) const override {
        std::string extension = sourcePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
               extension == ".tga" || extension == ".hdr" || extension == ".obj" || extension == ".gltf" ||
               extension == ".glb";
    }

    ImportResult Import(const ImportRequest& request) const override {
        ImportResult result;
        std::error_code error;
        std::filesystem::create_directories(request.artifactPath.parent_path(), error);
        const auto temporary = request.artifactPath.string() + ".tmp";
        std::filesystem::copy_file(request.sourcePath, temporary, std::filesystem::copy_options::overwrite_existing,
                                   error);
        if (!error) {
            std::filesystem::remove(request.artifactPath, error);
            error.clear();
            std::filesystem::rename(temporary, request.artifactPath, error);
        }
        if (error) {
            std::filesystem::remove(temporary, error);
            result.diagnostics.push_back({"error", "artifact commit failed: " + error.message()});
            return result;
        }
        std::string extension = request.sourcePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        result.type = (extension == ".obj" || extension == ".gltf" || extension == ".glb") ? "model" : "texture";
        result.succeeded = true;
        return result;
    }
};

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
        const std::filesystem::path allowedRoot = FindShaderContentRoot(request.sourcePath);
        ShaderCookRequest cookRequest;
        cookRequest.sourcePath = request.sourcePath;
        cookRequest.artifactPath = request.artifactPath;
        cookRequest.allowedRoot = allowedRoot;
        cookRequest.targetPlatform = request.targetPlatform;
        cookRequest.settingsJson = request.settingsJson;

        std::string error;
        ShaderCookResult cooked = ShaderCooker::Cook(cookRequest, &error);
        result.succeeded = cooked.succeeded;
        result.type = "shader";
        result.diagnostics = std::move(cooked.diagnostics);
        if (!result.succeeded && result.diagnostics.empty()) {
            result.diagnostics.push_back({"error", error.empty() ? "shader cook failed" : error});
        }
        return result;
    }
};
} // namespace

std::string IAssetImporter::GetArtifactExtension(const std::filesystem::path& sourcePath) const {
    return sourcePath.extension().string();
}

std::unique_ptr<IAssetImporter> CreatePassthroughAssetImporter() {
    return std::make_unique<PassthroughAssetImporter>();
}

std::unique_ptr<IAssetImporter> CreateShaderAssetImporter() {
    return std::make_unique<ShaderAssetImporter>();
}
