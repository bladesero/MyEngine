#include "Scene/Scene.h"

#include "Assets/AssetManager.h"
#include "Core/Logger.h"
#include "Core/Memory/MemoryService.h"
#include "Physics/ColliderComponent.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/TypeRegistry.h"
#include "Scene/WorldZoneStreamer.h"

#include <algorithm>
#include <mutex>

namespace { std::atomic<uint64_t> g_NextSceneLifetimeGeneration{1}; }
namespace { std::atomic<uint64_t> g_NextZoneLifetimeGeneration{1}; }

Scene::Scene(std::string name)
    : m_Name(std::move(name))
    , m_Lifetime(std::make_shared<SceneLifetimeState>())
    , m_ZoneStreamer(std::make_unique<WorldZoneStreamer>())
{
    m_Lifetime->generation = g_NextSceneLifetimeGeneration.fetch_add(1, std::memory_order_relaxed);
}
Scene::~Scene() {
    std::unique_lock<std::shared_mutex> lifetimeLock(m_Lifetime->gate);
    m_Lifetime->alive.store(false, std::memory_order_release);
    Clear();
    ReleaseAssetPins();
}

void Scene::AdoptAssetPins(std::vector<std::string> paths)
{
    ReleaseAssetPins();
    m_OwnedAssetPins = std::move(paths);
}

void Scene::ReleaseAssetPins()
{
    for (const std::string& path : m_OwnedAssetPins) AssetManager::Get().Unpin(path);
    m_OwnedAssetPins.clear();
}

WorldZoneID Scene::CreateZone(std::string stableName)
{
    if(stableName.empty())return 0;
    for(const auto& entry:m_Zones)if(entry.second->stableName==stableName)return 0;
    auto zone=std::make_unique<WorldZone>();
    zone->id=m_NextZoneID++;
    if(!zone->id)zone->id=m_NextZoneID++;
    zone->stableName=std::move(stableName);
    zone->lifetime=std::make_shared<SceneLifetimeState>();
    zone->lifetime->generation=g_NextZoneLifetimeGeneration.fetch_add(1,std::memory_order_relaxed);
    const WorldZoneID id=zone->id;
    m_Zones.emplace(id,std::move(zone));
    return id;
}

bool Scene::AssignActorToZone(WorldZoneID zoneID,ActorHandle actor)
{
    auto found=m_Zones.find(zoneID);
    if(found==m_Zones.end()||!TryGetActor(actor))return false;
    for(const auto& entry:m_Zones)
        if(std::find(entry.second->actors.begin(),entry.second->actors.end(),actor)!=entry.second->actors.end())
            return entry.first==zoneID;
    found->second->actors.push_back(actor);
    return true;
}

bool Scene::PinAssetToZone(WorldZoneID zoneID,const std::string& path)
{
    auto found=m_Zones.find(zoneID);
    if(found==m_Zones.end()||path.empty())return false;
    auto& paths=found->second->pinnedAssets;
    if(std::find(paths.begin(),paths.end(),path)!=paths.end())return true;
    AssetManager::Get().Pin(path);paths.push_back(path);return true;
}

WorldZoneLifetimeToken Scene::GetZoneLifetimeToken(WorldZoneID zoneID) const
{
    const auto found=m_Zones.find(zoneID);
    return found==m_Zones.end()?WorldZoneLifetimeToken{}:WorldZoneLifetimeToken(found->second->lifetime);
}

std::vector<WorldZoneStats> Scene::GetZoneStats() const
{
    std::vector<WorldZoneStats> result;result.reserve(m_Zones.size());
    for(const auto& entry:m_Zones){const WorldZone& zone=*entry.second;
        result.push_back({zone.stableName,zone.lifetime->generation,zone.actors.size(),
                          zone.pinnedAssets.size(),zone.tasks.TaskCount()});}
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.stableName<b.stableName;});
    return result;
}

