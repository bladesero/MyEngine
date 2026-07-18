#pragma once

#include "Renderer/GpuSceneDatabase.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/RHI/IRHIReadbackService.h"

#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>

class Camera;
class Scene;
class GpuBindGroup;
struct ShaderHandle;

struct ModernDeferredFrameStats {
    uint32_t candidateObjects = 0;
    uint32_t indirectDrawCapacity = 0;
    uint32_t indirectDrawCount = 0;
    uint32_t hizVisibleDrawCount = 0;
    uint32_t localLights = 0;
    uint32_t clusterCount = 0;
    uint32_t clusterOverflow = 0;
    uint64_t gpuSceneUploadBytes = 0;
    float gpuScenePrepareCpuMs = 0.0f;
    uint32_t materialResolves = 0;
    uint32_t materialCacheHits = 0;
    uint32_t texturedMaterials = 0;
    uint32_t gpuShadowSetupFailures = 0;
    uint32_t retiredShadowStreams = 0;
    bool indirectBudgetExceeded = false;
};

struct ModernPostProcessSettings {
    bool ssgiEnabled = true;
    bool ssrEnabled = true;
    bool taaEnabled = true;
    float ssgiIntensity = 1.0f;
    float ssgiMaxDistance = 10.0f;
    float ssgiHistoryWeight = 0.9f;
    uint32_t ssgiStepCount = 32;
    uint32_t ssgiFilterRounds = 3;
    float ssrMaxDistance = 10.0f;
    float ssrMaxRoughness = 0.8f;
    float ssrHistoryWeight = 0.9f;
    uint32_t ssrStepCount = 48;
    uint32_t ssrFilterRounds = 2;
    float taaHistoryWeight = 0.9f;
    float exposure = 1.0f;
    float gamma = 2.2f;
    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.0f;
};

class ModernDeferredPipeline {
public:
    enum class QualityProfile { Desktop, Console };
    enum class ScreenSpaceDebugMode { None, SSGI, SSRConfidence };
    ModernDeferredPipeline(IRHIDevice* device, IRHIReadbackService* readbackService = nullptr);
    ~ModernDeferredPipeline();

