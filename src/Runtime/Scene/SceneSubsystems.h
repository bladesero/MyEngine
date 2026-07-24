#pragma once

#include "API/RuntimeApi.h"

#include "Assets/NavMeshData.h"
#include "Core/EngineMath.h"
#include "Scene/ActorHandle.h"

#include <cstdint>
#include <memory>
#include <vector>

class Actor;
class Scene;

struct RaycastHit {
    Actor* actor = nullptr;
    Vec3 point = Vec3::Zero();
    Vec3 normal = Vec3::Up();
    float distance = 0.0f;
};

class IScenePhysicsSubsystem {
public:
    virtual ~IScenePhysicsSubsystem() = default;
    virtual void SetGravity(const Vec3& gravity) = 0;
    virtual const Vec3& GetGravity() const = 0;
    virtual void Clear() = 0;
    virtual void Step(Scene& scene, float deltaSeconds) = 0;
    virtual void StepFixed(Scene& scene, float fixedDeltaSeconds) = 0;
    virtual bool Raycast(const Scene& scene, const Ray& ray, float maxDistance, uint32_t layerMask,
                         RaycastHit& hit) const = 0;
    virtual bool OverlapSphere(const Scene& scene, const Vec3& center, float radius, uint32_t layerMask,
                               std::vector<ActorHandle>& outActors) const = 0;
};

struct NavigationSoundEvent {
    Vec3 position;
    float radius = 0.0f;
    ActorHandle source;
    float remaining = 0.0f;
};

class MYENGINE_RUNTIME_API ISceneNavigationSubsystem {
public:
    virtual ~ISceneNavigationSubsystem() = default;
    virtual bool Bake(const NavMeshBakeSettings& settings, const std::vector<AABB>& obstacles = {}) = 0;
    virtual void Clear() = 0;
    virtual bool IsBaked() const = 0;
    virtual bool FindPath(const Vec3& start, const Vec3& goal, std::vector<Vec3>& outPath) const = 0;
    virtual bool IsWalkable(const Vec3& position) const = 0;
    virtual void EmitSound(const Vec3& position, float radius, ActorHandle source, float duration = 0.25f) = 0;
    virtual std::vector<NavigationSoundEvent> QuerySounds(const Vec3& listener) const = 0;
    virtual void Update(float deltaSeconds) = 0;
    virtual const NavMeshBakeSettings& GetSettings() const = 0;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual const std::vector<uint8_t>& GetCells() const = 0;
    virtual bool SetBakedData(const NavMeshBakeSettings& settings, uint32_t width, uint32_t height,
                              std::vector<uint8_t> cells) = 0;
    virtual bool SetAreaWalkable(const AABB& bounds, bool walkable) = 0;
    virtual uint64_t GetRevision() const = 0;
};

using ScenePhysicsSubsystemFactory = std::unique_ptr<IScenePhysicsSubsystem> (*)();
using SceneNavigationSubsystemFactory = std::unique_ptr<ISceneNavigationSubsystem> (*)();

bool RegisterScenePhysicsSubsystemFactory(ScenePhysicsSubsystemFactory factory);
bool RegisterSceneNavigationSubsystemFactory(SceneNavigationSubsystemFactory factory);
std::unique_ptr<IScenePhysicsSubsystem> CreateScenePhysicsSubsystem();
std::unique_ptr<ISceneNavigationSubsystem> CreateSceneNavigationSubsystem();
MYENGINE_RUNTIME_API bool HasScenePhysicsSubsystemFactory();
MYENGINE_RUNTIME_API bool HasSceneNavigationSubsystemFactory();
