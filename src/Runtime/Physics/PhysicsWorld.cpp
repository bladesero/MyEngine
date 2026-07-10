#include "Physics/PhysicsWorld.h"

#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kMaxBodies = 65536;
constexpr uint32_t kMaxBodyPairs = 65536;
constexpr uint32_t kMaxContacts = 20480;
constexpr JPH::BroadPhaseLayer kBroadStatic(0);
constexpr JPH::BroadPhaseLayer kBroadMoving(1);

std::mutex gJoltMutex;
uint32_t gJoltUsers = 0;

void JoltTrace(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Logger::Info("[Jolt] ", buffer);
}

bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
{
    Logger::Error("[Jolt] Assertion failed: ", expression, " at ", file, ":", line,
                  message ? " - " : "", message ? message : "");
    return false;
}

void AcquireJolt()
{
    std::lock_guard<std::mutex> lock(gJoltMutex);
    if (gJoltUsers++ != 0) return;
    JPH::RegisterDefaultAllocator();
    JPH::Trace = JoltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

void ReleaseJolt()
{
    std::lock_guard<std::mutex> lock(gJoltMutex);
    if (--gJoltUsers != 0) return;
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

struct JoltScope {
    JoltScope() { AcquireJolt(); }
    ~JoltScope() { ReleaseJolt(); }
};

JPH::Vec3 ToJolt(const Vec3& value) { return {value.x, value.y, -value.z}; }
JPH::RVec3 ToJoltPosition(const Vec3& value) { return {value.x, value.y, -value.z}; }
Vec3 FromJolt(JPH::RVec3Arg value) {
    return {static_cast<float>(value.GetX()), static_cast<float>(value.GetY()),
            -static_cast<float>(value.GetZ())};
}

JPH::Quat ToJolt(const Quat& q) { return {-q.x, -q.y, q.z, q.w}; }
Quat FromJolt(JPH::QuatArg q) { return {-q.GetX(), -q.GetY(), q.GetZ(), q.GetW()}; }

void Decompose(const Mat4& m, Transform& transform)
{
    transform.position = {m.m[3][0], m.m[3][1], m.m[3][2]};
    const auto length = [&](int row) {
        return std::sqrt(m.m[row][0] * m.m[row][0] + m.m[row][1] * m.m[row][1] +
                         m.m[row][2] * m.m[row][2]);
    };
    transform.scale = {length(0), length(1), length(2)};
    const float sx = std::max(transform.scale.x, 1e-8f);
    const float sy = std::max(transform.scale.y, 1e-8f);
    const float sz = std::max(transform.scale.z, 1e-8f);
    const float pitch = std::asin(std::clamp(m.m[1][2] / sy, -1.0f, 1.0f));
    float yaw = 0.0f, roll = 0.0f;
    if (std::fabs(std::cos(pitch)) > 1e-4f) {
        yaw = std::atan2(-m.m[0][2] / sx, m.m[2][2] / sz);
        roll = std::atan2(-m.m[1][0] / sy, m.m[1][1] / sy);
    } else {
        yaw = std::atan2(m.m[0][1] / sx, m.m[0][0] / sx);
    }
    transform.rotation = {pitch * kRad2Deg, yaw * kRad2Deg, roll * kRad2Deg};
}

Quat WorldRotation(const Actor& actor)
{
    Mat4 world = actor.GetWorldMatrix();
    for (int row = 0; row < 3; ++row) {
        const float len = std::sqrt(world.m[row][0] * world.m[row][0] +
                                    world.m[row][1] * world.m[row][1] +
                                    world.m[row][2] * world.m[row][2]);
        if (len > 1e-8f) for (int col = 0; col < 3; ++col) world.m[row][col] /= len;
    }
    return Quat::FromMat4(world);
}

Vec3 WorldScale(const Actor& actor)
{
    const Mat4 world = actor.GetWorldMatrix();
    const auto rowLength = [&](int row) {
        return std::sqrt(world.m[row][0] * world.m[row][0] + world.m[row][1] * world.m[row][1] +
                         world.m[row][2] * world.m[row][2]);
    };
    return {rowLength(0), rowLength(1), rowLength(2)};
}

bool HasNonUniformParentScale(const Actor& actor)
{
    if (!actor.GetParent()) return false;
    const Vec3 scale = WorldScale(*actor.GetParent());
    return std::fabs(scale.x - scale.y) > 1e-4f || std::fabs(scale.y - scale.z) > 1e-4f;
}

void SetActorWorldPose(Actor& actor, const Vec3& position, const Quat& rotation)
{
    const Vec3 localScale = actor.GetTransform().scale;
    Mat4 world = Mat4::Scale(WorldScale(actor)) * rotation.ToMat4() * Mat4::Translation(position);
    if (actor.GetParent()) {
        Mat4 inverseParent;
        if (Mat4Invert(actor.GetParent()->GetWorldMatrix(), inverseParent)) world *= inverseParent;
    }
    Decompose(world, actor.GetTransform());
    actor.GetTransform().scale = localScale;
}

uint64_t HashFloat(uint64_t hash, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (hash ^ bits) * 1099511628211ull;
}

uint64_t HashUInt(uint64_t hash, uint32_t value)
{
    return (hash ^ value) * 1099511628211ull;
}

bool SamePose(const Vec3& position, const Quat& rotation, const Vec3& otherPosition, const Quat& otherRotation)
{
    if ((position - otherPosition).LengthSq() > 1e-8f) return false;
    const float dot = rotation.x * otherRotation.x + rotation.y * otherRotation.y +
                      rotation.z * otherRotation.z + rotation.w * otherRotation.w;
    return std::fabs(dot) > 0.99999f;
}

struct LayerState {
    uint32_t layer = 1;
    uint32_t mask = ~uint32_t{0};
    bool moving = false;
    bool valid = false;
};

class BroadPhaseInterface final : public JPH::BroadPhaseLayerInterface {
public:
    explicit BroadPhaseInterface(const std::vector<LayerState>& layers) : m_Layers(layers) {}
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer < m_Layers.size() && m_Layers[layer].moving ? kBroadMoving : kBroadStatic;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == kBroadMoving ? "MOVING" : "STATIC";
    }
#endif
private:
    const std::vector<LayerState>& m_Layers;
};

class PairFilter final : public JPH::ObjectLayerPairFilter {
public:
    explicit PairFilter(const std::vector<LayerState>& layers) : m_Layers(layers) {}
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a >= m_Layers.size() || b >= m_Layers.size()) return false;
        const LayerState& lhs = m_Layers[a]; const LayerState& rhs = m_Layers[b];
        return lhs.valid && rhs.valid && (lhs.mask & rhs.layer) != 0 && (rhs.mask & lhs.layer) != 0;
    }