    void Resize(uint32_t width, uint32_t height);
    void InvalidateTemporalHistory(std::string reason, bool resetObjectHistory = false);
    void SetQualityProfile(QualityProfile profile) { m_QualityProfile = profile; }
    bool Prepare(const Scene& scene, const Camera& camera, uint64_t frameNumber,
                 const ModernPostProcessSettings& settings = {});
    void CommitTemporalFrame();
    void AbortTemporalFrame(const std::string& reason);
    void SetDirectionalShadowInput(bool enabled, const std::shared_ptr<GpuTextureView>& shadowSrv,
                                   const Mat4* cascadeViewProjection, uint32_t cascadeCount, const float* cascadeSplits,
                                   float intensity);
    void AddDepthPrepass(RenderGraph& graph, RGTextureHandle sceneDepth);
    bool AddGpuDrivenShadowView(RenderGraph& graph, const std::string& name, RGTextureHandle shadowTarget,
                                RGTextureSubresource subresource, const Mat4& viewProjection);
    void AbortGpuDrivenShadowFrame();
    RGTextureHandle AddHiZPasses(RenderGraph& graph, RGTextureHandle sceneDepth,
                                 const std::shared_ptr<GpuTextureView>& sceneDepthSrv);
    void AddHiZOcclusionCulling(RenderGraph& graph, RGTextureHandle hiz);
    void AddGBufferPass(RenderGraph& graph, RGTextureHandle albedo, RGTextureHandle normal, RGTextureHandle material,
                        RGTextureHandle emissive, RGTextureHandle velocity, RGTextureHandle sceneDepth);
    RGTextureHandle AddClusteredLightingPasses(
        RenderGraph& graph, const Camera& camera, RGTextureHandle gbufferAlbedo,
        const std::shared_ptr<GpuTextureView>& gbufferAlbedoSrv, RGTextureHandle gbufferNormal,
        const std::shared_ptr<GpuTextureView>& gbufferNormalSrv, RGTextureHandle gbufferMaterial,
        const std::shared_ptr<GpuTextureView>& gbufferMaterialSrv, RGTextureHandle gbufferEmissive,
        const std::shared_ptr<GpuTextureView>& gbufferEmissiveSrv, RGTextureHandle sceneDepth,
        const std::shared_ptr<GpuTextureView>& sceneDepthSrv, RGTextureHandle environmentCube,
        const std::shared_ptr<GpuTextureView>& environmentCubeSrv, RGBufferHandle environmentSH,
        const std::shared_ptr<GpuBufferView>& environmentSHSrv, RGTextureHandle directionalShadow);
    const std::shared_ptr<GpuTextureView>& GetHdrSrv() const { return m_HdrSrv; }
    RGTextureHandle
    AddScreenSpaceEffects(RenderGraph& graph, RGTextureHandle hdr, const std::shared_ptr<GpuTextureView>& hdrSrv,
                          RGTextureHandle sceneDepth, const std::shared_ptr<GpuTextureView>& sceneDepthSrv,
                          RGTextureHandle gbufferAlbedo, const std::shared_ptr<GpuTextureView>& gbufferAlbedoSrv,
                          RGTextureHandle gbufferNormal, const std::shared_ptr<GpuTextureView>& gbufferNormalSrv,
                          RGTextureHandle gbufferMaterial, const std::shared_ptr<GpuTextureView>& gbufferMaterialSrv,
                          RGTextureHandle gbufferVelocity, const std::shared_ptr<GpuTextureView>& gbufferVelocitySrv,
                          RGTextureHandle hiz, ScreenSpaceDebugMode debugMode = ScreenSpaceDebugMode::None);
    RGTextureHandle
    AddTemporalPostProcess(RenderGraph& graph, RGTextureHandle input, const std::shared_ptr<GpuTextureView>& inputSrv,
                           RGTextureHandle sceneDepth, const std::shared_ptr<GpuTextureView>& sceneDepthSrv,
                           RGTextureHandle gbufferNormal, const std::shared_ptr<GpuTextureView>& gbufferNormalSrv,
                           RGTextureHandle gbufferVelocity, const std::shared_ptr<GpuTextureView>& gbufferVelocitySrv);
    const std::shared_ptr<GpuTextureView>& GetFinalPostSrv() const { return m_PostColor.srv; }
    const std::shared_ptr<GpuTextureView>& GetEffectsHdrSrv() const { return m_EffectsOutputSrv; }
    const std::shared_ptr<GpuTextureView>& GetHiZDebugSrv() const { return m_HiZSrv; }
    const std::shared_ptr<GpuTextureView>& GetSSGIDebugSrv() const { return m_SSGIDebugOutputSrv; }
    const std::shared_ptr<GpuTextureView>& GetSSRDebugSrv() const { return m_SSRDebugOutputSrv; }
    const std::string& GetHistoryResetReason() const { return m_HistoryResetReason; }
    const std::string& GetLastShadowSetupError() const { return m_LastShadowSetupError; }
    bool IsReady() const { return m_Ready; }
    const std::string& GetInitializationError() const { return m_InitializationError; }
    const ModernDeferredFrameStats& GetStats() const { return m_Stats; }
    GpuSceneDatabase& GetGpuScene() { return *m_GpuScene; }
    const Mat4& GetCurrentViewProjection() const { return m_GBufferConstants.viewProjection; }
    const Mat4& GetPreviousViewProjection() const { return m_GBufferConstants.previousViewProjection; }

private:
    bool EnsurePipelines();
    bool EnsureIndirectBuffers(uint32_t candidateCount);
    bool EnsureHiZResources();
    bool EnsureClusterResources();
    bool EnsureTemporalResources();
    struct ShadowIndirectStream;
    std::shared_ptr<ShadowIndirectStream> EnsureShadowIndirectStream(const std::string& name);
    std::shared_ptr<GpuBindGroup> AcquireBindGroup(const std::shared_ptr<GpuShader>& shader);
    void UpdateHistoryValidity(const Camera& camera, const ModernPostProcessSettings& settings);
    void ConsumeDiagnosticsReadback();

