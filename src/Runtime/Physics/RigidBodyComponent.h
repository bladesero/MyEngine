#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"

enum class BodyType {
    Static,
    Dynamic,
};

class RigidBodyComponent final : public Component {
public:
    const char* GetTypeName() const override { return "RigidBody"; }

    BodyType GetBodyType() const { return m_Type; }
    void SetBodyType(BodyType type) { m_Type = type; }
    bool IsDynamic() const { return m_Type == BodyType::Dynamic; }

    float GetMass() const { return m_Mass; }
    void SetMass(float mass) { m_Mass = mass > 0.0001f ? mass : 0.0001f; }
    float GetInverseMass() const { return IsDynamic() ? 1.0f / m_Mass : 0.0f; }

    const Vec3& GetVelocity() const { return m_Velocity; }
    void SetVelocity(const Vec3& velocity) {
        m_Velocity = velocity;
        if (velocity.LengthSq() > 1e-6f) WakeUp();
    }
    void SetVelocityFromPhysics(const Vec3& velocity) { m_Velocity = velocity; }
    void AddForce(const Vec3& force) { m_AccumulatedForce += force; WakeUp(); }
    Vec3 ConsumeForce() {
        const Vec3 force = m_AccumulatedForce;
        m_AccumulatedForce = Vec3::Zero();
        return force;
    }

    float GetRestitution() const { return m_Restitution; }
    void SetRestitution(float value);
    float GetLinearDamping() const { return m_LinearDamping; }
    void SetLinearDamping(float value);
    bool UsesGravity() const { return m_UseGravity; }
    void SetUseGravity(bool enabled) { m_UseGravity = enabled; }
    float GetFriction() const { return m_Friction; }
    void SetFriction(float value);
    bool IsSleeping() const { return m_Sleeping; }
    void WakeUp() { m_Sleeping = false; m_SleepTimer = 0.0f; }
    void UpdateSleep(float deltaSeconds);

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    BodyType m_Type = BodyType::Dynamic;
    float m_Mass = 1.0f;
    float m_Restitution = 0.1f;
    float m_LinearDamping = 0.05f;
    float m_Friction = 0.6f;
    bool m_UseGravity = true;
    bool m_Sleeping = false;
    float m_SleepTimer = 0.0f;
    Vec3 m_Velocity = Vec3::Zero();
    Vec3 m_AccumulatedForce = Vec3::Zero();
};