bool Scene::DestroyZone(WorldZoneID zoneID)
{
    auto found=m_Zones.find(zoneID);if(found==m_Zones.end())return false;
    WorldZone& zone=*found->second;
    {
        std::unique_lock<std::shared_mutex> lock(zone.lifetime->gate);
        zone.lifetime->alive.store(false,std::memory_order_release);
    }
    zone.tasks.CancelAndWait();
    for(ActorHandle actor:zone.actors)if(Actor* value=TryGetActor(actor))DestroyActor(value);
    for(const std::string& path:zone.pinnedAssets)AssetManager::Get().Unpin(path);
    m_Zones.erase(found);return true;
}

void Scene::DestroyAllZones()
{
    std::vector<WorldZoneID> ids;ids.reserve(m_Zones.size());
    for(const auto& entry:m_Zones)ids.push_back(entry.first);
    for(WorldZoneID id:ids)DestroyZone(id);
    m_NextZoneID=1;
}

namespace {
void ApplyActorLayer(Actor& actor, uint32_t layer)
{
    actor.SetLayer(layer);
    actor.ForEachComponent([&](Component& component) {
        if (auto* collider = dynamic_cast<ColliderComponent*>(&component)) {
            collider->SetLayer(layer);
        }
    });
}
}

ActorHandle Scene::ReserveHandle()
{
    for (uint32_t i = 0; i < m_Slots.size(); ++i) {
        Slot& slot = m_Slots[i];
        if (!slot.actor && !slot.reserved) { slot.reserved = true; return {i, slot.generation}; }
    }
    const uint32_t index = static_cast<uint32_t>(m_Slots.size());
    m_Slots.push_back({nullptr, 1, true});
    return {index, 1};
}

void Scene::ReleaseReserved(ActorHandle handle)
{
    if (handle.index >= m_Slots.size()) return;
    Slot& slot = m_Slots[handle.index];
    if (slot.generation == handle.generation && slot.reserved && !slot.actor) {
        slot.reserved = false;
        ++slot.generation;
        if (slot.generation == 0) slot.generation = 1;
    }
}

ActorHandle Scene::QueueCreateActor(const ActorCreateDesc& desc)
{
    ActorHandle handle = ReserveHandle();
    ActorCreateDesc queued = desc;
    if (queued.prefabRoot) queued.prefabInstanceRoot = handle;
    m_PendingCreates.push_back({handle, std::move(queued), false});
    return handle;
}

void Scene::QueueDestroyActor(ActorHandle handle)
{
    if (!handle) return;
    for (auto& pending : m_PendingCreates) {
        if (pending.handle == handle) { pending.cancelled = true; return; }
    }
    if (Actor* actor = TryGetActor(handle)) actor->MarkPendingDestroy();
    m_Commands.push_back({CommandKind::Destroy, handle});
}

void Scene::QueueSetParent(ActorHandle child, ActorHandle parent)
{ m_Commands.push_back({CommandKind::SetParent, child, parent}); }
void Scene::QueueMoveActor(ActorHandle child, ActorHandle parent, ActorHandle beforeSibling)
{
    Command c{CommandKind::MoveActor, child, parent};
    c.beforeSibling = beforeSibling;
    m_Commands.push_back(std::move(c));
}
void Scene::QueueSetActive(ActorHandle actor, bool active)
{ Command c{CommandKind::SetActive, actor}; c.flag = active; m_Commands.push_back(std::move(c)); }

void Scene::QueueSetTag(ActorHandle actor, const std::string& tag)
{ Command c{CommandKind::SetTag, actor}; c.text = tag; m_Commands.push_back(std::move(c)); }

void Scene::QueueSetLayer(ActorHandle actor, uint32_t layer)
{ Command c{CommandKind::SetLayer, actor}; c.value = layer; m_Commands.push_back(std::move(c)); }

void Scene::QueueSetEditorFlags(ActorHandle actor, uint32_t flags)
{ Command c{CommandKind::SetEditorFlags, actor}; c.value = flags; m_Commands.push_back(std::move(c)); }

