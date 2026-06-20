#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"

class CharacterControllerComponent final : public Component {
public:
    const char* GetTypeName() const override { return "CharacterController"; }

    void Move(const Vec3& velocity) { m_DesiredVelocity = velocity; }
    const Vec3& GetVelocity() const { return m_DesiredVelocity; }
    bool IsGrounded() const { return m_Grounded; }
    void SetUseGravity(bool enabled) { m_UseGravity = enabled; }
    bool UsesGravity() const { return m_UseGravity; }
    void SetStepOffset(float value);
    float GetStepOffset() const { return m_StepOffset; }
    void SetMaxSlopeAngle(float degrees);
    float GetMaxSlopeAngle() const { return m_MaxSlopeAngle; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    friend class PhysicsWorld;
    void SetGrounded(bool grounded) { m_Grounded = grounded; }

    Vec3 m_DesiredVelocity = Vec3::Zero();
    float m_StepOffset = 0.3f;
    float m_MaxSlopeAngle = 50.0f;
    bool m_UseGravity = true;
    bool m_Grounded = false;
};
