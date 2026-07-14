#pragma once

#include "Scene/Actor.h"
#include "Physics/PhysicsWorld.h"
#include "Navigation/NavigationWorld.h"
#include "Scene/WorldFrameScheduler.h"
#include "Core/TaskService.h"

#include <nlohmann/json.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

struct ComponentCreateDesc {
    ComponentTypeID type;
    bool enabled = true;
    nlohmann::json data = nlohmann::json::object();
    uint32_t version = 0;
};

struct ActorCreateDesc {
    std::string name = "Actor";
    ActorHandle parent;
    Transform transform;
    bool activeSelf = true;
    std::string tag;
    uint32_t layer = 0;
    uint32_t editorFlags = 0;
    uint64_t persistentID = 0;
    std::vector<ComponentCreateDesc> components;
    std::string prefabAssetPath;
    std::string prefabAssetUuid;
    std::string prefabLocalId;
    std::string nestedPrefabInstanceLocalId;
    ActorHandle prefabInstanceRoot;
    bool prefabRoot = false;
    nlohmann::json prefabOverrides = nlohmann::json::array();
};

struct SceneLifetimeState {
    uint64_t generation = 0;
    std::atomic<bool> alive{true};
    mutable std::shared_mutex gate;
};

class SceneLifetimeGuard {
public:
    SceneLifetimeGuard() = default;
    SceneLifetimeGuard(SceneLifetimeGuard&&) noexcept = default;
    SceneLifetimeGuard& operator=(SceneLifetimeGuard&&) noexcept = default;
    SceneLifetimeGuard(const SceneLifetimeGuard&) = delete;
    SceneLifetimeGuard& operator=(const SceneLifetimeGuard&) = delete;
    explicit operator bool() const { return static_cast<bool>(m_State); }
    uint64_t GetGeneration() const { return m_State ? m_State->generation : 0; }
private:
    friend class SceneLifetimeToken;
    SceneLifetimeGuard(std::shared_ptr<SceneLifetimeState> state,
                       std::shared_lock<std::shared_mutex> lock)
        : m_State(std::move(state)), m_Lock(std::move(lock)) {}
    std::shared_ptr<SceneLifetimeState> m_State;
    std::shared_lock<std::shared_mutex> m_Lock;
};

class SceneLifetimeToken {
public:
    SceneLifetimeToken() = default;
    bool IsAlive() const {
        const auto state = m_State.lock();
        return state && state->alive.load(std::memory_order_acquire);
    }
    uint64_t GetGeneration() const {
        const auto state = m_State.lock();
        return state ? state->generation : 0;
    }
    SceneLifetimeGuard TryAcquire() const {
        auto state = m_State.lock();
        if (!state) return {};
        std::shared_lock<std::shared_mutex> lock(state->gate);
        if (!state->alive.load(std::memory_order_acquire)) return {};
        return SceneLifetimeGuard(std::move(state), std::move(lock));
    }
private:
    friend class Scene;
    explicit SceneLifetimeToken(const std::shared_ptr<SceneLifetimeState>& state) : m_State(state) {}
    std::weak_ptr<SceneLifetimeState> m_State;
};

using WorldZoneID = uint64_t;
using WorldZoneLifetimeToken = SceneLifetimeToken;

struct WorldZoneStats {
    std::string stableName;
    uint64_t generation = 0;
    size_t actorCount = 0;
    size_t pinnedAssetCount = 0;
    size_t taskCount = 0;
};

