#pragma once

#include "Transform.h"
#include "Component.h"
#include "ActorHandle.h"
#include <string>
#include <vector>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <functional>

// ==========================================================================
// Actor  –  场景对象节点
//
//  - 持有 Transform（本地空间）
//  - 持有任意数量的 Component（按类型索引，每种类型最多一个）
//  - 支持父子层级：parent / children
//  - 有名字和唯一数值 ID（由 Scene 分配）
// ==========================================================================

class Scene;

class Actor {
public:
    // -----------------------------------------------------------------------
    // 构造 / 析构
    // -----------------------------------------------------------------------
    explicit Actor(std::string name, uint64_t id, ActorHandle handle = {});
    ~Actor();

    // 禁止拷贝，允许移动
    Actor(const Actor&)            = delete;
    Actor& operator=(const Actor&) = delete;
    Actor(Actor&&)                 = default;
    Actor& operator=(Actor&&)      = default;

    // -----------------------------------------------------------------------
    // 基础属性
    // -----------------------------------------------------------------------
    uint64_t           GetID()   const { return m_ID; }
    ActorHandle        GetHandle() const { return m_Handle; }
    const std::string& GetName() const { return m_Name; }
    void               SetName(const std::string& name) { m_Name = name; }
    const std::string& GetTag() const { return m_Tag; }
    void SetTag(std::string tag) { m_Tag = std::move(tag); }
    bool HasTag(const std::string& tag) const { return !tag.empty() && m_Tag == tag; }
    uint32_t GetLayer() const { return m_Layer; }
    void SetLayer(uint32_t layer) { m_Layer = layer; }
    uint32_t GetEditorFlags() const { return m_EditorFlags; }
    void SetEditorFlags(uint32_t flags) { m_EditorFlags = flags; }
    bool IsStatic() const { return (m_EditorFlags & 1u) != 0; }
    void SetStatic(bool value) {
        if (value) m_EditorFlags |= 1u;
        else m_EditorFlags &= ~1u;
    }

    bool IsActive() const { return m_ActiveInHierarchy && m_State != ActorState::PendingDestroy; }
    bool IsActiveSelf() const { return m_ActiveSelf; }
    bool IsActiveInHierarchy() const { return IsActive(); }
    void SetActive(bool active);
    ActorState GetState() const { return m_State; }
    bool IsPendingDestroy() const { return m_State == ActorState::PendingDestroy; }
    bool IsPrefabInstance() const { return !m_PrefabAssetPath.empty(); }
    bool IsPrefabRoot() const { return IsPrefabInstance() && m_PrefabInstanceRoot == m_Handle; }
    const std::string& GetPrefabAssetPath() const { return m_PrefabAssetPath; }
    const std::string& GetPrefabAssetUuid() const { return m_PrefabAssetUuid; }
    const std::string& GetPrefabLocalId() const { return m_PrefabLocalId; }
    const std::string& GetNestedPrefabInstanceLocalId() const { return m_NestedPrefabInstanceLocalId; }
    ActorHandle GetPrefabInstanceRoot() const { return m_PrefabInstanceRoot; }
    const nlohmann::json& GetPrefabOverrides() const { return m_PrefabOverrides; }
    Scene* GetScene() const { return m_Scene; }

    // -----------------------------------------------------------------------
    // Transform
    // -----------------------------------------------------------------------
    Transform&       GetTransform()       { return m_Transform; }
    const Transform& GetTransform() const { return m_Transform; }

    // 世界矩阵 = Parent.WorldMatrix * LocalMatrix
    Mat4 GetWorldMatrix() const;

    // 世界坐标位置
    Vec3 GetWorldPosition() const;

    // -----------------------------------------------------------------------
    // 父子层级
    // -----------------------------------------------------------------------
    Actor* GetParent() const { return m_Parent; }

    // 设置父节点（nullptr = 根节点）
    // 注意：不转移所有权，所有权由 Scene 持有
    void SetParent(Actor* parent);

    const std::vector<Actor*>& GetChildren() const { return m_Children; }

    // -----------------------------------------------------------------------
    // Component 管理
    // -----------------------------------------------------------------------

