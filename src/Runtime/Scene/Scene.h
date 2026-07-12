#pragma once

#include "Scene/Actor.h"
#include "Physics/PhysicsWorld.h"
#include "Navigation/NavigationWorld.h"
#include "Scene/WorldFrameScheduler.h"

#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>
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

private:
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
};
