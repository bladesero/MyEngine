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
    uint32_t skippedTextureUploads = 0;
    uint64_t textureUploadBytes = 0;
    uint64_t pendingTextureUploadBytes = 0;
    float textureUploadMs = 0.0f;
    uint32_t textureEvictions = 0;
    uint64_t textureEvictedBytes = 0;
};

enum class GpuTextureEvictionBlockReason { Pinned, Referenced };
struct GpuTextureEvictionRecord {
    std::string path;
    uint64_t bytes = 0;
};
struct GpuTextureEvictionBlocker {
    std::string path;
    uint64_t bytes = 0;
    GpuTextureEvictionBlockReason reason = GpuTextureEvictionBlockReason::Referenced;
};
struct GpuTextureGarbageCollectionReport {
    uint64_t bytesBefore = 0, bytesAfter = 0, targetBytes = 0, blockedBytes = 0;
    bool pressureDetected = false, targetReached = true;
    std::vector<GpuTextureEvictionRecord> evictions;
    std::vector<GpuTextureEvictionBlocker> blockers;
};
using GpuMeshEvictionRecord = GpuTextureEvictionRecord;
using GpuMeshEvictionBlocker = GpuTextureEvictionBlocker;
using GpuMeshGarbageCollectionReport = GpuTextureGarbageCollectionReport;

class MaterialResourceCache {
public:
    explicit MaterialResourceCache(IRHIDevice* device = nullptr);
    ~MaterialResourceCache();
    MaterialResourceCache(const MaterialResourceCache&) = delete;
    MaterialResourceCache& operator=(const MaterialResourceCache&) = delete;

    void SetDevice(IRHIDevice* device) { m_Device = device; }
    void SetTextureUploadBudgetBytes(uint64_t bytes) { m_TextureUploadBudgetBytes = bytes; }
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
    static GpuTextureGarbageCollectionReport CollectGlobalTextureGarbage(uint64_t highWatermarkBytes,
                                                                         float lowWatermarkRatio, size_t maxEvictions);
    static GpuMeshGarbageCollectionReport CollectGlobalMeshGarbage(uint64_t highWatermarkBytes, float lowWatermarkRatio,
                                                                   size_t maxEvictions,
                                                                   uint64_t activeGraceCollections = 2);
    static void TrackGlobalMeshResidency(MeshAsset* mesh);

private:
    IRHIDevice* m_Device = nullptr;
    MaterialResourceCacheStats m_FrameStats;
    uint64_t m_TextureUploadBudgetBytes = 32ull * 1024ull * 1024ull;
    struct TextureEntry {
        std::shared_ptr<GpuTexture> texture;
        std::weak_ptr<void> assetLifetime;
        std::string path;
        uint64_t bytes = 0, lastUsed = 0;
    };
    std::unordered_map<TextureAsset*, TextureEntry> m_TextureCache;
    std::unordered_map<GpuTexture*, std::shared_ptr<GpuTextureView>> m_TextureViews;
    std::vector<std::shared_ptr<GpuSampler>> m_TextureSamplers;
    std::shared_ptr<GpuTexture> m_DefaultTexture;
    std::shared_ptr<GpuTextureView> m_DefaultTextureView;
    std::shared_ptr<GpuSampler> m_LinearSampler;
    std::shared_ptr<GpuSampler> m_ShadowSampler;
    uint64_t m_UseClock = 0;
};
