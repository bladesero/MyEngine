#include "Assets/AssetImporter.h"

#include "Assets/AssetManager.h"
#include "Assets/MeshSdfVoxel.h"
#include "Assets/ModelAsset.h"

#include <algorithm>
#include <cctype>

namespace {
std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

bool CopyArtifact(const ImportRequest& request, ImportResult& result)
{
    std::error_code error;
    std::filesystem::create_directories(request.artifactPath.parent_path(), error);
    const auto temporary = request.artifactPath.string() + ".tmp";
    std::filesystem::copy_file(request.sourcePath, temporary,
        std::filesystem::copy_options::overwrite_existing, error);
    if (!error) {
        std::filesystem::remove(request.artifactPath, error);
        error.clear();
        std::filesystem::rename(temporary, request.artifactPath, error);
    }
    if (error) {
        std::filesystem::remove(temporary, error);
        result.diagnostics.push_back({"error", "artifact commit failed: " + error.message()});
        return false;
    }
    return true;
}

bool IsModelExtension(const std::string& extension)
{
    return extension == ".obj" || extension == ".gltf" || extension == ".glb";
}

bool ShouldSkipGltfSidecarCopy(const std::filesystem::path& sourceRoot,
                               const std::filesystem::path& sourcePath,
                               const std::filesystem::path& path)
{
    if (path == sourcePath) return true;
    const std::string extension = LowerExtension(path);
    if (extension == ".meta" || extension == ".sdfvox.xml") return true;
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(path, sourceRoot, ec);
    return ec || relative.empty() || relative.generic_string().find("..") == 0;
}

void MirrorGltfDependencies(const ImportRequest& request, ImportResult& result)
{
    if (LowerExtension(request.sourcePath) != ".gltf") return;
    const std::filesystem::path sourceRoot = request.sourcePath.parent_path();
    const std::filesystem::path artifactRoot = request.artifactPath.parent_path();
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(sourceRoot, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::filesystem::path source = it->path();
        if (ShouldSkipGltfSidecarCopy(sourceRoot, request.sourcePath, source)) continue;
        const std::filesystem::path relative = std::filesystem::relative(source, sourceRoot, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::path destination = artifactRoot / relative;
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) {
            result.diagnostics.push_back({
                "warning",
                "glTF dependency directory copy failed: " + ec.message()
            });
            ec.clear();
            continue;
        }
        std::filesystem::copy_file(source, destination,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            result.diagnostics.push_back({
                "warning",
                "glTF dependency copy failed: " + source.string() + ": " + ec.message()
            });
            ec.clear();
        }
    }
}

class PassthroughAssetImporter final : public IAssetImporter {
public:
    const char* GetName() const override { return "passthrough"; }
    uint32_t GetVersion() const override { return 1; }

    bool Supports(const std::filesystem::path& sourcePath) const override {
        const std::string extension = LowerExtension(sourcePath);
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
               extension == ".bmp" || extension == ".tga" || extension == ".hdr" ||
               IsModelExtension(extension);
    }

    ImportResult Import(const ImportRequest& request) const override {
        ImportResult result;
        if (!CopyArtifact(request, result)) return result;
        const std::string extension = LowerExtension(request.sourcePath);
        result.type = IsModelExtension(extension) ? "model" : "texture";
        result.succeeded = true;
        return result;
    }
};

class ModelSdfVoxelAssetImporter final : public IAssetImporter {
public:
    const char* GetName() const override { return "model-sdf-voxel"; }
    uint32_t GetVersion() const override { return 1; }

    bool Supports(const std::filesystem::path& sourcePath) const override {
        return IsModelExtension(LowerExtension(sourcePath));
    }

    ImportResult Import(const ImportRequest& request) const override {
        ImportResult result;
        result.type = "model";
        if (!CopyArtifact(request, result)) return result;
        MirrorGltfDependencies(request, result);

        if (!request.bakeSdfVoxel) {
            result.succeeded = true;
            return result;
        }

        ModelHandle model = AssetManager::Get().Load<ModelAsset>(request.artifactPath.string());
        if (!model && request.sourcePath != request.artifactPath) {
            model = AssetManager::Get().Load<ModelAsset>(request.sourcePath.string());
        }
        MeshAsset* mesh = model ? model->GetMeshPtr() : nullptr;
        if (!mesh) {
            result.diagnostics.push_back({
                "warning",
                "SDF/voxel bake skipped: imported model could not be loaded"
            });
            result.succeeded = true;
            return result;
        }

        MeshSdfVoxelBakeResult bake = MeshSdfVoxelBaker::BakeMedium(*mesh);
        for (const std::string& warning : bake.warnings) {
            result.diagnostics.push_back({"warning", "SDF/voxel bake: " + warning});
        }
        if (!bake.succeeded) {
            result.diagnostics.push_back({
                "warning",
                "SDF/voxel bake skipped: " + bake.error
            });
            result.succeeded = true;
            return result;
        }

        const std::filesystem::path sidecarPath =
            request.artifactPath.parent_path() /
            (request.artifactPath.stem().string() + ".sdfvox.xml");
        std::string error;
        if (!MeshSdfVoxelXml::Save(sidecarPath, bake.data, &error)) {
            result.diagnostics.push_back({
                "warning",
                "SDF/voxel XML write failed: " + error
            });
        } else {
            mesh->SetSdfVoxelPath(sidecarPath);
        }
        result.succeeded = true;
        return result;
    }
};
}

std::unique_ptr<IAssetImporter> CreatePassthroughAssetImporter() {
    return std::make_unique<PassthroughAssetImporter>();
}

std::unique_ptr<IAssetImporter> CreateModelSdfVoxelAssetImporter() {
    return std::make_unique<ModelSdfVoxelAssetImporter>();
}
