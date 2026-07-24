#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Assets/AssetManager.h"
#include "Renderer/GpuUploadQueue.h"
#include "Renderer/RHI/RHIResourceStats.h"
#include "Renderer/MaterialResourceCache.h"

#include <cstdint>
#include <string>
#include <vector>

struct RuntimeResourceBudgetConfig {
    size_t assetCpuHighWatermarkBytes = 512ull * 1024ull * 1024ull;
    float assetCpuLowWatermarkRatio = 0.8f;
    size_t maxAssetEvictionsPerFrame = 8;
    uint64_t maxLiveActors = 100000;
    float actorLowWatermarkRatio = 0.9f;
    uint64_t maxPendingUploadBytes = 256ull * 1024ull * 1024ull;
    size_t maxPendingUploadTasks = 1024;
    float uploadLowWatermarkRatio = 0.75f;
    GpuUploadBudget perFrameUpload;
    uint64_t maxGpuResourceBytes = 2ull * 1024ull * 1024ull * 1024ull;
    float gpuResourceLowWatermarkRatio = 0.85f;
    size_t maxGpuTextureEvictionsPerFrame = 4;
    size_t maxGpuMeshEvictionsPerFrame = 4;
    uint64_t activeMeshGraceCollections = 2;
    uint64_t maxLogicalDescriptors = 16384;
    uint64_t maxNativeDescriptorSlots = 8192;
    float descriptorLowWatermarkRatio = 0.8f;
    uint64_t pressureFramesBeforeQualityDegrade = 30;
    uint64_t healthyFramesBeforeQualityRestore = 120;
};

struct RuntimeResourceBudgetReport {
    uint64_t frameIndex = 0;
    AssetGarbageCollectionReport assets;
    GpuUploadQueueStats uploads;
    uint64_t liveActors = 0;
    RHIResourceStats rhi;
    GpuTextureGarbageCollectionReport gpuTextures;
    GpuMeshGarbageCollectionReport gpuMeshes;
    bool assetPressure = false;
    bool actorPressure = false;
    bool uploadPressure = false;
    bool gpuResourcePressure = false;
    bool descriptorPressure = false;
    bool nativeDescriptorPressure = false;
    bool transientPressure = false;
    uint64_t pressureFrames = 0;
    uint64_t violationTransitions = 0;
    uint32_t qualityDegradationLevel = 0;
    uint64_t qualityDegradationTransitions = 0;
    std::vector<std::string> violations;
};

class MYENGINE_RUNTIME_API RuntimeResourceBudgetController {
public:
    bool Configure(const RuntimeResourceBudgetConfig&, std::string* error = nullptr);
    const RuntimeResourceBudgetConfig& GetConfig() const { return m_Config; }
    const RuntimeResourceBudgetReport& Tick();
    const RuntimeResourceBudgetReport& GetLastReport() const { return m_Report; }
    void ResetStatistics();

private:
    RuntimeResourceBudgetConfig m_Config;
    RuntimeResourceBudgetReport m_Report;
    bool m_Configured = false;
    uint64_t m_ConsecutiveGpuPressure = 0, m_ConsecutiveGpuHealthy = 0;
};
