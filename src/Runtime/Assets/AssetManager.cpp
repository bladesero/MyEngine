#include "Assets/AssetManager.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetMeta.h"
#include "Core/RuntimeFileSystem.h"
#include "Core/Sha256.h"

#include <algorithm>
#include <filesystem>
#include <future>
#include <system_error>

namespace {

size_t TypeIndex(AssetType t) {
    const size_t i = static_cast<size_t>(t);
    return i < 13 ? i : 0;
}

size_t EstimateAssetCpuBytes(const Asset& a) {
    switch (a.GetType()) {
        case AssetType::Texture: {
            const auto& tx = static_cast<const TextureAsset&>(a);
            size_t bytes = tx.GetPixelData().size() + 512;
            for (const TextureMipData& mip : tx.GetMips()) {
                bytes += mip.rgba8.size() + mip.bc1.size();
            }
            return bytes;
        }
        case AssetType::Mesh: {
            const auto& m = static_cast<const MeshAsset&>(a);
            size_t bytes = m.GetVertices().size() * sizeof(MeshVertex) +
                m.GetIndices().size() * sizeof(uint32_t) + 512;
            for (const MeshLod& lod : m.GetLods()) {
                bytes += lod.indices.size() * sizeof(uint32_t);
            }
            bytes += m.GetColliderData().vertices.size() * sizeof(Vec3) +
                m.GetColliderData().indices.size() * sizeof(uint32_t);
            return bytes;
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
        case AssetType::Shader: {
            const auto& shader = static_cast<const ShaderAsset&>(a);
            size_t bytes = 512;
                for (size_t backend = 0; backend < kShaderBackendCount; ++backend)
                    for (size_t stage = 0; stage < kShaderStageCount; ++stage)
                    bytes += shader.GetBytecode(static_cast<ShaderBackend>(backend),
                                                static_cast<ShaderStage>(stage)).size();
            return bytes;
        }
        case AssetType::AudioClip: {
            const auto& clip = static_cast<const AudioClipAsset&>(a);
            return 512 + static_cast<size_t>(clip.GetChannels()) * 64;
        }
        case AssetType::Script: {
            const auto& script = static_cast<const ScriptAsset&>(a);
            return 256 + script.GetSource().size() + script.GetClasses().size() * 128;
        }
        default:
            return 256;
    }
}

bool IsBuiltinMaterialPath(const std::string& path)
{
    return path.rfind("__builtin__/", 0) == 0 || path.rfind("__builtin__\\", 0) == 0;
}

TextureHandle GetCheckerTexture()
{
    auto& assets = AssetManager::Get();
    const std::string path = "__builtin__/Checker";
    TextureHandle cached = assets.GetByPath<TextureAsset>(path);
    if (cached.IsValid()) return cached;

    constexpr int kTexSize = 16;
    constexpr int kCellSize = 2;
    std::vector<uint8_t> pixels(kTexSize * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            const bool light = ((x / kCellSize) + (y / kCellSize)) % 2 == 0;
            const int idx = (y * kTexSize + x) * 4;
            pixels[idx + 0] = light ? 230 : 50;
            pixels[idx + 1] = light ? 130 : 50;
            pixels[idx + 2] = light ? 30 : 50;
            pixels[idx + 3] = 255;
        }
    }

    auto checkerTex = std::make_shared<TextureAsset>(path);
    checkerTex->SetName("Checker");
    TextureDesc desc;
    desc.width = kTexSize;
    desc.height = kTexSize;
    desc.sRGB = false;
    checkerTex->SetPixelData(std::move(pixels), desc);
    return assets.Register(std::move(checkerTex));
}

MaterialHandle CreateKnownBuiltinMaterial(const std::string& path)
{
    auto& assets = AssetManager::Get();
    if (path == "__builtin__/CheckerMat") {
        auto material = MaterialAsset::CreateDefault("CheckerMat");
        material->SetTexture("BaseColorMap", GetCheckerTexture());
        material->SetParam("Metallic", MaterialParam::FromFloat(0.15f));
        material->SetParam("Roughness", MaterialParam::FromFloat(0.35f));
        return assets.Register(std::move(material));
    }
    if (path == "__builtin__/DynamicPbrMat") {
        auto material = MaterialAsset::CreateDefault("DynamicPbrMat");
        material->SetTexture("BaseColorMap", assets.GetWhiteTexture());
        material->SetParam("BaseColor", MaterialParam::FromColor({0.1f, 0.7f, 1.0f}));
        material->SetParam("Metallic", MaterialParam::FromFloat(0.75f));
        material->SetParam("Roughness", MaterialParam::FromFloat(0.2f));
        return assets.Register(std::move(material));
    }
    if (path == "__builtin__/GroundMat") {
        auto material = MaterialAsset::CreateDefault("GroundMat");
        material->SetTexture("BaseColorMap", assets.GetWhiteTexture());
        material->SetParam("BaseColor", MaterialParam::FromColor({0.55f, 0.55f, 0.52f}));
        material->SetParam("Metallic", MaterialParam::FromFloat(0.0f));
        material->SetParam("Roughness", MaterialParam::FromFloat(0.9f));
        material->SetParam("AmbientOcclusion", MaterialParam::FromFloat(1.0f));
        return assets.Register(std::move(material));
    }
    if (path == "__builtin__/SkinPbrMat") {
        auto material = MaterialAsset::CreateDefault("SkinPbrMat");
        material->SetTexture("BaseColorMap", assets.GetWhiteTexture());
        material->SetParam("BaseColor", MaterialParam::FromColor({0.85f, 0.25f, 0.15f}));
        material->SetParam("Metallic", MaterialParam::FromFloat(0.05f));
        material->SetParam("Roughness", MaterialParam::FromFloat(0.5f));
        return assets.Register(std::move(material));
    }
    return {};
}

} // namespace

