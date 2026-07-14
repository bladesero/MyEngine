#include "Camera/ThirdPersonCameraComponent.h"

#include "Physics/PhysicsWorld.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace {
nlohmann::json VecToJson(const Vec3& value) {
    return nlohmann::json::array({value.x, value.y, value.z});
}
Vec3 JsonToVec(const nlohmann::json& value, const Vec3& fallback) {
    return value.is_array() && value.size() == 3
               ? Vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>())
               : fallback;
}
} // namespace

void ThirdPersonCameraComponent::AddOrbit(float yawDegrees, float pitchDegrees) {
    m_Yaw += yawDegrees * m_Sensitivity;
    m_Pitch = std::clamp(m_Pitch + pitchDegrees * m_Sensitivity, -80.0f, 80.0f);
}

void ThirdPersonCameraComponent::SetTarget(ActorHandle target) {
    m_Target = target;
    m_TargetActorID = 0;
    if (Actor* owner = GetOwner()) {
        if (Scene* scene = owner->GetScene()) {
            if (Actor* actor = scene->TryGetActor(target))
                m_TargetActorID = actor->GetID();
        }
    }
}

void ThirdPersonCameraComponent::SetDistance(float value) {
    m_Distance = std::max(0.1f, value);
}
void ThirdPersonCameraComponent::SetSensitivity(float value) {
    m_Sensitivity = std::max(0.0f, value);
}
void ThirdPersonCameraComponent::SetCollisionRadius(float value) {
    m_CollisionRadius = std::max(0.0f, value);
}
void ThirdPersonCameraComponent::SetFollowSharpness(float value) {
    m_FollowSharpness = std::max(0.0f, value);
}

void ThirdPersonCameraComponent::OnLateUpdate(float deltaSeconds) {
    Actor* owner = GetOwner();
    Scene* scene = owner ? owner->GetScene() : nullptr;
    if (scene && !scene->TryGetActor(m_Target) && m_TargetActorID != 0)
        m_Target = scene->GetHandle(m_TargetActorID);
    Actor* target = scene ? scene->TryGetActor(m_Target) : nullptr;
    if (!owner || !target)
        return;
    const Vec3 focus = target->GetWorldPosition() + m_TargetOffset;
    const float yaw = m_Yaw * kDeg2Rad;
    const float pitch = m_Pitch * kDeg2Rad;
    const Vec3 forward(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    float distance = m_Distance;
    RaycastHit hit;
    if (scene->GetPhysicsWorld().Raycast(*scene, Ray{focus, -forward}, m_Distance, 0xffffffffu, hit) &&
        hit.actor != target)
        distance = std::max(0.1f, hit.distance - m_CollisionRadius);
    const Vec3 desired = focus - forward * distance;
    const float alpha =
        m_FollowSharpness <= 0.0f ? 1.0f : 1.0f - std::exp(-m_FollowSharpness * std::max(0.0f, deltaSeconds));
    Transform& transform = owner->GetTransform();
    transform.position = Vec3::Lerp(transform.position, desired, alpha);
    const Vec3 look = focus - transform.position;
    const float horizontal = std::sqrt(look.x * look.x + look.z * look.z);
    transform.rotation.x = -std::atan2(look.y, std::max(0.0001f, horizontal)) * kRad2Deg;
    transform.rotation.y = std::atan2(look.x, look.z) * kRad2Deg;
}

void ThirdPersonCameraComponent::Serialize(nlohmann::json& data) const {
    uint64_t targetID = m_TargetActorID;
    if (const Actor* owner = GetOwner()) {
        if (const Scene* scene = owner->GetScene()) {
            if (const Actor* target = scene->TryGetActor(m_Target))
                targetID = target->GetID();
        }
    }
    data["targetActorId"] = targetID;
    data["targetOffset"] = VecToJson(m_TargetOffset);
    data["distance"] = m_Distance;
    data["yaw"] = m_Yaw;
    data["pitch"] = m_Pitch;
    data["sensitivity"] = m_Sensitivity;
    data["collisionRadius"] = m_CollisionRadius;
    data["followSharpness"] = m_FollowSharpness;
}

void ThirdPersonCameraComponent::Deserialize(const nlohmann::json& data) {
    m_Target = {};
    m_TargetActorID = data.value("targetActorId", uint64_t(0));
    if (data.contains("targetOffset"))
        m_TargetOffset = JsonToVec(data["targetOffset"], m_TargetOffset);
    SetDistance(data.value("distance", 4.0f));
    m_Yaw = data.value("yaw", 180.0f);
    m_Pitch = std::clamp(data.value("pitch", 15.0f), -80.0f, 80.0f);
    SetSensitivity(data.value("sensitivity", 1.0f));
    SetCollisionRadius(data.value("collisionRadius", 0.2f));
    SetFollowSharpness(data.value("followSharpness", 12.0f));
}