    struct ComputeTexture {
        std::shared_ptr<GpuTexture> texture;
        std::shared_ptr<GpuTextureView> srv;
        std::shared_ptr<GpuTextureView> uav;
        std::shared_ptr<GpuTextureView> rtv;
    };

    struct ShadowIndirectStream {
        std::shared_ptr<GpuBuffer> args;
        std::shared_ptr<GpuBufferView> argsUav;
        std::shared_ptr<GpuBuffer> count;
        std::shared_ptr<GpuBufferView> countUav;
        std::shared_ptr<GpuBindGroup> cullingBindings;
        std::shared_ptr<GpuBindGroup> depthBindings;
        uint32_t capacity = 0;
        RHIResourceState argsState = RHIResourceState::UnorderedAccess;
        RHIResourceState countState = RHIResourceState::UnorderedAccess;
    };

    struct RetiredShadowIndirectStream {
        uint64_t releaseSubmission = 0;
        std::shared_ptr<ShadowIndirectStream> stream;
    };

    struct CullingConstants {
        Mat4 viewProjection = Mat4::Identity();
        uint32_t objectCount = 0;
        uint32_t renderSize[2]{};
        uint32_t hizMipCount = 1;
    };
    struct DepthConstants {
        Mat4 viewProjection = Mat4::Identity();
    };
    struct ModernGBufferConstants {
        Mat4 viewProjection = Mat4::Identity();
        Mat4 previousViewProjection = Mat4::Identity();
    };
    struct HiZConstants {
        uint32_t sourceSize[2]{};
        uint32_t destinationSize[2]{};
    };
    struct ClusterConstants {
        Mat4 view = Mat4::Identity();
        Mat4 inverseProjection = Mat4::Identity();
        Mat4 inverseViewProjection = Mat4::Identity();
        uint32_t renderSize[2]{};
        uint32_t tileCount[2]{};
        uint32_t clusterCount = 0;
        uint32_t lightCount = 0;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        Vec3 cameraPosition{};
        float padding = 0.0f;
        Vec4 directionalLight{-0.55f, -1.0f, -0.45f, 0.0f};
        Vec4 directionalColorAmbient{1.0f, 1.0f, 1.0f, 1.0f};
        Mat4 shadowViewProjection[3] = {Mat4::Identity(), Mat4::Identity(), Mat4::Identity()};
        Vec4 cascadeSplits{};
        Vec4 shadowInfo{};
    };
    struct ScreenSpaceConstants {
        Mat4 view = Mat4::Identity();
        Mat4 projection = Mat4::Identity();
        Mat4 inverseProjection = Mat4::Identity();
        Mat4 inverseViewProjection = Mat4::Identity();
        Mat4 previousInverseViewProjection = Mat4::Identity();
        Mat4 previousViewProjection = Mat4::Identity();
        Vec4 cameraPositionAmbient{};
        Vec4 previousCameraPosition{};
        uint32_t fullSize[2]{};
        uint32_t effectSize[2]{};
        float fullTexelSize[2]{};
        float effectTexelSize[2]{};
        uint32_t frameIndex = 0;
        uint32_t historyValid = 0;
        uint32_t filterStep = 0;
        uint32_t effectMode = 0;
        float ssgiIntensity = 1.0f;
        float ssgiMaxDistance = 10.0f;
        float ssgiHistoryWeight = 0.9f;
        float ssrMaxDistance = 10.0f;
        float ssrMaxRoughness = 0.8f;
        float ssrHistoryWeight = 0.9f;
        float taaHistoryWeight = 0.9f;
        float exposure = 1.0f;
        float gamma = 2.2f;
        float bloomThreshold = 1.0f;
        float bloomIntensity = 0.0f;
        uint32_t ssgiStepCount = 32;
        uint32_t ssrStepCount = 48;
        uint32_t padding[3]{};
    };
    static_assert(sizeof(ScreenSpaceConstants) == 528, "ScreenSpaceConstants size must match ModernScreenSpace.hlsl");
    static_assert(offsetof(ScreenSpaceConstants, ssgiIntensity) == 464,
                  "ScreenSpaceConstants SSGI tuning offset changed");
    static_assert(offsetof(ScreenSpaceConstants, ssrMaxRoughness) == 480,
                  "ScreenSpaceConstants SSR tuning offset changed");
    static_assert(offsetof(ScreenSpaceConstants, gamma) == 496,
                  "ScreenSpaceConstants post-process tuning offset changed");
    static_assert(offsetof(ScreenSpaceConstants, ssrStepCount) == 512,
                  "ScreenSpaceConstants step-count offset changed");