class Scene {
public:
    explicit Scene(std::string name = "Scene");
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }
    uint64_t GetMainCameraHintActorID() const { return m_MainCameraHintActorID; }
    void SetMainCameraHintActorID(uint64_t actorID) { m_MainCameraHintActorID = actorID; }
    float GetAmbientIntensity() const { return m_AmbientIntensity; }
    void SetAmbientIntensity(float intensity) {
        m_AmbientIntensity = intensity < 0.0f ? 0.0f : intensity;
    }

    ActorHandle QueueCreateActor(const ActorCreateDesc& desc = {});
    void QueueDestroyActor(ActorHandle actor);
    void QueueSetParent(ActorHandle child, ActorHandle parent);
    void QueueMoveActor(ActorHandle child, ActorHandle parent, ActorHandle beforeSibling);
    void QueueSetActive(ActorHandle actor, bool active);
    void QueueSetTag(ActorHandle actor, const std::string& tag);
    void QueueSetLayer(ActorHandle actor, uint32_t layer);
    void QueueSetEditorFlags(ActorHandle actor, uint32_t flags);
    ComponentHandle QueueAddComponent(ActorHandle actor, const ComponentTypeID& type,
                                      const nlohmann::json& initialData = nlohmann::json::object());
    void QueueRemoveComponent(const ComponentHandle& component);
    void QueueSetComponentEnabled(const ComponentHandle& component, bool enabled);
    bool FlushCommands();

    Actor* TryGetActor(ActorHandle handle);
    const Actor* TryGetActor(ActorHandle handle) const;
    ActorHandle GetHandle(uint64_t persistentID) const;

    // Compatibility wrappers. Structural changes made while traversing are queued.
    Actor* CreateActor(const std::string& name = "Actor");
    Actor* CreateActor(const std::string& name, Actor* parent);
    Actor* CreateActorWithID(const std::string& name, uint64_t id);
    void DestroyActor(Actor* actor);
    void DestroyActorDeferred(Actor* actor);
    void FlushPendingDestroy() { FlushCommands(); }
    void SetNextID(uint64_t nextID) { m_NextID = nextID; }
    void Clear();

    Actor* FindByID(uint64_t id) const;
    Actor* FindByName(const std::string& name) const;
    std::vector<Actor*> GetRootActors() const;
    const std::vector<std::unique_ptr<Actor>>& GetAllActors() const { return m_Actors; }
    size_t ActorCount() const { return m_Actors.size(); }
    void ForEach(const std::function<void(Actor&)>& fn) const;

    void BeginPlay();
    void EndPlay();
    void Pause() { if (m_State == SceneState::Playing) m_State = SceneState::Paused; }
    void Resume() { if (m_State == SceneState::Paused) m_State = SceneState::Playing; }
    void OnUpdate(float deltaSeconds);
    SceneState GetState() const { return m_State; }
    bool IsPlaying() const { return m_State == SceneState::Playing; }
    bool IsTraversing() const { return m_Traversing; }
    void SetTimeScale(float value) { m_TimeScale = value < 0.0f ? 0.0f : value; }
    float GetTimeScale() const { return m_TimeScale; }
    SceneLifetimeToken GetLifetimeToken() const { return SceneLifetimeToken(m_Lifetime); }
    uint64_t GetLifetimeGeneration() const { return m_Lifetime->generation; }
    void AdoptAssetPins(std::vector<std::string> paths);
    const std::vector<std::string>& GetOwnedAssetPins() const { return m_OwnedAssetPins; }
    WorldZoneID CreateZone(std::string stableName);
    bool DestroyZone(WorldZoneID zone);
    bool AssignActorToZone(WorldZoneID zone, ActorHandle actor);
    bool PinAssetToZone(WorldZoneID zone, const std::string& path);
    WorldZoneLifetimeToken GetZoneLifetimeToken(WorldZoneID zone) const;
    std::vector<WorldZoneStats> GetZoneStats() const;
    size_t GetZoneCount() const { return m_Zones.size(); }
    class WorldZoneStreamer& GetZoneStreamer();
    const class WorldZoneStreamer& GetZoneStreamer() const;

    template<typename Function>
    auto SubmitZoneTask(WorldZoneID zone, TaskDescriptor descriptor, Function&& function)
        -> TaskHandle<std::invoke_result_t<Function, CancellationToken, WorldZoneLifetimeToken>> {
        auto found=m_Zones.find(zone);
        if(found==m_Zones.end())throw std::invalid_argument("unknown world zone");
        WorldZoneLifetimeToken lifetime(found->second->lifetime);
        return TaskService::Get().Submit(found->second->tasks,std::move(descriptor),
            [lifetime,function=std::forward<Function>(function)](CancellationToken cancellation)mutable{
                cancellation.ThrowIfCancellationRequested();
                return function(cancellation,lifetime);
            });
    }
    WorldFrameScheduler& GetFrameScheduler() { return m_FrameScheduler; }
    const WorldFrameScheduler& GetFrameScheduler() const { return m_FrameScheduler; }

    PhysicsWorld& GetPhysicsWorld() { return m_PhysicsWorld; }
    const PhysicsWorld& GetPhysicsWorld() const { return m_PhysicsWorld; }
    NavigationWorld& GetNavigationWorld() { return m_NavigationWorld; }
    const NavigationWorld& GetNavigationWorld() const { return m_NavigationWorld; }
    void SetNavMeshAssetPath(std::string path){m_NavMeshAssetPath=std::move(path);}
    const std::string& GetNavMeshAssetPath()const{return m_NavMeshAssetPath;}
    void SetPreloadAssets(std::vector<std::string> paths){m_PreloadAssets=std::move(paths);}
    const std::vector<std::string>& GetPreloadAssets()const{return m_PreloadAssets;}
    void SetSceneManager(class SceneManager* manager){m_SceneManager=manager;}
    class SceneManager* GetSceneManager()const{return m_SceneManager;}
    void SetGameFlowController(class GameFlowController* controller){m_GameFlowController=controller;}
    class GameFlowController* GetGameFlowController()const{return m_GameFlowController;}