AssetManager& AssetManager::Get() {
    static AssetManager instance;
    return instance;
}

std::string AssetManager::NormalizePath(const std::string& path) const {
    return ResolvePath(path);
}

void AssetManager::SetProjectRoot(std::filesystem::path root) {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    if (root.empty()) {
        m_ProjectRoot.clear();
        return;
    }
    std::error_code error;
    m_ProjectRoot = std::filesystem::absolute(std::move(root), error).lexically_normal();
    if (error) m_ProjectRoot.clear();
}

void AssetManager::RegisterPersistentIdentity(const std::string& path,
                                              const std::string& uuid) {
    if (uuid.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_RegisteredIdentities[NormalizePath(path)] = uuid;
}

std::string AssetManager::ResolvePath(const std::string& path) const {
    if (path.rfind("__builtin__/", 0) == 0 || path.rfind("__builtin__\\", 0) == 0) {
        return path;
    }
    const size_t fragmentPosition = path.find('#');
    const std::string sourcePath = fragmentPosition == std::string::npos
        ? path : path.substr(0, fragmentPosition);
    const std::string fragment = fragmentPosition == std::string::npos
        ? std::string{} : path.substr(fragmentPosition);
    const std::filesystem::path input(sourcePath);
    std::filesystem::path resolved;
    const std::string generic = input.generic_string();
    std::error_code error;
    if (generic.rfind("Content/Engine/", 0) == 0 &&
        RuntimeFileSystem::Get().Exists(generic)) {
        return generic + fragment;
    }
    if (!m_EngineContentRoot.empty() && generic.rfind("Content/Engine/", 0) == 0) {
        resolved = std::filesystem::absolute(
            m_EngineContentRoot / std::filesystem::path(generic.substr(15)), error).lexically_normal();
    } else if (input.is_absolute() || m_ProjectRoot.empty()) {
        resolved = std::filesystem::absolute(input, error).lexically_normal();
    } else {
        resolved = std::filesystem::absolute(m_ProjectRoot / input, error).lexically_normal();
    }
    if (error) resolved = input.lexically_normal();
    return resolved.string() + fragment;
}

std::string AssetManager::MakeProjectRelativePath(const std::string& path) const {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    if (path.empty() || path.rfind("__builtin__/", 0) == 0 ||
        path.rfind("__builtin__\\", 0) == 0 || m_ProjectRoot.empty()) {
        return path;
    }
    const std::string resolvedPath = ResolvePath(path);
    const auto aliasIt = m_ArtifactToSourcePath.find(resolvedPath);
    if (aliasIt != m_ArtifactToSourcePath.end() && aliasIt->second != resolvedPath) {
        return MakeProjectRelativePath(aliasIt->second);
    }
    const size_t fragmentPosition = resolvedPath.find('#');
    const std::string sourcePath = fragmentPosition == std::string::npos
        ? resolvedPath : resolvedPath.substr(0, fragmentPosition);
    const std::string fragment = fragmentPosition == std::string::npos
        ? std::string{} : resolvedPath.substr(fragmentPosition);
    if (!fragment.empty()) {
        const auto baseAliasIt = m_ArtifactToSourcePath.find(sourcePath);
        if (baseAliasIt != m_ArtifactToSourcePath.end() && baseAliasIt->second != sourcePath) {
            return MakeProjectRelativePath(baseAliasIt->second + fragment);
        }
    }
    const std::filesystem::path absolutePath(sourcePath);
    std::error_code error;
    if (!m_EngineContentRoot.empty()) {
        const auto engineRelative = std::filesystem::relative(
            absolutePath, m_EngineContentRoot, error);
        if (!error && !engineRelative.empty() && !engineRelative.is_absolute() &&
            *engineRelative.begin() != "..") {
            return (std::filesystem::path("Content/Engine") / engineRelative).generic_string() + fragment;
        }
        error.clear();
    }
    const std::filesystem::path contentRoot = m_ProjectRoot / "Content";
    const std::filesystem::path contentRelative =
        std::filesystem::relative(absolutePath, contentRoot, error);
    if (error || contentRelative.empty() || contentRelative.is_absolute() ||
        (contentRelative.begin() != contentRelative.end() &&
         *contentRelative.begin() == "..")) {
        return absolutePath.string() + fragment;
    }
    return (std::filesystem::path("Content") / contentRelative).generic_string() + fragment;
}

MeshHandle AssetManager::ResolveMeshReference(const std::string& path) {
    if (path.empty()) return {};
    if (path == "__builtin__/Triangle") return GetTriangleMesh();
    if (path == "__builtin__/Quad") return GetQuadMesh();
    if (path == "__builtin__/Cube") return GetCubeMesh();
    MeshHandle mesh = GetByPath<MeshAsset>(path);
    return mesh ? mesh : Load<MeshAsset>(path);
}

MaterialHandle AssetManager::ResolveMaterialReference(
    const std::string& path, const std::string& meshPath) {
    if (path.empty()) return {};
    if (path == "__builtin__/Default") return GetDefaultMaterial();
    MaterialHandle material = GetByPath<MaterialAsset>(path);
    if (material) return material;

    if (IsBuiltinMaterialPath(path)) {
        MaterialHandle builtin = CreateKnownBuiltinMaterial(path);
        if (builtin) return builtin;
    }

    // Older scenes stored imported model materials as __builtin__/Name. Once the
    // mesh has loaded its source model, recover the matching material by name.
    if (path.rfind("__builtin__/", 0) == 0 && !meshPath.empty()) {
        const size_t fragment = meshPath.find('#');
        if (fragment != std::string::npos) {
            const std::string modelPath = meshPath.substr(0, fragment);
            ModelHandle model = GetByPath<ModelAsset>(modelPath);
            if (!model) model = Load<ModelAsset>(modelPath);
            const std::string legacyName = path.substr(std::string("__builtin__/").size());
            if (model) {
                for (const MaterialHandle& candidate : model->GetMaterials()) {
                    if (candidate && candidate->GetName() == legacyName) return candidate;
                }
            }
        }
        Logger::Warn("[AssetManager] Could not resolve legacy model material: ", path);
        return {};
    }
    return Load<MaterialAsset>(path);
}

void AssetManager::ApplyPersistentIdentity(Asset& asset)
{
    const std::string& path = asset.GetPath();
    if (path.rfind("__builtin__/", 0) == 0 || path.rfind("__builtin__\\", 0) == 0) {
        return;
    }

    const size_t fragment = path.find('#');
    const std::string sourcePath =
        fragment == std::string::npos ? path : path.substr(0, fragment);
    const auto registered = m_RegisteredIdentities.find(NormalizePath(sourcePath));
    if (registered != m_RegisteredIdentities.end()) {
        const std::string uuid = fragment == std::string::npos
            ? registered->second : registered->second + path.substr(fragment);
        asset.SetPersistentIdentity(MakeAssetID(uuid), uuid);
        return;
    }
    if (!std::filesystem::exists(sourcePath)) return;

    const auto meta = AssetMeta::Load(sourcePath);
    if (!meta) return;
    const std::string uuid = fragment == std::string::npos
        ? meta->uuid
        : meta->uuid + path.substr(fragment);
    asset.SetPersistentIdentity(MakeAssetID(uuid), uuid);
}

std::string AssetManager::ResolveImportedArtifactPath(const std::string& normalizedPath,
                                                      std::string* outUuid) const
{
    if (m_ProjectRoot.empty()) return {};
    const std::string extension = GetExtension(normalizedPath);
    if (extension != "gltf" && extension != "glb" && extension != "shader") return {};
    const std::string expectedType = extension == "shader" ? "shader" : "model";
    const std::string expectedArtifactExtension =
        extension == "shader" ? "shader" : "modelbin";

    AssetDatabase database;
    std::string error;
    if (!database.Open(m_ProjectRoot / ".myengine" / "AssetDatabase.json", &error)) {
        return {};
    }

    const AssetRecord* record = database.FindBySourcePath(normalizedPath);
    if (!record) {
        std::error_code ec;
        const std::filesystem::path relative =
            std::filesystem::relative(std::filesystem::path(normalizedPath), m_ProjectRoot, ec);
        if (!ec && !relative.empty() && !relative.is_absolute()) {
            record = database.FindBySourcePath(relative.generic_string());
        }
    }
    if (!record || record->state != AssetImportState::Ready ||
        record->artifactPath.empty() || record->type != expectedType) {
        return {};
    }

    std::error_code ec;
    const std::filesystem::path artifact =
        std::filesystem::absolute(record->artifactPath, ec).lexically_normal();
    if (ec || !std::filesystem::is_regular_file(artifact, ec) || ec) {
        Logger::Warn("[AssetCache] imported artifact stale type=", expectedType,
                     " source=", normalizedPath);
        return {};
    }
    if (GetExtension(artifact.string()) != expectedArtifactExtension) return {};
    if (outUuid) *outUuid = record->uuid;
    return artifact.string();
}

std::shared_ptr<Asset> AssetManager::LoadAsset(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const std::string normalizedPath = NormalizePath(path);
    const auto pathIt = m_PathToID.find(normalizedPath);
    if (pathIt != m_PathToID.end()) {
        const auto cached = m_Cache.find(pathIt->second);
        if (cached != m_Cache.end()) { m_LastUsed[pathIt->second]=++m_UseClock; return cached->second; }
    }

    const size_t fragment = normalizedPath.find('#');
    if (fragment != std::string::npos) {
        const std::string sourcePath = normalizedPath.substr(0, fragment);
        if (!LoadAsset(sourcePath)) return {};
        const auto subAsset = m_PathToID.find(normalizedPath);
        if (subAsset != m_PathToID.end()) {
            const auto cached = m_Cache.find(subAsset->second);
            if (cached != m_Cache.end()) return cached->second;
        }
        std::string artifactUuid;
        const std::string artifactPath =
            ResolveImportedArtifactPath(sourcePath, &artifactUuid);
        if (!artifactPath.empty()) {
            const std::string artifactSubAssetPath =
                artifactPath + normalizedPath.substr(fragment);
            const auto artifactSubAsset = m_PathToID.find(artifactSubAssetPath);
            if (artifactSubAsset != m_PathToID.end()) {
                m_ArtifactToSourcePath[NormalizePath(artifactSubAssetPath)] = normalizedPath;
                m_ArtifactToSourcePath[NormalizePath(artifactPath)] = sourcePath;
                m_PathToID[normalizedPath] = artifactSubAsset->second;
                const auto cached = m_Cache.find(artifactSubAsset->second);
                if (cached != m_Cache.end()) return cached->second;
            }
        }
        Logger::Error("[AssetManager] Source did not provide sub-asset: ", normalizedPath);
        return {};
    }

    std::string artifactUuid;
    const std::string artifactPath =
        ResolveImportedArtifactPath(normalizedPath, &artifactUuid);
    if (!artifactPath.empty()) {
        const auto artifactIt = m_PathToID.find(artifactPath);
        if (artifactIt != m_PathToID.end()) {
            const auto cached = m_Cache.find(artifactIt->second);
            if (cached != m_Cache.end()) return cached->second;
        }
        const auto artifactLoader = m_Loaders.find(GetExtension(artifactPath));
        if (artifactLoader != m_Loaders.end()) {
            std::shared_ptr<Asset> artifact =
                InvokeLoader(artifactPath, artifactLoader->second);
            if (artifact) {
                if (!artifactUuid.empty()) {
                    m_RegisteredIdentities[NormalizePath(artifactPath)] = artifactUuid;
                }
                ApplyPersistentIdentity(*artifact);
                const AssetID id = artifact->GetID();
                m_Cache[id] = artifact;
                m_PathToID[artifactPath] = id;
                m_PathToID[normalizedPath] = id;
                m_ArtifactToSourcePath[NormalizePath(artifactPath)] = normalizedPath;
                RefreshDependencies(*artifact);
                RegisterAssetMemoryFor(*artifact);
                CaptureSourceWriteTime(id, artifactPath);
                Logger::Info("[AssetCache] imported artifact cache hit type=",
                             AssetTypeToString(artifact->GetType()),
                             " source=", normalizedPath);
                PublishAssetChanged({AssetChangeType::Imported, normalizedPath, id, {}});
                return artifact;
            }
            Logger::Warn("[AssetCache] imported artifact cache miss: ", normalizedPath);
        }
    }

    const std::string ext = GetExtension(normalizedPath);
    const auto loaderIt = m_Loaders.find(ext);
    if (loaderIt == m_Loaders.end()) {
        Logger::Error("[AssetManager] No loader for extension '.", ext,
                      "': ", normalizedPath);
        return {};
    }

    std::shared_ptr<Asset> asset = InvokeLoader(normalizedPath, loaderIt->second);
    if (!asset) {
        Logger::Error("[AssetManager] Loader returned null for: ", normalizedPath);
        return {};
    }

    ApplyPersistentIdentity(*asset);
    const AssetID id = asset->GetID();
    m_Cache[id] = asset;
    m_LastUsed[id] = ++m_UseClock;
    m_PathToID[normalizedPath] = id;
    RefreshDependencies(*asset);
    RegisterAssetMemoryFor(*asset);
    CaptureSourceWriteTime(id, normalizedPath);
    Logger::Info("[AssetManager] Loaded [", AssetTypeToString(asset->GetType()),
                 "] '", asset->GetName(), "' uuid=", asset->GetUuid());
    PublishAssetChanged({AssetChangeType::Imported, normalizedPath, id, {}});
    return asset;
}

std::future<std::shared_ptr<Asset>> AssetManager::LoadAsync(const std::string& path)
{
    return std::async(std::launch::async, [this, path] {
        return LoadAsset(path);
    });
}

bool AssetManager::Reload(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const std::string normalizedPath = NormalizePath(path);
    const auto pathIt = m_PathToID.find(normalizedPath);
    if (pathIt == m_PathToID.end()) return false;

    const auto cachedIt = m_Cache.find(pathIt->second);
    if (cachedIt == m_Cache.end() || !cachedIt->second) return false;

    const auto loaderIt = m_Loaders.find(GetExtension(normalizedPath));
    if (loaderIt == m_Loaders.end()) return false;

    std::shared_ptr<Asset> replacement = InvokeLoader(normalizedPath, loaderIt->second);
    if (!replacement || replacement->GetType() != cachedIt->second->GetType()) {
        Logger::Error("[AssetManager] Reload failed for: ", normalizedPath);
        PublishAssetChanged({AssetChangeType::Failed, normalizedPath,
                             cachedIt->second->GetID(), "reload failed"});
        return false;
    }

    ApplyPersistentIdentity(*replacement);
    Asset& existing = *cachedIt->second;
    ReleaseAssetMemoryFor(existing);
    if (!existing.ReloadFrom(*replacement)) {
        RegisterAssetMemoryFor(existing);
        Logger::Error("[AssetManager] Asset does not support in-place reload: ", normalizedPath);
        return false;
    }

    existing.IncrementVersion();
    RefreshDependencies(existing);
    RegisterAssetMemoryFor(existing);
    CaptureSourceWriteTime(existing.GetID(), normalizedPath);
    Logger::Info("[AssetManager] Reloaded '", existing.GetName(),
                 "' version=", existing.GetVersion());
    PublishAssetChanged({AssetChangeType::Reloaded, normalizedPath,
                         existing.GetID(), {}});
    return true;
}

AssetManager::ListenerID AssetManager::SubscribeAssetChanged(
    AssetChangedCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const ListenerID id = m_NextAssetChangedListenerID++;
    m_AssetChangedListeners.emplace(id, std::move(callback));
    return id;
}

void AssetManager::UnsubscribeAssetChanged(ListenerID listenerID) {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_AssetChangedListeners.erase(listenerID);
}

void AssetManager::PublishAssetChanged(AssetChangedEvent event) {
    std::vector<AssetChangedCallback> callbacks;
    for (const auto& [id, callback] : m_AssetChangedListeners) callbacks.push_back(callback);
    for (const auto& callback : callbacks) if (callback) callback(event);
}

size_t AssetManager::PollHotReload()
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    std::vector<std::string> changedPaths;
    for (const auto& [id, previousWriteTime] : m_SourceWriteTimes) {
        const auto assetIt = m_Cache.find(id);
        if (assetIt == m_Cache.end() || !assetIt->second) continue;
        const std::string path = NormalizePath(assetIt->second->GetPath());
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) continue;
        const auto currentWriteTime = std::filesystem::last_write_time(path, error);
        if (error) continue;
        if (currentWriteTime != previousWriteTime) {
            std::string hashError;
            const std::string currentHash = Sha256::HashFile(path, &hashError);
            if (hashError.empty() && m_SourceHashes[id] != currentHash) {
                PublishAssetChanged({AssetChangeType::SourceChanged, path, id, {}});
                changedPaths.push_back(path);
            } else {
                m_SourceWriteTimes[id] = currentWriteTime;
            }
        }
    }

    size_t reloadCount = 0;
    for (const std::string& changedPath : changedPaths) {
        reloadCount += Reload(changedPath) ? 1u : 0u;
    }
    return reloadCount;
}

