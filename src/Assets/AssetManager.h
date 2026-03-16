#pragma once

#include "Assets/Asset.h"
#include "Assets/TextureAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/MaterialAsset.h"
#include "Assets/ModelAsset.h"
#include "Core/Logger.h"

#include <unordered_map>
#include <memory>
#include <string>
#include <functional>
#include <typeindex>

// ==========================================================================
// AssetManager  –  全局资产注册表与生命周期管理
//
// 职责：
//   - 按路径/ID 缓存所有已加载资产（单例访问）
//   - 提供类型化的 Load<T> / Get<T> / Unload 接口
//   - 内置加载器注册机制（LoaderFn），各模块可注册自己的文件加载器
//   - 追踪内置资产（三角形、白色纹理、默认材质等）
//
// 线程模型：单线程（主线程调用），异步扩展预留接口。
// ==========================================================================

// 资产加载器函数类型：接收路径，返回已填充的 Asset 智能指针（失败返回 nullptr）
using AssetLoaderFn = std::function<std::shared_ptr<Asset>(const std::string& path)>;

class AssetManager {
public:
    // ---- 单例 -------------------------------------------------------------
    static AssetManager& Get() {
        static AssetManager instance;
        return instance;
    }

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // ---- 加载器注册 --------------------------------------------------------
    // 按文件扩展名注册加载器（不含点，如 "png", "obj"）
    void RegisterLoader(const std::string& extension, AssetLoaderFn fn) {
        m_Loaders[extension] = std::move(fn);
    }

    // ---- 加载 / 获取 -------------------------------------------------------

    // 通用：按路径加载资产，自动根据扩展名选择加载器
    // 若已缓存则直接返回；否则调用对应 Loader。
    template<typename T = Asset>
    AssetHandle<T> Load(const std::string& path) {
        AssetID id = MakeAssetID(path);

        // 命中缓存
        auto it = m_Cache.find(id);
        if (it != m_Cache.end()) {
            auto typed = std::dynamic_pointer_cast<T>(it->second);
            if (typed) return AssetHandle<T>{ typed };
            Logger::Warn("[AssetManager] Type mismatch for: ", path);
            return {};
        }

        // 调用加载器
        std::string ext = GetExtension(path);
        auto loaderIt = m_Loaders.find(ext);
        if (loaderIt == m_Loaders.end()) {
            Logger::Error("[AssetManager] No loader for extension '.", ext, "': ", path);
            return {};
        }

        auto asset = loaderIt->second(path);
        if (!asset) {
            Logger::Error("[AssetManager] Loader returned null for: ", path);
            return {};
        }

        m_Cache[id] = asset;
        Logger::Info("[AssetManager] Loaded [", AssetTypeToString(asset->GetType()),
                     "] '", asset->GetName(), "'");

        auto typed = std::dynamic_pointer_cast<T>(asset);
        if (!typed) {
            Logger::Warn("[AssetManager] Loaded asset type mismatch: ", path);
            return {};
        }
        return AssetHandle<T>{ typed };
    }

    // 直接注册一个已构建好的资产（内置资产、运行时生成等）
    template<typename T>
    AssetHandle<T> Register(std::shared_ptr<T> asset) {
        if (!asset) return {};
        m_Cache[asset->GetID()] = asset;
        return AssetHandle<T>{ std::move(asset) };
    }

    // 按 ID 获取（已加载才有效）
    template<typename T = Asset>
    AssetHandle<T> GetByID(AssetID id) const {
        auto it = m_Cache.find(id);
        if (it == m_Cache.end()) return {};
        auto typed = std::dynamic_pointer_cast<T>(it->second);
        return typed ? AssetHandle<T>{ typed } : AssetHandle<T>{};
    }

    // 按路径获取（不触发加载）
    template<typename T = Asset>
    AssetHandle<T> GetByPath(const std::string& path) const {
        return GetByID<T>(MakeAssetID(path));
    }

    // 是否已缓存
    bool IsLoaded(const std::string& path) const {
        return m_Cache.count(MakeAssetID(path)) > 0;
    }
    bool IsLoaded(AssetID id) const {
        return m_Cache.count(id) > 0;
    }

    // ---- 卸载 --------------------------------------------------------------

    // 卸载指定资产（若外部还有 AssetHandle 持有则 shared_ptr 不释放）
    void Unload(const std::string& path) { Unload(MakeAssetID(path)); }
    void Unload(AssetID id) {
        auto it = m_Cache.find(id);
        if (it != m_Cache.end()) {
            Logger::Info("[AssetManager] Unload '", it->second->GetName(), "'");
            m_Cache.erase(it);
        }
    }

    // 卸载所有引用计数为 0（只有 AssetManager 持有）的资产
    void UnloadUnreferenced() {
        for (auto it = m_Cache.begin(); it != m_Cache.end(); ) {
            // shared_ptr use_count == 1 means only the cache holds it
            if (it->second.use_count() == 1) {
                Logger::Info("[AssetManager] GC '", it->second->GetName(), "'");
                it = m_Cache.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 卸载全部
    void Clear() {
        Logger::Info("[AssetManager] Clearing all assets (", m_Cache.size(), ")");
        m_Cache.clear();
    }

    // ---- 内置资产 ----------------------------------------------------------
    // 首次调用时创建并缓存；后续直接返回已缓存句柄。

    TextureHandle  GetWhiteTexture();
    TextureHandle  GetBlackTexture();
    TextureHandle  GetNormalTexture();   // (0.5, 0.5, 1.0) flat normal

    MeshHandle     GetTriangleMesh();
    MeshHandle     GetQuadMesh();
    MeshHandle     GetCubeMesh();

    MaterialHandle GetDefaultMaterial();

    // ---- 统计 --------------------------------------------------------------
    size_t CachedCount() const { return m_Cache.size(); }

    void PrintStats() const {
        Logger::Info("[AssetManager] Cached assets: ", m_Cache.size());
        for (const auto& [id, asset] : m_Cache) {
            Logger::Info("  [", AssetTypeToString(asset->GetType()), "] ",
                         asset->GetName(), "  refs=", asset.use_count() - 1);
        }
    }

private:
    AssetManager() = default;

    static std::string GetExtension(const std::string& path) {
        auto dot = path.rfind('.');
        if (dot == std::string::npos) return "";
        auto slash = path.find_last_of("/\\");
        if (slash != std::string::npos && dot < slash) return "";
        std::string ext = path.substr(dot + 1);
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext;
    }

    std::unordered_map<AssetID,     std::shared_ptr<Asset>> m_Cache;
    std::unordered_map<std::string, AssetLoaderFn>          m_Loaders;
};
