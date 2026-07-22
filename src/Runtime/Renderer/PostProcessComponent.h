#pragma once

#include "Scene/Component.h"

#include <cstdint>

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
    uint32_t GetSSAOSampleCount() const { return m_SSAOSampleCount; }
    void SetSSAOSampleCount(uint32_t sampleCount);

    float GetSSAOBias() const { return m_SSAOBias; }
    void SetSSAOBias(float bias);

    float GetSSAOPower() const { return m_SSAOPower; }
    void SetSSAOPower(float power);

    float GetSSAOIntensity() const { return m_SSAOIntensity; }
    void SetSSAOIntensity(float intensity);

    float GetSSAOScale() const { return m_SSAOScale; }
    void SetSSAOScale(float scale);
    bool IsSSAOHalfResolution() const { return m_SSAOScale <= 0.75f; }
    void SetSSAOHalfResolution(bool enabled) { m_SSAOScale = enabled ? 0.5f : 1.0f; }
    bool UsesRayTracedAOReplacement() const { return m_RayTracedAOReplacement; }
    void SetRayTracedAOReplacement(bool enabled) { m_RayTracedAOReplacement = enabled; }

    bool IsSSGIEnabled() const { return m_SSGIEnabled; }
    void SetSSGIEnabled(bool enabled) { m_SSGIEnabled = enabled; }
    bool IsSSGIHalfResolution() const { return m_SSGIHalfResolution; }
    void SetSSGIHalfResolution(bool enabled) { m_SSGIHalfResolution = enabled; }
    float GetSSGIIntensity() const { return m_SSGIIntensity; }
    void SetSSGIIntensity(float intensity);
    float GetSSGIMaxDistance() const { return m_SSGIMaxDistance; }
    void SetSSGIMaxDistance(float distance);
    float GetSSGIHistoryWeight() const { return m_SSGIHistoryWeight; }
    void SetSSGIHistoryWeight(float weight);
    uint32_t GetSSGIStepCount() const { return m_SSGIStepCount; }
    void SetSSGIStepCount(uint32_t stepCount);
    uint32_t GetSSGIFilterRounds() const { return m_SSGIFilterRounds; }
    void SetSSGIFilterRounds(uint32_t rounds);
    bool UsesRayTracedDiffuseReplacement() const { return m_RayTracedDiffuseReplacement; }
    void SetRayTracedDiffuseReplacement(bool enabled) { m_RayTracedDiffuseReplacement = enabled; }

    bool IsSSREnabled() const { return m_SSREnabled; }
    void SetSSREnabled(bool enabled) { m_SSREnabled = enabled; }
    bool IsSSRHalfResolution() const { return m_SSRHalfResolution; }
    void SetSSRHalfResolution(bool enabled) { m_SSRHalfResolution = enabled; }
    float GetSSRMaxDistance() const { return m_SSRMaxDistance; }
    void SetSSRMaxDistance(float distance);
    float GetSSRMaxRoughness() const { return m_SSRMaxRoughness; }
    void SetSSRMaxRoughness(float roughness);
    float GetSSRHistoryWeight() const { return m_SSRHistoryWeight; }
    void SetSSRHistoryWeight(float weight);
    uint32_t GetSSRStepCount() const { return m_SSRStepCount; }
    void SetSSRStepCount(uint32_t stepCount);
    uint32_t GetSSRFilterRounds() const { return m_SSRFilterRounds; }
    void SetSSRFilterRounds(uint32_t rounds);
    bool UsesRayTracedReflectionReplacement() const { return m_RayTracedReflectionReplacement; }
    void SetRayTracedReflectionReplacement(bool enabled) { m_RayTracedReflectionReplacement = enabled; }
    float GetRTReflectionIntensityClamp() const { return m_RTReflectionIntensityClamp; }
    void SetRTReflectionIntensityClamp(float intensity);
    float GetRTReflectionAtrousRadiusScale() const { return m_RTReflectionAtrousRadiusScale; }
    void SetRTReflectionAtrousRadiusScale(float scale);

    bool UsesRayTracedShadowReplacement() const { return m_RayTracedShadowReplacement; }
    void SetRayTracedShadowReplacement(bool enabled) { m_RayTracedShadowReplacement = enabled; }

    bool IsTAAEnabled() const { return m_TAAEnabled; }
    void SetTAAEnabled(bool enabled) { m_TAAEnabled = enabled; }
    float GetTAAHistoryWeight() const { return m_TAAHistoryWeight; }
    void SetTAAHistoryWeight(float weight);
    float GetTAAJitterSpread() const { return m_TAAJitterSpread; }
    void SetTAAJitterSpread(float spread);
    float GetTAAHistoryClipExpansion() const { return m_TAAHistoryClipExpansion; }
    void SetTAAHistoryClipExpansion(float expansion);

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
    uint32_t m_SSAOSampleCount = 16;
    float m_SSAOBias = 0.025f;
    float m_SSAOPower = 1.5f;
    float m_SSAOIntensity = 0.0f;
    float m_SSAOScale = 1.0f;
    bool m_RayTracedAOReplacement = false;
    bool m_SSGIEnabled = true;
    bool m_SSGIHalfResolution = true;
    float m_SSGIIntensity = 1.0f;
    float m_SSGIMaxDistance = 10.0f;
    float m_SSGIHistoryWeight = 0.9f;
    uint32_t m_SSGIStepCount = 32;
    uint32_t m_SSGIFilterRounds = 3;
    bool m_RayTracedDiffuseReplacement = false;
    bool m_SSREnabled = true;
    bool m_SSRHalfResolution = true;
    float m_SSRMaxDistance = 10.0f;
    float m_SSRMaxRoughness = 0.8f;
    float m_SSRHistoryWeight = 0.9f;
    uint32_t m_SSRStepCount = 48;
    uint32_t m_SSRFilterRounds = 2;
    bool m_RayTracedReflectionReplacement = false;
    float m_RTReflectionIntensityClamp = 10.0f;
    float m_RTReflectionAtrousRadiusScale = 2.0f;
    bool m_RayTracedShadowReplacement = false;
    bool m_TAAEnabled = true;
    float m_TAAHistoryWeight = 0.8f;
    float m_TAAJitterSpread = 1.0f;
    float m_TAAHistoryClipExpansion = 0.0f;
};