ComponentHandle Scene::QueueAddComponent(ActorHandle actor, const ComponentTypeID& type,
                                         const nlohmann::json& initialData)
{
    Command c{CommandKind::AddComponent, actor}; c.componentType = type; c.data = initialData;
    m_Commands.push_back(std::move(c));
    return {actor, type};
}

void Scene::QueueRemoveComponent(const ComponentHandle& component)
{
    Command c{CommandKind::RemoveComponent, component.actor}; c.componentType = component.type;
    m_Commands.push_back(std::move(c));
}

void Scene::QueueSetComponentEnabled(const ComponentHandle& component, bool enabled)
{
    Command c{CommandKind::SetComponentEnabled, component.actor}; c.componentType = component.type; c.flag = enabled;
    m_Commands.push_back(std::move(c));
}

Actor* Scene::TryGetActor(ActorHandle handle)
{
    if (!handle || handle.index >= m_Slots.size()) return nullptr;
    Slot& slot = m_Slots[handle.index];
    return slot.generation == handle.generation && !slot.reserved ? slot.actor : nullptr;
}

const Actor* Scene::TryGetActor(ActorHandle handle) const
{ return const_cast<Scene*>(this)->TryGetActor(handle); }

ActorHandle Scene::GetHandle(uint64_t persistentID) const
{
    const auto it = m_IDHandles.find(persistentID);
    return it == m_IDHandles.end() ? ActorHandle{} : it->second;
}