private:
    void ReleaseAssetPins();
    void DestroyAllZones();
    struct WorldZone {
        WorldZoneID id = 0;
        std::string stableName;
        std::shared_ptr<SceneLifetimeState> lifetime;
        std::vector<ActorHandle> actors;
        std::vector<std::string> pinnedAssets;
        TaskScope tasks;
    };
    struct Slot { Actor* actor = nullptr; uint32_t generation = 1; bool reserved = false; };
    struct PendingCreate { ActorHandle handle; ActorCreateDesc desc; bool cancelled = false; };
    enum class CommandKind { Destroy, SetParent, MoveActor, SetActive, SetTag, SetLayer, SetEditorFlags, AddComponent, RemoveComponent, SetComponentEnabled };
    struct Command {
        CommandKind kind;
        ActorHandle actor;
        ActorHandle other;
        ActorHandle beforeSibling;
        ComponentTypeID componentType;
        std::string text;
        nlohmann::json data;
        uint32_t value = 0;
        bool flag = false;
    };

    ActorHandle ReserveHandle();
    void ReleaseReserved(ActorHandle handle);
    void DestroyActorInternal(Actor* actor);
    bool MoveActorInternal(Actor* actor, Actor* parent, Actor* beforeSibling);
    bool MoveRootActorBefore(Actor* actor, Actor* beforeSibling);
    std::vector<Actor*> OrderedActors(bool reverse = false) const;
    void FinalizeCreated(const std::vector<Actor*>& actors);

    std::string m_Name;
    uint64_t m_MainCameraHintActorID = 0;
    float m_AmbientIntensity = 1.0f;
    uint64_t m_NextID = 1;
    std::vector<std::unique_ptr<Actor>> m_Actors;
    std::unordered_map<uint64_t, Actor*> m_IDMap;
    std::unordered_map<uint64_t, ActorHandle> m_IDHandles;
    std::vector<Slot> m_Slots;
    std::vector<PendingCreate> m_PendingCreates;
    std::vector<Command> m_Commands;
    PhysicsWorld m_PhysicsWorld;
    NavigationWorld m_NavigationWorld;
    WorldFrameScheduler m_FrameScheduler;
    std::string m_NavMeshAssetPath;
    std::vector<std::string> m_PreloadAssets;
    SceneState m_State = SceneState::Edit;
    bool m_Traversing = false;
    bool m_Flushing = false;
    float m_TimeScale = 1.0f;
    class SceneManager* m_SceneManager = nullptr;
    class GameFlowController* m_GameFlowController = nullptr;
    std::shared_ptr<SceneLifetimeState> m_Lifetime;
    std::vector<std::string> m_OwnedAssetPins;
    std::unordered_map<WorldZoneID,std::unique_ptr<WorldZone>> m_Zones;
    WorldZoneID m_NextZoneID = 1;
    std::unique_ptr<class WorldZoneStreamer> m_ZoneStreamer;
};
