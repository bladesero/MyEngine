#include "Physics/RigidBodyComponent.h"

#include <algorithm>
#include <string>

namespace {
void WriteVec3(nlohmann::json& data, const char* key, const Vec3& value)
{ data[key] = nlohmann::json::array({value.x, value.y, value.z}); }

Vec3 ReadVec3(const nlohmann::json& data, const char* key, const Vec3& fallback = Vec3::Zero())
{
    const auto it = data.find(key);
    if (it == data.end() || !it->is_array() || it->size() != 3) return fallback;
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}
}

void RigidBodyComponent::SetMass(float mass)
{ m_Mass = mass > 0.0001f ? mass : 0.0001f; m_SettingsDirty = true; }

void RigidBodyComponent::SetRestitution(float value)
{ m_Restitution = std::clamp(value, 0.0f, 1.0f); m_SettingsDirty = true; }

void RigidBodyComponent::SetLinearDamping(float value)
{ m_LinearDamping = std::max(0.0f, value); m_SettingsDirty = true; }

void RigidBodyComponent::SetAngularDamping(float value)
{ m_AngularDamping = std::max(0.0f, value); m_SettingsDirty = true; }

void RigidBodyComponent::SetFriction(float value)
{ m_Friction = std::clamp(value, 0.0f, 2.0f); m_SettingsDirty = true; }

void RigidBodyComponent::Teleport(const Vec3& position, const Vec3& rotation)
{
    m_TeleportPosition = position;
    m_TeleportRotation = rotation;
    m_TeleportPending = true;
    WakeUp();
}

void RigidBodyComponent::SetKinematicTarget(const Vec3& position, const Vec3& rotation)
{
    m_KinematicTargetPosition = position;
    m_KinematicTargetRotation = rotation;
    m_KinematicTargetPending = true;
    WakeUp();
}

void RigidBodyComponent::Serialize(nlohmann::json& data) const
{
    const char* type = m_Type == BodyType::Static ? "static" :
        (m_Type == BodyType::Kinematic ? "kinematic" : "dynamic");
    data["bodyType"] = type;
    data["mass"] = m_Mass;
    WriteVec3(data, "velocity", m_Velocity);
    WriteVec3(data, "angularVelocity", m_AngularVelocity);
    data["restitution"] = m_Restitution;
    data["linearDamping"] = m_LinearDamping;
    data["angularDamping"] = m_AngularDamping;
    data["friction"] = m_Friction;
    data["useGravity"] = m_UseGravity;
    WriteVec3(data, "linearAxisLocks", m_LinearAxisLocks);
    WriteVec3(data, "angularAxisLocks", m_AngularAxisLocks);
    data["collisionDetection"] = m_CollisionMode == CollisionDetectionMode::Continuous
        ? "continuous" : "discrete";
}

void RigidBodyComponent::Deserialize(const nlohmann::json& data)
{
    const std::string type = data.value("bodyType", std::string("dynamic"));
    SetBodyType(type == "static" ? BodyType::Static :
        (type == "kinematic" ? BodyType::Kinematic : BodyType::Dynamic));
    SetMass(data.value("mass", 1.0f));
    SetVelocity(ReadVec3(data, "velocity"));
    SetAngularVelocity(ReadVec3(data, "angularVelocity"));
    SetRestitution(data.value("restitution", 0.1f));
    SetLinearDamping(data.value("linearDamping", 0.05f));
    SetAngularDamping(data.value("angularDamping", 0.05f));
    SetFriction(data.value("friction", 0.6f));
    SetUseGravity(data.value("useGravity", true));
    SetLinearAxisLocks(ReadVec3(data, "linearAxisLocks"));
    SetAngularAxisLocks(ReadVec3(data, "angularAxisLocks"));
    SetCollisionDetectionMode(data.value("collisionDetection", std::string("discrete")) == "continuous"
        ? CollisionDetectionMode::Continuous : CollisionDetectionMode::Discrete);
}
