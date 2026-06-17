#include "Renderer/PostProcessComponent.h"

#include <algorithm>

void PostProcessComponent::SetExposure(float exposure)
{
    m_Exposure = (std::max)(0.0f, exposure);
}

void PostProcessComponent::SetGamma(float gamma)
{
    m_Gamma = (std::max)(0.1f, gamma);
}

void PostProcessComponent::SetVignette(float vignette)
{
    m_Vignette = std::clamp(vignette, 0.0f, 1.0f);
}

void PostProcessComponent::SetSaturation(float saturation)
{
    m_Saturation = (std::max)(0.0f, saturation);
}

void PostProcessComponent::SetContrast(float contrast)
{
    m_Contrast = (std::max)(0.0f, contrast);
}

void PostProcessComponent::SetAntiAliasingStrength(float strength)
{
    m_AntiAliasingStrength = std::clamp(strength, 0.0f, 1.0f);
}

void PostProcessComponent::SetBloomThreshold(float threshold)
{
    m_BloomThreshold = (std::max)(0.0f, threshold);
}

void PostProcessComponent::SetBloomIntensity(float intensity)
{
    m_BloomIntensity = std::clamp(intensity, 0.0f, 8.0f);
}

void PostProcessComponent::SetSSAORadius(float radius)
{
    m_SSAORadius = std::clamp(radius, 0.01f, 10.0f);
}

void PostProcessComponent::SetSSAOBias(float bias)
{
    m_SSAOBias = std::clamp(bias, 0.0f, 0.5f);
}

void PostProcessComponent::SetSSAOPower(float power)
{
    m_SSAOPower = std::clamp(power, 0.1f, 8.0f);
}

void PostProcessComponent::SetSSAOIntensity(float intensity)
{
    m_SSAOIntensity = std::clamp(intensity, 0.0f, 4.0f);
}

void PostProcessComponent::Serialize(nlohmann::json& data) const
{
    data["toneMapping"] = m_ToneMapping;
    data["exposure"] = m_Exposure;
    data["gamma"] = m_Gamma;
    data["vignette"] = m_Vignette;
    data["saturation"] = m_Saturation;
    data["contrast"] = m_Contrast;
    data["antiAliasingStrength"] = m_AntiAliasingStrength;
    data["bloomEnabled"] = m_BloomEnabled;
    data["bloomThreshold"] = m_BloomThreshold;
    data["bloomIntensity"] = m_BloomIntensity;
    data["ssaoRadius"] = m_SSAORadius;
    data["ssaoBias"] = m_SSAOBias;
    data["ssaoPower"] = m_SSAOPower;
    data["ssaoIntensity"] = m_SSAOIntensity;
}

void PostProcessComponent::Deserialize(const nlohmann::json& data)
{
    SetToneMappingEnabled(data.value("toneMapping", true));
    SetExposure(data.value("exposure", 1.0f));
    SetGamma(data.value("gamma", 2.2f));
    SetVignette(data.value("vignette", 0.0f));
    SetSaturation(data.value("saturation", 1.0f));
    SetContrast(data.value("contrast", 1.0f));
    SetAntiAliasingStrength(data.value("antiAliasingStrength", 0.0f));
    SetBloomEnabled(data.value("bloomEnabled", false));
    SetBloomThreshold(data.value("bloomThreshold", 1.0f));
    SetBloomIntensity(data.value("bloomIntensity", 0.0f));
    SetSSAORadius(data.value("ssaoRadius", 1.2f));
    SetSSAOBias(data.value("ssaoBias", 0.025f));
    SetSSAOPower(data.value("ssaoPower", 1.5f));
    SetSSAOIntensity(data.value("ssaoIntensity", 0.0f));
}