bool Scene::FlushCommands()
{
    if (m_Traversing || m_Flushing) return false;
    m_Flushing = true;
    std::vector<PendingCreate> creates = std::move(m_PendingCreates);
    std::vector<Command> commands = std::move(m_Commands);
    m_PendingCreates.clear(); m_Commands.clear();
    std::vector<Actor*> created;
    std::vector<bool> skip(commands.size(), false);

    // Normalize conflicts before applying the batch: destroy wins, last scalar
    // mutation wins, and an add/remove pair for a previously absent component
    // is a no-op.
    std::unordered_map<uint64_t, size_t> lastParent;
    std::unordered_map<uint64_t, size_t> lastActive;
    std::unordered_map<uint64_t, size_t> lastTag;
    std::unordered_map<uint64_t, size_t> lastLayer;
    std::unordered_map<uint64_t, size_t> lastEditorFlags;
    std::unordered_map<std::string, size_t> pendingAdds;
    std::unordered_map<uint64_t, bool> destroysActor;
    for (const Command& command : commands) if (command.kind == CommandKind::Destroy)
        destroysActor[command.actor.ToUInt64()] = true;
    for (size_t i = 0; i < commands.size(); ++i) {
        const Command& command = commands[i];
        const uint64_t actorKey = command.actor.ToUInt64();
        if (destroysActor.count(actorKey) && command.kind != CommandKind::Destroy) { skip[i] = true; continue; }
        if (command.kind == CommandKind::SetParent || command.kind == CommandKind::MoveActor) {
            if (lastParent.count(actorKey)) skip[lastParent[actorKey]] = true;
            lastParent[actorKey] = i;
        } else if (command.kind == CommandKind::SetActive) {
            if (lastActive.count(actorKey)) skip[lastActive[actorKey]] = true;
            lastActive[actorKey] = i;
        } else if (command.kind == CommandKind::SetTag) {
            if (lastTag.count(actorKey)) skip[lastTag[actorKey]] = true;
            lastTag[actorKey] = i;
        } else if (command.kind == CommandKind::SetLayer) {
            if (lastLayer.count(actorKey)) skip[lastLayer[actorKey]] = true;
            lastLayer[actorKey] = i;
        } else if (command.kind == CommandKind::SetEditorFlags) {
            if (lastEditorFlags.count(actorKey)) skip[lastEditorFlags[actorKey]] = true;
            lastEditorFlags[actorKey] = i;
        } else if (command.kind == CommandKind::AddComponent) {
            pendingAdds[std::to_string(actorKey) + "|" + command.componentType] = i;
        } else if (command.kind == CommandKind::RemoveComponent) {
            const std::string key = std::to_string(actorKey) + "|" + command.componentType;
            const auto add = pendingAdds.find(key);
            if (add != pendingAdds.end()) { skip[add->second] = true; skip[i] = true; pendingAdds.erase(add); }
        }
    }

    for (auto& pending : creates) {
        if (pending.cancelled) { ReleaseReserved(pending.handle); continue; }
        Slot& slot = m_Slots[pending.handle.index];
        const uint64_t id = pending.desc.persistentID ? pending.desc.persistentID : m_NextID++;
        if (m_IDMap.count(id)) { Logger::Error("[Scene] duplicate actor id ", id); ReleaseReserved(pending.handle); continue; }
        auto actor = std::make_unique<Actor>(pending.desc.name, id, pending.handle);
        Actor* raw = actor.get(); raw->m_Scene = this; raw->m_State = ActorState::Constructed;
        raw->m_ActiveSelf = pending.desc.activeSelf; raw->m_Transform = pending.desc.transform;
        raw->m_Tag = pending.desc.tag;
        raw->m_Layer = pending.desc.layer;
        raw->m_EditorFlags = pending.desc.editorFlags;
        raw->m_PrefabAssetPath = pending.desc.prefabAssetPath;
        raw->m_PrefabAssetUuid = pending.desc.prefabAssetUuid;
        raw->m_PrefabLocalId = pending.desc.prefabLocalId;
        raw->m_NestedPrefabInstanceLocalId = pending.desc.nestedPrefabInstanceLocalId;
        raw->m_PrefabInstanceRoot = pending.desc.prefabInstanceRoot;
        raw->m_PrefabOverrides = pending.desc.prefabOverrides;
        slot.actor = raw; slot.reserved = false;
        m_IDMap[id] = raw; m_IDHandles[id] = pending.handle;
        m_Actors.push_back(std::move(actor)); created.push_back(raw);
        if (id >= m_NextID) m_NextID = id + 1;
        if (MemoryService::Get().IsInitialized()) MemoryService::Get().SceneNotifyActorCreated();
    }

    for (const auto& pending : creates) {
        Actor* actor = TryGetActor(pending.handle);
        if (!actor) continue;
        if (Actor* parent = TryGetActor(pending.desc.parent)) actor->SetParent(parent);
        for (const auto& componentDesc : pending.desc.components) {
            std::unique_ptr<Component> component = ComponentRegistry::Get().CreateDetached(componentDesc.type);
            if (!component) { Logger::Warn("[Scene] unregistered component '", componentDesc.type, "'"); continue; }
            component->SetEnabled(componentDesc.enabled);
            std::string deserializeError;
            if (!TypeRegistry::Get().Deserialize(*component, componentDesc.type,
                    componentDesc.version, componentDesc.data, &deserializeError)) {
                Logger::Error("[Scene] component deserialize failed actor=", actor->GetName(),
                              " type=", componentDesc.type, " path=/properties: ",
                              deserializeError); continue;
            }
            const std::type_index componentType(typeid(*component));
            actor->AddComponentObject(componentType, std::move(component), false);
        }
    }
    // Relationships and initial active flags must be applied before OnInitialize.
    for (size_t commandIndex = 0; commandIndex < commands.size(); ++commandIndex) {
        if (skip[commandIndex]) continue;
        const Command& command = commands[commandIndex];
        Actor* actor = TryGetActor(command.actor);
        if (!actor) continue;
        if (command.kind == CommandKind::SetParent) actor->SetParent(TryGetActor(command.other));
        else if (command.kind == CommandKind::MoveActor)
            MoveActorInternal(actor, TryGetActor(command.other), TryGetActor(command.beforeSibling));
        else if (command.kind == CommandKind::SetActive) actor->m_ActiveSelf = command.flag;
        else if (command.kind == CommandKind::SetTag) actor->m_Tag = command.text;
        else if (command.kind == CommandKind::SetLayer) ApplyActorLayer(*actor, command.value);
        else if (command.kind == CommandKind::SetEditorFlags) actor->m_EditorFlags = command.value;
    }
    FinalizeCreated(created);

    std::vector<ActorHandle> destroys;
    for (size_t commandIndex = 0; commandIndex < commands.size(); ++commandIndex) {
        if (skip[commandIndex]) continue;
        const Command& command = commands[commandIndex];
        Actor* actor = TryGetActor(command.actor);
        if (!actor && command.kind != CommandKind::Destroy) continue;
        if (actor && actor->IsPendingDestroy() && command.kind != CommandKind::Destroy) continue;
        switch (command.kind) {
        case CommandKind::Destroy: destroys.push_back(command.actor); break;
        case CommandKind::SetParent: break;
        case CommandKind::MoveActor: break;
        case CommandKind::SetActive:
            if (actor && std::find(created.begin(), created.end(), actor) == created.end()) {
                actor->m_ActiveSelf = command.flag;
                actor->RefreshActiveInHierarchy(!actor->m_Parent || actor->m_Parent->IsActiveInHierarchy(), IsPlaying());
            }
            break;
        case CommandKind::SetTag:
            if (actor) actor->m_Tag = command.text;
            break;
        case CommandKind::SetLayer:
            if (actor) ApplyActorLayer(*actor, command.value);
            break;
        case CommandKind::SetEditorFlags:
            if (actor) actor->m_EditorFlags = command.value;
            break;
        case CommandKind::AddComponent: if (actor && !actor->HasComponentType(command.componentType)) {
            auto component = ComponentRegistry::Get().CreateDetached(command.componentType);
            if (component) {
                std::string deserializeError;
                if (!TypeRegistry::Get().Deserialize(*component, command.componentType, 0,
                                                      command.data, &deserializeError)) break;
                const std::type_index componentType(typeid(*component));
                actor->AddComponentObject(componentType, std::move(component), true);
            }
        } break;
        case CommandKind::RemoveComponent: if (actor) actor->RemoveComponentByTypeName(command.componentType); break;
        case CommandKind::SetComponentEnabled: if (actor) {
            if (Component* comp = actor->GetComponentByTypeName(command.componentType)) {
                const bool before = comp->m_EffectiveEnabled; comp->SetEnabled(command.flag);
                const bool after = IsPlaying() && actor->IsActive() && comp->IsEnabled();
                if (!before && after) { comp->OnEnable(); comp->m_EffectiveEnabled = true; }
                else if (before && !after) { comp->OnDisable(); comp->m_EffectiveEnabled = false; }
            }
        } break;
        }
    }
    for (ActorHandle handle : destroys) if (Actor* actor = TryGetActor(handle)) DestroyActorInternal(actor);
    m_Flushing = false;
    return true;
}

