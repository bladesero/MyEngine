#include "Renderer/LightComponent.h"

#include "Scene/Actor.h"

#include <algorithm>
#include <string>

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

const char* LightTypeToString(LightType type) {
    switch (type) {
    case LightType::Directional:
        return "directional";
    case LightType::Point:
        return "point";
    case LightType::Spot:
        return "spot";
    default:
        return "directional";
    }
}

LightType LightTypeFromString(const std::string& value) {
    if (value == "point")
        return LightType::Point;
    if (value == "spot")
        return LightType::Spot;
    return LightType::Directional;
}

Mat4 RotationMatrix(const Transform& transform) {
    return Mat4::RotationY(transform.rotation.y * kDeg2Rad) * Mat4::RotationX(transform.rotation.x * kDeg2Rad) *
           Mat4::RotationZ(transform.rotation.z * kDeg2Rad);
}

Mat4 WorldRotationMatrix(const Actor& actor) {
    const Mat4 localRotation = RotationMatrix(actor.GetTransform());
    const Actor* parent = actor.GetParent();
    return parent ? localRotation * WorldRotationMatrix(*parent) : localRotation;
}

} // namespace

void LightComponent::SetIntensity(float intensity) {
    m_Intensity = (std::max)(0.0f, intensity);
}

void LightComponent::SetRange(float range) {
    m_Range = (std::max)(0.01f, range);
}

void LightComponent::SetInnerConeAngle(float angleDegrees) {
    m_InnerConeAngle = std::clamp(angleDegrees, 0.0f, 89.0f);
    if (m_OuterConeAngle < m_InnerConeAngle) {
        m_OuterConeAngle = m_InnerConeAngle;
    }
}

void LightComponent::SetOuterConeAngle(float angleDegrees) {
    m_OuterConeAngle = std::clamp(angleDegrees, 0.0f, 89.0f);
    if (m_InnerConeAngle > m_OuterConeAngle) {
        m_InnerConeAngle = m_OuterConeAngle;
    }
}

Vec3 LightComponent::GetDirection() const {
    const Actor* owner = GetOwner();
    if (!owner)
        return Vec3::Forward();

    // Direction is derived only from actor rotations; position and scale must not skew lighting orientation.
    return WorldRotationMatrix(*owner).TransformDir(Vec3::Forward()).Normalized();
}

void LightComponent::SetShadowIntensity(float intensity) {
    m_ShadowIntensity = std::clamp(intensity, 0.0f, 1.0f);
}

void LightComponent::Serialize(nlohmann::json& data) const {
    data["type"] = LightTypeToString(m_Type);
    data["color"] = VecToJson(m_Color);
    data["intensity"] = m_Intensity;
    data["range"] = m_Range;
    data["innerConeAngle"] = m_InnerConeAngle;
    data["outerConeAngle"] = m_OuterConeAngle;
    data["castShadows"] = m_CastShadows;
    data["shadowIntensity"] = m_ShadowIntensity;
}

void LightComponent::Deserialize(const nlohmann::json& data) {
    SetLightType(LightTypeFromString(data.value("type", std::string("directional"))));
    SetColor(VecFromJson(data.value("color", nlohmann::json::array()), Vec3::One()));
    SetIntensity(data.value("intensity", 3.0f));
    SetRange(data.value("range", 8.0f));
    SetInnerConeAngle(data.value("innerConeAngle", 25.0f));
    SetOuterConeAngle(data.value("outerConeAngle", 35.0f));
    SetCastShadows(data.value("castShadows", true));
    SetShadowIntensity(data.value("shadowIntensity", 1.0f));
}
