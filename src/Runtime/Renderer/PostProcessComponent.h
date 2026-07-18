#pragma once

#include "Scene/Component.h"

class PostProcessComponent final : public Component {
public:
    const char* GetTypeName() const override { return "PostProcess"; }

    bool IsToneMappingEnabled() const { return m_ToneMapping; }
    void SetToneMappingEnabled(bool enabled) { m_ToneMapping = enabled; }

    float GetExposure() const { return m_Exposure; }
    void SetExposure(float exposure);

    float GetGamma() const { return m_Gamma; }
    void SetGamma(float gamma);

    float GetVignette() const { return m_Vignette; }
    void SetVignette(float vignette);

    float GetSaturation() const { return m_Saturation; }
    void SetSaturation(float saturation);

    float GetContrast() const { return m_Contrast; }
    void SetContrast(float contrast);

    float GetAntiAliasingStrength() const { return m_AntiAliasingStrength; }
    void SetAntiAliasingStrength(float strength);

    bool IsBloomEnabled() const { return m_BloomEnabled; }
    void SetBloomEnabled(bool enabled) { m_BloomEnabled = enabled; }

    float GetBloomThreshold() const { return m_BloomThreshold; }
    void SetBloomThreshold(float threshold);

    float GetBloomIntensity() const { return m_BloomIntensity; }
    void SetBloomIntensity(float intensity);

    float GetSSAORadius() const { return m_SSAORadius; }
    void SetSSAORadius(float radius);

    float GetSSAOBias() const { return m_SSAOBias; }
    void SetSSAOBias(float bias);

    float GetSSAOPower() const { return m_SSAOPower; }
    void SetSSAOPower(float power);

    float GetSSAOIntensity() const { return m_SSAOIntensity; }
    void SetSSAOIntensity(float intensity);

    float GetSSAOScale() const { return m_SSAOScale; }
    void SetSSAOScale(float scale);

    bool IsSSGIEnabled() const { return m_SSGIEnabled; }
    void SetSSGIEnabled(bool enabled) { m_SSGIEnabled = enabled; }
    float GetSSGIIntensity() const { return m_SSGIIntensity; }
    void SetSSGIIntensity(float intensity);
    float GetSSGIMaxDistance() const { return m_SSGIMaxDistance; }
    void SetSSGIMaxDistance(float distance);

    bool IsSSREnabled() const { return m_SSREnabled; }
    void SetSSREnabled(bool enabled) { m_SSREnabled = enabled; }
    float GetSSRMaxRoughness() const { return m_SSRMaxRoughness; }
    void SetSSRMaxRoughness(float roughness);

    bool IsTAAEnabled() const { return m_TAAEnabled; }
    void SetTAAEnabled(bool enabled) { m_TAAEnabled = enabled; }
    float GetTAAHistoryWeight() const { return m_TAAHistoryWeight; }
    void SetTAAHistoryWeight(float weight);

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    bool m_ToneMapping = true;
    float m_Exposure = 1.0f;
    float m_Gamma = 2.2f;
    float m_Vignette = 0.0f;
    float m_Saturation = 1.0f;
    float m_Contrast = 1.0f;
    float m_AntiAliasingStrength = 0.0f;
    bool m_BloomEnabled = false;
    float m_BloomThreshold = 1.0f;
    float m_BloomIntensity = 0.0f;
    float m_SSAORadius = 1.2f;
    float m_SSAOBias = 0.025f;
    float m_SSAOPower = 1.5f;
    float m_SSAOIntensity = 0.0f;
    float m_SSAOScale = 1.0f;
    bool m_SSGIEnabled = true;
    float m_SSGIIntensity = 1.0f;
    float m_SSGIMaxDistance = 10.0f;
    bool m_SSREnabled = true;
    float m_SSRMaxRoughness = 0.8f;
    bool m_TAAEnabled = true;
    float m_TAAHistoryWeight = 0.9f;
};
