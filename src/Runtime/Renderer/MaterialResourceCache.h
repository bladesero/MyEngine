#pragma once

#include "Renderer/RHI/GpuSampler.h"
#include "Renderer/RHI/GpuTexture.h"
#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/RHI/IRHIDevice.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class MeshAsset;
class TextureAsset;

struct MaterialResourceCacheStats {
    uint32_t textureUploads = 0;
    uint64_t textureUploadBytes = 0;
    float textureUploadMs = 0.0f;
};

class MaterialResourceCache {
public:
    explicit MaterialResourceCache(IRHIDevice* device = nullptr);

    void SetDevice(IRHIDevice* device) { m_Device = device; }
    void ResetFrameStats() { m_FrameStats = {}; }
    const MaterialResourceCacheStats& GetFrameStats() const { return m_FrameStats; }

    void EnsureMeshUploaded(MeshAsset* mesh);
    void EnsureTextureUploaded(TextureAsset* texture);
    void EnsureNamedBindingDefaults();

    std::shared_ptr<GpuTextureView> GetTextureView(GpuTexture* texture);
    std::shared_ptr<GpuSampler> GetSamplerForTexture(TextureAsset* texture);

    std::shared_ptr<GpuTextureView> GetDefaultTextureView();
    std::shared_ptr<GpuSampler> GetLinearSampler();
    std::shared_ptr<GpuSampler> GetShadowSampler();

    static RHISamplerDesc SamplerDescForTexture(const TextureAsset& texture);
    static bool SameSamplerDesc(const RHISamplerDesc& left, const RHISamplerDesc& right);
    static void AppendSamplerDesc(std::string& out, const RHISamplerDesc& desc);

private:
    IRHIDevice* m_Device = nullptr;
    MaterialResourceCacheStats m_FrameStats;
    std::unordered_map<TextureAsset*, std::shared_ptr<GpuTexture>> m_TextureCache;
    std::unordered_map<GpuTexture*, std::shared_ptr<GpuTextureView>> m_TextureViews;
    std::vector<std::shared_ptr<GpuSampler>> m_TextureSamplers;
    std::shared_ptr<GpuTexture> m_DefaultTexture;
    std::shared_ptr<GpuTextureView> m_DefaultTextureView;
    std::shared_ptr<GpuSampler> m_LinearSampler;
    std::shared_ptr<GpuSampler> m_ShadowSampler;
};
