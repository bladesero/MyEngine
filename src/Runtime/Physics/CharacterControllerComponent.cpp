#include "Physics/CharacterControllerComponent.h"

#include <algorithm>

void CharacterControllerComponent::SetStepOffset(float value)
{
    m_StepOffset = (std::max)(0.0f, value);
}

void CharacterControllerComponent::SetMaxSlopeAngle(float degrees)
{
    m_MaxSlopeAngle = std::clamp(degrees, 0.0f, 89.0f);
}

void CharacterControllerComponent::Serialize(nlohmann::json& data) const
{
    data["velocity"] = nlohmann::json::array({
        m_DesiredVelocity.x, m_DesiredVelocity.y, m_DesiredVelocity.z
    });
    data["useGravity"] = m_UseGravity;
    data["stepOffset"] = m_StepOffset;
    data["maxSlopeAngle"] = m_MaxSlopeAngle;
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
    SetMaxSlopeAngle(data.value("maxSlopeAngle", 50.0f));
}
