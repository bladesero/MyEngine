#include "Game/RuntimeResourceBudget.h"

#include "Core/Memory/MemoryService.h"
#include "Core/FrameStats.h"
#include "Core/RuntimeQualityDegradation.h"

#include <algorithm>
#include <cmath>

namespace {
bool RatioValid(float value){return std::isfinite(value)&&value>0.0f&&value<=1.0f;}
}

bool RuntimeResourceBudgetController::Configure(const RuntimeResourceBudgetConfig& value,
                                                std::string* error)
{
    if(!value.assetCpuHighWatermarkBytes||!value.maxAssetEvictionsPerFrame||
       !value.maxLiveActors||!value.maxPendingUploadBytes||!value.maxPendingUploadTasks||
       !value.maxGpuResourceBytes||!value.maxGpuTextureEvictionsPerFrame||
       !value.maxGpuMeshEvictionsPerFrame||
       !value.pressureFramesBeforeQualityDegrade||!value.healthyFramesBeforeQualityRestore||
       !value.maxLogicalDescriptors||!value.maxNativeDescriptorSlots||
       !RatioValid(value.assetCpuLowWatermarkRatio)||!RatioValid(value.actorLowWatermarkRatio)||
       !RatioValid(value.uploadLowWatermarkRatio)||!RatioValid(value.gpuResourceLowWatermarkRatio)||
       !RatioValid(value.descriptorLowWatermarkRatio)||!value.perFrameUpload.maxTasks||
       !value.perFrameUpload.maxBytes){
        if(error)*error="invalid runtime resource budget";return false;
    }
    m_Config=value;m_Configured=true;
    AssetManager::Get().SetAssetCpuBudgetBytes(value.assetCpuHighWatermarkBytes);
    MemoryService::Get().SetSceneActorBudget(value.maxLiveActors);
    GpuUploadQueue::Get().SetDefaultBudget(value.perFrameUpload);
    if(error)error->clear();return true;
}