    // 添加组件（每种类型最多一个）
    // 返回指向新建组件的裸指针（所有权由 Actor 持有）
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        auto key = std::type_index(typeid(T));
        auto found = m_ComponentLookup.find(key);
        if (found != m_ComponentLookup.end()) return static_cast<T*>(m_Components[found->second].component.get());
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        if (ShouldDeferStructuralMutation()) {
            nlohmann::json data = nlohmann::json::object();
            comp->Serialize(data);
            QueueComponentAdd(comp->GetTypeName(), data);
            return nullptr;
        }
        T* raw    = comp.get();
        AddComponentObject(key, std::move(comp), true);
        return raw;
    }

    // 获取组件（不存在返回 nullptr）
    template<typename T>
    T* GetComponent() const {
        auto key = std::type_index(typeid(T));
        auto it = m_ComponentLookup.find(key);
        if (it != m_ComponentLookup.end()) return static_cast<T*>(m_Components[it->second].component.get());
        return nullptr;
    }

    // 是否持有某组件
    template<typename T>
    bool HasComponent() const {
        return m_ComponentLookup.count(std::type_index(typeid(T))) > 0;
    }

    // 移除组件
    Component* GetComponentByTypeName(const std::string& typeName) const;
    bool HasComponentType(const std::string& typeName) const {
        return GetComponentByTypeName(typeName) != nullptr;
    }
    bool RemoveComponentByTypeName(const std::string& typeName);

    template<typename T>
    bool RemoveComponent() {
        auto key = std::type_index(typeid(T));
        auto it = m_ComponentLookup.find(key);
        return it != m_ComponentLookup.end() && RemoveComponentAt(it->second);
    }

    // 遍历所有组件（用于序列化）
    void ForEachComponent(const std::function<void(Component&)>& fn) const {
        for (const auto& entry : m_Components) fn(*entry.component);
    }

    // -----------------------------------------------------------------------
    // 生命周期（由 Scene 驱动）
    // -----------------------------------------------------------------------
    void Update(float deltaSeconds);
    void FixedUpdate(float deltaSeconds);
    void LateUpdate(float deltaSeconds);

private:
    void AddChild(Actor* child);
    void RemoveChild(Actor* child);
    bool MoveChildBefore(Actor* child, Actor* beforeChild);
    Component* AddComponentObject(std::type_index type, std::unique_ptr<Component> component,
                                  bool finalizeNow);
    bool RemoveComponentAt(size_t index);
    void RebuildComponentLookup();
    std::vector<Component*> OrderedComponents(bool reverse = false) const;
    void FinalizeConstruction(bool playing);
    void BeginPlay();
    void BeginPlayPhase();
    void EnablePlayPhase();
    void StartPlayPhase();
    void EndPlay();
    void RefreshActiveInHierarchy(bool parentActive, bool playing);
    void MarkPendingDestroy();
    void OnComponentEnabledChanged(Component& component, bool enabled);
    bool ShouldDeferStructuralMutation() const;
    void QueueComponentAdd(const std::string& type, const nlohmann::json& data);

    uint64_t    m_ID;
    ActorHandle m_Handle;
    std::string m_Name;
    std::string m_Tag;
    uint32_t    m_Layer = 0;
    uint32_t    m_EditorFlags = 0;
    bool        m_ActiveSelf = true;
    bool        m_ActiveInHierarchy = true;
    ActorState  m_State = ActorState::Constructed;

    Transform   m_Transform;

    Actor*              m_Parent   = nullptr;
    std::vector<Actor*> m_Children;

    struct ComponentEntry {
        std::type_index type{typeid(void)};
        std::unique_ptr<Component> component;
        uint64_t insertionOrder = 0;
    };
    std::vector<ComponentEntry> m_Components;
    std::unordered_map<std::type_index, size_t> m_ComponentLookup;
    uint64_t m_NextComponentOrder = 1;
    Scene* m_Scene = nullptr;
    std::string m_PrefabAssetPath;
    std::string m_PrefabAssetUuid;
    std::string m_PrefabLocalId;
    std::string m_NestedPrefabInstanceLocalId;
    ActorHandle m_PrefabInstanceRoot;
    nlohmann::json m_PrefabOverrides = nlohmann::json::array();

    friend class Scene;
    friend class SceneSerializer;
    friend class ComponentRegistry;
    friend class Component;
    friend class PrefabSystem;
};