private:
    const std::vector<LayerState>& m_Layers;
};

class ObjectBroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    explicit ObjectBroadFilter(const std::vector<LayerState>& layers) : m_Layers(layers) {}
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
        if (layer >= m_Layers.size() || !m_Layers[layer].valid) return false;
        return m_Layers[layer].moving || broad == kBroadMoving;
    }
private:
    const std::vector<LayerState>& m_Layers;
};

class RayLayerFilter final : public JPH::ObjectLayerFilter {
public:
    RayLayerFilter(const std::vector<LayerState>& layers, uint32_t mask) : m_Layers(layers), m_Mask(mask) {}
    bool ShouldCollide(JPH::ObjectLayer layer) const override {
        return layer < m_Layers.size() && m_Layers[layer].valid && (m_Mask & m_Layers[layer].layer) != 0;
    }
private:
    const std::vector<LayerState>& m_Layers;
    uint32_t m_Mask;
};

struct ContactRecord {
    uint64_t a = 0, b = 0;
    Vec3 point = Vec3::Zero();
    Vec3 normal = Vec3::Up();
    float depth = 0.0f;
    bool trigger = false;
    bool removed = false;
};
}

class PhysicsWorld::Impl final : private JoltScope, public JPH::ContactListener {
public:
    Impl()
        : layers(kMaxBodies + 1), broad(layers), pair(layers), objectBroad(layers),
          tempAllocator(64 * 1024 * 1024),
          jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
               std::max(1u, std::thread::hardware_concurrency()) - 1)
    {
        system.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContacts, broad, objectBroad, pair);
        system.SetContactListener(this);
    }

    ~Impl() override
    {
        system.SetContactListener(nullptr);
        for (auto& [id, info] : bodies) Destroy(info);
        bodies.clear();
    }

    struct BodyInfo {
        JPH::BodyID body;
        JPH::ObjectLayer slot = 0;
        uint64_t signature = 0;
        bool character = false;
        JPH::Ref<JPH::Character> characterObject;
        Vec3 lastEnginePosition = Vec3::Zero();
        Quat lastEngineRotation;
        bool hasLastPose = false;
        float characterVerticalVelocity = 0.0f;
    };

    std::vector<LayerState> layers;
    BroadPhaseInterface broad;
    PairFilter pair;
    ObjectBroadFilter objectBroad;
    JPH::TempAllocatorImpl tempAllocator;
    JPH::JobSystemThreadPool jobs;
    JPH::PhysicsSystem system;
    std::unordered_map<uint64_t, BodyInfo> bodies;
    std::unordered_map<uint32_t, uint64_t> bodyActors;
    JPH::ObjectLayer nextSlot = 1;
    std::vector<JPH::ObjectLayer> freeSlots;
    std::mutex contactMutex;
    std::vector<ContactRecord> contacts;
    std::map<std::pair<uint64_t, uint64_t>, ContactRecord> activePairs;

    JPH::ObjectLayer AllocateLayer(uint32_t layer, uint32_t mask, bool moving)
    {
        JPH::ObjectLayer result = JPH::cObjectLayerInvalid;
        if (!freeSlots.empty()) { result = freeSlots.back(); freeSlots.pop_back(); }
        else if (nextSlot < layers.size()) result = nextSlot++;
        if (result == JPH::cObjectLayerInvalid) return result;
        layers[result] = {layer, mask, moving, true};
        return result;
    }

    void Destroy(BodyInfo& info)
    {
        bodyActors.erase(info.body.GetIndexAndSequenceNumber());
        if (info.characterObject) {
            info.characterObject->RemoveFromPhysicsSystem();
            info.characterObject = nullptr;
        } else if (!info.body.IsInvalid()) {
            auto& api = system.GetBodyInterface();
            api.RemoveBody(info.body);
            api.DestroyBody(info.body);
        }
        if (info.slot < layers.size() && layers[info.slot].valid) {
            layers[info.slot].valid = false;
            freeSlots.push_back(info.slot);
        }
    }

    static ColliderComponent* FindCollider(Actor& actor)
    {
        if (auto* value = actor.GetComponent<BoxColliderComponent>(); value && value->IsEnabled()) return value;
        if (auto* value = actor.GetComponent<SphereColliderComponent>(); value && value->IsEnabled()) return value;
        if (auto* value = actor.GetComponent<CapsuleColliderComponent>(); value && value->IsEnabled()) return value;
        return nullptr;
    }

    static JPH::RefConst<JPH::Shape> CreateShape(Actor& actor, ColliderComponent& collider)
    {
        const Vec3 scale = WorldScale(actor);
        if (auto* box = dynamic_cast<BoxColliderComponent*>(&collider)) {
            const Vec3 half = box->GetHalfExtents();
            return new JPH::BoxShape(JPH::Vec3(std::max(0.001f, half.x * scale.x),
                                               std::max(0.001f, half.y * scale.y),
                                               std::max(0.001f, half.z * scale.z)));
        }
        if (auto* sphere = dynamic_cast<SphereColliderComponent*>(&collider)) {
            return new JPH::SphereShape(std::max(0.001f, sphere->GetRadius() *
                std::max({scale.x, scale.y, scale.z})));
        }
        auto* capsule = dynamic_cast<CapsuleColliderComponent*>(&collider);
        const float radius = std::max(0.001f, capsule->GetRadius() * std::max(scale.x, scale.z));
        return new JPH::CapsuleShape(std::max(0.0f, capsule->GetHalfHeight() * scale.y), radius);
    }

    static uint64_t Signature(Actor& actor, ColliderComponent& collider, RigidBodyComponent* body, bool character)
    {
        uint64_t hash = 1469598103934665603ull;
        hash = HashUInt(hash, collider.GetLayer());
        hash = HashUInt(hash, collider.GetLayerMask());
        hash = HashFloat(hash, collider.IsTrigger() ? 1.0f : 0.0f);
        const Vec3 scale = WorldScale(actor);
        hash = HashFloat(HashFloat(HashFloat(hash, scale.x), scale.y), scale.z);
        if (auto* value = dynamic_cast<BoxColliderComponent*>(&collider)) {
            hash ^= 1; const Vec3 v = value->GetHalfExtents();
            hash = HashFloat(HashFloat(HashFloat(hash, v.x), v.y), v.z);
        } else if (auto* value = dynamic_cast<SphereColliderComponent*>(&collider)) {
            hash ^= 2; hash = HashFloat(hash, value->GetRadius());
        } else if (auto* value = dynamic_cast<CapsuleColliderComponent*>(&collider)) {
            hash ^= 3; hash = HashFloat(HashFloat(hash, value->GetRadius()), value->GetHalfHeight());
        }
        if (body) {
            hash = HashFloat(hash, static_cast<float>(body->GetBodyType()));
            hash = HashFloat(hash, body->GetMass());
            hash = HashFloat(hash, body->GetFriction());
            hash = HashFloat(hash, body->GetRestitution());
            hash = HashFloat(hash, body->GetLinearDamping());
            hash = HashFloat(hash, body->GetAngularDamping());
            hash = HashFloat(hash, body->UsesGravity() ? 1.0f : 0.0f);
            hash = HashFloat(hash, static_cast<float>(body->GetCollisionDetectionMode()));
            const Vec3 l = body->GetLinearAxisLocks(), a = body->GetAngularAxisLocks();
            hash = HashFloat(HashFloat(HashFloat(hash, l.x), l.y), l.z);
            hash = HashFloat(HashFloat(HashFloat(hash, a.x), a.y), a.z);
        }
        return character ? hash ^ 0x9e3779b97f4a7c15ull : hash;
    }

    JPH::EAllowedDOFs AllowedDOFs(const RigidBodyComponent& body)
    {
        using D = JPH::EAllowedDOFs;
        D result = D::None;
        const Vec3 linear = body.GetLinearAxisLocks(), angular = body.GetAngularAxisLocks();
        if (linear.x < 0.5f) result |= D::TranslationX;
        if (linear.y < 0.5f) result |= D::TranslationY;
        if (linear.z < 0.5f) result |= D::TranslationZ;
        if (angular.x < 0.5f) result |= D::RotationX;
        if (angular.y < 0.5f) result |= D::RotationY;
        if (angular.z < 0.5f) result |= D::RotationZ;
        return result;
    }

    bool CreateRegular(Actor& actor, ColliderComponent& collider, RigidBodyComponent* body, uint64_t signature)
    {
        const bool moving = body && body->GetBodyType() != BodyType::Static;
        const JPH::ObjectLayer slot = AllocateLayer(collider.GetLayer(), collider.GetLayerMask(), moving);
        if (slot == JPH::cObjectLayerInvalid) return false;
        const JPH::EMotionType motion = !body || body->GetBodyType() == BodyType::Static ? JPH::EMotionType::Static :
            (body->GetBodyType() == BodyType::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic);
        JPH::BodyCreationSettings settings(CreateShape(actor, collider), ToJoltPosition(actor.GetWorldPosition()),
                                           ToJolt(WorldRotation(actor)), motion, slot);
        settings.mUserData = actor.GetID();
        settings.mIsSensor = collider.IsTrigger();
        if (body) {
            settings.mFriction = body->GetFriction(); settings.mRestitution = body->GetRestitution();
            settings.mLinearDamping = body->GetLinearDamping(); settings.mAngularDamping = body->GetAngularDamping();
            settings.mGravityFactor = body->UsesGravity() ? 1.0f : 0.0f;
            settings.mMotionQuality = body->GetCollisionDetectionMode() == CollisionDetectionMode::Continuous
                ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
            settings.mAllowedDOFs = AllowedDOFs(*body);
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = body->GetMass();
            settings.mLinearVelocity = ToJolt(body->GetVelocity());
            settings.mAngularVelocity = ToJolt(body->GetAngularVelocity());
        }
        const JPH::BodyID id = system.GetBodyInterface().CreateAndAddBody(settings,
            moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (id.IsInvalid()) { layers[slot].valid = false; return false; }
        BodyInfo info{id, slot, signature, false, nullptr};
        info.lastEnginePosition = actor.GetWorldPosition();
        info.lastEngineRotation = WorldRotation(actor);
        info.hasLastPose = true;
        bodies[actor.GetID()] = std::move(info);
        bodyActors[id.GetIndexAndSequenceNumber()] = actor.GetID();
        if (body) body->m_SettingsDirty = false;
        return true;
    }

    bool CreateCharacter(Actor& actor, CapsuleColliderComponent& capsule,
                         CharacterControllerComponent& controller, uint64_t signature)
    {
        const JPH::ObjectLayer slot = AllocateLayer(capsule.GetLayer(), capsule.GetLayerMask(), true);
        if (slot == JPH::cObjectLayerInvalid) return false;
        const Vec3 scale = WorldScale(actor);
        JPH::CharacterSettings settings;
        settings.mLayer = slot;
        settings.mFriction = 0.0f;
        settings.mShape = new JPH::CapsuleShape(std::max(0.0f, capsule.GetHalfHeight() * scale.y),
            std::max(0.001f, capsule.GetRadius() * std::max(scale.x, scale.z)));
        settings.mMaxSlopeAngle = controller.GetMaxSlopeAngle() * kDeg2Rad;
        settings.mGravityFactor = controller.UsesGravity() ? 1.0f : 0.0f;
        JPH::Ref<JPH::Character> character = new JPH::Character(&settings,
            ToJoltPosition(actor.GetWorldPosition()), ToJolt(WorldRotation(actor)), actor.GetID(), &system);
        character->AddToPhysicsSystem(JPH::EActivation::Activate);
        const JPH::BodyID id = character->GetBodyID();
        BodyInfo info{id, slot, signature, true, character};
        info.lastEnginePosition = actor.GetWorldPosition();
        info.lastEngineRotation = WorldRotation(actor);
        info.hasLastPose = true;
        bodies[actor.GetID()] = std::move(info);
        bodyActors[id.GetIndexAndSequenceNumber()] = actor.GetID();
        return true;
    }

    void ApplyCommands(Actor& actor, BodyInfo& info, RigidBodyComponent& body, float dt)
    {
        auto& api = system.GetBodyInterface();
        const Vec3 actorPosition = actor.GetWorldPosition();
        const Quat actorRotation = WorldRotation(actor);
        if (info.hasLastPose && !SamePose(actorPosition, actorRotation,
                                          info.lastEnginePosition, info.lastEngineRotation) &&
            !body.m_TeleportPending && !body.m_KinematicTargetPending) {
            api.SetPositionAndRotation(info.body, ToJoltPosition(actorPosition), ToJolt(actorRotation),
                                       JPH::EActivation::Activate);
        }
        if (body.m_LinearVelocityDirty) { api.SetLinearVelocity(info.body, ToJolt(body.m_Velocity)); body.m_LinearVelocityDirty = false; }
        if (body.m_AngularVelocityDirty) { api.SetAngularVelocity(info.body, ToJolt(body.m_AngularVelocity)); body.m_AngularVelocityDirty = false; }
        if (body.m_AccumulatedForce.LengthSq() > 0.0f) api.AddForce(info.body, ToJolt(body.m_AccumulatedForce));
        if (body.m_AccumulatedTorque.LengthSq() > 0.0f) api.AddTorque(info.body, ToJolt(body.m_AccumulatedTorque));
        if (body.m_AccumulatedImpulse.LengthSq() > 0.0f) api.AddImpulse(info.body, ToJolt(body.m_AccumulatedImpulse));
        if (body.m_AccumulatedAngularImpulse.LengthSq() > 0.0f)
            api.AddAngularImpulse(info.body, ToJolt(body.m_AccumulatedAngularImpulse));
        body.m_AccumulatedForce = body.m_AccumulatedTorque = body.m_AccumulatedImpulse =
            body.m_AccumulatedAngularImpulse = Vec3::Zero();
        if (body.m_TeleportPending) {
            api.SetPositionAndRotation(info.body, ToJoltPosition(body.m_TeleportPosition),
                ToJolt(Quat::FromEulerRad(body.m_TeleportRotation.x * kDeg2Rad,
                    body.m_TeleportRotation.y * kDeg2Rad, body.m_TeleportRotation.z * kDeg2Rad)), JPH::EActivation::Activate);
            body.m_TeleportPending = false;
        }
        if (body.IsKinematic() && body.m_KinematicTargetPending) {
            api.MoveKinematic(info.body, ToJoltPosition(body.m_KinematicTargetPosition),
                ToJolt(Quat::FromEulerRad(body.m_KinematicTargetRotation.x * kDeg2Rad,
                    body.m_KinematicTargetRotation.y * kDeg2Rad, body.m_KinematicTargetRotation.z * kDeg2Rad)), dt);
            body.m_KinematicTargetPending = false;
        }
        if (body.m_WakeRequested) { api.ActivateBody(info.body); body.m_WakeRequested = false; }
    }

    void Reconcile(Scene& scene, float dt)
    {
        std::set<uint64_t> desired;
        scene.ForEach([&](Actor& actor) {
            if (!actor.IsActive()) return;
            ColliderComponent* collider = FindCollider(actor);
            if (!collider) return;
            auto* body = actor.GetComponent<RigidBodyComponent>();
            auto* controller = actor.GetComponent<CharacterControllerComponent>();
            RigidBodyComponent* activeBody = body && body->IsEnabled() ? body : nullptr;
            const bool isCharacter = controller && controller->IsEnabled() &&
                dynamic_cast<CapsuleColliderComponent*>(collider) && !activeBody;
            if (activeBody && controller && controller->IsEnabled()) {
                Logger::Warn("[Physics] Actor '", actor.GetName(), "' cannot use RigidBody and CharacterController together");
            }
            if ((activeBody || isCharacter) && HasNonUniformParentScale(actor)) {
                Logger::Error("[Physics] Actor '", actor.GetName(), "' has a moving physics body under non-uniform parent scale");
                return;
            }
            desired.insert(actor.GetID());
            const uint64_t signature = Signature(actor, *collider, activeBody, isCharacter);
            auto found = bodies.find(actor.GetID());
            if (found != bodies.end() && found->second.signature != signature) {
                Destroy(found->second); bodies.erase(found); found = bodies.end();
            }
            if (found == bodies.end()) {
                if (isCharacter) CreateCharacter(actor, *static_cast<CapsuleColliderComponent*>(collider), *controller, signature);
                else CreateRegular(actor, *collider, activeBody, signature);
                found = bodies.find(actor.GetID());
            }
            if (found == bodies.end()) return;
            if (isCharacter) {
                Vec3 velocity = controller->GetVelocity();
                if (!controller->IsGrounded()) {
                    const Vec3 current = controller->GetActualVelocity();
                    const float control = controller->GetAirControl();
                    velocity.x = current.x + (velocity.x - current.x) * control;
                    velocity.z = current.z + (velocity.z - current.z) * control;
                }
                float jumpSpeed = 0.0f;
                if (controller->ConsumeJump(jumpSpeed))
                    found->second.characterVerticalVelocity = jumpSpeed;
                if (controller->UsesGravity()) {
                    if (controller->IsGrounded() && found->second.characterVerticalVelocity < 0.0f)
                        found->second.characterVerticalVelocity = 0.0f;
                    found->second.characterVerticalVelocity += FromJolt(system.GetGravity()).y * dt;
                    velocity.y = found->second.characterVerticalVelocity;
                } else {
                    found->second.characterVerticalVelocity = velocity.y;
                }
                controller->SetActualVelocity(velocity);
                found->second.characterObject->SetLinearVelocity(ToJolt(velocity));
            } else if (activeBody) {
                ApplyCommands(actor, found->second, *activeBody, dt);
            } else {
                BodyInfo& info = found->second;
                const Vec3 position = actor.GetWorldPosition();
                const Quat rotation = WorldRotation(actor);
                if (!info.hasLastPose || !SamePose(position, rotation, info.lastEnginePosition, info.lastEngineRotation)) {
                    system.GetBodyInterface().SetPositionAndRotation(info.body, ToJoltPosition(position), ToJolt(rotation),
                        JPH::EActivation::DontActivate);
                    info.lastEnginePosition = position;
                    info.lastEngineRotation = rotation;
                    info.hasLastPose = true;
                }
            }
        });
        for (auto it = bodies.begin(); it != bodies.end();) {
            if (desired.count(it->first)) { ++it; continue; }
            Destroy(it->second); it = bodies.erase(it);
        }
    }

    void PullTransforms(Scene& scene)
    {
        auto& api = system.GetBodyInterface();
        for (auto& [actorID, info] : bodies) {
            Actor* actor = scene.FindByID(actorID); if (!actor) continue;
            if (info.character) {
                JPH::RVec3 p; JPH::Quat q; info.characterObject->GetPositionAndRotation(p, q);
                SetActorWorldPose(*actor, FromJolt(p), FromJolt(q));
                info.lastEnginePosition = actor->GetWorldPosition();
                info.lastEngineRotation = WorldRotation(*actor);
                info.hasLastPose = true;
                info.characterObject->PostSimulation(0.1f);
                if (auto* controller = actor->GetComponent<CharacterControllerComponent>()) {
                    controller->SetGrounded(info.characterObject->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround);
                }
                continue;
            }
            auto* body = actor->GetComponent<RigidBodyComponent>();
            if (!body) continue;
            const bool sleeping = !api.IsActive(info.body);
            body->SetVelocityFromPhysics(sleeping ? Vec3::Zero() : FromJolt(api.GetLinearVelocity(info.body)));
            body->SetAngularVelocityFromPhysics(sleeping ? Vec3::Zero() : FromJolt(api.GetAngularVelocity(info.body)));
            body->SetSleepingFromPhysics(sleeping);
            if (body->GetBodyType() != BodyType::Static) {
                JPH::RVec3 p; JPH::Quat q; api.GetPositionAndRotation(info.body, p, q);
                SetActorWorldPose(*actor, FromJolt(p), FromJolt(q));
            }
            info.lastEnginePosition = actor->GetWorldPosition();
            info.lastEngineRotation = WorldRotation(*actor);
            info.hasLastPose = true;
        }
    }

    void Clear()
    {
        for (auto& [id, info] : bodies) Destroy(info);
        bodies.clear();
        bodyActors.clear();
        { std::lock_guard<std::mutex> lock(contactMutex); contacts.clear(); }
        activePairs.clear();
    }

    void Dispatch(Scene& scene)
    {
        std::vector<ContactRecord> queued;
        { std::lock_guard<std::mutex> lock(contactMutex); queued.swap(contacts); }
        std::map<std::pair<uint64_t, uint64_t>, ContactRecord> current = activePairs;
        std::set<std::pair<uint64_t, uint64_t>> touched;
        for (const ContactRecord& record : queued) {
            const auto key = std::minmax(record.a, record.b);
            if (record.removed) current.erase(key);
            else { current[key] = record; touched.insert(key); }
        }
        for (const auto& [key, old] : activePairs) if (!current.count(key)) Emit(scene, old, CollisionEventPhase::Exit);
        for (const auto& key : touched) Emit(scene, current[key], activePairs.count(key) ? CollisionEventPhase::Stay : CollisionEventPhase::Enter);
        activePairs = std::move(current);
    }

    static void Emit(Scene& scene, const ContactRecord& record, CollisionEventPhase phase)
    {
        Actor* a = scene.FindByID(record.a); Actor* b = scene.FindByID(record.b);
        if (!a || !b) return;
        auto send = [&](Actor& target, Actor& other, bool reverse) {
            CollisionEvent event; event.otherHandle = other.GetHandle(); event.other = &other;
            event.point = record.point; event.normal = reverse ? record.normal * -1.0f : record.normal;
            event.depth = record.depth; event.trigger = record.trigger; event.phase = phase;
            target.ForEachComponent([&](Component& component) {
                if (component.IsEnabled()) component.OnCollisionEvent(event);
            });
        };
        send(*a, *b, false); send(*b, *a, true);
    }

    void QueueContact(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                      const JPH::ContactSettings& settings)
    {
        ContactRecord record; record.a = a.GetUserData(); record.b = b.GetUserData();
        record.normal = FromJolt(manifold.mWorldSpaceNormal); record.depth = manifold.mPenetrationDepth;
        record.trigger = settings.mIsSensor || a.IsSensor() || b.IsSensor();
        if (!manifold.mRelativeContactPointsOn1.empty()) record.point = FromJolt(manifold.GetWorldSpaceContactPointOn1(0));
        std::lock_guard<std::mutex> lock(contactMutex); contacts.push_back(record);
    }

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                        JPH::ContactSettings& s) override { QueueContact(a, b, m, s); }
    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& m,
                            JPH::ContactSettings& s) override { QueueContact(a, b, m, s); }
    void OnContactRemoved(const JPH::SubShapeIDPair& pairValue) override
    {
        const auto a = bodyActors.find(pairValue.GetBody1ID().GetIndexAndSequenceNumber());
        const auto b = bodyActors.find(pairValue.GetBody2ID().GetIndexAndSequenceNumber());
        if (a == bodyActors.end() || b == bodyActors.end()) return;
        ContactRecord record; record.a = a->second; record.b = b->second; record.removed = true;
        std::lock_guard<std::mutex> lock(contactMutex); contacts.push_back(record);
    }
};