    IRHIDevice* m_Device = nullptr;
    IRHIReadbackService* m_ReadbackService = nullptr;
    std::shared_ptr<GpuSceneDatabase> m_GpuScene;
    // D3D12 bind groups are CPU-side binding records; root values/descriptors are copied into the command list when
    // bound, so one stable record per shader avoids per-pass heap churn. Vulkan keeps per-dispatch groups because a
    // descriptor set can still be observed by earlier queued commands when updated in place.
    std::unordered_map<GpuShader*, std::shared_ptr<GpuBindGroup>> m_D3D12BindGroups;
    std::shared_ptr<ShaderHandle> m_CullingHandle;
    std::shared_ptr<ShaderHandle> m_OcclusionCullingHandle;
    std::shared_ptr<ShaderHandle> m_DepthHandle;
    std::shared_ptr<ShaderHandle> m_GBufferHandle;
    std::shared_ptr<GpuShader> m_CullingShader;
    std::shared_ptr<GpuShader> m_OcclusionCullingShader;
    std::shared_ptr<GpuShader> m_DepthShader;
    std::shared_ptr<GpuShader> m_GBufferShader;
    std::shared_ptr<GpuComputePipeline> m_CullingPipeline;
    std::shared_ptr<GpuComputePipeline> m_OcclusionCullingPipeline;
    std::shared_ptr<GpuGraphicsPipeline> m_DepthPipeline;
    std::shared_ptr<GpuGraphicsPipeline> m_ShadowDepthPipeline;
    std::shared_ptr<GpuGraphicsPipeline> m_GBufferPipeline;
    std::shared_ptr<ShaderHandle> m_HiZInitHandle;
    std::shared_ptr<ShaderHandle> m_HiZReduceHandle;
    std::shared_ptr<GpuShader> m_HiZInitShader;
    std::shared_ptr<GpuShader> m_HiZReduceShader;
    std::shared_ptr<GpuComputePipeline> m_HiZInitPipeline;
    std::shared_ptr<GpuComputePipeline> m_HiZReducePipeline;
    std::shared_ptr<ShaderHandle> m_ClusterCountHandle;
    std::shared_ptr<ShaderHandle> m_ClusterPrefixHandle;
    std::shared_ptr<ShaderHandle> m_ClusterScatterHandle;
    std::shared_ptr<ShaderHandle> m_ClusterLightingHandle;
    std::shared_ptr<GpuShader> m_ClusterCountShader;
    std::shared_ptr<GpuShader> m_ClusterPrefixShader;
    std::shared_ptr<GpuShader> m_ClusterScatterShader;
    std::shared_ptr<GpuShader> m_ClusterLightingShader;
    std::shared_ptr<GpuComputePipeline> m_ClusterCountPipeline;
    std::shared_ptr<GpuComputePipeline> m_ClusterPrefixPipeline;
    std::shared_ptr<GpuComputePipeline> m_ClusterScatterPipeline;
    std::shared_ptr<GpuComputePipeline> m_ClusterLightingPipeline;
    std::shared_ptr<ShaderHandle> m_SSGITraceHandle, m_SSRTraceHandle, m_TemporalHandle, m_AtrousHandle,
        m_EffectsCompositeHandle, m_TAAHandle, m_BloomToneHandle;
    std::shared_ptr<GpuShader> m_SSGITraceShader, m_SSRTraceShader, m_TemporalShader, m_AtrousShader,
        m_EffectsCompositeShader, m_TAAShader, m_BloomToneShader;
    std::shared_ptr<GpuComputePipeline> m_SSGITracePipeline, m_SSRTracePipeline, m_TemporalPipeline, m_AtrousPipeline,
        m_EffectsCompositePipeline, m_TAAPipeline, m_BloomTonePipeline;
    std::array<std::shared_ptr<GpuSampler>, kGpuSceneMaterialSamplerCount> m_MaterialSamplers;
    std::shared_ptr<GpuSampler> m_LinearClampSampler;
    std::shared_ptr<GpuSampler> m_PointClampSampler;
    std::shared_ptr<GpuSampler> m_ShadowSampler;
    std::shared_ptr<GpuTexture> m_ShadowFallback;
    std::shared_ptr<GpuTextureView> m_ShadowFallbackSrv;
    std::shared_ptr<GpuTexture> m_EnvironmentFallback;
    std::shared_ptr<GpuTextureView> m_EnvironmentFallbackSrv;
    std::shared_ptr<GpuBuffer> m_EnvironmentSHFallback;
    std::shared_ptr<GpuBufferView> m_EnvironmentSHFallbackSrv;
    std::shared_ptr<GpuTextureView> m_EnvironmentCubeSrv;
    std::shared_ptr<GpuTextureView> m_DirectionalShadowSrv;
    std::shared_ptr<GpuBuffer> m_IndirectArgs;
    std::shared_ptr<GpuBufferView> m_IndirectArgsUav;
    std::shared_ptr<GpuBuffer> m_IndirectCount;
    std::shared_ptr<GpuBufferView> m_IndirectCountUav;
    std::unordered_map<std::string, std::shared_ptr<ShadowIndirectStream>> m_ShadowIndirectStreams;
    // Shadow graph construction must not advance the persistent RHI state. The graph may still fail during prepare or
    // execution, so streams are committed only after Renderer confirms that the frame completed successfully.
    std::vector<std::shared_ptr<ShadowIndirectStream>> m_PendingShadowStreams;
    std::vector<RetiredShadowIndirectStream> m_RetiredShadowStreams;
    std::shared_ptr<GpuTexture> m_HiZ;
    std::shared_ptr<GpuTextureView> m_HiZSrv;
    std::vector<std::shared_ptr<GpuTextureView>> m_HiZMipSrvs;
    std::vector<std::shared_ptr<GpuTextureView>> m_HiZMipUavs;
    std::shared_ptr<GpuBuffer> m_ClusterCounts;
    std::shared_ptr<GpuBufferView> m_ClusterCountsView;
    std::shared_ptr<GpuBuffer> m_ClusterOffsets;
    std::shared_ptr<GpuBufferView> m_ClusterOffsetsView;
    std::shared_ptr<GpuBuffer> m_ClusterLightIndices;
    std::shared_ptr<GpuBufferView> m_ClusterLightIndicesView;
    std::shared_ptr<GpuBuffer> m_ClusterOverflow;
    std::shared_ptr<GpuBufferView> m_ClusterOverflowView;
    std::shared_ptr<GpuTexture> m_Hdr;
    std::shared_ptr<GpuTextureView> m_HdrRtv;
    std::shared_ptr<GpuTextureView> m_HdrSrv;
    std::shared_ptr<GpuTextureView> m_HdrUav;
    ComputeTexture m_SSGITrace;
    ComputeTexture m_SSGIHistory[2];
    ComputeTexture m_SSGIFilter[2];
    ComputeTexture m_SSRTrace;
    ComputeTexture m_SSRHistory[2];
    ComputeTexture m_SSRFilter[2];
    ComputeTexture m_EffectsHdr;
    ComputeTexture m_ScreenSpaceDebug;
    std::shared_ptr<GpuTextureView> m_EffectsOutputSrv;
    std::shared_ptr<GpuTextureView> m_SSGIDebugOutputSrv;
    std::shared_ptr<GpuTextureView> m_SSRDebugOutputSrv;
    ComputeTexture m_TAAHistory[2];
    ComputeTexture m_DepthHistory[2];
    ComputeTexture m_NormalHistory[2];
    ComputeTexture m_PostColor;
    RGTextureHandle m_FrameDepthHistoryRead;
    RGTextureHandle m_FrameDepthHistoryWrite;
    RGTextureHandle m_FrameNormalHistoryRead;
    RGTextureHandle m_FrameNormalHistoryWrite;
    RGTextureHandle m_FrameEnvironment;
    uint32_t m_IndirectCapacity = 0;
    CullingConstants m_CullingConstants;
    DepthConstants m_DepthConstants;
    ModernGBufferConstants m_GBufferConstants;
    ClusterConstants m_ClusterConstants;
    ScreenSpaceConstants m_ScreenSpaceConstants;
    ModernPostProcessSettings m_PostSettings;
    QualityProfile m_QualityProfile = QualityProfile::Desktop;
    ModernDeferredFrameStats m_Stats;
    std::string m_InitializationError;
    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    bool m_Ready = false;
    RHIResourceState m_IndirectArgsState = RHIResourceState::UnorderedAccess;
    RHIResourceState m_IndirectCountState = RHIResourceState::UnorderedAccess;
    bool m_HiZInShaderState = false;
    bool m_ClusterBuffersInShaderState = false;
    RHIResourceState m_ClusterOverflowState = RHIResourceState::UnorderedAccess;
    bool m_HdrInShaderState = false;
    std::shared_ptr<GpuReadbackTicket> m_IndirectCountReadback;
    std::shared_ptr<GpuReadbackTicket> m_VisibleIndirectCountReadback;
    std::shared_ptr<GpuReadbackTicket> m_ClusterOverflowReadback;
    uint64_t m_LastDiagnosticsReadbackFrame = 0;
    bool m_DiagnosticsReadbackThisFrame = false;
    uint32_t m_LastIndirectDrawCount = 0;
    uint32_t m_LastVisibleIndirectDrawCount = 0;
    uint32_t m_LastClusterOverflow = 0;
    bool m_GeometryHistoryInShaderState = false;
    bool m_PostColorInShaderState = false;
    bool m_SSGIResourcesInShaderState = false;
    bool m_SSRResourcesInShaderState = false;
    bool m_TAAResourcesInShaderState = false;
    bool m_EffectsResourceInShaderState = false;
    bool m_ScreenSpaceDebugInShaderState = false;
    bool m_HistoryValid = false;
    uint32_t m_HistoryPing = 0;
    uint32_t m_PendingHistoryPing = 0;
    uint32_t m_JitterSequenceIndex = 0;
    uint32_t m_PendingJitterSequenceIndex = 0;
    bool m_TemporalFramePending = false;
    Mat4 m_PreviousViewProjection = Mat4::Identity();
    Mat4 m_PendingViewProjection = Mat4::Identity();
    bool m_HasPreviousViewProjection = false;
    uint64_t m_CurrentFrameNumber = 0;
    uint64_t m_LastCommittedFrameNumber = 0;
    uint64_t m_ShadowSubmissionSerial = 0;
    bool m_HasCommittedFrameNumber = false;
    Vec3 m_PreviousCameraPosition{};
    Vec3 m_PreviousCameraForward{};
    Vec3 m_PendingCameraPosition{};
    Vec3 m_PendingCameraForward{};
    float m_PreviousFov = 0.0f;
    float m_PreviousNear = 0.0f;
    float m_PreviousFar = 0.0f;
    Mat4 m_PreviousProjection = Mat4::Identity();
    Mat4 m_PendingProjection = Mat4::Identity();
    bool m_HasPreviousProjection = false;
    ModernPostProcessSettings m_PreviousPostSettings;
    ModernPostProcessSettings m_PendingPostSettings;
    std::string m_HistoryResetReason = "first frame";
    std::string m_LastShadowSetupError;
    static constexpr uint64_t kDiagnosticsReadbackInterval = 30;
    static constexpr uint64_t kShadowStreamRetireSubmissions = 3;
};