std::shared_ptr<Asset> AssetManager::InvokeLoader(const std::string& normalizedPath,
                                                  const AssetLoaderFn& loader) const
{
    try {
        return loader(normalizedPath);
    }
    catch (const std::exception& e) {
        Logger::Error("[AssetManager] Loader exception for '", normalizedPath, "': ", e.what());
    }
    catch (...) {
        Logger::Error("[AssetManager] Loader unknown exception for '", normalizedPath, "'");
    }
    return {};
}

bool AssetManager::CaptureSourceWriteTime(AssetID id, const std::string& normalizedPath)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(normalizedPath, error) || error) {
        m_SourceWriteTimes.erase(id);
        return false;
    }
    const auto writeTime = std::filesystem::last_write_time(normalizedPath, error);
    if (error) {
        Logger::Warn("[AssetManager] Failed to read source timestamp: ", normalizedPath);
        return false;
    }
    m_SourceWriteTimes[id] = writeTime;
    std::string hashError;
    const std::string hash = Sha256::HashFile(normalizedPath, &hashError);
    if (hashError.empty()) m_SourceHashes[id] = hash;
    return true;
}

void AssetManager::RefreshDependencies(Asset& asset)
{
    std::vector<AssetID> dependencies;
    if (asset.GetType() == AssetType::Material) {
        const auto& material = static_cast<const MaterialAsset&>(asset);
        for (const auto& [slot, texture] : material.GetTextures()) {
            (void)slot;
            if (texture) dependencies.push_back(texture->GetID());
        }
        if (material.GetShaderAsset()) dependencies.push_back(material.GetShaderAsset()->GetID());
    } else if (asset.GetType() == AssetType::Model) {
        const auto& model = static_cast<const ModelAsset&>(asset);
        if (model.GetMesh()) dependencies.push_back(model.GetMesh()->GetID());
        for (const MaterialHandle& material : model.GetMaterials()) {
            if (material) dependencies.push_back(material->GetID());
        }
    }

    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                       dependencies.end());
    asset.SetDependencies(std::move(dependencies));
}

