#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"

#include <cstdint>
#include <string>

class ReflectionProbeComponent final : public Component {
public:
    ReflectionProbeComponent();

    const char* GetTypeName() const override { return "ReflectionProbe"; }

    const std::string& GetProbeId() const { return m_ProbeId; }
    void SetProbeId(std::string value);
    void RegenerateProbeId();

    const Vec3& GetBoxExtents() const { return m_BoxExtents; }
    void SetBoxExtents(const Vec3& value);
    const Vec3& GetCaptureOffset() const { return m_CaptureOffset; }
    void SetCaptureOffset(const Vec3& value) { m_CaptureOffset = value; }
    float GetBlendDistance() const { return m_BlendDistance; }
    void SetBlendDistance(float value);
    int GetPriority() const { return m_Priority; }
    void SetPriority(int value) { m_Priority = value; }
    float GetIntensity() const { return m_Intensity; }
    void SetIntensity(float value);
    uint32_t GetLayerMask() const { return m_LayerMask; }
    void SetLayerMask(uint32_t value) { m_LayerMask = value; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    std::string m_ProbeId;
    Vec3 m_BoxExtents{5.0f};
    Vec3 m_CaptureOffset = Vec3::Zero();
    float m_BlendDistance = 1.0f;
    int m_Priority = 0;
    float m_Intensity = 1.0f;
    uint32_t m_LayerMask = ~uint32_t{0};
};

class SHProbeVolumeComponent final : public Component {
public:
    SHProbeVolumeComponent();

    const char* GetTypeName() const override { return "SHProbeVolume"; }

    const std::string& GetProbeId() const { return m_ProbeId; }
    void SetProbeId(std::string value);
    void RegenerateProbeId();

    const Vec3& GetBoxExtents() const { return m_BoxExtents; }
    void SetBoxExtents(const Vec3& value);
    float GetGridSpacing() const { return m_GridSpacing; }
    void SetGridSpacing(float value);
    float GetBlendDistance() const { return m_BlendDistance; }
    void SetBlendDistance(float value);
    int GetPriority() const { return m_Priority; }
    void SetPriority(int value) { m_Priority = value; }
    float GetIntensity() const { return m_Intensity; }
    void SetIntensity(float value);
    uint32_t GetLayerMask() const { return m_LayerMask; }
    void SetLayerMask(uint32_t value) { m_LayerMask = value; }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    std::string m_ProbeId;
    Vec3 m_BoxExtents{5.0f};
    float m_GridSpacing = 2.0f;
    float m_BlendDistance = 1.0f;
    int m_Priority = 0;
    float m_Intensity = 1.0f;
    uint32_t m_LayerMask = ~uint32_t{0};
};
