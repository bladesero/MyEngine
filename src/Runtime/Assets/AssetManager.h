#pragma once

#include "Assets/Asset.h"
#include "Assets/TextureAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/MaterialAsset.h"
#include "Assets/ModelAsset.h"
#include "Assets/ShaderAsset.h"
#include "Core/Logger.h"

#include <array>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <future>
#include <mutex>
#include <filesystem>

enum class AssetChangeType { SourceChanged, Imported, Reloaded, Failed, Removed, Moved };
struct AssetChangedEvent {
    AssetChangeType type = AssetChangeType::SourceChanged;
    std::string path;
    AssetID id = kInvalidAssetID;
    std::string message;
};

using AssetLoaderFn = std::function<std::shared_ptr<Asset>(const std::string& path)>;

std::shared_ptr<TextureAsset> LoadTextureAssetFromFile(const std::string& path);
std::shared_ptr<ModelAsset>   LoadModelAssetFromObj(const std::string& path);
std::shared_ptr<ModelAsset>   LoadModelAssetFromGltf(const std::string& path);

class AssetManager {
public:
    using AssetChangedCallback = std::function<void(const AssetChangedEvent&)>;
    using ListenerID = uint64_t;
    // Defined in AssetManager.cpp so every EXE/DLL caller reaches the same
    // instance owned by MyEngineRuntime.
    static AssetManager& Get();

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    void RegisterLoader(const std::string& extension, AssetLoaderFn fn) {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        m_Loaders[extension] = std::move(fn);
    }

    template<typename T = Asset>
    AssetHandle<T> Load(const std::string& path) {
        std::shared_ptr<Asset> asset = LoadAsset(path);
        auto typed = std::dynamic_pointer_cast<T>(asset);
        if (!typed) {
            Logger::Warn("[AssetManager] Loaded asset type mismatch: ", path);
            return {};
        }
        return AssetHandle<T>{ typed };
    }

    std::shared_ptr<Asset> LoadAsset(const std::string& path);
    std::future<std::shared_ptr<Asset>> LoadAsync(const std::string& path);
    bool Reload(const std::string& path);
    size_t PollHotReload();
    ListenerID SubscribeAssetChanged(AssetChangedCallback callback);
    void UnsubscribeAssetChanged(ListenerID listenerID);

    template<typename T>
    AssetHandle<T> Register(std::shared_ptr<T> asset) {
        if (!asset) return {};
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        ApplyPersistentIdentity(*asset);
        const AssetID id = asset->GetID();
        auto it = m_Cache.find(id);
        if (it != m_Cache.end()) {
            auto existing = std::dynamic_pointer_cast<T>(it->second);
            if (existing) {
                ReleaseAssetMemoryFor(*existing);
                if (existing->ReloadFrom(*asset)) {
                    existing->IncrementVersion();
                    RefreshDependencies(*existing);
                    RegisterAssetMemoryFor(*existing);
                    m_PathToID[NormalizePath(existing->GetPath())] = id;
                    return AssetHandle<T>{ std::move(existing) };
                }
                RegisterAssetMemoryFor(*existing);
            } else {
                ReleaseAssetMemoryFor(*it->second);
            }
        }
        m_Cache[id] = std::static_pointer_cast<Asset>(asset);
        m_PathToID[NormalizePath(asset->GetPath())] = id;
        RefreshDependencies(*asset);
        RegisterAssetMemoryFor(*asset);
        return AssetHandle<T>{ std::move(asset) };
    }

    template<typename T = Asset>
    AssetHandle<T> GetByID(AssetID id) const {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        auto it = m_Cache.find(id);
        if (it == m_Cache.end()) return {};
        auto typed = std::dynamic_pointer_cast<T>(it->second);
        return typed ? AssetHandle<T>{ typed } : AssetHandle<T>{};
    }

    template<typename T = Asset>
    AssetHandle<T> GetByPath(const std::string& path) const {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        const auto it = m_PathToID.find(NormalizePath(path));
        return it != m_PathToID.end() ? GetByID<T>(it->second) : AssetHandle<T>{};
    }