AssetManager::AssetManager() {
    std::filesystem::path cursor = std::filesystem::current_path();
    while (!cursor.empty()) {
        const auto candidate = cursor / "EngineContent";
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec) && !ec) {
            m_EngineContentRoot = std::filesystem::absolute(candidate).lexically_normal();
            break;
        }
        const auto parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
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
    auto gltfLoader = [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadModelAssetFromGltf(path));
    };
    RegisterLoader("gltf", gltfLoader);
    RegisterLoader("glb", gltfLoader);
    RegisterLoader("modelbin", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadModelCacheAssetFromFile(path));
    });
    RegisterLoader("mat", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadMaterialAssetFromFile(path));
    });
    RegisterLoader("rml", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(std::make_shared<UIPrefabAsset>(path));
    });
    RegisterLoader("rcss", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(std::make_shared<UIStyleAsset>(path));
    });
    RegisterLoader("ttf", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(std::make_shared<UIFontAsset>(path));
    });
    RegisterLoader("otf", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(std::make_shared<UIFontAsset>(path));
    });
    RegisterLoader("ui.json", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(std::make_shared<UIPrefabAsset>(path));
    });
    RegisterLoader("shader", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadShaderAssetFromFile(path));
    });
    auto audioLoader = [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadAudioClipAssetFromFile(path));
    };
    RegisterLoader("wav", audioLoader);
    RegisterLoader("flac", audioLoader);
    RegisterLoader("mp3", audioLoader);
    RegisterLoader("ogg", audioLoader);
    RegisterLoader("as", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadScriptAssetFromFile(path));
    });
    RegisterLoader("navmesh", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadNavMeshAssetFromFile(path));
    });
    RegisterLoader("particle", [](const std::string& path) -> std::shared_ptr<Asset> {
        return std::static_pointer_cast<Asset>(LoadParticleAssetFromFile(path));
    });
}