void Scene::FinalizeCreated(const std::vector<Actor*>& actors)
{
    const bool runtimeActive = m_State == SceneState::Playing || m_State == SceneState::Paused;
    for (Actor* actor : actors) actor->RefreshActiveInHierarchy(!actor->m_Parent || actor->m_Parent->IsActiveInHierarchy(), false);
    for (Actor* actor : OrderedActors()) {
        if (std::find(actors.begin(), actors.end(), actor) != actors.end()) actor->FinalizeConstruction(runtimeActive);
    }
    if (runtimeActive) {
        for (Actor* actor : OrderedActors()) if (std::find(actors.begin(), actors.end(), actor) != actors.end()) actor->BeginPlayPhase();
        for (Actor* actor : OrderedActors()) if (std::find(actors.begin(), actors.end(), actor) != actors.end()) actor->EnablePlayPhase();
        for (Actor* actor : OrderedActors()) if (std::find(actors.begin(), actors.end(), actor) != actors.end()) actor->StartPlayPhase();
    }
}

Actor* Scene::CreateActor(const std::string& name)
{
    const ActorHandle handle = QueueCreateActor(ActorCreateDesc{name});
    if (!m_Traversing) FlushCommands();
    return TryGetActor(handle);
}

Actor* Scene::CreateActor(const std::string& name, Actor* parent)
{
    ActorCreateDesc desc; desc.name = name; desc.parent = parent ? parent->GetHandle() : ActorHandle{};
    const ActorHandle handle = QueueCreateActor(desc); if (!m_Traversing) FlushCommands(); return TryGetActor(handle);
}

