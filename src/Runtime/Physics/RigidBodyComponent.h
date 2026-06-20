#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"

#include <cstdint>

enum class BodyType {
    Static,
    Dynamic,
    Kinematic,
};

enum class CollisionDetectionMode {
    Discrete,
    Continuous,
};

class RigidBodyComponent final : public Component {
public:
    const char* GetTypeName() const override { return "RigidBody"; }

    BodyType GetBodyType() const { return m_Type; }
    void SetBodyType(BodyType type) { m_Type = type; m_SettingsDirty = true; WakeUp(); }
    bool IsDynamic() const { return m_Type == BodyType::Dynamic; }
    bool IsKinematic() const { return m_Type == BodyType::Kinematic; }

    float GetMass() const { return m_Mass; }
    void SetMass(float mass);
    float GetInverseMass() const { return IsDynamic() ? 1.0f / m_Mass : 0.0f; }

    const Vec3& GetVelocity() const { return m_Velocity; }
    void SetVelocity(const Vec3& velocity) { m_Velocity = velocity; m_LinearVelocityDirty = true; WakeUp(); }
    const Vec3& GetAngularVelocity() const { return m_AngularVelocity; }
    void SetAngularVelocity(const Vec3& velocity) { m_AngularVelocity = velocity; m_AngularVelocityDirty = true; WakeUp(); }

    void AddForce(const Vec3& force) { m_AccumulatedForce += force; WakeUp(); }
    void AddTorque(const Vec3& torque) { m_AccumulatedTorque += torque; WakeUp(); }
    void AddImpulse(const Vec3& impulse) { m_AccumulatedImpulse += impulse; WakeUp(); }
    void AddAngularImpulse(const Vec3& impulse) { m_AccumulatedAngularImpulse += impulse; WakeUp(); }
    void Teleport(const Vec3& position, const Vec3& rotation = Vec3::Zero());
    void SetKinematicTarget(const Vec3& position, const Vec3& rotation);

    float GetRestitution() const { return m_Restitution; }
    void SetRestitution(float value);
    float GetLinearDamping() const { return m_LinearDamping; }
    void SetLinearDamping(float value);
    float GetAngularDamping() const { return m_AngularDamping; }
    void SetAngularDamping(float value);
    bool UsesGravity() const { return m_UseGravity; }
    void SetUseGravity(bool enabled) { m_UseGravity = enabled; m_SettingsDirty = true; }
    float GetFriction() const { return m_Friction; }
    void SetFriction(float value);

    const Vec3& GetLinearAxisLocks() const { return m_LinearAxisLocks; }
    void SetLinearAxisLocks(const Vec3& locks) { m_LinearAxisLocks = locks; m_SettingsDirty = true; }
    const Vec3& GetAngularAxisLocks() const { return m_AngularAxisLocks; }
    void SetAngularAxisLocks(const Vec3& locks) { m_AngularAxisLocks = locks; m_SettingsDirty = true; }
    CollisionDetectionMode GetCollisionDetectionMode() const { return m_CollisionMode; }
    void SetCollisionDetectionMode(CollisionDetectionMode mode) { m_CollisionMode = mode; m_SettingsDirty = true; }

    bool IsSleeping() const { return m_Sleeping; }
    void WakeUp() { m_Sleeping = false; m_WakeRequested = true; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    friend class PhysicsWorld;
    void SetVelocityFromPhysics(const Vec3& value) { m_Velocity = value; }
    void SetAngularVelocityFromPhysics(const Vec3& value) { m_AngularVelocity = value; }
    void SetSleepingFromPhysics(bool value) { m_Sleeping = value; m_WakeRequested = false; }

    BodyType m_Type = BodyType::Dynamic;
    CollisionDetectionMode m_CollisionMode = CollisionDetectionMode::Discrete;
    float m_Mass = 1.0f;
    float m_Restitution = 0.1f;
    float m_LinearDamping = 0.05f;
    float m_AngularDamping = 0.05f;
    float m_Friction = 0.6f;
    bool m_UseGravity = true;
    bool m_Sleeping = false;
    bool m_SettingsDirty = true;
    bool m_LinearVelocityDirty = false;
    bool m_AngularVelocityDirty = false;
    bool m_WakeRequested = false;
    bool m_TeleportPending = false;
    bool m_KinematicTargetPending = false;
    Vec3 m_Velocity = Vec3::Zero();
    Vec3 m_AngularVelocity = Vec3::Zero();
    Vec3 m_AccumulatedForce = Vec3::Zero();
    Vec3 m_AccumulatedTorque = Vec3::Zero();
    Vec3 m_AccumulatedImpulse = Vec3::Zero();
    Vec3 m_AccumulatedAngularImpulse = Vec3::Zero();
    Vec3 m_LinearAxisLocks = Vec3::Zero();
    Vec3 m_AngularAxisLocks = Vec3::Zero();
    Vec3 m_TeleportPosition = Vec3::Zero();
    Vec3 m_TeleportRotation = Vec3::Zero();
    Vec3 m_KinematicTargetPosition = Vec3::Zero();
    Vec3 m_KinematicTargetRotation = Vec3::Zero();
};
