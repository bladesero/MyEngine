#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Assets/Asset.h"
#include "Core/EngineMath.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct BakedReflectionProbe {
    std::string probeId;
    Vec3 worldPosition = Vec3::Zero();
    Vec3 boxExtents{1.0f};
    float rgbmRange = 4.0f;
    uint32_t arrayLayer = 0;
};

struct BakedSHProbeVolume {
    std::string probeId;
    Vec3 worldPosition = Vec3::Zero();
    Vec3 boxExtents{1.0f};
    uint32_t gridWidth = 0;
    uint32_t gridHeight = 0;
    uint32_t gridDepth = 0;
    uint32_t coefficientOffset = 0;
};

using SHCoefficient = std::array<float, 4>;

class MYENGINE_RUNTIME_API LightingProbeAsset final : public Asset {
public:
    static constexpr uint32_t CurrentVersion = 1;
    static constexpr uint32_t SHCoefficientCount = 9;

    explicit LightingProbeAsset(const std::string& path) : Asset(AssetType::LightingProbes, path) {}

    const std::string& GetSceneGuid() const { return m_SceneGuid; }
    void SetSceneGuid(std::string value) { m_SceneGuid = std::move(value); }
    uint64_t GetDependencyHash() const { return m_DependencyHash; }
    void SetDependencyHash(uint64_t value) { m_DependencyHash = value; }
    uint32_t GetReflectionResolution() const { return m_ReflectionResolution; }
    void SetReflectionResolution(uint32_t value);
    uint32_t GetReflectionMipCount() const { return m_ReflectionMipCount; }
    void SetReflectionMipCount(uint32_t value) { m_ReflectionMipCount = value; }

    std::vector<BakedReflectionProbe>& ReflectionProbes() { return m_ReflectionProbes; }
    const std::vector<BakedReflectionProbe>& GetReflectionProbes() const { return m_ReflectionProbes; }
    std::vector<BakedSHProbeVolume>& SHVolumes() { return m_SHVolumes; }
    const std::vector<BakedSHProbeVolume>& GetSHVolumes() const { return m_SHVolumes; }
    std::vector<uint8_t>& ReflectionPixels() { return m_ReflectionPixels; }
    const std::vector<uint8_t>& GetReflectionPixels() const { return m_ReflectionPixels; }
    std::vector<SHCoefficient>& SHCoefficients() { return m_SHCoefficients; }
    const std::vector<SHCoefficient>& GetSHCoefficients() const { return m_SHCoefficients; }

    size_t GetBytesPerReflectionProbe() const;
    bool Validate(std::string* error = nullptr) const;
    void MarkReady() { SetState(AssetState::Ready); }

private:
    std::string m_SceneGuid;
    uint64_t m_DependencyHash = 0;
    uint32_t m_ReflectionResolution = 256;
    uint32_t m_ReflectionMipCount = 9;
    std::vector<BakedReflectionProbe> m_ReflectionProbes;
    std::vector<BakedSHProbeVolume> m_SHVolumes;
    std::vector<uint8_t> m_ReflectionPixels;
    std::vector<SHCoefficient> m_SHCoefficients;

    friend MYENGINE_RUNTIME_API std::shared_ptr<LightingProbeAsset>
    LoadLightingProbeAssetFromFile(const std::string& path);
};

using LightingProbeHandle = AssetHandle<LightingProbeAsset>;

MYENGINE_RUNTIME_API std::shared_ptr<LightingProbeAsset> LoadLightingProbeAssetFromFile(const std::string& path);
MYENGINE_RUNTIME_API bool SaveLightingProbeAssetToFile(const LightingProbeAsset& asset, const std::string& path,
                                                       std::string* error = nullptr);

MYENGINE_RUNTIME_API uint16_t LightingProbeFloatToHalf(float value);
MYENGINE_RUNTIME_API float LightingProbeHalfToFloat(uint16_t value);
