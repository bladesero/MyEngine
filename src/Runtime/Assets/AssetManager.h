#pragma once

#include "Assets/Asset.h"
#include "Assets/TextureAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/MaterialAsset.h"
#include "Assets/ModelAsset.h"
#include "Core/Logger.h"

#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

using AssetLoaderFn = std::function<std::shared_ptr<Asset>(const std::string& path)>;

std::shared_ptr<TextureAsset> LoadTextureAssetFromFile(const std::string& path);
std::shared_ptr<ModelAsset>   LoadModelAssetFromObj(const std::string& path);

class AssetManager {
public:
    static AssetManager& Get() {
        static AssetManager instance;
        return instance;
    }

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    void RegisterLoader(const std::string& extension, AssetLoaderFn fn) {
        m_Loaders[extension] = std::move(fn);
    }

    template<typename T = Asset>
    AssetHandle<T> Load(const std::string& path) {
        const std::string normalizedPath = NormalizePath(path);
        const AssetID id = MakeAssetID(normalizedPath);

        auto it = m_Cache.find(id);
        if (it != m_Cache.end()) {
            auto typed = std::dynamic_pointer_cast<T>(it->second);
            if (typed) return AssetHandle<T>{ typed };
            Logger::Warn("[AssetManager] Type mismatch for: ", normalizedPath);
            return {};
        }

        const std::string ext = GetExtension(normalizedPath);
        auto loaderIt = m_Loaders.find(ext);
        std::shared_ptr<Asset> asset;
        if (loaderIt == m_Loaders.end()) {
            if (ext == "png" || ext == "jpg" || ext == "jpeg" ||
                ext == "bmp" || ext == "tga" || ext == "gif" ||
                ext == "psd" || ext == "hdr" || ext == "pic" ||
                ext == "pnm" || ext == "ppm" || ext == "pgm" ||
                ext == "pam") {
                asset = std::static_pointer_cast<Asset>(LoadTextureAssetFromFile(normalizedPath));
            } else if (ext == "obj") {
                asset = std::static_pointer_cast<Asset>(LoadModelAssetFromObj(normalizedPath));
            } else {
                Logger::Error("[AssetManager] No loader for extension '.", ext, "': ", normalizedPath);
                return {};
            }
        } else {
            asset = loaderIt->second(normalizedPath);
        }
        if (!asset) {
            Logger::Error("[AssetManager] Loader returned null for: ", normalizedPath);
            return {};
        }

        m_Cache[id] = asset;
        Logger::Info("[AssetManager] Loaded [", AssetTypeToString(asset->GetType()),
                     "] '", asset->GetName(), "'");

        auto typed = std::dynamic_pointer_cast<T>(asset);
        if (!typed) {
            Logger::Warn("[AssetManager] Loaded asset type mismatch: ", normalizedPath);
            return {};
        }
        return AssetHandle<T>{ typed };
    }

    template<typename T>
    AssetHandle<T> Register(std::shared_ptr<T> asset) {
        if (!asset) return {};
        m_Cache[asset->GetID()] = asset;
        return AssetHandle<T>{ std::move(asset) };
    }

    template<typename T = Asset>
    AssetHandle<T> GetByID(AssetID id) const {
        auto it = m_Cache.find(id);
        if (it == m_Cache.end()) return {};
        auto typed = std::dynamic_pointer_cast<T>(it->second);
        return typed ? AssetHandle<T>{ typed } : AssetHandle<T>{};
    }

    template<typename T = Asset>
    AssetHandle<T> GetByPath(const std::string& path) const {
        return GetByID<T>(MakeAssetID(NormalizePath(path)));
    }

    bool IsLoaded(const std::string& path) const {
        return m_Cache.count(MakeAssetID(NormalizePath(path))) > 0;
    }
    bool IsLoaded(AssetID id) const {
        return m_Cache.count(id) > 0;
    }

    void Unload(const std::string& path) { Unload(MakeAssetID(NormalizePath(path))); }
    void Unload(AssetID id) {
        auto it = m_Cache.find(id);
        if (it != m_Cache.end()) {
            Logger::Info("[AssetManager] Unload '", it->second->GetName(), "'");
            m_Cache.erase(it);
        }
    }

    void UnloadUnreferenced() {
        for (auto it = m_Cache.begin(); it != m_Cache.end(); ) {
            if (it->second.use_count() == 1) {
                Logger::Info("[AssetManager] GC '", it->second->GetName(), "'");
                it = m_Cache.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Clear() {
        Logger::Info("[AssetManager] Clearing all assets (", m_Cache.size(), ")");
        m_Cache.clear();
    }

    TextureHandle  GetWhiteTexture();
    TextureHandle  GetBlackTexture();
    TextureHandle  GetNormalTexture();

    MeshHandle     GetTriangleMesh();
    MeshHandle     GetQuadMesh();
    MeshHandle     GetCubeMesh();

    MaterialHandle GetDefaultMaterial();

    size_t CachedCount() const { return m_Cache.size(); }

    // Sorted list of loaded asset paths of the given type (for editor pickers).
    std::vector<std::string> GetCachedPathsByType(AssetType type) const;

    void PrintStats() const {
        Logger::Info("[AssetManager] Cached assets: ", m_Cache.size());
        for (const auto& [id, asset] : m_Cache) {
            Logger::Info("  [", AssetTypeToString(asset->GetType()), "] ",
                         asset->GetName(), "  refs=", asset.use_count() - 1);
        }
    }

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

    static std::string NormalizePath(const std::string& path);

    std::unordered_map<AssetID,     std::shared_ptr<Asset>> m_Cache;
    std::unordered_map<std::string, AssetLoaderFn>          m_Loaders;
};