std::shared_future<std::shared_ptr<Asset>> AssetManager::RequestAsync(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const std::string normalized = NormalizePath(path);
    auto existing = m_AsyncRequests.find(normalized);
    if (existing != m_AsyncRequests.end()) return existing->second;
    auto request = std::async(std::launch::async, [this, normalized] {
        std::shared_ptr<Asset> result = LoadAsset(normalized);
        std::lock_guard<std::recursive_mutex> doneLock(m_Mutex);
        m_AsyncRequests.erase(normalized);
        return result;
    }).share();
    m_AsyncRequests.emplace(normalized, request);
    return request;
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
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
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
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const auto it = m_PathToID.find(NormalizePath(path));
    if (it != m_PathToID.end()) Unload(it->second);
}

void AssetManager::Unload(AssetID id) {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    auto it = m_Cache.find(id);
    if (it != m_Cache.end()) {
        const std::string path = it->second->GetPath();
        Logger::Info("[AssetManager] Unload '", it->second->GetName(), "'");
        ReleaseAssetMemoryFor(*it->second);
        for (auto pathIt=m_PathToID.begin();pathIt!=m_PathToID.end();)
            if(pathIt->second==id) pathIt=m_PathToID.erase(pathIt); else ++pathIt;
        m_SourceWriteTimes.erase(id);
        m_SourceHashes.erase(id);
        m_LastUsed.erase(id);
        m_Cache.erase(it);
        PublishAssetChanged({AssetChangeType::Removed, path, id, {}});
    }
}