Actor* Scene::CreateActorWithID(const std::string& name, uint64_t id)
{
    if (Actor* existing = FindByID(id)) return existing;
    ActorCreateDesc desc; desc.name = name; desc.persistentID = id;
    const ActorHandle handle = QueueCreateActor(desc); if (!m_Traversing) FlushCommands(); return TryGetActor(handle);
}

void Scene::DestroyActor(Actor* actor)
{ if (actor) { QueueDestroyActor(actor->GetHandle()); if (!m_Traversing) FlushCommands(); } }
void Scene::DestroyActorDeferred(Actor* actor)
{ if (actor) QueueDestroyActor(actor->GetHandle()); }

void Scene::DestroyActorInternal(Actor* actor)
{
    if (!actor) return;
    for (Actor* child : std::vector<Actor*>(actor->m_Children)) DestroyActorInternal(child);
    if (m_State == SceneState::Playing || m_State == SceneState::Paused) actor->EndPlay();
    actor->SetParent(nullptr);
    const ActorHandle handle = actor->m_Handle; const uint64_t id = actor->m_ID;
    for(auto& entry:m_Zones){auto& actors=entry.second->actors;
        actors.erase(std::remove(actors.begin(),actors.end(),handle),actors.end());}
    actor->m_State = ActorState::Destroyed;
    m_IDMap.erase(id); m_IDHandles.erase(id);
    if (handle.index < m_Slots.size()) {
        Slot& slot = m_Slots[handle.index]; slot.actor = nullptr; slot.reserved = false; ++slot.generation; if (!slot.generation) slot.generation = 1;
    }
    const auto it = std::find_if(m_Actors.begin(), m_Actors.end(), [actor](const auto& value) { return value.get() == actor; });
    if (it != m_Actors.end()) { m_Actors.erase(it); if (MemoryService::Get().IsInitialized()) MemoryService::Get().SceneNotifyActorDestroyed(); }
}

bool Scene::MoveRootActorBefore(Actor* actor, Actor* beforeSibling)
{
    if (!actor || actor == beforeSibling) return false;
    const auto actorIt = std::find_if(m_Actors.begin(), m_Actors.end(),
        [actor](const auto& value) { return value.get() == actor; });
    if (actorIt == m_Actors.end()) return false;
    auto beforeIt = m_Actors.end();
    if (beforeSibling) {
        beforeIt = std::find_if(m_Actors.begin(), m_Actors.end(),
            [beforeSibling](const auto& value) { return value.get() == beforeSibling; });
        if (beforeIt == m_Actors.end()) return false;
    }

    std::unique_ptr<Actor> moved = std::move(*actorIt);
    m_Actors.erase(actorIt);
    if (beforeSibling) {
        beforeIt = std::find_if(m_Actors.begin(), m_Actors.end(),
            [beforeSibling](const auto& value) { return value.get() == beforeSibling; });
    } else {
        beforeIt = m_Actors.end();
    }
    m_Actors.insert(beforeIt, std::move(moved));
    return true;
}

