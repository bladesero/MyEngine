#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Core/Logger.h"

#include <algorithm>

Actor::Actor(std::string name, uint64_t id, ActorHandle handle)
    : m_ID(id), m_Handle(handle), m_Name(std::move(name)) {}

Actor::~Actor()
{
    for (Component* comp : OrderedComponents(true)) {
        if (comp->m_BeganPlay) { comp->OnEndPlay(); comp->m_BeganPlay = false; }
        if (comp->m_EffectiveEnabled) { comp->OnDisable(); comp->m_EffectiveEnabled = false; }
        if (comp->m_Attached) { comp->OnDetach(); comp->m_Attached = false; }
    }
}

void Component::SetEnabled(bool enabled)
{
    if (m_Enabled == enabled) return;
    if (m_Owner) m_Owner->OnComponentEnabledChanged(*this, enabled);
    else m_Enabled = enabled;
}

void Actor::OnComponentEnabledChanged(Component& component, bool enabled)
{
    if (m_Scene && m_Scene->IsTraversing()) {
        m_Scene->QueueSetComponentEnabled({m_Handle, component.GetTypeName()}, enabled);
        return;
    }
    component.m_Enabled = enabled;
    const bool effective = m_Scene && m_Scene->IsPlaying() && IsActive() && enabled;
    if (effective && !component.m_EffectiveEnabled) { component.OnEnable(); component.m_EffectiveEnabled = true; }
    else if (!effective && component.m_EffectiveEnabled) { component.OnDisable(); component.m_EffectiveEnabled = false; }
}

bool Actor::ShouldDeferStructuralMutation() const
{
    return m_Scene && m_Scene->IsTraversing();
}

void Actor::QueueComponentAdd(const std::string& type, const nlohmann::json& data)
{
    if (m_Scene) m_Scene->QueueAddComponent(m_Handle, type, data);
}

Mat4 Actor::GetWorldMatrix() const
{
    const Mat4 local = m_Transform.GetLocalMatrix();
    return m_Parent ? local * m_Parent->GetWorldMatrix() : local;
}

Vec3 Actor::GetWorldPosition() const
{
    return m_Parent ? m_Parent->GetWorldMatrix().TransformPoint(m_Transform.position)
                    : m_Transform.position;
}

void Actor::SetActive(bool active)
{
    if (m_ActiveSelf == active) return;
    if (m_Scene && m_Scene->IsTraversing()) {
        m_Scene->QueueSetActive(m_Handle, active);
        return;
    }
    m_ActiveSelf = active;
    RefreshActiveInHierarchy(!m_Parent || m_Parent->IsActiveInHierarchy(),
                             m_Scene && m_Scene->IsPlaying());
}

Component* Actor::GetComponentByTypeName(const std::string& typeName) const
{
    for (const auto& entry : m_Components) {
        if (entry.component && typeName == entry.component->GetTypeName()) return entry.component.get();
    }
    return nullptr;
}

bool Actor::RemoveComponentByTypeName(const std::string& typeName)
{
    for (size_t i = 0; i < m_Components.size(); ++i) {
        if (typeName == m_Components[i].component->GetTypeName()) return RemoveComponentAt(i);
    }
    return false;
}

Component* Actor::AddComponentObject(std::type_index type,
                                     std::unique_ptr<Component> component,
                                     bool finalizeNow)
{
    if (!component) return nullptr;
    const auto existing = m_ComponentLookup.find(type);
    if (existing != m_ComponentLookup.end()) return m_Components[existing->second].component.get();
    Component* raw = component.get();
    raw->m_Owner = this;
    m_ComponentLookup[type] = m_Components.size();
    m_Components.push_back({type, std::move(component), m_NextComponentOrder++});
    if (finalizeNow) {
        raw->OnAttach(); raw->m_Attached = true;
        raw->OnInitialize(); raw->m_Initialized = true;
        if (m_Scene && m_Scene->IsPlaying()) {
            raw->OnBeginPlay(); raw->m_BeganPlay = true;
            if (IsActive() && raw->IsEnabled()) { raw->OnEnable(); raw->m_EffectiveEnabled = true; }
            raw->OnStart(); raw->m_Started = true;
        }
    }
    return raw;
}