void AssetManager::UnloadUnreferenced() {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    for (auto it = m_Cache.begin(); it != m_Cache.end();) {
        if (it->second.use_count() == 1) {
            const AssetID id = it->first;
            Logger::Info("[AssetManager] GC '", it->second->GetName(), "'");
            ReleaseAssetMemoryFor(*it->second);
            m_PathToID.erase(NormalizePath(it->second->GetPath()));
            m_SourceWriteTimes.erase(id);
            it = m_Cache.erase(it);
        } else {
            ++it;
        }
    }
}

void AssetManager::Pin(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    ++m_PinCounts[NormalizePath(path)];
}

void AssetManager::Unpin(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const auto it=m_PinCounts.find(NormalizePath(path));
    if(it==m_PinCounts.end())return;
    if(it->second<=1)m_PinCounts.erase(it);else --it->second;
}

void AssetManager::Touch(AssetID id)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    if(m_Cache.count(id))m_LastUsed[id]=++m_UseClock;
}

size_t AssetManager::CollectGarbage(const AssetCacheBudget& budget)
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    const size_t high=budget.cpuHighWatermarkBytes?budget.cpuHighWatermarkBytes:m_AssetCpuBudgetBytes;
    if(!high||m_AssetCpuTotalBytes<=high)return 0;
    const size_t target=static_cast<size_t>(static_cast<double>(high)*std::clamp(budget.lowWatermarkRatio,0.0f,1.0f));
    std::vector<std::pair<uint64_t,AssetID>> candidates;
    for(const auto& entry:m_Cache){
        if(entry.second.use_count()!=1)continue;
        bool pinned=false;
        for(const auto& path:m_PathToID)if(path.second==entry.first&&
            (m_PinCounts.count(path.first)||path.first.rfind("__builtin__/",0)==0)){pinned=true;break;}
        if(!pinned)candidates.push_back({m_LastUsed[entry.first],entry.first});
    }
    std::sort(candidates.begin(),candidates.end());
    size_t evicted=0;
    for(const auto& candidate:candidates){
        if(m_AssetCpuTotalBytes<=target||evicted>=budget.maxEvictionsPerCollect)break;
        Unload(candidate.second);++evicted;
    }
    return evicted;
}