const RuntimeResourceBudgetReport& RuntimeResourceBudgetController::Tick()
{
    if(!m_Configured){std::string ignored;Configure({},&ignored);}
    const bool wasAny=m_Report.assetPressure||m_Report.actorPressure||m_Report.uploadPressure||
        m_Report.gpuResourcePressure||m_Report.descriptorPressure||
        m_Report.nativeDescriptorPressure||m_Report.transientPressure;
    ++m_Report.frameIndex;
    m_Report.assets=AssetManager::Get().CollectGarbageDetailed({
        m_Config.assetCpuHighWatermarkBytes,m_Config.assetCpuLowWatermarkRatio,
        m_Config.maxAssetEvictionsPerFrame});
    m_Report.uploads=GpuUploadQueue::Get().GetStats();
    m_Report.liveActors=MemoryService::Get().GetSceneLiveActorCount();
    m_Report.gpuTextures=MaterialResourceCache::CollectGlobalTextureGarbage(
        m_Config.maxGpuResourceBytes,m_Config.gpuResourceLowWatermarkRatio,
        m_Config.maxGpuTextureEvictionsPerFrame);
    m_Report.gpuMeshes=MaterialResourceCache::CollectGlobalMeshGarbage(
        m_Config.maxGpuResourceBytes,m_Config.gpuResourceLowWatermarkRatio,
        m_Config.maxGpuMeshEvictionsPerFrame,m_Config.activeMeshGraceCollections);
    m_Report.rhi=RHIResourceStatsProvider::GetStats();

    const size_t assetLow=static_cast<size_t>(m_Config.assetCpuHighWatermarkBytes*
        m_Config.assetCpuLowWatermarkRatio);
    if(m_Report.assetPressure)m_Report.assetPressure=m_Report.assets.bytesAfter>assetLow;
    else m_Report.assetPressure=m_Report.assets.bytesAfter>m_Config.assetCpuHighWatermarkBytes;
    const uint64_t actorLow=static_cast<uint64_t>(m_Config.maxLiveActors*m_Config.actorLowWatermarkRatio);
    if(m_Report.actorPressure)m_Report.actorPressure=m_Report.liveActors>actorLow;
    else m_Report.actorPressure=m_Report.liveActors>m_Config.maxLiveActors;
    const uint64_t uploadLow=static_cast<uint64_t>(m_Config.maxPendingUploadBytes*
        m_Config.uploadLowWatermarkRatio);
    if(m_Report.uploadPressure)m_Report.uploadPressure=
        m_Report.uploads.pendingBytes>uploadLow||
        m_Report.uploads.pendingTasks>static_cast<size_t>(m_Config.maxPendingUploadTasks*
            m_Config.uploadLowWatermarkRatio);
    else m_Report.uploadPressure=m_Report.uploads.pendingBytes>m_Config.maxPendingUploadBytes||
        m_Report.uploads.pendingTasks>m_Config.maxPendingUploadTasks;
    const uint64_t gpuLow=static_cast<uint64_t>(m_Config.maxGpuResourceBytes*
        m_Config.gpuResourceLowWatermarkRatio);
    if(m_Report.gpuResourcePressure)m_Report.gpuResourcePressure=m_Report.rhi.liveResourceBytes>gpuLow;
    else m_Report.gpuResourcePressure=m_Report.rhi.liveResourceBytes>m_Config.maxGpuResourceBytes;
    const uint64_t descriptorLow=static_cast<uint64_t>(m_Config.maxLogicalDescriptors*
        m_Config.descriptorLowWatermarkRatio);
    if(m_Report.descriptorPressure)m_Report.descriptorPressure=m_Report.rhi.liveDescriptors>descriptorLow;
    else m_Report.descriptorPressure=m_Report.rhi.liveDescriptors>m_Config.maxLogicalDescriptors;
    const uint64_t nativeDescriptorLow=static_cast<uint64_t>(m_Config.maxNativeDescriptorSlots*
        m_Config.descriptorLowWatermarkRatio);
    if(m_Report.nativeDescriptorPressure)m_Report.nativeDescriptorPressure=
        m_Report.rhi.liveNativeDescriptorSlots>nativeDescriptorLow;
    else m_Report.nativeDescriptorPressure=
        m_Report.rhi.liveNativeDescriptorSlots>m_Config.maxNativeDescriptorSlots;
    m_Report.transientPressure=FrameStatsProvider::GetRendererStats().transientBudgetExceeded;

    m_Report.violations.clear();
    if(m_Report.assetPressure)m_Report.violations.push_back(
        m_Report.assets.targetReached?"asset_cpu_pressure":"asset_cpu_eviction_blocked");
    if(m_Report.actorPressure)m_Report.violations.push_back("actor_count_pressure");
    if(m_Report.uploadPressure)m_Report.violations.push_back("gpu_upload_backlog_pressure");
    if(m_Report.gpuResourcePressure)m_Report.violations.push_back("gpu_resource_residency_pressure");
    if(m_Report.descriptorPressure)m_Report.violations.push_back("gpu_descriptor_pressure");
    if(m_Report.nativeDescriptorPressure)m_Report.violations.push_back("gpu_native_descriptor_slot_pressure");
    if(m_Report.transientPressure)m_Report.violations.push_back("render_graph_transient_pressure");
    const bool any=!m_Report.violations.empty();
    const bool gpuPressure=m_Report.gpuResourcePressure||m_Report.descriptorPressure||
        m_Report.nativeDescriptorPressure||m_Report.transientPressure;
    if(gpuPressure){++m_ConsecutiveGpuPressure;m_ConsecutiveGpuHealthy=0;}
    else{++m_ConsecutiveGpuHealthy;m_ConsecutiveGpuPressure=0;}
    auto quality=RuntimeQualityDegradation::Get();
    if(m_ConsecutiveGpuPressure>=m_Config.pressureFramesBeforeQualityDegrade&&quality.level<2){
        RuntimeQualityDegradation::SetLevel(quality.level+1);m_ConsecutiveGpuPressure=0;}
    else if(m_ConsecutiveGpuHealthy>=m_Config.healthyFramesBeforeQualityRestore&&quality.level>0){
        RuntimeQualityDegradation::SetLevel(quality.level-1);m_ConsecutiveGpuHealthy=0;}
    RuntimeQualityDegradation::SetFrameCounters(m_ConsecutiveGpuPressure,m_ConsecutiveGpuHealthy);
    quality=RuntimeQualityDegradation::Get();m_Report.qualityDegradationLevel=quality.level;
    m_Report.qualityDegradationTransitions=quality.transitions;
    if(any)++m_Report.pressureFrames;
    if(any!=wasAny)++m_Report.violationTransitions;
    RuntimeResourceFrameStats frame;frame.assetCpuBytes=m_Report.assets.bytesAfter;
    frame.assetEvictedBytes=m_Report.assets.evictions.empty()?0:
        m_Report.assets.bytesBefore-m_Report.assets.bytesAfter;
    frame.assetBlockedBytes=m_Report.assets.blockedBytes;frame.pendingUploadBytes=m_Report.uploads.pendingBytes;
    frame.peakPendingUploadBytes=m_Report.uploads.peakPendingBytes;frame.liveActors=m_Report.liveActors;
    frame.pendingUploadTasks=static_cast<uint32_t>(std::min<size_t>(m_Report.uploads.pendingTasks,UINT32_MAX));
    frame.gpuResourceBytes=m_Report.rhi.liveResourceBytes;frame.peakGpuResourceBytes=m_Report.rhi.peakResourceBytes;
    frame.liveGpuDescriptors=m_Report.rhi.liveDescriptors;
    frame.liveNativeDescriptorSlots=m_Report.rhi.liveNativeDescriptorSlots;
    frame.qualityDegradationLevel=m_Report.qualityDegradationLevel;
    frame.assetPressure=m_Report.assetPressure;
    frame.uploadPressure=m_Report.uploadPressure;frame.actorPressure=m_Report.actorPressure;
    frame.gpuResourcePressure=m_Report.gpuResourcePressure;frame.descriptorPressure=m_Report.descriptorPressure;
    frame.nativeDescriptorPressure=m_Report.nativeDescriptorPressure;
    frame.transientPressure=m_Report.transientPressure;FrameStatsProvider::SetResourceStats(frame);
    return m_Report;
}

void RuntimeResourceBudgetController::ResetStatistics()
{
    const bool configured=m_Configured;const auto config=m_Config;
    m_Report={};GpuUploadQueue::Get().ResetStatistics();
    m_ConsecutiveGpuPressure=0;m_ConsecutiveGpuHealthy=0;RuntimeQualityDegradation::Reset();
    m_Configured=configured;m_Config=config;
}
