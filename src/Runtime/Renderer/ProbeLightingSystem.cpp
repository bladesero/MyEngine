#include "Renderer/ProbeLightingSystem.h"

#include "Assets/AssetManager.h"
#include "Assets/LightingProbeAsset.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/ProbeComponents.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace {
struct alignas(16) GpuReflectionProbeData {
    Mat4 worldToLocal;
    Mat4 localToWorld;
    float extentsIntensity[4];
    float positionRange[4];
    uint32_t layerPriority[4];
    float blendInfo[4];
};

struct alignas(16) GpuSHVolumeData {
    Mat4 worldToLocal;
    float extentsIntensity[4];
    float blendPriority[4];
    uint32_t gridAndOffset[4];
};

float BoundaryWeight(const Vec3& local, const Vec3& extents, float blendDistance) {
    if (std::abs(local.x) > extents.x || std::abs(local.y) > extents.y || std::abs(local.z) > extents.z)
        return 0.0f;
    if (blendDistance <= 0.0001f)
        return 1.0f;
    const float edge =
        (std::min)({extents.x - std::abs(local.x), extents.y - std::abs(local.y), extents.z - std::abs(local.z)});
    return std::clamp(edge / blendDistance, 0.0f, 1.0f);
}

template <typename T>
bool UploadStructuredBuffer(IRHIDevice* device, const std::vector<T>& values, const char* name,
                            std::shared_ptr<GpuBuffer>& buffer, std::shared_ptr<GpuBufferView>& view) {
    const T zero{};
    const void* data = values.empty() ? static_cast<const void*>(&zero) : static_cast<const void*>(values.data());
    const uint32_t count = static_cast<uint32_t>((std::max)(size_t{1}, values.size()));
    RHIBufferDesc desc;
    desc.size = count * static_cast<uint32_t>(sizeof(T));
    desc.stride = static_cast<uint32_t>(sizeof(T));
    desc.usage = RHIResourceUsage::ShaderResource;
    desc.debugName = name;
    buffer = device->CreateBuffer(desc, data);
    if (!buffer)
        return false;
    RHIBufferViewDesc viewDesc;
    viewDesc.elementCount = count;
    viewDesc.usage = RHIResourceUsage::ShaderResource;
    view = device->CreateBufferView(buffer, viewDesc);
    return static_cast<bool>(view);
}
} // namespace

ProbeLightingSystem::ProbeLightingSystem(IRHIDevice* device) : m_Device(device) {
    UploadFallbackResources();
}

void ProbeLightingSystem::Reset() {
    m_AssetPath.clear();
    m_Asset.reset();
    m_ReflectionTexture.reset();
    m_ReflectionTextureView.reset();
    m_ReflectionMetadata.reset();
    m_ReflectionMetadataView.reset();
    m_SHVolumeMetadata.reset();
    m_SHVolumeMetadataView.reset();
    m_SHCoefficients.reset();
    m_SHCoefficientView.reset();
    m_CpuReflectionProbes.clear();
    m_ReflectionProbeCount = 0;
    m_SHVolumeCount = 0;
    m_LastError.clear();
}

bool ProbeLightingSystem::LoadAsset(const std::string& path) {
    m_AssetPath = path;
    m_Asset.reset();
    if (path.empty())
        return UploadFallbackResources();
    auto handle = AssetManager::Get().Load<LightingProbeAsset>(path);
    if (!handle.IsValid()) {
        m_LastError = "failed to load lighting probe asset '" + path + "'";
        m_ReflectionProbeCount = 0;
        m_SHVolumeCount = 0;
        m_CpuReflectionProbes.clear();
        UploadFallbackResources();
        return false;
    }
    m_Asset = handle.Shared();
    return UploadAsset();
}

