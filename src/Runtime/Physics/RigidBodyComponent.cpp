#include "Physics/RigidBodyComponent.h"

#include <algorithm>

namespace {

nlohmann::json VecToJson(const Vec3& value)
{
    return nlohmann::json::array({ value.x, value.y, value.z });
}

Vec3 VecFromJson(const nlohmann::json& value, const Vec3& fallback)
{
    if (!value.is_array() || value.size() != 3) return fallback;
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
}

} // namespace

void RigidBodyComponent::SetRestitution(float value)
{
    m_Restitution = std::clamp(value, 0.0f, 1.0f);
}

void RigidBodyComponent::SetLinearDamping(float value)
{
    m_LinearDamping = std::max(0.0f, value);
}

void RigidBodyComponent::SetFriction(float value)
{
    m_Friction = std::clamp(value, 0.0f, 2.0f);
}

void RigidBodyComponent::UpdateSleep(float deltaSeconds)
{
    if (!IsDynamic()) return;
    if (m_Velocity.LengthSq() < 0.0004f && m_AccumulatedForce.LengthSq() < 1e-8f) {
        m_SleepTimer += deltaSeconds;
        if (m_SleepTimer >= 0.5f) {
            m_Sleeping = true;
            m_Velocity = Vec3::Zero();
        }
    } else {
        m_SleepTimer = 0.0f;
        m_Sleeping = false;
    }
}

void RigidBodyComponent::Serialize(nlohmann::json& data) const
{
    data["bodyType"] = IsDynamic() ? "dynamic" : "static";
    data["mass"] = m_Mass;
    data["velocity"] = VecToJson(m_Velocity);
    data["restitution"] = m_Restitution;
    data["linearDamping"] = m_LinearDamping;
    data["friction"] = m_Friction;
    data["useGravity"] = m_UseGravity;
}

void RigidBodyComponent::Deserialize(const nlohmann::json& data)
{
    SetBodyType(data.value("bodyType", std::string("dynamic")) == "static"
        ? BodyType::Static : BodyType::Dynamic);
    SetMass(data.value("mass", 1.0f));
    if (data.contains("velocity")) {
        SetVelocity(VecFromJson(data["velocity"], Vec3::Zero()));
    }
    SetRestitution(data.value("restitution", 0.1f));
    SetLinearDamping(data.value("linearDamping", 0.05f));
    SetFriction(data.value("friction", 0.6f));
    SetUseGravity(data.value("useGravity", true));
}
