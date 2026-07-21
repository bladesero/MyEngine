#include "Renderer/SkylightComponent.h"

#include <algorithm>

namespace {

nlohmann::json VecToJson(const Vec3& value) {
    return nlohmann::json::array({value.x, value.y, value.z});
}

Vec3 VecFromJson(const nlohmann::json& value, const Vec3& fallback) {
    if (!value.is_array() || value.size() != 3)
        return fallback;
    return {
        value[0].get<float>(),
        value[1].get<float>(),
        value[2].get<float>(),
    };
}

Vec3 ClampPositive(const Vec3& value) {
    return {
        (std::max)(0.0f, value.x),
        (std::max)(0.0f, value.y),
        (std::max)(0.0f, value.z),
    };
}

} // namespace

void SkylightComponent::SetEnvironmentColor(const Vec3& color) {
    m_EnvironmentColor = ClampPositive(color);
}

void SkylightComponent::SetEnvironmentIntensity(float intensity) {
    m_EnvironmentIntensity = (std::max)(0.0f, intensity);
}

void SkylightComponent::SetSkyIntensity(float intensity) {
    m_SkyIntensity = (std::max)(0.0f, intensity);
}

void SkylightComponent::SetSkyTint(const Vec3& tint) {
    m_SkyTint = ClampPositive(tint);
}

void SkylightComponent::SetHorizonTint(const Vec3& tint) {
    m_HorizonTint = ClampPositive(tint);
}

void SkylightComponent::SetGroundTint(const Vec3& tint) {
    m_GroundTint = ClampPositive(tint);
}

void SkylightComponent::Serialize(nlohmann::json& data) const {
    data["environmentColor"] = VecToJson(m_EnvironmentColor);
    data["environmentIntensity"] = m_EnvironmentIntensity;
    data["skyIntensity"] = m_SkyIntensity;
    data["skyTint"] = VecToJson(m_SkyTint);
    data["horizonTint"] = VecToJson(m_HorizonTint);
    data["groundTint"] = VecToJson(m_GroundTint);
}

void SkylightComponent::Deserialize(const nlohmann::json& data) {
    SetEnvironmentColor(VecFromJson(data.value("environmentColor", nlohmann::json::array()), Vec3::One()));
    SetEnvironmentIntensity(data.value("environmentIntensity", 1.0f));
    SetSkyIntensity(data.value("skyIntensity", 1.0f));
    SetSkyTint(VecFromJson(data.value("skyTint", nlohmann::json::array()), Vec3::One()));
    SetHorizonTint(VecFromJson(data.value("horizonTint", nlohmann::json::array()), Vec3::One()));
    SetGroundTint(VecFromJson(data.value("groundTint", nlohmann::json::array()), Vec3::One()));
}
