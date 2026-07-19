#include "Assets/LightingProbeAsset.h"

#include "Core/RuntimeFileSystem.h"
#include "Core/TransactionalFileWriter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace {
constexpr char kMagic[8] = {'M', 'E', 'L', 'P', 'R', 'B', '1', '\0'};
constexpr uint32_t kMaxStringLength = 4096;
constexpr uint32_t kMaxReflectionProbes = 32;
constexpr uint32_t kMaxSHVolumes = 8;
constexpr uint32_t kMaxSHSamples = 32768;

void SetError(std::string* error, const std::string& value) {
    if (error)
        *error = value;
}

template <typename T> void Append(std::vector<uint8_t>& bytes, const T& value) {
    const auto* begin = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(T));
}

void AppendString(std::vector<uint8_t>& bytes, const std::string& value) {
    const uint32_t size = static_cast<uint32_t>(value.size());
    Append(bytes, size);
    bytes.insert(bytes.end(), value.begin(), value.end());
}

template <typename T> bool Read(const std::vector<uint8_t>& bytes, size_t& cursor, T& value) {
    if (cursor > bytes.size() || sizeof(T) > bytes.size() - cursor)
        return false;
    std::memcpy(&value, bytes.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

bool ReadString(const std::vector<uint8_t>& bytes, size_t& cursor, std::string& value) {
    uint32_t size = 0;
    if (!Read(bytes, cursor, size) || size > kMaxStringLength || cursor > bytes.size() || size > bytes.size() - cursor)
        return false;
    value.assign(reinterpret_cast<const char*>(bytes.data() + cursor), size);
    cursor += size;
    return true;
}

void AppendVec3(std::vector<uint8_t>& bytes, const Vec3& value) {
    Append(bytes, value.x);
    Append(bytes, value.y);
    Append(bytes, value.z);
}

bool ReadVec3(const std::vector<uint8_t>& bytes, size_t& cursor, Vec3& value) {
    return Read(bytes, cursor, value.x) && Read(bytes, cursor, value.y) && Read(bytes, cursor, value.z);
}

uint32_t FullMipCount(uint32_t resolution) {
    uint32_t count = 0;
    do {
        ++count;
        resolution = (std::max)(1u, resolution / 2u);
    } while (resolution > 1u);
    return count;
}
} // namespace

uint16_t LightingProbeFloatToHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    int32_t exponent = static_cast<int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10)
            return static_cast<uint16_t>(sign);
        mantissa = (mantissa | 0x800000u) >> static_cast<uint32_t>(1 - exponent);
        return static_cast<uint16_t>(sign | ((mantissa + 0x1000u) >> 13u));
    }
    if (exponent >= 31)
        return static_cast<uint16_t>(sign | 0x7c00u);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10u) |
                                 ((mantissa + 0x1000u) >> 13u));
}

