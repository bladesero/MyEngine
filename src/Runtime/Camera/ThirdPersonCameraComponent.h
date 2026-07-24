#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Scene/ActorHandle.h"
#include "Scene/Component.h"

class MYENGINE_RUNTIME_API ThirdPersonCameraComponent final : public Component {
public:
    const char* GetTypeName() const override { return "ThirdPersonCamera"; }
    int GetExecutionOrder() const override { return 1000; }
    void OnLateUpdate(float deltaSeconds) override;

    void SetTarget(ActorHandle target);
    ActorHandle GetTarget() const { return m_Target; }
    void AddOrbit(float yawDegrees, float pitchDegrees);
    void SetDistance(float value);
    float GetDistance() const { return m_Distance; }
    void SetTargetOffset(const Vec3& value) { m_TargetOffset = value; }
    const Vec3& GetTargetOffset() const { return m_TargetOffset; }
    void SetSensitivity(float value);
    float GetSensitivity() const { return m_Sensitivity; }
    void SetCollisionRadius(float value);
    float GetCollisionRadius() const { return m_CollisionRadius; }
    void SetFollowSharpness(float value);
    float GetFollowSharpness() const { return m_FollowSharpness; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    ActorHandle m_Target;
    uint64_t m_TargetActorID = 0;
    Vec3 m_TargetOffset = {0.0f, 1.5f, 0.0f};
    float m_Distance = 4.0f;
    float m_Yaw = 180.0f;
    float m_Pitch = 15.0f;
    float m_Sensitivity = 1.0f;
    float m_CollisionRadius = 0.2f;
    float m_FollowSharpness = 12.0f;
};
