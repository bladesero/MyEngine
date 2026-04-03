#pragma once

#include "Transform.h"
#include "Component.h"
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

class Actor {
public:
    // -----------------------------------------------------------------------
    // 构造 / 析构
    // -----------------------------------------------------------------------
    explicit Actor(std::string name, uint64_t id);
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
    const std::string& GetName() const { return m_Name; }
    void               SetName(const std::string& name) { m_Name = name; }

    bool IsActive() const { return m_Active; }
    void SetActive(bool active) { m_Active = active; }

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
        auto it  = m_Components.find(key);
        if (it != m_Components.end()) {
            return static_cast<T*>(it->second.get());
        }
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw    = comp.get();
        raw->m_Owner = this;
        raw->OnAttach();
        m_Components.emplace(key, std::move(comp));
        return raw;
    }

    // 获取组件（不存在返回 nullptr）
    template<typename T>
    T* GetComponent() const {
        auto key = std::type_index(typeid(T));
        auto it  = m_Components.find(key);
        if (it != m_Components.end()) {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    // 是否持有某组件
    template<typename T>
    bool HasComponent() const {
        return m_Components.count(std::type_index(typeid(T))) > 0;
    }

    // 移除组件
    template<typename T>
    bool RemoveComponent() {
        auto key = std::type_index(typeid(T));
        auto it  = m_Components.find(key);
        if (it == m_Components.end()) return false;
        it->second->OnDetach();
        m_Components.erase(it);
        return true;
    }

    // 遍历所有组件（用于序列化）
    void ForEachComponent(const std::function<void(Component&)>& fn) const {
        for (const auto& [key, comp] : m_Components) {
            fn(*comp);
        }
    }

    // -----------------------------------------------------------------------
    // 生命周期（由 Scene 驱动）
    // -----------------------------------------------------------------------
    void Update(float deltaSeconds);

private:
    void AddChild(Actor* child);
    void RemoveChild(Actor* child);

    uint64_t    m_ID;
    std::string m_Name;
    bool        m_Active = true;

    Transform   m_Transform;

    Actor*              m_Parent   = nullptr;
    std::vector<Actor*> m_Children;

    std::unordered_map<std::type_index, std::unique_ptr<Component>> m_Components;
};