bool ProbeLightingSystem::UploadFallbackResources() {
    if (!m_Device)
        return false;
    const std::array<uint16_t, 4> black = {0, 0, 0, LightingProbeFloatToHalf(1.0f)};
    RHITextureDesc textureDesc;
    textureDesc.format = RHIFormat::RGBA16Float;
    textureDesc.usage = RHIResourceUsage::ShaderResource;
    textureDesc.array = true;
    textureDesc.debugName = "LocalReflectionProbeFallback";
    const RHITextureSubresourceData subresource{black.data(), 8, 8, 0, 0};
    m_ReflectionTexture = m_Device->UploadTexture(textureDesc, &subresource, 1);
    if (!m_ReflectionTexture)
        return false;
    RHITextureViewDesc textureViewDesc;
    textureViewDesc.usage = RHIResourceUsage::ShaderResource;
    m_ReflectionTextureView = m_Device->CreateTextureView(m_ReflectionTexture, textureViewDesc);
    std::vector<GpuReflectionProbeData> reflections;
    std::vector<GpuSHVolumeData> volumes;
    std::vector<SHCoefficient> coefficients;
    return m_ReflectionTextureView &&
           UploadStructuredBuffer(m_Device, reflections, "LocalReflectionProbeMetadataFallback", m_ReflectionMetadata,
                                  m_ReflectionMetadataView) &&
           UploadStructuredBuffer(m_Device, volumes, "LocalSHVolumeMetadataFallback", m_SHVolumeMetadata,
                                  m_SHVolumeMetadataView) &&
           UploadStructuredBuffer(m_Device, coefficients, "LocalProbeSHCoefficientsFallback", m_SHCoefficients,
                                  m_SHCoefficientView);
}

bool ProbeLightingSystem::UploadAsset() {
    if (!m_Device || !m_Asset)
        return false;
    const uint32_t resolution = m_Asset->GetReflectionResolution();
    const auto& probes = m_Asset->GetReflectionProbes();
    if (!probes.empty()) {
        RHITextureDesc desc;
        desc.width = resolution;
        desc.height = resolution;
        desc.mipLevels = m_Asset->GetReflectionMipCount();
        desc.arrayLayers = static_cast<uint32_t>(probes.size());
        desc.format = RHIFormat::RGBA16Float;
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.array = true;
        desc.debugName = "LocalReflectionProbeAtlas";
        const auto& encodedPixels = m_Asset->GetReflectionPixels();
        std::vector<uint16_t> linearPixels(encodedPixels.size());
        std::vector<RHITextureSubresourceData> subresources;
        size_t offset = 0;
        for (uint32_t layer = 0; layer < desc.arrayLayers; ++layer) {
            uint32_t size = resolution;
            for (uint32_t mip = 0; mip < desc.mipLevels; ++mip) {
                const uint32_t channelCount = size * size * 4u;
                if (offset + channelCount > encodedPixels.size()) {
                    m_LastError = "reflection probe atlas payload is truncated";
                    return false;
                }
                for (uint32_t channel = 0; channel < channelCount; channel += 4u) {
                    const float multiplier = encodedPixels[offset + channel + 3u] / 255.0f;
                    const float decodeScale = multiplier * probes[layer].rgbmRange / 255.0f;
                    linearPixels[offset + channel] =
                        LightingProbeFloatToHalf(encodedPixels[offset + channel] * decodeScale);
                    linearPixels[offset + channel + 1u] =
                        LightingProbeFloatToHalf(encodedPixels[offset + channel + 1u] * decodeScale);
                    linearPixels[offset + channel + 2u] =
                        LightingProbeFloatToHalf(encodedPixels[offset + channel + 2u] * decodeScale);
                    linearPixels[offset + channel + 3u] = LightingProbeFloatToHalf(1.0f);
                }
                const uint32_t rowPitch = size * 4u * static_cast<uint32_t>(sizeof(uint16_t));
                const uint32_t slicePitch = rowPitch * size;
                subresources.push_back({linearPixels.data() + offset, rowPitch, slicePitch, mip, layer});
                offset += channelCount;
                size = (std::max)(1u, size / 2u);
            }
        }
        if (offset != encodedPixels.size()) {
            m_LastError = "reflection probe atlas payload size does not match its metadata";
            return false;
        }
        m_ReflectionTexture =
            m_Device->UploadTexture(desc, subresources.data(), static_cast<uint32_t>(subresources.size()));
        if (!m_ReflectionTexture) {
            m_LastError = "RHI failed to upload the local reflection probe atlas";
            return false;
        }
        RHITextureViewDesc viewDesc;
        viewDesc.mipCount = desc.mipLevels;
        viewDesc.layerCount = desc.arrayLayers;
        viewDesc.usage = RHIResourceUsage::ShaderResource;
        m_ReflectionTextureView = m_Device->CreateTextureView(m_ReflectionTexture, viewDesc);
        if (!m_ReflectionTextureView) {
            m_LastError = "RHI failed to create the local reflection probe atlas view";
            return false;
        }
    }
    std::vector<SHCoefficient> coefficients = m_Asset->GetSHCoefficients();
    if (!UploadStructuredBuffer(m_Device, coefficients, "LocalProbeSHCoefficients", m_SHCoefficients,
                                m_SHCoefficientView)) {
        m_LastError = "RHI failed to upload local probe SH coefficients";
        return false;
    }
    return true;
}

