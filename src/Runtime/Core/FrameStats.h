#pragma once

#include <cstdint>
#include <string>

struct RendererFrameStats {
    float renderSubmissionCpuMs = 0.0f;
    float renderGraphBuildCpuMs = 0.0f;
    float renderGraphExecuteCpuMs = 0.0f;
    float renderGraphPrepareCpuMs = 0.0f;
    float frameWaitCpuMs = 0.0f;
    float presentCpuMs = 0.0f;
    float shadowCpuMs = 0.0f;
    float mainCpuMs = 0.0f;
    float ssaoCpuMs = 0.0f;
    float compositeCpuMs = 0.0f;
    float shadowGpuMs = 0.0f;
    float mainGpuMs = 0.0f;
    float ssaoGpuMs = 0.0f;
    float compositeGpuMs = 0.0f;
    uint32_t drawCalls = 0;
    uint32_t shadowDrawCalls = 0;
    uint32_t mainDrawCalls = 0;
    uint32_t fullscreenDrawCalls = 0;
    uint32_t subMeshCount = 0;
    uint32_t bindGroupCreates = 0;
    uint32_t textureUploads = 0;
    uint64_t textureUploadBytes = 0;
    float textureUploadMs = 0.0f;
    bool gpuTimingAvailable = false;
    uint64_t transientRequestedBytes = 0, transientAllocatedBytes = 0, transientReusedBytes = 0;
    uint64_t renderGraphPooledBytes = 0, renderGraphPoolEvictedBytes = 0;
    uint32_t transientResources = 0, transientDescriptors = 0, renderGraphPoolEvictions = 0;
    bool transientBudgetExceeded = false;
    uint64_t gpuSceneUploadBytes = 0;
    float gpuScenePrepareCpuMs = 0.0f;
    uint32_t gpuSceneMaterialResolves = 0;
    uint32_t gpuSceneMaterialCacheHits = 0;
    uint32_t gpuSceneTexturedMaterials = 0;
    uint32_t gpuSceneCandidates = 0;
    uint32_t gpuFrustumVisible = 0;
    uint32_t gpuHiZOccluded = 0;
    uint32_t indirectDrawCount = 0;
    uint32_t clusterCount = 0;
    uint32_t clusterOverflow = 0;
    uint32_t localLightCount = 0;
    uint32_t bindlessResourcesUsed = 0;
    uint32_t bindlessResourcesCapacity = 0;
    uint32_t rayTracingRequestedMask = 0;
    uint32_t rayTracingEffectiveMask = 0;
    uint32_t rayTracingBlasCount = 0;
    uint32_t rayTracingTlasInstanceCount = 0;
    uint64_t rayTracingAccelerationStructureBytes = 0;
    float rayTracingBuildCpuMs = 0.0f;
    bool rayTracingTlasUpdated = false;
    std::string rayTracingFallbackReason;
    std::string historyResetReason;
};

struct RuntimeResourceFrameStats {
    uint64_t assetCpuBytes = 0;
    uint64_t assetEvictedBytes = 0;
    uint64_t assetBlockedBytes = 0;
    uint64_t pendingUploadBytes = 0;
    uint64_t peakPendingUploadBytes = 0;
    uint64_t liveActors = 0;
    uint64_t gpuResourceBytes = 0, peakGpuResourceBytes = 0, liveGpuDescriptors = 0;
    uint64_t liveNativeDescriptorSlots = 0;
    uint32_t qualityDegradationLevel = 0;
    uint32_t pendingUploadTasks = 0;
    bool assetPressure = false;
    bool uploadPressure = false;
    bool actorPressure = false;
    bool gpuResourcePressure = false, descriptorPressure = false, nativeDescriptorPressure = false;
    bool transientPressure = false;
};

class FrameStatsProvider {
public:
    static RendererFrameStats GetRendererStats();
    static void SetRendererStats(const RendererFrameStats& stats);
    static RuntimeResourceFrameStats GetResourceStats();
    static void SetResourceStats(const RuntimeResourceFrameStats& stats);
};

struct FrameStats {
    uint64_t frameNumber = 0;
    float fps = 0.0f;
    float frameMs = 0.0f;
    float updateMs = 0.0f;
    float renderMs = 0.0f;
    float smoothedFrameMs = 0.0f;
    RendererFrameStats renderer;
    RuntimeResourceFrameStats resources;
};
