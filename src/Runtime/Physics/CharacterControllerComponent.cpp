#include "Physics/CharacterControllerComponent.h"

#include <algorithm>

void CharacterControllerComponent::SetStepOffset(float value)
{
    m_StepOffset = (std::max)(0.0f, value);
}

Vec3 CharacterControllerComponent::Integrate(
    float deltaSeconds, const Vec3& gravity)
{
    if (m_UseGravity) {
        if (m_Grounded && m_VerticalVelocity < 0.0f) m_VerticalVelocity = 0.0f;
        m_VerticalVelocity += gravity.y * deltaSeconds;
    } else {
        m_VerticalVelocity = m_DesiredVelocity.y;
    }
    m_Grounded = false;
    Vec3 velocity = m_DesiredVelocity;
    velocity.y = m_VerticalVelocity;
    return velocity * deltaSeconds;
}

void CharacterControllerComponent::ClipVelocity(const Vec3& normal)
{
    Vec3 velocity = m_DesiredVelocity;
    velocity.y = m_VerticalVelocity;
    const float intoSurface = velocity.Dot(normal);
    if (intoSurface < 0.0f) velocity -= normal * intoSurface;
    m_DesiredVelocity.x = velocity.x;
    m_DesiredVelocity.z = velocity.z;
    m_VerticalVelocity = velocity.y;
}

void CharacterControllerComponent::Serialize(nlohmann::json& data) const
{
    data["velocity"] = nlohmann::json::array({
        m_DesiredVelocity.x, m_DesiredVelocity.y, m_DesiredVelocity.z
    });
    data["useGravity"] = m_UseGravity;
    data["stepOffset"] = m_StepOffset;
}

void CharacterControllerComponent::Deserialize(const nlohmann::json& data)
{
    if (data.contains("velocity") && data["velocity"].is_array() &&
        data["velocity"].size() == 3) {
        m_DesiredVelocity = {
            data["velocity"][0].get<float>(),
            data["velocity"][1].get<float>(),
            data["velocity"][2].get<float>()
        };
    }
    SetUseGravity(data.value("useGravity", true));
    SetStepOffset(data.value("stepOffset", 0.3f));
}
