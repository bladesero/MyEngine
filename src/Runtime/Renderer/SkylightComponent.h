#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"

class SkylightComponent final : public Component {
public:
    const char* GetTypeName() const override { return "Skylight"; }

    const Vec3& GetEnvironmentColor() const { return m_EnvironmentColor; }
    void SetEnvironmentColor(const Vec3& color);

    float GetEnvironmentIntensity() const { return m_EnvironmentIntensity; }
    void SetEnvironmentIntensity(float intensity);

    float GetSkyIntensity() const { return m_SkyIntensity; }
    void SetSkyIntensity(float intensity);

    const Vec3& GetSkyTint() const { return m_SkyTint; }
    void SetSkyTint(const Vec3& tint);

    const Vec3& GetHorizonTint() const { return m_HorizonTint; }
    void SetHorizonTint(const Vec3& tint);

    const Vec3& GetGroundTint() const { return m_GroundTint; }
    void SetGroundTint(const Vec3& tint);

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    Vec3 m_EnvironmentColor = Vec3::One();
    float m_EnvironmentIntensity = 1.0f;
    float m_SkyIntensity = 1.0f;
    Vec3 m_SkyTint = Vec3::One();
    Vec3 m_HorizonTint = Vec3::One();
    Vec3 m_GroundTint = Vec3::One();
};
