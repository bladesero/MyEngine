#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"

enum class LightType : uint8_t {
    Directional = 0,
    Point,
    Spot,
};

class LightComponent final : public Component {
public:
    const char* GetTypeName() const override { return "Light"; }

    LightType GetLightType() const { return m_Type; }
    void SetLightType(LightType type) { m_Type = type; }

    const Vec3& GetColor() const { return m_Color; }
    void SetColor(const Vec3& color) { m_Color = color; }

    float GetIntensity() const { return m_Intensity; }
    void SetIntensity(float intensity);

    float GetRange() const { return m_Range; }
    void SetRange(float range);

    float GetInnerConeAngle() const { return m_InnerConeAngle; }
    void SetInnerConeAngle(float angleDegrees);

    float GetOuterConeAngle() const { return m_OuterConeAngle; }
    void SetOuterConeAngle(float angleDegrees);

    Vec3 GetDirection() const;

    bool CastsShadows() const { return m_CastShadows; }
    void SetCastShadows(bool enabled) { m_CastShadows = enabled; }

    float GetShadowIntensity() const { return m_ShadowIntensity; }
    void SetShadowIntensity(float intensity);

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    LightType m_Type = LightType::Directional;
    Vec3 m_Color = Vec3::One();
    float m_Intensity = 3.0f;
    float m_Range = 8.0f;
    float m_InnerConeAngle = 25.0f;
    float m_OuterConeAngle = 35.0f;
    bool m_CastShadows = true;
    float m_ShadowIntensity = 1.0f;
};
