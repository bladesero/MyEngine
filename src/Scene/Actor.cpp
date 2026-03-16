#include "Scene/Actor.h"
#include <algorithm>

// --------------------------------------------------------------------------
Actor::Actor(std::string name, uint64_t id)
    : m_ID(id), m_Name(std::move(name))
{
}

Actor::~Actor()
{
    // 先摘除父子关系，防止悬空指针
    SetParent(nullptr);

    // 清空所有组件（触发 OnDetach）
    for (auto& [key, comp] : m_Components) {
        comp->OnDetach();
    }
    m_Components.clear();
}

// --------------------------------------------------------------------------
// Transform / 世界矩阵
// --------------------------------------------------------------------------

Mat4 Actor::GetWorldMatrix() const
{
    Mat4 local = m_Transform.GetLocalMatrix();
    if (m_Parent) {
        return m_Parent->GetWorldMatrix() * local;
    }
    return local;
}

Vec3 Actor::GetWorldPosition() const
{
    if (m_Parent) {
        return m_Parent->GetWorldMatrix().TransformPoint(m_Transform.position);
    }
    return m_Transform.position;
}

// --------------------------------------------------------------------------
// 父子层级
// --------------------------------------------------------------------------

void Actor::SetParent(Actor* parent)
{
    if (m_Parent == parent) return;

    // 从旧父节点摘除
    if (m_Parent) {
        m_Parent->RemoveChild(this);
    }

    m_Parent = parent;

    // 加入新父节点
    if (m_Parent) {
        m_Parent->AddChild(this);
    }
}

void Actor::AddChild(Actor* child)
{
    if (!child) return;
    auto it = std::find(m_Children.begin(), m_Children.end(), child);
    if (it == m_Children.end()) {
        m_Children.push_back(child);
    }
}

void Actor::RemoveChild(Actor* child)
{
    auto it = std::find(m_Children.begin(), m_Children.end(), child);
    if (it != m_Children.end()) {
        m_Children.erase(it);
    }
}

// --------------------------------------------------------------------------
// Update
// --------------------------------------------------------------------------

void Actor::Update(float deltaSeconds)
{
    if (!m_Active) return;

    for (auto& [key, comp] : m_Components) {
        if (comp->IsEnabled()) {
            comp->OnUpdate(deltaSeconds);
        }
    }
}