float LightingProbeHalfToFloat(uint16_t value) {
    const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16u;
    uint32_t exponent = (value >> 10u) & 0x1fu;
    uint32_t mantissa = value & 0x3ffu;
    uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1u;
                --exponent;
            }
            mantissa &= 0x3ffu;
            bits = sign | ((exponent + 127u - 15u) << 23u) | (mantissa << 13u);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13u);
    } else {
        bits = sign | ((exponent + 127u - 15u) << 23u) | (mantissa << 13u);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

void LightingProbeAsset::SetReflectionResolution(uint32_t value) {
    if (value != 64 && value != 128 && value != 256)
        value = 128;
    m_ReflectionResolution = value;
    m_ReflectionMipCount = FullMipCount(value);
}

size_t LightingProbeAsset::GetBytesPerReflectionProbe() const {
    size_t bytes = 0;
    uint32_t size = m_ReflectionResolution;
    for (uint32_t mip = 0; mip < m_ReflectionMipCount; ++mip) {
        bytes += static_cast<size_t>(size) * size * 4u;
        size = (std::max)(1u, size / 2u);
    }
    return bytes;
}

bool LightingProbeAsset::Validate(std::string* error) const {
    if (m_ReflectionResolution != 64 && m_ReflectionResolution != 128 && m_ReflectionResolution != 256) {
        SetError(error, "reflection resolution must be 64, 128, or 256");
        return false;
    }
    if (m_ReflectionMipCount == 0 || m_ReflectionMipCount > FullMipCount(m_ReflectionResolution)) {
        SetError(error, "invalid reflection mip count");
        return false;
    }
    if (m_ReflectionProbes.size() > kMaxReflectionProbes || m_SHVolumes.size() > kMaxSHVolumes) {
        SetError(error, "probe count exceeds the runtime budget");
        return false;
    }
    if (m_ReflectionPixels.size() != GetBytesPerReflectionProbe() * m_ReflectionProbes.size()) {
        SetError(error, "reflection pixel payload size does not match metadata");
        return false;
    }
    if (m_SHCoefficients.size() % SHCoefficientCount != 0 ||
        m_SHCoefficients.size() / SHCoefficientCount > kMaxSHSamples) {
        SetError(error, "SH coefficient payload exceeds the runtime budget");
        return false;
    }
    for (size_t i = 0; i < m_ReflectionProbes.size(); ++i) {
        const auto& probe = m_ReflectionProbes[i];
        if (probe.probeId.empty() || probe.arrayLayer != i || probe.rgbmRange < 4.0f || probe.rgbmRange > 64.0f) {
            SetError(error, "invalid reflection probe metadata");
            return false;
        }
    }
    for (const auto& volume : m_SHVolumes) {
        const uint64_t samples = static_cast<uint64_t>(volume.gridWidth) * volume.gridHeight * volume.gridDepth;
        if (volume.probeId.empty() || samples == 0 || samples > 8192 ||
            static_cast<uint64_t>(volume.coefficientOffset) + samples * SHCoefficientCount > m_SHCoefficients.size()) {
            SetError(error, "invalid SH volume metadata");
            return false;
        }
    }
    if (error)
        error->clear();
    return true;
}

bool SaveLightingProbeAssetToFile(const LightingProbeAsset& asset, const std::string& path, std::string* error) {
    if (!asset.Validate(error))
        return false;
    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), std::begin(kMagic), std::end(kMagic));
    Append(bytes, LightingProbeAsset::CurrentVersion);
    AppendString(bytes, asset.GetSceneGuid());
    Append(bytes, asset.GetDependencyHash());
    Append(bytes, asset.GetReflectionResolution());
    Append(bytes, asset.GetReflectionMipCount());
    const uint32_t reflectionCount = static_cast<uint32_t>(asset.GetReflectionProbes().size());
    const uint32_t volumeCount = static_cast<uint32_t>(asset.GetSHVolumes().size());
    const uint64_t pixelBytes = asset.GetReflectionPixels().size();
    const uint64_t coefficientCount = asset.GetSHCoefficients().size();
    Append(bytes, reflectionCount);
    Append(bytes, volumeCount);
    Append(bytes, pixelBytes);
    Append(bytes, coefficientCount);
    for (const auto& probe : asset.GetReflectionProbes()) {
        AppendString(bytes, probe.probeId);
        AppendVec3(bytes, probe.worldPosition);
        AppendVec3(bytes, probe.boxExtents);
        Append(bytes, probe.rgbmRange);
        Append(bytes, probe.arrayLayer);
    }
    for (const auto& volume : asset.GetSHVolumes()) {
        AppendString(bytes, volume.probeId);
        AppendVec3(bytes, volume.worldPosition);
        AppendVec3(bytes, volume.boxExtents);
        Append(bytes, volume.gridWidth);
        Append(bytes, volume.gridHeight);
        Append(bytes, volume.gridDepth);
        Append(bytes, volume.coefficientOffset);
    }
    bytes.insert(bytes.end(), asset.GetReflectionPixels().begin(), asset.GetReflectionPixels().end());
    for (const SHCoefficient& coefficient : asset.GetSHCoefficients())
        for (float channel : coefficient)
            Append(bytes, LightingProbeFloatToHalf(channel));

    TransactionalWriteOptions options;
    options.validator = [](const std::filesystem::path& candidate, std::string* validationError) {
        if (LoadLightingProbeAssetFromFile(candidate.string()))
            return true;
        SetError(validationError, "written lighting probe asset failed validation");
        return false;
    };
    return TransactionalFileWriter::WriteText(
        path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), options, error);
}