bool Actor::RemoveComponentAt(size_t index)
{
    if (index >= m_Components.size()) return false;
    Component* comp = m_Components[index].component.get();
    if (ShouldDeferStructuralMutation()) {
        m_Scene->QueueRemoveComponent({m_Handle, comp->GetTypeName()});
        return true;
    }
    if (comp->m_BeganPlay) { comp->OnEndPlay(); comp->m_BeganPlay = false; }
    if (comp->m_EffectiveEnabled) { comp->OnDisable(); comp->m_EffectiveEnabled = false; }
    if (comp->m_Attached) { comp->OnDetach(); comp->m_Attached = false; }
    m_Components.erase(m_Components.begin() + static_cast<std::ptrdiff_t>(index));
    RebuildComponentLookup();
    return true;
}

void Actor::RebuildComponentLookup()
{
    m_ComponentLookup.clear();
    for (size_t i = 0; i < m_Components.size(); ++i) m_ComponentLookup[m_Components[i].type] = i;
}

std::vector<Component*> Actor::OrderedComponents(bool reverse) const
{
    std::vector<const ComponentEntry*> entries;
    entries.reserve(m_Components.size());
    for (const auto& entry : m_Components) entries.push_back(&entry);
    std::stable_sort(entries.begin(), entries.end(), [](const ComponentEntry* a, const ComponentEntry* b) {
        const int ao = a->component->GetExecutionOrder();
        const int bo = b->component->GetExecutionOrder();
        return ao != bo ? ao < bo : a->insertionOrder < b->insertionOrder;
    });
    if (reverse) std::reverse(entries.begin(), entries.end());
    std::vector<Component*> result;
    result.reserve(entries.size());
    for (const ComponentEntry* entry : entries) result.push_back(entry->component.get());
    return result;
}

void Actor::SetParent(Actor* parent)
{
    if (m_Parent == parent || parent == this) return;
    if (m_Scene && m_Scene->IsTraversing()) {
        m_Scene->QueueSetParent(m_Handle, parent ? parent->GetHandle() : ActorHandle{});
        return;
    }
    for (Actor* ancestor = parent; ancestor; ancestor = ancestor->m_Parent) {
        if (ancestor == this) { Logger::Warn("[Scene] rejected cyclic actor parenting"); return; }
    }
    if (m_Parent) m_Parent->RemoveChild(this);
    m_Parent = parent;
    if (m_Parent) m_Parent->AddChild(this);
    RefreshActiveInHierarchy(!m_Parent || m_Parent->IsActiveInHierarchy(),
                             m_Scene && m_Scene->IsPlaying());
}

void Actor::AddChild(Actor* child)
{
    if (child && std::find(m_Children.begin(), m_Children.end(), child) == m_Children.end())
        m_Children.push_back(child);
}

void Actor::RemoveChild(Actor* child)
{
    const auto it = std::find(m_Children.begin(), m_Children.end(), child);
    if (it != m_Children.end()) m_Children.erase(it);
}

bool Actor::MoveChildBefore(Actor* child, Actor* beforeChild)
{
    if (!child || child == beforeChild) return false;
    const auto childIt = std::find(m_Children.begin(), m_Children.end(), child);
    if (childIt == m_Children.end()) return false;
    auto beforeIt = m_Children.end();
    if (beforeChild) {
        beforeIt = std::find(m_Children.begin(), m_Children.end(), beforeChild);
        if (beforeIt == m_Children.end()) return false;
    }

    Actor* moved = *childIt;
    m_Children.erase(childIt);
    if (beforeChild) beforeIt = std::find(m_Children.begin(), m_Children.end(), beforeChild);
    else beforeIt = m_Children.end();
    m_Children.insert(beforeIt, moved);
    return true;
}