AssetCacheStats AssetManager::GetCacheStats() const
{
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    AssetCacheStats stats;stats.cachedAssets=m_Cache.size();stats.estimatedCpuBytes=m_AssetCpuTotalBytes;
    stats.pinnedAssets=m_PinCounts.size();stats.loadingAssets=m_AsyncRequests.size();return stats;
}

void AssetManager::Clear() {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    Logger::Info("[AssetManager] Clearing all assets (", m_Cache.size(), ")");
    for (auto& kv : m_Cache) {
        ReleaseAssetMemoryFor(*kv.second);
    }
    m_Cache.clear();
    m_PathToID.clear();
    m_SourceWriteTimes.clear();
    m_SourceHashes.clear();
    m_RegisteredIdentities.clear();
    m_ArtifactToSourcePath.clear();
    m_LastUsed.clear();
    m_PinCounts.clear();
}

void AssetManager::PrintStats() const {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    Logger::Info("[AssetManager] Cached assets: ", m_Cache.size());
    for (const auto& kv : m_Cache) {
        const auto& asset = kv.second;
        Logger::Info("  [", AssetTypeToString(asset->GetType()), "] ", asset->GetName(),
                     "  refs=", asset.use_count() - 1);
    }
    LogAssetMemorySummary();
}

void AssetManager::SetAssetCpuBudgetBytes(size_t bytes) {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    m_AssetCpuBudgetBytes = bytes;
    MaybeWarnAssetBudget();
}

size_t AssetManager::GetEstimatedAssetCpuBytesByType(AssetType type) const {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
    return m_AssetCpuBytesByType[TypeIndex(type)];
}

void AssetManager::LogAssetMemorySummary() const {
    std::lock_guard<std::recursive_mutex> lock(m_Mutex);
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