std::shared_ptr<LightingProbeAsset> LoadLightingProbeAssetFromFile(const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!RuntimeFileSystem::Get().ReadAllBytes(path, bytes) || bytes.size() < sizeof(kMagic) ||
        std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
        return {};
    size_t cursor = sizeof(kMagic);
    uint32_t version = 0;
    auto asset = std::make_shared<LightingProbeAsset>(path);
    uint32_t reflectionCount = 0, volumeCount = 0;
    uint64_t pixelBytes = 0, coefficientCount = 0;
    if (!Read(bytes, cursor, version) || version != LightingProbeAsset::CurrentVersion ||
        !ReadString(bytes, cursor, asset->m_SceneGuid) || !Read(bytes, cursor, asset->m_DependencyHash) ||
        !Read(bytes, cursor, asset->m_ReflectionResolution) || !Read(bytes, cursor, asset->m_ReflectionMipCount) ||
        !Read(bytes, cursor, reflectionCount) || !Read(bytes, cursor, volumeCount) ||
        !Read(bytes, cursor, pixelBytes) || !Read(bytes, cursor, coefficientCount) ||
        reflectionCount > kMaxReflectionProbes || volumeCount > kMaxSHVolumes ||
        coefficientCount > static_cast<uint64_t>(kMaxSHSamples) * LightingProbeAsset::SHCoefficientCount)
        return {};
    asset->m_ReflectionProbes.resize(reflectionCount);
    for (auto& probe : asset->m_ReflectionProbes)
        if (!ReadString(bytes, cursor, probe.probeId) || !ReadVec3(bytes, cursor, probe.worldPosition) ||
            !ReadVec3(bytes, cursor, probe.boxExtents) || !Read(bytes, cursor, probe.rgbmRange) ||
            !Read(bytes, cursor, probe.arrayLayer))
            return {};
    asset->m_SHVolumes.resize(volumeCount);
    for (auto& volume : asset->m_SHVolumes)
        if (!ReadString(bytes, cursor, volume.probeId) || !ReadVec3(bytes, cursor, volume.worldPosition) ||
            !ReadVec3(bytes, cursor, volume.boxExtents) || !Read(bytes, cursor, volume.gridWidth) ||
            !Read(bytes, cursor, volume.gridHeight) || !Read(bytes, cursor, volume.gridDepth) ||
            !Read(bytes, cursor, volume.coefficientOffset))
            return {};
    const uint64_t coefficientBytes = coefficientCount * 4u * sizeof(uint16_t);
    if (pixelBytes > bytes.size() - cursor || coefficientBytes > bytes.size() - cursor - pixelBytes)
        return {};
    asset->m_ReflectionPixels.assign(bytes.begin() + static_cast<ptrdiff_t>(cursor),
                                     bytes.begin() + static_cast<ptrdiff_t>(cursor + pixelBytes));
    cursor += static_cast<size_t>(pixelBytes);
    asset->m_SHCoefficients.resize(static_cast<size_t>(coefficientCount));
    for (auto& coefficient : asset->m_SHCoefficients)
        for (float& channel : coefficient) {
            uint16_t half = 0;
            if (!Read(bytes, cursor, half))
                return {};
            channel = LightingProbeHalfToFloat(half);
        }
    if (cursor != bytes.size() || !asset->Validate())
        return {};
    asset->SetState(AssetState::Ready);
    return asset;
}
