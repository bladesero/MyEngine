#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"

class CharacterControllerComponent final : public Component {
public:
    const char* GetTypeName() const override { return "CharacterController"; }

    void Move(const Vec3& velocity) { m_DesiredVelocity = velocity; }
    bool Jump(float speed = -1.0f) {
        if (!m_Grounded || m_JumpRequested) return false;
        m_JumpRequested = true;
        m_RequestedJumpSpeed = speed > 0.0f ? speed : m_JumpSpeed;
        return true;
    }
    const Vec3& GetVelocity() const { return m_DesiredVelocity; }
    bool IsGrounded() const { return m_Grounded; }
    void SetUseGravity(bool enabled) { m_UseGravity = enabled; }
    bool UsesGravity() const { return m_UseGravity; }
    void SetStepOffset(float value);
    float GetStepOffset() const { return m_StepOffset; }
    void SetMaxSlopeAngle(float degrees);
    float GetMaxSlopeAngle() const { return m_MaxSlopeAngle; }
    void SetJumpSpeed(float value);
    float GetJumpSpeed() const { return m_JumpSpeed; }
    void SetAirControl(float value);
    float GetAirControl() const { return m_AirControl; }
    const Vec3& GetActualVelocity() const { return m_ActualVelocity; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    friend class PhysicsWorld;
    void SetGrounded(bool grounded) { m_Grounded = grounded; }
    bool ConsumeJump(float& speed) {
        if (!m_JumpRequested) return false;
        speed = m_RequestedJumpSpeed;
        m_JumpRequested = false;
        return true;
    }
    void SetActualVelocity(const Vec3& velocity) { m_ActualVelocity = velocity; }

    Vec3 m_DesiredVelocity = Vec3::Zero();
    Vec3 m_ActualVelocity = Vec3::Zero();
    float m_StepOffset = 0.3f;
    float m_MaxSlopeAngle = 50.0f;
    float m_JumpSpeed = 5.5f;
    float m_AirControl = 0.35f;
    float m_RequestedJumpSpeed = 0.0f;
    bool m_UseGravity = true;
    bool m_Grounded = false;
    bool m_JumpRequested = false;
};
