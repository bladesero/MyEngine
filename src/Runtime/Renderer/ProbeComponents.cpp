#include "Renderer/ProbeComponents.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace {
std::string GenerateProbeId() {
    static std::atomic<uint64_t> sequence{0};
    static std::mt19937_64 random(std::random_device{}());
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const uint64_t a = random() ^ now;
    const uint64_t b = random() ^ ++sequence;
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << a << std::setw(16) << b;
    return out.str();
}

nlohmann::json Vec3Json(const Vec3& value) {
    return nlohmann::json::array({value.x, value.y, value.z});
}

Vec3 ReadVec3(const nlohmann::json& value, const Vec3& fallback) {
    if (!value.is_array() || value.size() != 3)
        return fallback;
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

Vec3 PositiveExtents(const Vec3& value) {
    return {(std::max)(0.01f, value.x), (std::max)(0.01f, value.y), (std::max)(0.01f, value.z)};
}
} // namespace

ReflectionProbeComponent::ReflectionProbeComponent() : m_ProbeId(GenerateProbeId()) {}

void ReflectionProbeComponent::SetProbeId(std::string value) {
    m_ProbeId = value.empty() ? GenerateProbeId() : std::move(value);
}

void ReflectionProbeComponent::RegenerateProbeId() {
    m_ProbeId = GenerateProbeId();
}

void ReflectionProbeComponent::SetBoxExtents(const Vec3& value) {
    m_BoxExtents = PositiveExtents(value);
    m_BlendDistance = (std::min)(m_BlendDistance, (std::min)({m_BoxExtents.x, m_BoxExtents.y, m_BoxExtents.z}));
}

void ReflectionProbeComponent::SetBlendDistance(float value) {
    m_BlendDistance = std::clamp(value, 0.0f, (std::min)({m_BoxExtents.x, m_BoxExtents.y, m_BoxExtents.z}));
}

void ReflectionProbeComponent::SetIntensity(float value) {
    m_Intensity = (std::max)(0.0f, value);
}

void ReflectionProbeComponent::Serialize(nlohmann::json& data) const {
    data = {{"probeId", m_ProbeId},
            {"boxExtents", Vec3Json(m_BoxExtents)},
            {"captureOffset", Vec3Json(m_CaptureOffset)},
            {"blendDistance", m_BlendDistance},
            {"priority", m_Priority},
            {"intensity", m_Intensity},
            {"layerMask", m_LayerMask}};
}

void ReflectionProbeComponent::Deserialize(const nlohmann::json& data) {
    SetProbeId(data.value("probeId", std::string{}));
    SetBoxExtents(ReadVec3(data.value("boxExtents", nlohmann::json::array()), Vec3{5.0f}));
    SetCaptureOffset(ReadVec3(data.value("captureOffset", nlohmann::json::array()), Vec3::Zero()));
    SetBlendDistance(data.value("blendDistance", 1.0f));
    SetPriority(data.value("priority", 0));
    SetIntensity(data.value("intensity", 1.0f));
    SetLayerMask(data.value("layerMask", ~uint32_t{0}));
}

SHProbeVolumeComponent::SHProbeVolumeComponent() : m_ProbeId(GenerateProbeId()) {}

void SHProbeVolumeComponent::SetProbeId(std::string value) {
    m_ProbeId = value.empty() ? GenerateProbeId() : std::move(value);
}

void SHProbeVolumeComponent::RegenerateProbeId() {
    m_ProbeId = GenerateProbeId();
}

void SHProbeVolumeComponent::SetBoxExtents(const Vec3& value) {
    m_BoxExtents = PositiveExtents(value);
    m_BlendDistance = (std::min)(m_BlendDistance, (std::min)({m_BoxExtents.x, m_BoxExtents.y, m_BoxExtents.z}));
}

void SHProbeVolumeComponent::SetGridSpacing(float value) {
    m_GridSpacing = (std::max)(0.1f, value);
}

void SHProbeVolumeComponent::SetBlendDistance(float value) {
    m_BlendDistance = std::clamp(value, 0.0f, (std::min)({m_BoxExtents.x, m_BoxExtents.y, m_BoxExtents.z}));
}

void SHProbeVolumeComponent::SetIntensity(float value) {
    m_Intensity = (std::max)(0.0f, value);
}

void SHProbeVolumeComponent::Serialize(nlohmann::json& data) const {
    data = {{"probeId", m_ProbeId},
            {"boxExtents", Vec3Json(m_BoxExtents)},
            {"gridSpacing", m_GridSpacing},
            {"blendDistance", m_BlendDistance},
            {"priority", m_Priority},
            {"intensity", m_Intensity},
            {"layerMask", m_LayerMask}};
}

void SHProbeVolumeComponent::Deserialize(const nlohmann::json& data) {
    SetProbeId(data.value("probeId", std::string{}));
    SetBoxExtents(ReadVec3(data.value("boxExtents", nlohmann::json::array()), Vec3{5.0f}));
    SetGridSpacing(data.value("gridSpacing", 2.0f));
    SetBlendDistance(data.value("blendDistance", 1.0f));
    SetPriority(data.value("priority", 0));
    SetIntensity(data.value("intensity", 1.0f));
    SetLayerMask(data.value("layerMask", ~uint32_t{0}));
}
