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

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    friend class PhysicsWorld;
    Vec3 Integrate(float deltaSeconds, const Vec3& gravity);
    void SetGrounded(bool grounded) { m_Grounded = grounded; }
    void ClipVelocity(const Vec3& normal);

    Vec3 m_DesiredVelocity = Vec3::Zero();
    float m_VerticalVelocity = 0.0f;
    float m_StepOffset = 0.3f;
    bool m_UseGravity = true;
    bool m_Grounded = false;
};
