#include "Assets/AssetManager.h"

#include <algorithm>
#include <filesystem>

std::string AssetManager::NormalizePath(const std::string& path) {
    if (path.rfind("__builtin__/", 0) == 0 || path.rfind("__builtin__\\", 0) == 0) {
        return path;
    }

    return std::filesystem::absolute(std::filesystem::path(path))
        .lexically_normal()
        .string();
}

AssetManager::AssetManager() {
    RegisterDefaultLoaders();
}

void AssetManager::RegisterDefaultLoaders() {
    // Common texture formats supported by stb_image.
    auto textureLoader = [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadTextureAssetFromFile(path));
    };

    RegisterLoader("png", textureLoader);
    RegisterLoader("jpg", textureLoader);
    RegisterLoader("jpeg", textureLoader);
    RegisterLoader("bmp", textureLoader);
    RegisterLoader("tga", textureLoader);
    RegisterLoader("gif", textureLoader);
    RegisterLoader("psd", textureLoader);
    RegisterLoader("hdr", textureLoader);
    RegisterLoader("pic", textureLoader);
    RegisterLoader("pnm", textureLoader);
    RegisterLoader("ppm", textureLoader);
    RegisterLoader("pgm", textureLoader);
    RegisterLoader("pam", textureLoader);

    RegisterLoader("obj", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadModelAssetFromObj(path));
    });
}

TextureHandle AssetManager::GetWhiteTexture()
{
    const std::string path = "__builtin__/White";
    if (IsLoaded(path)) return GetByPath<TextureAsset>(path);
    return Register(TextureAsset::CreateSolid("White", 255, 255, 255, 255));
}

TextureHandle AssetManager::GetBlackTexture()
{
    const std::string path = "__builtin__/Black";
    if (IsLoaded(path)) return GetByPath<TextureAsset>(path);
    return Register(TextureAsset::CreateSolid("Black", 0, 0, 0, 255));
}

TextureHandle AssetManager::GetNormalTexture()
{
    const std::string path = "__builtin__/FlatNormal";
    if (IsLoaded(path)) return GetByPath<TextureAsset>(path);
    // (128, 128, 255) = (0.5, 0.5, 1.0) in RGBA8 = flat +Z normal
    return Register(TextureAsset::CreateSolid("FlatNormal", 128, 128, 255, 255));
}

MeshHandle AssetManager::GetTriangleMesh()
{
    const std::string path = "__builtin__/Triangle";
    if (IsLoaded(path)) return GetByPath<MeshAsset>(path);
    return Register(MeshAsset::CreateTriangle("Triangle"));
}

MeshHandle AssetManager::GetQuadMesh()
{
    const std::string path = "__builtin__/Quad";
    if (IsLoaded(path)) return GetByPath<MeshAsset>(path);
    return Register(MeshAsset::CreateQuad("Quad"));
}

MeshHandle AssetManager::GetCubeMesh()
{
    const std::string path = "__builtin__/Cube";
    if (IsLoaded(path)) return GetByPath<MeshAsset>(path);
    return Register(MeshAsset::CreateCube("Cube"));
}

std::vector<std::string> AssetManager::GetCachedPathsByType(AssetType type) const
{
    std::vector<std::string> out;
    for (const auto& kv : m_Cache) {
        const auto& asset = kv.second;
        if (asset && asset->GetType() == type) {
            out.push_back(asset->GetPath());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

MaterialHandle AssetManager::GetDefaultMaterial()
{
    const std::string path = "__builtin__/Default";
    if (IsLoaded(path)) return GetByPath<MaterialAsset>(path);

    auto mat = MaterialAsset::CreateDefault("Default");
    // Bind default textures used by the mesh shader.
    mat->SetTexture("BaseColorMap", GetWhiteTexture());
    mat->SetTexture("NormalMap",    GetNormalTexture());
    return Register(std::move(mat));
}
