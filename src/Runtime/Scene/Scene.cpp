#include "Scene/Scene.h"
#include "Core/Memory/MemoryService.h"

#include <algorithm>

// --------------------------------------------------------------------------
Scene::Scene(std::string name)
    : m_Name(std::move(name))
{
}

Scene::~Scene() {
    Clear();
}

// --------------------------------------------------------------------------
// 创建
// --------------------------------------------------------------------------

Actor* Scene::CreateActor(const std::string& name)
{
    uint64_t id  = m_NextID++;
    auto     ptr = std::make_unique<Actor>(name, id);
    Actor*   raw = ptr.get();
    raw->m_Scene = this;

    m_IDMap[id] = raw;
    m_Actors.push_back(std::move(ptr));
    if (MemoryService::Get().IsInitialized()) {
        MemoryService::Get().SceneNotifyActorCreated();
    }
    return raw;
}

Actor* Scene::CreateActor(const std::string& name, Actor* parent)
{
    Actor* actor = CreateActor(name);
    if (parent) {
        actor->SetParent(parent);
    }
    return actor;
}

Actor* Scene::CreateActorWithID(const std::string& name, uint64_t id)
{
    // 若 ID 已存在则直接返回已有节点（防止重复）
    if (m_IDMap.count(id)) return m_IDMap[id];

    auto   ptr = std::make_unique<Actor>(name, id);
    Actor* raw = ptr.get();
    raw->m_Scene = this;
    m_IDMap[id] = raw;
    m_Actors.push_back(std::move(ptr));
    // 确保 m_NextID 始终大于已分配的最大 ID
    if (id >= m_NextID) m_NextID = id + 1;
    if (MemoryService::Get().IsInitialized()) {
        MemoryService::Get().SceneNotifyActorCreated();
    }
    return raw;
}

void Scene::Clear()
{
    m_PendingDestroy.clear();
    m_IDMap.clear();
    const size_t n = m_Actors.size();
    m_Actors.clear();
    m_NextID = 1;
    if (MemoryService::Get().IsInitialized() && n > 0) {
        MemoryService::Get().SceneNotifyActorsDestroyed(static_cast<uint64_t>(n));
    }
}

// --------------------------------------------------------------------------
// 销毁
// --------------------------------------------------------------------------

void Scene::DestroyActor(Actor* actor)
{
    if (!actor) return;
    DestroyActorInternal(actor);
}

void Scene::DestroyActorDeferred(Actor* actor)
{
    if (!actor) return;
    // 防止重复加入队列
    auto it = std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), actor);
    if (it == m_PendingDestroy.end()) {
        m_PendingDestroy.push_back(actor);
    }
}

void Scene::FlushPendingDestroy()
{
    // 拷贝一份，防止 DestroyActorInternal 过程中修改队列
    std::vector<Actor*> toDestroy = std::move(m_PendingDestroy);
    m_PendingDestroy.clear();

    for (Actor* actor : toDestroy) {
        // 可能已被同批次父节点销毁，做有效性校验
        if (m_IDMap.count(actor->GetID())) {
            DestroyActorInternal(actor);
        }
    }
}

void Scene::DestroyActorInternal(Actor* actor)
{
    if (!actor) return;

    // 递归先销毁所有子节点（拷贝一份，因为 SetParent 会修改 children 列表）
    std::vector<Actor*> children = actor->GetChildren();
    for (Actor* child : children) {
        DestroyActorInternal(child);
    }

    // 从 ID 表移除
    m_IDMap.erase(actor->GetID());

    // 从父节点摘除（Actor 析构也会做，这里提前处理更安全）
    actor->SetParent(nullptr);

    // 从 m_Actors 移除（unique_ptr 析构销毁对象）
    auto it = std::find_if(m_Actors.begin(), m_Actors.end(),
        [actor](const std::unique_ptr<Actor>& p){ return p.get() == actor; });
    if (it != m_Actors.end()) {
        m_Actors.erase(it);
        if (MemoryService::Get().IsInitialized()) {
            MemoryService::Get().SceneNotifyActorDestroyed();
        }
    }
}

// --------------------------------------------------------------------------
// 查找
// --------------------------------------------------------------------------

Actor* Scene::FindByID(uint64_t id) const
{
    auto it = m_IDMap.find(id);
    return (it != m_IDMap.end()) ? it->second : nullptr;
}

Actor* Scene::FindByName(const std::string& name) const
{
    for (const auto& ptr : m_Actors) {
        if (ptr->GetName() == name) return ptr.get();
    }
    return nullptr;
}

std::vector<Actor*> Scene::GetRootActors() const
{
    std::vector<Actor*> roots;
    for (const auto& ptr : m_Actors) {
        if (ptr->GetParent() == nullptr) {
            roots.push_back(ptr.get());
        }
    }
    return roots;
}

// --------------------------------------------------------------------------
// 遍历
// --------------------------------------------------------------------------

void Scene::ForEach(const std::function<void(Actor&)>& fn) const
{
    for (const auto& ptr : m_Actors) {
        fn(*ptr);
    }
}

// --------------------------------------------------------------------------
// Update
// --------------------------------------------------------------------------

void Scene::OnUpdate(float deltaSeconds)
{
    // 先刷掉上一帧的延迟销毁
    FlushPendingDestroy();

    for (const auto& ptr : m_Actors) {
        ptr->Update(deltaSeconds);
    }
    m_PhysicsWorld.Step(*this, deltaSeconds);
}