void Actor::FinalizeConstruction(bool playing)
{
    for (Component* comp : OrderedComponents()) if (!comp->m_Attached) { comp->OnAttach(); comp->m_Attached = true; }
    for (Component* comp : OrderedComponents()) if (!comp->m_Initialized) { comp->OnInitialize(); comp->m_Initialized = true; }
    m_State = m_ActiveInHierarchy ? ActorState::Active : ActorState::Inactive;
    (void)playing;
}

void Actor::BeginPlay()
{
    if (m_State == ActorState::PendingDestroy || m_State == ActorState::Destroyed) return;
    BeginPlayPhase(); EnablePlayPhase(); StartPlayPhase();
}

void Actor::BeginPlayPhase()
{
    for (Component* comp : OrderedComponents()) if (!comp->m_BeganPlay) { comp->OnBeginPlay(); comp->m_BeganPlay = true; }
}

void Actor::EnablePlayPhase()
{
    for (Component* comp : OrderedComponents()) {
        if (IsActive() && comp->IsEnabled() && !comp->m_EffectiveEnabled) { comp->OnEnable(); comp->m_EffectiveEnabled = true; }
    }
}

void Actor::StartPlayPhase()
{
    for (Component* comp : OrderedComponents()) if (!comp->m_Started) { comp->OnStart(); comp->m_Started = true; }
}

void Actor::EndPlay()
{
    for (Component* comp : OrderedComponents(true)) if (comp->m_BeganPlay) { comp->OnEndPlay(); comp->m_BeganPlay = false; }
    for (Component* comp : OrderedComponents(true)) if (comp->m_EffectiveEnabled) { comp->OnDisable(); comp->m_EffectiveEnabled = false; }
}

void Actor::RefreshActiveInHierarchy(bool parentActive, bool playing)
{
    const bool next = m_ActiveSelf && parentActive &&
        m_State != ActorState::PendingDestroy && m_State != ActorState::Destroyed;
    const bool changed = m_ActiveInHierarchy != next;
    m_ActiveInHierarchy = next;
    if (m_State != ActorState::PendingDestroy && m_State != ActorState::Destroyed)
        m_State = next ? ActorState::Active : ActorState::Inactive;

    // Disable is child-to-parent; enable is parent-to-child.
    if (!next) for (Actor* child : m_Children) child->RefreshActiveInHierarchy(false, playing);
    if (changed && playing) {
        for (Component* comp : OrderedComponents(!next)) {
            const bool effective = next && comp->IsEnabled();
            if (effective && !comp->m_EffectiveEnabled) { comp->OnEnable(); comp->m_EffectiveEnabled = true; }
            else if (!effective && comp->m_EffectiveEnabled) { comp->OnDisable(); comp->m_EffectiveEnabled = false; }
        }
    }
    if (next) for (Actor* child : m_Children) child->RefreshActiveInHierarchy(true, playing);
}

void Actor::MarkPendingDestroy()
{
    if (m_State == ActorState::PendingDestroy || m_State == ActorState::Destroyed) return;
    m_State = ActorState::PendingDestroy;
    m_ActiveInHierarchy = false;
    for (Actor* child : m_Children) child->MarkPendingDestroy();
}

void Actor::Update(float deltaSeconds)
{
    if (!IsActive()) return;
    for (Component* comp : OrderedComponents()) if (comp->m_EffectiveEnabled) comp->OnUpdate(deltaSeconds);
}

void Actor::FixedUpdate(float deltaSeconds)
{
    if (!IsActive()) return;
    for (Component* comp : OrderedComponents()) if (comp->m_EffectiveEnabled) comp->OnFixedUpdate(deltaSeconds);
}

void Actor::LateUpdate(float deltaSeconds)
{
    if (!IsActive()) return;
    for (Component* comp : OrderedComponents()) if (comp->m_EffectiveEnabled) comp->OnLateUpdate(deltaSeconds);
}