PhysicsWorld::PhysicsWorld() : m_Impl(std::make_unique<Impl>()) { SetGravity(m_Gravity); }
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::SetGravity(const Vec3& gravity)
{
    m_Gravity = gravity;
    if (m_Impl) m_Impl->system.SetGravity(ToJolt(gravity));
}

void PhysicsWorld::Clear()
{
    if (m_Impl) m_Impl->Clear();
    m_Accumulator = 0.0f;
}

void PhysicsWorld::Step(Scene& scene, float deltaSeconds)
{
    if (deltaSeconds <= 0.0f) return;
    m_Accumulator += std::min(deltaSeconds, 0.25f);
    while (m_Accumulator >= m_FixedDelta) {
        scene.ForEach([&](Actor& actor) { actor.FixedUpdate(m_FixedDelta); });
        m_Impl->Reconcile(scene, m_FixedDelta);
        const JPH::EPhysicsUpdateError error = m_Impl->system.Update(m_FixedDelta, 1, &m_Impl->tempAllocator, &m_Impl->jobs);
        if (error != JPH::EPhysicsUpdateError::None) Logger::Error("[Physics] Jolt update error: ", static_cast<uint32_t>(error));
        m_Impl->PullTransforms(scene);
        m_Impl->Dispatch(scene);
        m_Accumulator -= m_FixedDelta;
    }
}