    bool IsLoaded(const std::string& path) const {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        return m_PathToID.count(NormalizePath(path)) > 0;
    }
    bool IsLoaded(AssetID id) const {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        return m_Cache.count(id) > 0;
    }

    void Unload(const std::string& path);
    void Unload(AssetID id);

    void UnloadUnreferenced();

    void Clear();

    void SetProjectRoot(std::filesystem::path root);
    void RegisterPersistentIdentity(const std::string& path, const std::string& uuid);
    void SetEngineContentRoot(std::filesystem::path root) { m_EngineContentRoot = std::move(root); }
    const std::filesystem::path& GetProjectRoot() const { return m_ProjectRoot; }
    std::string ResolvePath(const std::string& path) const;
    std::string MakeProjectRelativePath(const std::string& path) const;
    MeshHandle ResolveMeshReference(const std::string& path);
    MaterialHandle ResolveMaterialReference(const std::string& path,
                                             const std::string& meshPath = {});

    TextureHandle  GetWhiteTexture();
    TextureHandle  GetBlackTexture();
    TextureHandle  GetNormalTexture();

    MeshHandle     GetTriangleMesh();
    MeshHandle     GetQuadMesh();
    MeshHandle     GetCubeMesh();

    MaterialHandle GetDefaultMaterial();

    size_t CachedCount() const {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        return m_Cache.size();
    }

    // Sorted list of loaded asset paths of the given type (for editor pickers).
    std::vector<std::string> GetCachedPathsByType(AssetType type) const;

    void PrintStats() const;

    // -----------------------------------------------------------------------
    // CPU-side asset memory (rough estimate for budgeting / telemetry)
    // -----------------------------------------------------------------------
    void SetAssetCpuBudgetBytes(size_t bytes);
    size_t GetAssetCpuBudgetBytes() const {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        return m_AssetCpuBudgetBytes;
    }
    size_t GetEstimatedAssetCpuBytes() const {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        return m_AssetCpuTotalBytes;
    }
    size_t GetEstimatedAssetCpuBytesByType(AssetType type) const;
    void   LogAssetMemorySummary() const;

private:
    AssetManager();
    void RegisterDefaultLoaders();

    static std::string GetExtension(const std::string& path) {
        const auto dot = path.rfind('.');
        if (dot == std::string::npos) return "";
        const auto slash = path.find_last_of("/\\");
        if (slash != std::string::npos && dot < slash) return "";
        std::string ext = path.substr(dot + 1);
        for (auto& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return ext;
    }

    std::string NormalizePath(const std::string& path) const;
    void ApplyPersistentIdentity(Asset& asset);
    void RefreshDependencies(Asset& asset);
    std::shared_ptr<Asset> InvokeLoader(const std::string& normalizedPath,
                                        const AssetLoaderFn& loader) const;
    bool CaptureSourceWriteTime(AssetID id, const std::string& normalizedPath);
    void PublishAssetChanged(AssetChangedEvent event);

    void RegisterAssetMemoryFor(const Asset& asset);
    void ReleaseAssetMemoryFor(const Asset& asset);
    void MaybeWarnAssetBudget() const;

    std::unordered_map<AssetID,     std::shared_ptr<Asset>> m_Cache;
    std::unordered_map<std::string, AssetID>                m_PathToID;
    std::unordered_map<std::string, AssetLoaderFn>          m_Loaders;
    std::unordered_map<std::string, std::string>            m_RegisteredIdentities;

    size_t              m_AssetCpuBudgetBytes = 0; // 0 = no budget / no warn
    size_t              m_AssetCpuTotalBytes = 0;
    std::array<size_t, 7> m_AssetCpuBytesByType{};
    std::unordered_map<AssetID, std::filesystem::file_time_type> m_SourceWriteTimes;
    std::unordered_map<AssetID, std::string> m_SourceHashes;
    std::unordered_map<ListenerID, AssetChangedCallback> m_AssetChangedListeners;
    ListenerID m_NextAssetChangedListenerID = 1;
    mutable std::recursive_mutex m_Mutex;
    std::filesystem::path m_ProjectRoot;
    std::filesystem::path m_EngineContentRoot;
};