bool ProbeLightingSystem::Prepare(const Scene& scene) {
    const std::string& path = scene.GetLightingProbeAssetPath();
    if (path != m_AssetPath && !LoadAsset(path))
        return false;
    if (!m_Asset) {
        m_ReflectionProbeCount = 0;
        m_SHVolumeCount = 0;
        if (!m_ReflectionTextureView && !UploadFallbackResources()) {
            m_LastError = "RHI failed to create local probe fallback resources";
            return false;
        }
        return true;
    }
    return UpdateSceneMetadata(scene);
}

bool ProbeLightingSystem::UpdateSceneMetadata(const Scene& scene) {
    std::unordered_map<std::string, const BakedReflectionProbe*> bakedReflections;
    for (const auto& value : m_Asset->GetReflectionProbes())
        bakedReflections[value.probeId] = &value;
    std::unordered_map<std::string, const BakedSHProbeVolume*> bakedVolumes;
    for (const auto& value : m_Asset->GetSHVolumes())
        bakedVolumes[value.probeId] = &value;

    std::vector<GpuReflectionProbeData> reflectionGpu;
    std::vector<GpuSHVolumeData> volumeGpu;
    m_CpuReflectionProbes.clear();
    scene.ForEach([&](Actor& actor) {
        if (const auto* component = actor.GetComponent<ReflectionProbeComponent>()) {
            const auto found = bakedReflections.find(component->GetProbeId());
            if (found != bakedReflections.end() && reflectionGpu.size() < 32) {
                Mat4 inverse = Mat4::Identity();
                if (!Mat4Invert(actor.GetWorldMatrix(), inverse))
                    return;
                const auto* baked = found->second;
                GpuReflectionProbeData gpu{};
                gpu.worldToLocal = inverse;
                gpu.localToWorld = actor.GetWorldMatrix();
                gpu.extentsIntensity[0] = component->GetBoxExtents().x;
                gpu.extentsIntensity[1] = component->GetBoxExtents().y;
                gpu.extentsIntensity[2] = component->GetBoxExtents().z;
                gpu.extentsIntensity[3] = component->GetIntensity();
                const Vec3 position = actor.GetWorldMatrix().TransformPoint(component->GetCaptureOffset());
                gpu.positionRange[0] = position.x;
                gpu.positionRange[1] = position.y;
                gpu.positionRange[2] = position.z;
                gpu.positionRange[3] = baked->rgbmRange;
                gpu.layerPriority[0] = baked->arrayLayer;
                gpu.layerPriority[2] = component->GetLayerMask();
                gpu.blendInfo[0] = component->GetBlendDistance();
                gpu.blendInfo[1] = static_cast<float>(component->GetPriority());
                reflectionGpu.push_back(gpu);
                m_CpuReflectionProbes.push_back({inverse, component->GetBoxExtents(), component->GetBlendDistance(),
                                                 component->GetIntensity(), baked->rgbmRange, component->GetPriority(),
                                                 component->GetLayerMask(), baked->arrayLayer});
            }
        }
        if (const auto* component = actor.GetComponent<SHProbeVolumeComponent>()) {
            const auto found = bakedVolumes.find(component->GetProbeId());
            if (found != bakedVolumes.end() && volumeGpu.size() < 8) {
                Mat4 inverse = Mat4::Identity();
                if (!Mat4Invert(actor.GetWorldMatrix(), inverse))
                    return;
                const auto* baked = found->second;
                GpuSHVolumeData gpu{};
                gpu.worldToLocal = inverse;
                gpu.extentsIntensity[0] = component->GetBoxExtents().x;
                gpu.extentsIntensity[1] = component->GetBoxExtents().y;
                gpu.extentsIntensity[2] = component->GetBoxExtents().z;
                gpu.extentsIntensity[3] = component->GetIntensity();
                gpu.blendPriority[0] = component->GetBlendDistance();
                gpu.blendPriority[1] = static_cast<float>(component->GetPriority());
                gpu.gridAndOffset[0] = baked->gridWidth;
                gpu.gridAndOffset[1] = baked->gridHeight;
                gpu.gridAndOffset[2] = baked->gridDepth;
                gpu.gridAndOffset[3] = baked->coefficientOffset;
                volumeGpu.push_back(gpu);
            }
        }
    });
    if (!UploadStructuredBuffer(m_Device, reflectionGpu, "LocalReflectionProbeMetadata", m_ReflectionMetadata,
                                m_ReflectionMetadataView) ||
        !UploadStructuredBuffer(m_Device, volumeGpu, "LocalSHVolumeMetadata", m_SHVolumeMetadata,
                                m_SHVolumeMetadataView)) {
        m_LastError = "RHI failed to upload local probe metadata";
        return false;
    }
    m_ReflectionProbeCount = static_cast<uint32_t>(reflectionGpu.size());
    m_SHVolumeCount = static_cast<uint32_t>(volumeGpu.size());
    m_LastError.clear();
    return true;
}