bool PhysicsWorld::Raycast(const Scene& scene, const Ray& ray, float maxDistance,
                           uint32_t layerMask, RaycastHit& hit) const
{
    if (maxDistance <= 0.0f || ray.direction.LengthSq() < 1e-8f) return false;
    // Queries are valid in edit mode before the first simulation tick too.
    m_Impl->Reconcile(const_cast<Scene&>(scene), m_FixedDelta);
    const Vec3 direction = ray.direction.Normalized();
    JPH::RRayCast cast(ToJoltPosition(ray.origin), ToJolt(direction) * maxDistance);
    JPH::RayCastResult result;
    RayLayerFilter filter(m_Impl->layers, layerMask);
    if (!m_Impl->system.GetNarrowPhaseQuery().CastRay(cast, result, {}, filter)) return false;
    const uint64_t actorID = m_Impl->system.GetBodyInterface().GetUserData(result.mBodyID);
    hit.actor = const_cast<Scene&>(scene).FindByID(actorID);
    if (!hit.actor) return false;
    hit.distance = result.mFraction * maxDistance;
    hit.point = ray.origin + direction * hit.distance;
    const JPH::BodyLockInterface& lockInterface = m_Impl->system.GetBodyLockInterface();
    JPH::BodyLockRead lock(lockInterface, result.mBodyID);
    hit.normal = lock.Succeeded() ? FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2,
        ToJoltPosition(hit.point))) : Vec3::Up();
    return true;
}

bool PhysicsWorld::OverlapSphere(const Scene& scene, const Vec3& center, float radius,
                                 uint32_t layerMask, std::vector<ActorHandle>& outActors) const
{
    outActors.clear();
    if (radius <= 0.0f) return false;
    m_Impl->Reconcile(const_cast<Scene&>(scene), m_FixedDelta);

    const SphereShape query{center, radius};
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        ColliderComponent* collider = Impl::FindCollider(actor);
        if (!collider || (collider->GetLayer() & layerMask) == 0) return;

        ContactManifold contact;
        bool overlapping = false;
        if (auto* sphere = dynamic_cast<SphereColliderComponent*>(collider)) {
            overlapping = Collide(query, sphere->GetWorldShape(), contact);
        } else if (auto* box = dynamic_cast<BoxColliderComponent*>(collider)) {
            overlapping = Collide(query, box->GetWorldShape(), contact);
        } else if (auto* capsule = dynamic_cast<CapsuleColliderComponent*>(collider)) {
            overlapping = Collide(capsule->GetWorldShape(), query, contact);
        }
        if (overlapping) outActors.push_back(actor.GetHandle());
    });
    return !outActors.empty();
}
