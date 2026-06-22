#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <atomic>
#include <vector>

// ==========================================================================
// Asset system – type hierarchy
//
//  Asset               ← 所有资产的抽象基类
//  ├─ TextureAsset     ← 纹理（CPU 像素数据 + GPU 句柄）
//  ├─ MeshAsset        ← 网格（顶点/索引缓冲区）
//  ├─ MaterialAsset    ← 材质（着色器 + 纹理槽 + 参数）
//  └─ ModelAsset       ← 模型（若干 MeshAsset + MaterialAsset 的组合）
//
// AssetHandle<T>       ← 引用计数的智能句柄（= shared_ptr<T> 的薄封装）
// AssetID              ← 资产唯一 ID（路径哈希）
// ==========================================================================

// --------------------------------------------------------------------------
// AssetID  –  由文件路径哈希而来的 64 位唯一标识
// --------------------------------------------------------------------------
using AssetID = uint64_t;
static constexpr AssetID kInvalidAssetID = 0;

inline AssetID MakeAssetID(const std::string& path) {
    // FNV-1a 64-bit hash
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : path) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1; // never return 0
}

// --------------------------------------------------------------------------
// AssetType  –  枚举
// --------------------------------------------------------------------------
enum class AssetType : uint8_t {
    Unknown  = 0,
    Texture,
    Mesh,
    Material,
    Model,
    Shader,
    AudioClip,
    Script,
    UIPrefab,
    UIStyle,
    UIFont,
};

inline const char* AssetTypeToString(AssetType t) {
    switch (t) {
        case AssetType::Texture:  return "Texture";
        case AssetType::Mesh:     return "Mesh";
        case AssetType::Material: return "Material";
        case AssetType::Model:    return "Model";
        case AssetType::Shader:   return "Shader";
        case AssetType::AudioClip:return "AudioClip";
        case AssetType::Script:   return "Script";
        case AssetType::UIPrefab: return "UIPrefab";
        case AssetType::UIStyle:  return "UIStyle";
        case AssetType::UIFont:   return "UIFont";
        default:                  return "Unknown";
    }
}

// --------------------------------------------------------------------------
// AssetState  –  加载状态
// --------------------------------------------------------------------------
enum class AssetState : uint8_t {
    Unloaded = 0,   // 尚未加载
    Loading,        // 正在加载（预留异步扩展）
    Ready,          // 已就绪
    Failed,         // 加载失败
};

// --------------------------------------------------------------------------
// Asset  –  基类
// --------------------------------------------------------------------------
class Asset {
public:
    virtual ~Asset() = default;

    AssetID            GetID()       const { return m_ID; }
    const std::string& GetPath()     const { return m_Path; }
    const std::string& GetName()     const { return m_Name; }
    AssetType          GetType()     const { return m_Type; }
    AssetState         GetState()    const { return m_State; }
    const std::string& GetUuid()     const { return m_Uuid; }
    uint64_t           GetVersion()  const { return m_Version; }
    const std::vector<AssetID>& GetDependencies() const { return m_Dependencies; }

    bool IsReady()  const { return m_State == AssetState::Ready;  }
    bool IsFailed() const { return m_State == AssetState::Failed; }

    // 资产引用计数（由 AssetManager 维护，外部只读）
    int  RefCount() const { return m_RefCount.load(); }

    void SetName(const std::string& name) { m_Name = name; }
    virtual bool ReloadFrom(const Asset& source) {
        (void)source;
        return false;
    }

protected:
    explicit Asset(AssetType type, const std::string& path)
        : m_Type(type)
        , m_Path(path)
        , m_ID(MakeAssetID(path))
        , m_Name(ExtractName(path))
        , m_State(AssetState::Unloaded)
    {}

    void SetState(AssetState s) { m_State = s; }

private:
    friend class AssetManager;
    void SetPersistentIdentity(AssetID id, std::string uuid) {
        m_ID = id;
        m_Uuid = std::move(uuid);
    }
    void IncrementVersion() { ++m_Version; }
    void SetDependencies(std::vector<AssetID> dependencies) {
        m_Dependencies = std::move(dependencies);
    }

    static std::string ExtractName(const std::string& path) {
        auto pos = path.find_last_of("/\\");
        std::string filename = (pos == std::string::npos) ? path : path.substr(pos + 1);
        auto dot = filename.rfind('.');
        return (dot == std::string::npos) ? filename : filename.substr(0, dot);
    }

    void AddRef()    { ++m_RefCount; }
    void Release()   { --m_RefCount; }

    AssetType          m_Type;
    std::string        m_Path;
    AssetID            m_ID;
    std::string        m_Name;
    std::string        m_Uuid;
    AssetState         m_State;
    uint64_t           m_Version = 1;
    std::vector<AssetID> m_Dependencies;
    std::atomic<int>   m_RefCount{ 0 };
};

// --------------------------------------------------------------------------
// AssetHandle<T>  –  类型安全的引用计数句柄
// --------------------------------------------------------------------------
template<typename T>
class AssetHandle {
    static_assert(std::is_base_of<Asset, T>::value, "T must derive from Asset");

public:
    AssetHandle() = default;
    explicit AssetHandle(std::shared_ptr<T> ptr) : m_Ptr(std::move(ptr)) {}

    T*       Get()       const { return m_Ptr.get(); }
    T*       operator->()const { return m_Ptr.get(); }
    T&       operator*() const { return *m_Ptr; }
    explicit operator bool() const { return m_Ptr != nullptr; }

    bool IsValid() const { return m_Ptr && m_Ptr->IsReady(); }

    void Reset() { m_Ptr.reset(); }

    bool operator==(const AssetHandle& o) const { return m_Ptr == o.m_Ptr; }
    bool operator!=(const AssetHandle& o) const { return m_Ptr != o.m_Ptr; }

    // 访问底层 shared_ptr（AssetManager 内部使用）
    const std::shared_ptr<T>& Shared() const { return m_Ptr; }

private:
    std::shared_ptr<T> m_Ptr;
};
