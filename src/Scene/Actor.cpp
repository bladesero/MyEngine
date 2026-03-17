#include "Scene/Actor.h"
#include <algorithm>

// --------------------------------------------------------------------------
Actor::Actor(std::string name, uint64_t id)
    : m_ID(id), m_Name(std::move(name))
{
}

Actor::~Actor()
{
    // 鍏堟憳闄ょ埗瀛愬叧绯伙紝闃叉鎮┖鎸囬拡

    // 娓呯┖鎵€鏈夌粍浠讹紙瑙﹀彂 OnDetach锛?
    for (auto& [key, comp] : m_Components) {
        comp->OnDetach();
    }
    m_Components.clear();
}

// --------------------------------------------------------------------------
// Transform / 涓栫晫鐭╅樀
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
// 鐖跺瓙灞傜骇
// --------------------------------------------------------------------------

void Actor::SetParent(Actor* parent)
{
    if (m_Parent == parent) return;

    // 浠庢棫鐖惰妭鐐规憳闄?
    if (m_Parent) {
        m_Parent->RemoveChild(this);
    }

    m_Parent = parent;

    // 鍔犲叆鏂扮埗鑺傜偣
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