ProbeSelection ProbeLightingSystem::SelectReflectionProbes(const Vec3& worldPosition, uint32_t layerMask) const {
    struct Candidate {
        uint8_t index = 255;
        int priority = 0;
        float weight = 0.0f;
    };
    std::array<Candidate, 2> best{};
    for (size_t index = 0; index < m_CpuReflectionProbes.size(); ++index) {
        const auto& probe = m_CpuReflectionProbes[index];
        if ((probe.layerMask & layerMask) == 0)
            continue;
        const float weight =
            BoundaryWeight(probe.worldToLocal.TransformPoint(worldPosition), probe.extents, probe.blendDistance);
        if (weight <= 0.0f)
            continue;
        Candidate candidate{static_cast<uint8_t>(index), probe.priority, weight};
        const auto better = [](const Candidate& a, const Candidate& b) {
            return a.index != 255 &&
                   (b.index == 255 || a.priority > b.priority || (a.priority == b.priority && a.weight > b.weight));
        };
        if (better(candidate, best[0])) {
            best[1] = best[0];
            best[0] = candidate;
        } else if (better(candidate, best[1])) {
            best[1] = candidate;
        }
    }
    ProbeSelection result;
    result.primary = best[0].index;
    result.secondary = best[1].index;
    if (best[1].index != 255) {
        const float total = best[0].weight + best[1].weight;
        result.secondaryWeight = total > 0.0f ? best[1].weight / total : 0.0f;
    }
    return result;
}
