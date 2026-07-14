#pragma once

#include "Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <memory>
struct GpuBuffer;
struct GpuTexture;

enum class RHINativeDescriptorKind : uint8_t { Resource, Sampler, RenderTarget, DepthStencil };

struct RHIResourceStats {
    uint64_t liveBufferBytes = 0, liveTextureBytes = 0, liveResourceBytes = 0;
    uint64_t peakResourceBytes = 0, createdResourceBytes = 0, releasedResourceBytes = 0;
    uint64_t liveBuffers = 0, liveTextures = 0, liveDescriptors = 0;
    uint64_t peakBuffers = 0, peakTextures = 0, peakDescriptors = 0;
    uint64_t liveNativeDescriptorSlots = 0, peakNativeDescriptorSlots = 0;
    uint64_t liveNativeResourceSlots = 0, liveNativeSamplerSlots = 0;
    uint64_t liveNativeRenderTargetSlots = 0, liveNativeDepthStencilSlots = 0;
    uint64_t failedNativeDescriptorAllocations = 0;
};

class RHIResourceStatsProvider {
public:
    static RHIResourceStats GetStats();
    static void ResetPeaksAndTotals();
    static void AddBuffer(uint64_t bytes);
    static void AddTexture(uint64_t bytes);
    static void AddDescriptors(uint32_t count);
    static void AddNativeDescriptorSlots(RHINativeDescriptorKind kind, uint32_t count = 1);
    static void RecordNativeDescriptorAllocationFailure(RHINativeDescriptorKind kind);
    static void ReleaseBuffer(uint64_t bytes);
    static void ReleaseTexture(uint64_t bytes);
    static void ReleaseDescriptors(uint32_t count);
    static void ReleaseNativeDescriptorSlots(RHINativeDescriptorKind kind, uint32_t count = 1);
};

uint64_t EstimateRHITextureBytes(const RHITextureDesc& desc);
void CommitRHIResourceAccounting(const std::shared_ptr<GpuBuffer>& resource);
void CommitRHIResourceAccounting(const std::shared_ptr<GpuTexture>& resource);
