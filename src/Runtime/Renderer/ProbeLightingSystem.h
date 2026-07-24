#pragma once

#include "API/RuntimeApi.h"

#include "Core/EngineMath.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class IRHIDevice;
class Scene;
struct GpuBuffer;
struct GpuBufferView;
struct GpuTexture;
struct GpuTextureView;

struct ProbeSelection {
    uint8_t primary = 255;
    uint8_t secondary = 255;
    float secondaryWeight = 0.0f;
};

class MYENGINE_RUNTIME_API ProbeLightingSystem {
public:
    explicit ProbeLightingSystem(IRHIDevice* device);

    bool Prepare(const Scene& scene);
    void Reset();
    ProbeSelection SelectReflectionProbes(const Vec3& worldPosition, uint32_t layerMask = ~uint32_t{0}) const;

    GpuTexture* GetReflectionTexture() const { return m_ReflectionTexture.get(); }
    const std::shared_ptr<GpuTextureView>& GetReflectionTextureView() const { return m_ReflectionTextureView; }
    const std::shared_ptr<GpuBufferView>& GetReflectionMetadataView() const { return m_ReflectionMetadataView; }
    const std::shared_ptr<GpuBufferView>& GetSHVolumeMetadataView() const { return m_SHVolumeMetadataView; }
    const std::shared_ptr<GpuBufferView>& GetSHCoefficientView() const { return m_SHCoefficientView; }
    uint32_t GetReflectionProbeCount() const { return m_ReflectionProbeCount; }
    uint32_t GetSHVolumeCount() const { return m_SHVolumeCount; }
    const std::string& GetLastError() const { return m_LastError; }

private:
    bool LoadAsset(const std::string& path);
    bool UploadAsset();
    bool UploadFallbackResources();
    bool UpdateSceneMetadata(const Scene& scene);

    struct CpuReflectionProbe {
        Mat4 worldToLocal = Mat4::Identity();
        Vec3 extents{1.0f};
        float blendDistance = 0.0f;
        float intensity = 1.0f;
        float rgbmRange = 4.0f;
        int priority = 0;
        uint32_t layerMask = ~uint32_t{0};
        uint32_t arrayLayer = 0;
    };

    IRHIDevice* m_Device = nullptr;
    std::string m_AssetPath;
    std::shared_ptr<class LightingProbeAsset> m_Asset;
    std::shared_ptr<GpuTexture> m_ReflectionTexture;
    std::shared_ptr<GpuTextureView> m_ReflectionTextureView;
    std::shared_ptr<GpuBuffer> m_ReflectionMetadata;
    std::shared_ptr<GpuBufferView> m_ReflectionMetadataView;
    std::shared_ptr<GpuBuffer> m_SHVolumeMetadata;
    std::shared_ptr<GpuBufferView> m_SHVolumeMetadataView;
    std::shared_ptr<GpuBuffer> m_SHCoefficients;
    std::shared_ptr<GpuBufferView> m_SHCoefficientView;
    std::vector<CpuReflectionProbe> m_CpuReflectionProbes;
    uint32_t m_ReflectionProbeCount = 0;
    uint32_t m_SHVolumeCount = 0;
    std::string m_LastError;
};
