#include "Assets/AssetManager.h"

#include <algorithm>
#include <filesystem>

namespace {

size_t TypeIndex(AssetType t) {
    const size_t i = static_cast<size_t>(t);
    return i < 6 ? i : 0;
}

size_t EstimateAssetCpuBytes(const Asset& a) {
    switch (a.GetType()) {
        case AssetType::Texture: {
            const auto& tx = static_cast<const TextureAsset&>(a);
            return tx.GetPixelData().size() + 512;
        }
        case AssetType::Mesh: {
            const auto& m = static_cast<const MeshAsset&>(a);
            return m.GetVertices().size() * sizeof(MeshVertex) + m.GetIndices().size() * sizeof(uint32_t) + 512;
        }
        case AssetType::Material: {
            const auto& mat = static_cast<const MaterialAsset&>(a);
            return 1024 + mat.GetTextures().size() * 128 + mat.GetParams().size() * 64;
        }
        case AssetType::Model: {
            const auto& mod = static_cast<const ModelAsset&>(a);
            size_t n = 2048 + mod.GetNodes().size() * 128;
            n += static_cast<size_t>(std::max(0, mod.MaterialCount())) * 512;
            return n;
        }
        default:
            return 256;
    }
}

} // namespace

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

void AssetManager::Unload(const std::string& path) {
    Unload(MakeAssetID(NormalizePath(path)));
}

void AssetManager::Unload(AssetID id) {
    auto it = m_Cache.find(id);
    if (it != m_Cache.end()) {
        Logger::Info("[AssetManager] Unload '", it->second->GetName(), "'");
        ReleaseAssetMemoryFor(*it->second);
        m_Cache.erase(it);
    }
}

void AssetManager::UnloadUnreferenced() {
    for (auto it = m_Cache.begin(); it != m_Cache.end();) {
        if (it->second.use_count() == 1) {
            Logger::Info("[AssetManager] GC '", it->second->GetName(), "'");
            ReleaseAssetMemoryFor(*it->second);
            it = m_Cache.erase(it);
        } else {
            ++it;
        }
    }
}

void AssetManager::Clear() {
    Logger::Info("[AssetManager] Clearing all assets (", m_Cache.size(), ")");
    for (auto& kv : m_Cache) {
        ReleaseAssetMemoryFor(*kv.second);
    }
    m_Cache.clear();
}

void AssetManager::PrintStats() const {
    Logger::Info("[AssetManager] Cached assets: ", m_Cache.size());
    for (const auto& kv : m_Cache) {
        const auto& asset = kv.second;
        Logger::Info("  [", AssetTypeToString(asset->GetType()), "] ", asset->GetName(),
                     "  refs=", asset.use_count() - 1);
    }
    LogAssetMemorySummary();
}

void AssetManager::SetAssetCpuBudgetBytes(size_t bytes) {
    m_AssetCpuBudgetBytes = bytes;
    MaybeWarnAssetBudget();
}

size_t AssetManager::GetEstimatedAssetCpuBytesByType(AssetType type) const {
    return m_AssetCpuBytesByType[TypeIndex(type)];
}

void AssetManager::LogAssetMemorySummary() const {
    Logger::Info("[AssetManager] Estimated CPU asset memory total=", m_AssetCpuTotalBytes);
    if (m_AssetCpuBudgetBytes != 0) {
        Logger::Info("[AssetManager]   budget=", m_AssetCpuBudgetBytes);
    }
    for (size_t i = 0; i < m_AssetCpuBytesByType.size(); ++i) {
        if (m_AssetCpuBytesByType[i] == 0) {
            continue;
        }
        const auto t = static_cast<AssetType>(i);
        Logger::Info("[AssetManager]   ", AssetTypeToString(t), ": ", m_AssetCpuBytesByType[i]);
    }
}

void AssetManager::RegisterAssetMemoryFor(const Asset& asset) {
    const size_t est = EstimateAssetCpuBytes(asset);
    const size_t idx = TypeIndex(asset.GetType());
    m_AssetCpuBytesByType[idx] += est;
    m_AssetCpuTotalBytes += est;
    MaybeWarnAssetBudget();
}

void AssetManager::ReleaseAssetMemoryFor(const Asset& asset) {
    const size_t est = EstimateAssetCpuBytes(asset);
    const size_t idx = TypeIndex(asset.GetType());
    if (m_AssetCpuBytesByType[idx] >= est) {
        m_AssetCpuBytesByType[idx] -= est;
    } else {
        m_AssetCpuBytesByType[idx] = 0;
    }
    if (m_AssetCpuTotalBytes >= est) {
        m_AssetCpuTotalBytes -= est;
    } else {
        m_AssetCpuTotalBytes = 0;
    }
}

void AssetManager::MaybeWarnAssetBudget() const {
    if (m_AssetCpuBudgetBytes == 0 || m_AssetCpuTotalBytes <= m_AssetCpuBudgetBytes) {
        return;
    }
    Logger::Warn("[AssetManager] Estimated asset CPU memory exceeds budget: ", m_AssetCpuTotalBytes,
                 " > ", m_AssetCpuBudgetBytes);
}