bool Scene::MoveActorInternal(Actor* actor, Actor* parent, Actor* beforeSibling)
{
    if (!actor) return false;
    if (actor == parent || actor == beforeSibling) {
        Logger::Warn("[Scene] rejected actor move to itself");
        return false;
    }
    for (Actor* ancestor = parent; ancestor; ancestor = ancestor->GetParent()) {
        if (ancestor == actor) {
            Logger::Warn("[Scene] rejected cyclic actor move");
            return false;
        }
    }
    if (beforeSibling && beforeSibling->GetParent() != parent) {
        Logger::Warn("[Scene] rejected actor move with sibling outside target parent");
        return false;
    }

    const auto nextSiblingOf = [](Actor* value) -> Actor* {
        if (!value) return nullptr;
        Actor* parentActor = value->GetParent();
        const std::vector<Actor*> siblings = parentActor ? parentActor->GetChildren() : value->m_Scene->GetRootActors();
        const auto it = std::find(siblings.begin(), siblings.end(), value);
        if (it == siblings.end()) return nullptr;
        const auto next = std::next(it);
        return next != siblings.end() ? *next : nullptr;
    };
    if (actor->GetParent() == parent && nextSiblingOf(actor) == beforeSibling) return false;

    actor->SetParent(parent);
    if (parent) return parent->MoveChildBefore(actor, beforeSibling);
    return MoveRootActorBefore(actor, beforeSibling);
}

void Scene::Clear()
{
    if (m_State == SceneState::Playing || m_State == SceneState::Paused) EndPlay();
    if (m_ZoneStreamer) m_ZoneStreamer->Reset(*this);
    DestroyAllZones();
    m_PhysicsWorld.Clear();
    m_NavigationWorld.Clear();
    m_NavMeshAssetPath.clear();m_PreloadAssets.clear();
    m_PendingCreates.clear(); m_Commands.clear();
    m_IDMap.clear(); m_IDHandles.clear();
    const size_t count = m_Actors.size(); m_Actors.clear(); m_NextID = 1;
    for (Slot& slot : m_Slots) { slot.actor = nullptr; slot.reserved = false; ++slot.generation; if (!slot.generation) slot.generation = 1; }
    if (MemoryService::Get().IsInitialized() && count) MemoryService::Get().SceneNotifyActorsDestroyed(static_cast<uint64_t>(count));
}

Actor* Scene::FindByID(uint64_t id) const { const auto it = m_IDMap.find(id); return it == m_IDMap.end() ? nullptr : it->second; }
Actor* Scene::FindByName(const std::string& name) const { for (const auto& actor : m_Actors) if (actor->GetName() == name) return actor.get(); return nullptr; }
std::vector<Actor*> Scene::GetRootActors() const { std::vector<Actor*> result; for (const auto& actor : m_Actors) if (!actor->GetParent()) result.push_back(actor.get()); return result; }

std::vector<Actor*> Scene::OrderedActors(bool reverse) const
{
    std::vector<Actor*> result;
    std::function<void(Actor*)> visit = [&](Actor* actor) { result.push_back(actor); for (Actor* child : actor->GetChildren()) visit(child); };
    for (Actor* root : GetRootActors()) visit(root);
    if (reverse) std::reverse(result.begin(), result.end());
    return result;
}

void Scene::ForEach(const std::function<void(Actor&)>& fn) const { for (Actor* actor : OrderedActors()) fn(*actor); }

void Scene::BeginPlay()
{
    FlushCommands(); m_State = SceneState::Playing;
    for (Actor* actor : OrderedActors()) actor->BeginPlayPhase();
    for (Actor* actor : OrderedActors()) actor->EnablePlayPhase();
    for (Actor* actor : OrderedActors()) actor->StartPlayPhase();
}

void Scene::EndPlay()
{
    if (m_State != SceneState::Playing && m_State != SceneState::Paused) return;
    m_State = SceneState::Stopping;
    for (Actor* actor : OrderedActors(true)) actor->EndPlay();
    m_State = SceneState::Edit;
}

void Scene::OnUpdate(float deltaSeconds)
{
    // Headless/runtime users may drive Scene directly without a SceneLayer.
    if (m_State == SceneState::Edit) BeginPlay();
    if (m_State != SceneState::Playing && m_State != SceneState::Paused) return;
    m_FrameScheduler.Tick(*this, deltaSeconds);
}

WorldZoneStreamer& Scene::GetZoneStreamer() { return *m_ZoneStreamer; }
const WorldZoneStreamer& Scene::GetZoneStreamer() const { return *m_ZoneStreamer; }
