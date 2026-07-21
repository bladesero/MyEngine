#include "Renderer/Renderer.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Core/EngineTime.h"
#include "Core/FrameStats.h"
#include "Core/Logger.h"
#include "Renderer/EnvironmentPass.h"
#include "Renderer/DeferredLightingPass.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/GBufferPass.h"
#include "Renderer/GpuUploadQueue.h"
#include "Renderer/LightComponent.h"
#include "Renderer/MainPass.h"
#include "Renderer/ModernDeferredPipeline.h"
#include "Renderer/ProbeLightingSystem.h"
#include "Renderer/PostProcessPass.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/ShaderManager.h"
#include "Renderer/ShadowPass.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/ScreenUIPass.h"
#include "Renderer/SceneLighting.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "UI/Render/UIDrawList.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace {
struct PostProcessRuntimeOptions {
    bool ssaoEnabled = false;
    float ssaoScale = 1.0f;
    ModernPostProcessSettings modern;
};

PostProcessRuntimeOptions CollectPostProcessOptions(const Scene& scene) {
    PostProcessRuntimeOptions options;
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive())
            return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled())
            return;
        options.ssaoEnabled = post->GetSSAOIntensity() > 0.0f;
        options.ssaoScale = post->GetSSAOScale();
        options.modern.ssaoRadius = post->GetSSAORadius();
        options.modern.ssaoBias = post->GetSSAOBias();
        options.modern.ssaoPower = post->GetSSAOPower();
        options.modern.ssaoIntensity = post->GetSSAOIntensity();
        options.modern.ssaoScale = post->GetSSAOScale();
        options.modern.rayTracedShadowReplacement = post->UsesRayTracedShadowReplacement();
        options.modern.rayTracedAOReplacement = post->UsesRayTracedAOReplacement();
        options.modern.rayTracedReflectionReplacement = post->UsesRayTracedReflectionReplacement();
        options.modern.rayTracedDiffuseReplacement = post->UsesRayTracedDiffuseReplacement();
        options.modern.ssgiEnabled = post->IsSSGIEnabled();
        options.modern.ssrEnabled = post->IsSSREnabled();
        options.modern.taaEnabled = post->IsTAAEnabled();
        options.modern.ssgiIntensity = post->GetSSGIIntensity();
        options.modern.ssgiMaxDistance = post->GetSSGIMaxDistance();
        options.modern.ssgiHistoryWeight = post->GetSSGIHistoryWeight();
        options.modern.ssgiStepCount = post->GetSSGIStepCount();
        options.modern.ssgiFilterRounds = post->GetSSGIFilterRounds();
        options.modern.ssrMaxDistance = post->GetSSRMaxDistance();
        options.modern.ssrMaxRoughness = post->GetSSRMaxRoughness();
        options.modern.ssrHistoryWeight = post->GetSSRHistoryWeight();
        options.modern.ssrStepCount = post->GetSSRStepCount();
        options.modern.ssrFilterRounds = post->GetSSRFilterRounds();
        options.modern.taaHistoryWeight = post->GetTAAHistoryWeight();
        options.modern.taaJitterSpread = post->GetTAAJitterSpread();
        options.modern.taaHistoryClipExpansion = post->GetTAAHistoryClipExpansion();
        options.modern.exposure = post->GetExposure();
        options.modern.gamma = post->GetGamma();
        options.modern.bloomThreshold = post->GetBloomThreshold();
        options.modern.bloomIntensity = post->IsBloomEnabled() ? post->GetBloomIntensity() : 0.0f;
        found = true;
    });
    return options;
}

ModernDeferredPipeline::ScreenSpaceDebugMode ResolveModernScreenSpaceDebugMode(RendererDebugView view) {
    switch (view) {
    case RendererDebugView::SSGI:
        return ModernDeferredPipeline::ScreenSpaceDebugMode::SSGI;
    case RendererDebugView::SSRConfidence:
        return ModernDeferredPipeline::ScreenSpaceDebugMode::SSRConfidence;
    case RendererDebugView::TAAHistoryAge:
        return ModernDeferredPipeline::ScreenSpaceDebugMode::TAAHistoryAge;
    case RendererDebugView::TAARejectReason:
        return ModernDeferredPipeline::ScreenSpaceDebugMode::TAARejectReason;
    default:
        return ModernDeferredPipeline::ScreenSpaceDebugMode::None;
    }
}

Vec3 CollectEnvironmentSunDirection(const Scene& scene) {
    Vec3 sunDirection = EnvironmentPass::DefaultSunDirection();
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive())
            return;
        auto* light = actor.GetComponent<LightComponent>();
        if (!light || !light->IsEnabled() || light->GetLightType() != LightType::Directional) {
            return;
        }
        const Vec3 lightDirection = light->GetDirection();
        if (lightDirection.LengthSq() > 1e-8f) {
            sunDirection = (-lightDirection).Normalized();
            found = true;
        }
    });
    return sunDirection;
}

float ElapsedMs(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - start).count();
}

void CollectMaterialShaderPaths(const MaterialAsset* material,
                                std::unordered_set<const MaterialAsset*>& visitedMaterials,
                                std::unordered_set<std::string>& uniqueShaders, std::vector<std::string>& shaders) {
    if (!material || !visitedMaterials.insert(material).second)
        return;
    if (const ShaderAssetHandle& shader = material->GetShaderAsset(); shader.IsValid()) {
        const std::string path = AssetManager::Get().MakeProjectRelativePath(shader->GetPath());
        if (!path.empty() && uniqueShaders.insert(path).second)
            shaders.push_back(path);
    }
    if (material->HasParent()) {
        const MaterialHandle parent = AssetManager::Get().Load<MaterialAsset>(material->GetParentPath());
        if (parent.IsValid())
            CollectMaterialShaderPaths(parent.Get(), visitedMaterials, uniqueShaders, shaders);
    }
}

std::vector<std::string> CollectSceneShaderPaths(const Scene& scene) {
    std::unordered_set<const MaterialAsset*> visitedMaterials;
    std::unordered_set<std::string> uniqueShaders;
    std::vector<std::string> shaders;
    scene.ForEach([&](Actor& actor) {
        if (const auto* renderer = actor.GetComponent<MeshRendererComponent>()) {
            for (const MaterialHandle& material : renderer->GetMaterials())
                CollectMaterialShaderPaths(material.Get(), visitedMaterials, uniqueShaders, shaders);
        }
        if (const auto* skinned = actor.GetComponent<SkinnedMeshRendererComponent>()) {
            const MaterialHandle material = skinned->GetMaterial();
            CollectMaterialShaderPaths(material.Get(), visitedMaterials, uniqueShaders, shaders);
        }
    });
    return shaders;
}
} // namespace

Renderer::Renderer(IRHIDevice* device, IRHIFrameContext* frameContext, IRHIReadbackService* readbackService)
    : m_Device(device), m_FrameContext(frameContext), m_ReadbackService(readbackService),
      m_ShadowPass(std::make_unique<ShadowPass>(device)),
      m_EnvironmentPass(std::make_unique<EnvironmentPass>(device, readbackService)),
      m_MainPass(std::make_unique<MainPass>(device)), m_GBufferPass(std::make_unique<GBufferPass>(device)),
      m_DeferredLightingPass(std::make_unique<DeferredLightingPass>(device)),
      m_PostProcessPass(std::make_unique<PostProcessPass>(device)),
      m_ScreenUIPass(std::make_unique<ScreenUIPass>(device)),
      m_ProbeLightingSystem(std::make_unique<ProbeLightingSystem>(device)),
      m_RenderGraph(device ? std::make_unique<RenderGraph>(*device) : nullptr) {
    ShaderManager::Get().SetDevice(device);
    RefreshPipelineDiagnostics();
}

Renderer::~Renderer() = default;

void Renderer::RefreshPipelineDiagnostics() {
    const RHIBackend backend = m_Device ? m_Device->GetBackend() : RHIBackend::Unknown;
    const RHIDeviceCapabilities capabilities = m_Device ? m_Device->GetCapabilities() : RHIDeviceCapabilities{};
    // Before the first Modern frame, report the capability-resolved path without constructing its shaders. Once an
    // initialization attempt fails, the same diagnostics switch to Classic and retain the concrete failure reason.
    const bool implementationAvailable = m_ModernImplementationReady || !m_ModernInitializationAttempted;
    m_PipelineDiagnostics =
        ResolveRenderPipeline(m_RenderPath, m_DeviceProfile, backend, capabilities, implementationAvailable);
    if (m_ModernInitializationAttempted && !m_ModernImplementationReady && m_ModernDeferredPipeline &&
        !m_ModernDeferredPipeline->GetInitializationError().empty()) {
        m_PipelineDiagnostics.fallbackReason = m_ModernDeferredPipeline->GetInitializationError();
        m_PipelineDiagnostics.usedFallback = m_RenderPath == RenderPath::Deferred;
    }
}

void Renderer::EnsureModernPipeline() {
    if (m_ModernInitializationAttempted || m_RenderPath != RenderPath::Deferred ||
        m_DeviceProfile == GraphicsDeviceProfile::Mobile || !m_Device ||
        !HasModernDeferredCapabilities(m_Device->GetBackend(), m_Device->GetCapabilities())) {
        return;
    }

    m_ModernInitializationAttempted = true;
    const auto start = std::chrono::steady_clock::now();
    m_ModernDeferredPipeline = std::make_unique<ModernDeferredPipeline>(m_Device, m_ReadbackService);
    m_ModernImplementationReady = m_ModernDeferredPipeline->IsReady();
    if (m_ModernImplementationReady) {
        m_ModernDeferredPipeline->SetHardwareRayTracingEnabled(m_HardwareRayTracingEnabled);
        m_ModernDeferredPipeline->SetQualityProfile(m_DeviceProfile == GraphicsDeviceProfile::Console
                                                        ? ModernDeferredPipeline::QualityProfile::Console
                                                        : ModernDeferredPipeline::QualityProfile::Desktop);
        m_ModernDeferredPipeline->Resize(m_Width, m_Height);
        Logger::Info("[Renderer] Modern Deferred initialized on first use in ",
                     ElapsedMs(start, std::chrono::steady_clock::now()), " ms");
    } else {
        Logger::Warn("[Renderer] Modern Deferred initialization failed; Classic Deferred will be used: ",
                     m_ModernDeferredPipeline->GetInitializationError());
    }
    RefreshPipelineDiagnostics();
}

bool Renderer::PrewarmStartupShaders(const Scene& scene) {
    if (!m_Device)
        return true;

    constexpr uint8_t kCommonShaders = 1u << 0;
    constexpr uint8_t kDeferredCoreShaders = 1u << 1;
    constexpr uint8_t kClassicDeferredEffects = 1u << 2;
    constexpr uint8_t kModernDeferredShaders = 1u << 3;
    constexpr uint8_t kModernRayTracingShaders = 1u << 4;
    uint8_t required = kCommonShaders;
    if (m_RenderPath == RenderPath::Deferred) {
        // Modern still constructs the Classic compatibility/post-process passes for legacy Code Shaders and the
        // SSAO pressure fallback, so prepare those artifacts in the same background batch.
        required |= kDeferredCoreShaders | kClassicDeferredEffects;
        if (m_DeviceProfile != GraphicsDeviceProfile::Mobile &&
            HasModernDeferredCapabilities(m_Device->GetBackend(), m_Device->GetCapabilities())) {
            required |= kModernDeferredShaders;
            if (m_HardwareRayTracingEnabled && m_Device->GetCapabilities().inlineRayQueries)
                required |= kModernRayTracingShaders;
        }
    }
    const uint8_t missing = required & ~m_ShaderPrewarmMask;
    const uint64_t sceneGeneration = scene.GetLifetimeGeneration();
    if (m_ShaderPrewarmSceneGeneration != sceneGeneration) {
        m_ShaderPrewarmSceneGeneration = sceneGeneration;
        m_SceneShaderPrewarmComplete = false;
        m_SceneShaderPrewarmPaths = CollectSceneShaderPaths(scene);
    }
    if (missing == 0 && m_SceneShaderPrewarmComplete)
        return true;

    std::vector<std::string> shaders;
    if ((missing & kCommonShaders) != 0) {
        shaders.insert(shaders.end(),
                       {EngineShaders::kShadowDepth, EngineShaders::kShadowDepthSkinned,
                        EngineShaders::kAtmosphereCubemap, EngineShaders::kEnvironmentMipmap,
                        EngineShaders::kAtmosphereSH, EngineShaders::kShadowedMainPass, EngineShaders::kPostProcessFXAA,
                        EngineShaders::kProceduralSky, EngineShaders::kScreenUI, EngineShaders::kMesh});
    }
    if ((missing & kDeferredCoreShaders) != 0) {
        shaders.insert(shaders.end(), {EngineShaders::kGBuffer, EngineShaders::kDeferredLighting});
    }
    if ((missing & kClassicDeferredEffects) != 0) {
        shaders.insert(shaders.end(), {EngineShaders::kPostProcessSSAO, EngineShaders::kPostProcessSSAOBlur});
    }
    if ((missing & kModernDeferredShaders) != 0) {
        shaders.insert(shaders.end(),
                       {EngineShaders::kModernCulling, EngineShaders::kModernOcclusionCulling,
                        EngineShaders::kModernDepth, EngineShaders::kModernGBuffer, EngineShaders::kModernHiZInit,
                        EngineShaders::kModernHiZReduce, EngineShaders::kClusterCount, EngineShaders::kClusterPrefix,
                        EngineShaders::kClusterScatter, EngineShaders::kClusterLighting,
                        EngineShaders::kModernSSGITrace, EngineShaders::kModernSSRTrace, EngineShaders::kModernTemporal,
                        EngineShaders::kModernAtrous, EngineShaders::kModernEffectsComposite, EngineShaders::kModernTAA,
                        EngineShaders::kModernBloomTone});
    }
    if ((missing & kModernRayTracingShaders) != 0) {
        shaders.insert(shaders.end(), {EngineShaders::kModernRTShadow, EngineShaders::kModernRTAO,
                                       EngineShaders::kModernRTDiffuse, EngineShaders::kModernRTReflection});
    }
    if (!m_SceneShaderPrewarmComplete)
        shaders.insert(shaders.end(), m_SceneShaderPrewarmPaths.begin(), m_SceneShaderPrewarmPaths.end());
    const ShaderPrewarmStatus status = ShaderManager::Get().PrewarmCacheArtifactsAsync(shaders);
    if (status == ShaderPrewarmStatus::Ready) {
        m_ShaderPrewarmMask |= missing;
        m_SceneShaderPrewarmComplete = true;
        return true;
    }
    if (status == ShaderPrewarmStatus::Failed) {
        // Failed artifacts stay cached until a shader is explicitly recompiled, so proceeding cannot trigger a
        // synchronous render-thread cook. Modern initialization can now fail fast and resolve to Classic Deferred
        // instead of leaving the viewport on a permanent clear frame while retrying slangc every frame.
        m_ShaderPrewarmMask |= missing;
        m_SceneShaderPrewarmComplete = true;
        Logger::Warn("[Renderer] Startup shader prewarm was incomplete; continuing with available shaders so the ",
                     "resolved pipeline can fall back without blocking the viewport");
        return true;
    }
    return false;
}

void Renderer::SetRenderPath(RenderPath path) {
    if (m_RenderPath == path)
        return;
    m_RenderPath = path;
    RefreshPipelineDiagnostics();
    InvalidateTemporalHistory("render path changed", true);
}

void Renderer::SetDeviceProfile(GraphicsDeviceProfile profile) {
    if (m_DeviceProfile == profile)
        return;
    m_DeviceProfile = profile;
    if (m_ModernDeferredPipeline) {
        m_ModernDeferredPipeline->SetQualityProfile(profile == GraphicsDeviceProfile::Console
                                                        ? ModernDeferredPipeline::QualityProfile::Console
                                                        : ModernDeferredPipeline::QualityProfile::Desktop);
    }
    RefreshPipelineDiagnostics();
    InvalidateTemporalHistory("device profile changed", true);
    if (m_PipelineDiagnostics.usedFallback) {
        Logger::Warn("[Renderer] Requested ", GraphicsDeviceProfileName(profile), " profile resolved to ",
                     ResolvedRenderPipelineName(m_PipelineDiagnostics.resolvedPipeline), ": ",
                     m_PipelineDiagnostics.fallbackReason);
    }
}

void Renderer::SetHardwareRayTracingEnabled(bool enabled) {
    if (m_HardwareRayTracingEnabled == enabled)
        return;
    m_HardwareRayTracingEnabled = enabled;
    m_ShaderPrewarmMask &= static_cast<uint8_t>(~(1u << 4u));
    if (m_ModernDeferredPipeline)
        m_ModernDeferredPipeline->SetHardwareRayTracingEnabled(enabled);
    InvalidateTemporalHistory("hardware ray tracing setting changed", false);
}

uint32_t Renderer::GetRayTracingRequestedMask() const {
    return m_ModernDeferredPipeline ? m_ModernDeferredPipeline->GetRayTracingRequestedMask() : 0u;
}

uint32_t Renderer::GetRayTracingEffectiveMask() const {
    return m_ModernDeferredPipeline ? m_ModernDeferredPipeline->GetRayTracingEffectiveMask() : 0u;
}

std::string Renderer::GetRayTracingFallbackReason() const {
    return m_ModernDeferredPipeline ? m_ModernDeferredPipeline->GetRayTracingFallbackReason()
                                    : "Modern Deferred pipeline is unavailable";
}

void Renderer::Resize(uint32_t width, uint32_t height) {
    m_Width = (std::max)(width, 1u);
    m_Height = (std::max)(height, 1u);
    if (m_ShadowPass)
        m_ShadowPass->Resize(width, height);
    if (m_EnvironmentPass)
        m_EnvironmentPass->Resize(width, height);
    if (m_MainPass)
        m_MainPass->Resize(width, height);
    if (m_GBufferPass)
        m_GBufferPass->Resize(width, height);
    if (m_DeferredLightingPass)
        m_DeferredLightingPass->Resize(width, height);
    if (m_PostProcessPass)
        m_PostProcessPass->Resize(width, height);
    if (m_ScreenUIPass)
        m_ScreenUIPass->Resize(width, height);
    if (m_ModernDeferredPipeline)
        m_ModernDeferredPipeline->Resize(width, height);
}

void Renderer::ReleaseFrameResources() {
    if (m_RenderGraph)
        m_RenderGraph->Reset();
}

void Renderer::RenderScene(const Scene& scene, const Camera& camera, bool present) {
    FrameStatsProvider::SetRendererStats({});
    if (!PrewarmStartupShaders(scene)) {
        // Shader cooking is CPU-only and runs off the render thread. Keep the window/editor responsive with a valid
        // clear frame; the next frame consumes the completed artifacts and creates GPU objects on the render thread.
        if (m_FrameContext) {
            m_FrameContext->BeginFrame(0.12f, 0.12f, 0.18f);
            if (present)
                m_FrameContext->EndFrame();
        }
        return;
    }
    EnsureModernPipeline();
    RefreshPipelineDiagnostics();
    if (!m_Device || !m_FrameContext || !m_ShadowPass || !m_MainPass || !m_GBufferPass || !m_DeferredLightingPass ||
        !m_PostProcessPass)
        return;

    GpuUploadQueue::Get().Process(*m_Device, GpuUploadQueue::Get().GetDefaultBudget());

    m_FrameContext->BeginFrame(0.12f, 0.12f, 0.18f);
    const auto submissionStart = std::chrono::steady_clock::now();
    const auto endFrameOnFailure = [this, present]() {
        if (m_ModernDeferredPipeline)
            m_ModernDeferredPipeline->AbortTemporalFrame("render graph frame aborted");
        if (present)
            m_FrameContext->EndFrame();
    };
    GpuCommandList* commandList = m_FrameContext->GetGraphicsCommandList();
    if (!commandList) {
        endFrameOnFailure();
        return;
    }

    const uint32_t timestampSlot = m_FrameContext->GetFrameIndex() % m_FrameTimestampPools.size();
    auto& timestampPool = m_FrameTimestampPools[timestampSlot];
    if (!timestampPool && m_Device->GetCapabilities().timestampQueries)
        timestampPool = m_Device->CreateTimestampQueryPool(2);
    if (timestampPool && m_FrameTimestampRecorded[timestampSlot]) {
        std::vector<uint64_t> ticks;
        if (timestampPool->ReadResults(0, 2, ticks) && ticks.size() == 2 && ticks[1] >= ticks[0] &&
            timestampPool->GetFrequency() > 0) {
            RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
            stats.mainGpuMs = static_cast<float>(static_cast<double>(ticks[1] - ticks[0]) * 1000.0 /
                                                 static_cast<double>(timestampPool->GetFrequency()));
            stats.gpuTimingAvailable = true;
            FrameStatsProvider::SetRendererStats(stats);
        }
    }
    if (timestampPool)
        commandList->WriteTimestamp(timestampPool.get(), 0);

    m_RenderGraph->Reset();
    if (m_ProbeLightingSystem && !m_ProbeLightingSystem->Prepare(scene) &&
        !m_ProbeLightingSystem->GetLastError().empty())
        Logger::Warn("[Renderer] local lighting probes unavailable; using global environment: ",
                     m_ProbeLightingSystem->GetLastError());
    if (m_ProbeLightingSystem) {
        const uint32_t mipCount = m_ProbeLightingSystem->GetReflectionTexture()
                                      ? m_ProbeLightingSystem->GetReflectionTexture()->desc.mipLevels
                                      : 1u;
        m_MainPass->SetProbeInput(
            m_ProbeLightingSystem->GetReflectionTextureView(), m_ProbeLightingSystem->GetReflectionMetadataView(),
            m_ProbeLightingSystem->GetSHVolumeMetadataView(), m_ProbeLightingSystem->GetSHCoefficientView(),
            m_ProbeLightingSystem->GetReflectionProbeCount(), m_ProbeLightingSystem->GetSHVolumeCount(), mipCount);
        m_DeferredLightingPass->SetProbeInput(
            m_ProbeLightingSystem->GetReflectionTextureView(), m_ProbeLightingSystem->GetReflectionMetadataView(),
            m_ProbeLightingSystem->GetSHVolumeMetadataView(), m_ProbeLightingSystem->GetSHCoefficientView(),
            m_ProbeLightingSystem->GetReflectionProbeCount(), m_ProbeLightingSystem->GetSHVolumeCount(), mipCount);
        if (m_ModernDeferredPipeline)
            m_ModernDeferredPipeline->SetProbeInput(
                m_ProbeLightingSystem->GetReflectionTextureView(), m_ProbeLightingSystem->GetReflectionMetadataView(),
                m_ProbeLightingSystem->GetSHVolumeMetadataView(), m_ProbeLightingSystem->GetSHCoefficientView(),
                m_ProbeLightingSystem->GetReflectionProbeCount(), m_ProbeLightingSystem->GetSHVolumeCount(), mipCount);
    }
    const SceneEnvironmentData environment = CollectSceneEnvironmentData(scene);
    if (environment.activeSkylightCount > 1) {
        if (!m_SkylightConflictLogged) {
            Logger::Warn("[Renderer] Multiple active Skylight components found; using actor ",
                         environment.sourceActorID, " and ignoring ", environment.activeSkylightCount - 1,
                         " additional component(s)");
            m_SkylightConflictLogged = true;
        }
    } else {
        m_SkylightConflictLogged = false;
    }
    const Vec3 environmentSunDirection = CollectEnvironmentSunDirection(scene);
    m_EnvironmentPass->SetSunDirection(environmentSunDirection);
    m_EnvironmentPass->SetEnvironmentSettings(environment);
    m_MainPass->SetSunDirection(environmentSunDirection);
    m_MainPass->SetEnvironmentSettings(environment);
    const bool backendSupportsPostProcess =
        m_Device->GetBackend() == RHIBackend::D3D11 || m_Device->GetBackend() == RHIBackend::D3D12 ||
        m_Device->GetBackend() == RHIBackend::Metal || m_Device->GetBackend() == RHIBackend::Vulkan;
    const bool useOffscreen = backendSupportsPostProcess || m_OutputOffscreen;
    const bool useDeferred = useOffscreen && m_PipelineDiagnostics.resolvedPipeline != ResolvedRenderPipeline::Forward;
    const bool modernRequested =
        useDeferred && m_PipelineDiagnostics.resolvedPipeline == ResolvedRenderPipeline::ModernDeferred;
    const SceneLightData sceneLights = CollectSceneLights(scene, environment);
    PostProcessRuntimeOptions postOptions = CollectPostProcessOptions(scene);
    if (!HasRendererFeature(m_FeatureMask, RendererFeatureMask::SSAO))
        postOptions.modern.ssaoIntensity = 0.0f;
    bool modernFrameReady = false;
    if (modernRequested) {
        modernFrameReady = m_ModernDeferredPipeline &&
                           m_ModernDeferredPipeline->Prepare(scene, camera, Time::FrameCount(), postOptions.modern);
        if (!modernFrameReady) {
            Logger::Warn("[Renderer] Modern Deferred frame preparation failed; using Classic Deferred for this "
                         "frame: ",
                         m_ModernDeferredPipeline ? m_ModernDeferredPipeline->GetInitializationError()
                                                  : "pipeline unavailable");
        }
    }
    const bool shadowsEnabled = HasRendererFeature(m_FeatureMask, RendererFeatureMask::Shadows);
    bool rayTracedDirectionalShadow = false;
    RGTextureHandle directionalShadow;
    RGTextureHandle spotShadow;
    RGTextureHandle pointShadow;
    std::shared_ptr<GpuTextureView> directionalShadowSrv;
    if (shadowsEnabled) {
        if (!m_ShadowPass->PrepareGraphResources(scene, camera)) {
            Logger::Error("[Renderer] ShadowPass failed to prepare graph resources");
            endFrameOnFailure();
            return;
        }
        const auto shadowResources = m_ShadowPass->GetGraphResources();
        rayTracedDirectionalShadow =
            modernFrameReady && m_ModernDeferredPipeline->ConfigureRayTracedShadow(
                                    m_ShadowPass->IsDirectionalShadowEnabled(), sceneLights.directionalShadowIntensity);
        directionalShadowSrv = shadowResources.directionalSrv;
        const auto importShadowResources = [&]() {
            directionalShadow = m_RenderGraph->ImportTexture(
                "DirectionalShadow", shadowResources.directional, shadowResources.directionalCascadeViews[0],
                shadowResources.initialState, RHIResourceState::ShaderResource);
            spotShadow = m_RenderGraph->ImportTexture("SpotShadow", shadowResources.spot, shadowResources.spotView,
                                                      shadowResources.initialState, RHIResourceState::ShaderResource);
            pointShadow =
                m_RenderGraph->ImportTexture("PointShadow", shadowResources.point, shadowResources.pointViews[0],
                                             shadowResources.initialState, RHIResourceState::ShaderResource);
        };
        importShadowResources();

        const auto addCpuShadowPass = [&](const char* passName) {
            m_RenderGraph->AddPass(
                passName,
                [directionalShadow, spotShadow, pointShadow, rayTracedDirectionalShadow](RenderGraphBuilder& builder) {
                    for (uint32_t cascade = 0; cascade < 3; ++cascade) {
                        builder.WriteDepth(directionalShadow, RGTextureSubresource{0, 1, cascade, 1}, RHILoadOp::Clear,
                                           RHIStoreOp::Store, 1.0f);
                    }
                    builder.WriteDepth(spotShadow, RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
                    for (uint32_t face = 0; face < 6; ++face) {
                        builder.WriteDepth(pointShadow, RGTextureSubresource{0, 1, face, 1}, RHILoadOp::Clear,
                                           RHIStoreOp::Store, 1.0f);
                    }
                },
                [this, &scene, rayTracedDirectionalShadow](GpuCommandList& commands, const RenderGraphResources&) {
                    const auto start = std::chrono::steady_clock::now();
                    m_ShadowPass->ExecuteGraphManaged(commands, scene, !rayTracedDirectionalShadow);
                    const auto end = std::chrono::steady_clock::now();
                    RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                    stats.shadowCpuMs += ElapsedMs(start, end);
                    const auto& shadowStats = m_ShadowPass->GetLastStats();
                    stats.shadowDrawCalls = shadowStats.drawCalls;
                    stats.bindGroupCreates += shadowStats.bindGroupCreates;
                    FrameStatsProvider::SetRendererStats(stats);
                },
                RenderGraph::PassFlags::ManualRenderingScope | RenderGraph::PassFlags::ManualResourceTransitions);
        };

        if (modernFrameReady) {
            m_ShadowPass->BeginGpuDrivenFrame();
            const bool hasCompatibilityObjects =
                m_ModernDeferredPipeline->GetGpuScene().GetStats().compatibilityObjects != 0;
            bool gpuShadowReady = true;
            const auto addInactiveShadowClear = [&](const std::string& name, RGTextureHandle target,
                                                    RGTextureSubresource subresource) {
                if (shadowResources.initialState != RHIResourceState::Undefined)
                    return;
                m_RenderGraph->AddPass(name,
                                       [target, subresource](RenderGraphBuilder& builder) {
                                           builder.WriteDepth(target, subresource, RHILoadOp::Clear, RHIStoreOp::Store,
                                                              1.0f);
                                       },
                                       {});
            };
            const auto addGpuShadowView = [&](const std::string& name, RGTextureHandle target,
                                              RGTextureSubresource subresource, const Mat4& viewProjection) {
                if (!gpuShadowReady)
                    return;
                if (!m_ModernDeferredPipeline->AddGpuDrivenShadowView(*m_RenderGraph, name, target, subresource,
                                                                      viewProjection)) {
                    gpuShadowReady = false;
                    return;
                }
                if (!hasCompatibilityObjects)
                    return;
                m_RenderGraph->AddPass(
                    name + "Compatibility",
                    [target, subresource](RenderGraphBuilder& builder) {
                        builder.WriteDepth(target, subresource, RHILoadOp::Load, RHIStoreOp::Store, 1.0f);
                    },
                    [this, &scene, viewProjection](GpuCommandList& commands, const RenderGraphResources&) {
                        const auto start = std::chrono::steady_clock::now();
                        m_ShadowPass->DrawCompatibilityScene(commands, scene, viewProjection);
                        RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                        stats.shadowCpuMs += ElapsedMs(start, std::chrono::steady_clock::now());
                        const auto& shadowStats = m_ShadowPass->GetLastStats();
                        stats.shadowDrawCalls = shadowStats.drawCalls;
                        stats.bindGroupCreates += shadowStats.bindGroupCreates;
                        FrameStatsProvider::SetRendererStats(stats);
                    });
            };
            if (m_ShadowPass->IsDirectionalShadowEnabled() && !rayTracedDirectionalShadow) {
                for (uint32_t cascade = 0; cascade < m_ShadowPass->GetCascadeCount(); ++cascade) {
                    addGpuShadowView("GpuShadowCascade" + std::to_string(cascade), directionalShadow,
                                     RGTextureSubresource{0, 1, cascade, 1}, m_ShadowPass->GetCascadeViewProj(cascade));
                }
            }
            for (uint32_t cascade = m_ShadowPass->IsDirectionalShadowEnabled() && !rayTracedDirectionalShadow
                                        ? m_ShadowPass->GetCascadeCount()
                                        : 0u;
                 cascade < 3u; ++cascade) {
                addInactiveShadowClear("GpuShadowCascadeInit" + std::to_string(cascade), directionalShadow,
                                       RGTextureSubresource{0, 1, cascade, 1});
            }
            if (m_ShadowPass->GetSpotShadowIndex() >= 0) {
                addGpuShadowView("GpuShadowSpot", spotShadow, RGTextureSubresource{0, 1, 0, 1},
                                 m_ShadowPass->GetSpotLightViewProj());
            } else {
                addInactiveShadowClear("GpuShadowSpotInit", spotShadow, RGTextureSubresource{0, 1, 0, 1});
            }
            if (m_ShadowPass->GetPointShadowIndex() >= 0) {
                for (uint32_t face = 0; face < 6; ++face) {
                    addGpuShadowView("GpuShadowPoint" + std::to_string(face), pointShadow,
                                     RGTextureSubresource{0, 1, face, 1}, m_ShadowPass->GetPointLightViewProj(face));
                }
            } else {
                for (uint32_t face = 0; face < 6; ++face) {
                    addInactiveShadowClear("GpuShadowPointInit" + std::to_string(face), pointShadow,
                                           RGTextureSubresource{0, 1, face, 1});
                }
            }
            if (!gpuShadowReady) {
                Logger::Warn("[Renderer] GPU-driven shadow setup failed; using CPU shadows for this frame: ",
                             m_ModernDeferredPipeline->GetLastShadowSetupError());
                // Shadows are the first graph workload. Rebuild this portion so no already-staged GPU pass can clear
                // an atlas or commit an indirect-buffer state after one of the later views failed validation.
                m_ModernDeferredPipeline->AbortGpuDrivenShadowFrame();
                m_RenderGraph->Reset();
                importShadowResources();
                addCpuShadowPass("ShadowFallback");
            }
        } else {
            addCpuShadowPass("Shadow");
        }
    } else if (modernFrameReady) {
        m_ModernDeferredPipeline->ConfigureRayTracedShadow(false, sceneLights.directionalShadowIntensity);
    }
    const bool environmentGraphReady = m_EnvironmentPass->PrepareGraphResources();
    RGTextureHandle environmentCube;
    RGBufferHandle environmentSH;
    std::shared_ptr<GpuTextureView> environmentCubeSrv;
    std::shared_ptr<GpuBufferView> environmentSHSrv;
    if (environmentGraphReady) {
        const auto environmentResources = m_EnvironmentPass->GetGraphResources();
        environmentCubeSrv = environmentResources.environmentView;
        environmentSHSrv = environmentResources.shBufferView;
        environmentCube = m_RenderGraph->ImportTexture(
            "EnvironmentCube", environmentResources.environment, environmentResources.environmentView,
            environmentResources.environmentInitialState, RHIResourceState::ShaderResource);
        environmentSH =
            m_RenderGraph->ImportBuffer("EnvironmentSH2", environmentResources.shBuffer,
                                        environmentResources.shInitialState, RHIResourceState::ShaderResource);
        m_RenderGraph->AddPass(
            "Environment",
            [environmentCube, environmentSH, generated = environmentResources.generated](RenderGraphBuilder& builder) {
                if (generated) {
                    builder.ReadTexture(environmentCube);
                    builder.ReadBuffer(environmentSH);
                } else {
                    builder.WriteColor(environmentCube, RHILoadOp::Clear, RHIStoreOp::Store, {0.0f, 0.0f, 0.0f, 1.0f});
                    builder.ReadWriteUAV(environmentSH);
                }
            },
            [this](GpuCommandList& commands, const RenderGraphResources&) {
                m_EnvironmentPass->ExecuteGraphManaged(commands);
            },
            RenderGraph::PassFlags::ManualRenderingScope | RenderGraph::PassFlags::ManualResourceTransitions);
    }
    m_MainPass->SetHdrPassthrough(useOffscreen);
    const bool ssaoEnabled = HasRendererFeature(m_FeatureMask, RendererFeatureMask::SSAO) && postOptions.ssaoEnabled;
    m_PostProcessPass->SetSSAOEnabled(ssaoEnabled);
    m_PostProcessPass->SetSSAOScale(postOptions.ssaoScale);
    uint32_t cascadeCount = 0;
    Mat4 cascades[3] = {Mat4::Identity(), Mat4::Identity(), Mat4::Identity()};
    if (shadowsEnabled) {
        cascadeCount = m_ShadowPass->GetCascadeCount();
        cascades[0] = m_ShadowPass->GetCascadeViewProj(0);
        cascades[1] = cascadeCount > 1 ? m_ShadowPass->GetCascadeViewProj(1) : cascades[0];
        cascades[2] = cascadeCount > 2 ? m_ShadowPass->GetCascadeViewProj(2) : cascades[0];
        m_MainPass->SetShadowInput(m_ShadowPass->GetLightViewProj(), m_ShadowPass->GetLightDirection(),
                                   m_ShadowPass->IsDirectionalShadowEnabled() && !rayTracedDirectionalShadow,
                                   m_ShadowPass->GetShadowMapTexture(), m_ShadowPass->GetSpotLightViewProj(),
                                   m_ShadowPass->GetSpotShadowIndex(), m_ShadowPass->GetSpotShadowMapTexture(),
                                   m_ShadowPass->GetPointShadowPosition(), m_ShadowPass->GetPointShadowRange(),
                                   m_ShadowPass->GetPointShadowIndex(), m_ShadowPass->GetPointShadowMapTexture(),
                                   cascadeCount > 0 ? cascades : nullptr, cascadeCount,
                                   m_ShadowPass->GetCascadeSplits());
    } else {
        const Mat4 identity = Mat4::Identity();
        m_MainPass->SetShadowInput(identity, Vec3::Zero(), false, nullptr, identity, -1, nullptr, Vec3::Zero(), 1.0f,
                                   -1, nullptr, nullptr, 0, nullptr);
    }
    m_MainPass->SetEnvironmentInput(m_EnvironmentPass->GetEnvironmentCubemap(), m_EnvironmentPass->GetSH2BufferView(),
                                    m_EnvironmentPass->GetSH2Coefficients());
    if (useOffscreen) {
        if (!m_PostProcessPass->PrepareGraphResources()) {
            endFrameOnFailure();
            return;
        }
        const auto postResources = m_PostProcessPass->GetGraphResources();
        const auto sceneColor =
            m_RenderGraph->ImportTexture("SceneColor", postResources.sceneColor, postResources.sceneColorRtv,
                                         postResources.sceneColorState, RHIResourceState::ShaderResource);
        const auto sceneDepth =
            m_RenderGraph->ImportTexture("SceneDepth", postResources.sceneDepth, postResources.sceneDepthDsv,
                                         postResources.sceneDepthState, RHIResourceState::ShaderResource);
        // SSGI contributes indirect radiance; it does not produce an ambient-visibility term. Keep SSAO independent
        // and let Modern composite it into HDR before transparent rendering and temporal post-processing.
        const bool frameSsaoEnabled = ssaoEnabled;
        m_PostProcessPass->SetSSAOEnabled(frameSsaoEnabled);
        m_PostProcessPass->SetInputPreprocessed(modernFrameReady);
        RGTextureHandle ssao;
        RGTextureHandle ssaoBlur;
        if (frameSsaoEnabled) {
            ssao = m_RenderGraph->ImportTexture("SSAO", postResources.ssao, postResources.ssaoRtv,
                                                postResources.ssaoState, RHIResourceState::ShaderResource);
            ssaoBlur = m_RenderGraph->ImportTexture("SSAOBlur", postResources.ssaoBlur, postResources.ssaoBlurRtv,
                                                    postResources.ssaoBlurState, RHIResourceState::ShaderResource);
        }
        const auto addSsaoPasses = [&]() {
            m_RenderGraph->AddPass(
                "SSAO",
                [sceneDepth, ssao](RenderGraphBuilder& builder) {
                    builder.ReadTexture(sceneDepth);
                    builder.WriteColor(ssao, RHILoadOp::Clear, RHIStoreOp::Store, {1, 1, 1, 1});
                },
                [this, &scene, &camera, modernFrameReady](GpuCommandList& commands, const RenderGraphResources&) {
                    const auto start = std::chrono::steady_clock::now();
                    const Mat4* projection =
                        modernFrameReady ? &m_ModernDeferredPipeline->GetCurrentProjection() : nullptr;
                    m_PostProcessPass->DrawSSAOOcclusion(commands, scene, camera, projection);
                    const auto end = std::chrono::steady_clock::now();
                    RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                    stats.ssaoCpuMs += ElapsedMs(start, end);
                    ++stats.fullscreenDrawCalls;
                    FrameStatsProvider::SetRendererStats(stats);
                });
            m_RenderGraph->AddPass(
                "SSAOBlurH",
                [ssao, ssaoBlur](RenderGraphBuilder& builder) {
                    builder.ReadTexture(ssao);
                    builder.WriteColor(ssaoBlur, RHILoadOp::Clear, RHIStoreOp::Store, {1, 1, 1, 1});
                },
                [this](GpuCommandList& commands, const RenderGraphResources&) {
                    const auto start = std::chrono::steady_clock::now();
                    m_PostProcessPass->DrawSSAOBlurHorizontal(commands);
                    const auto end = std::chrono::steady_clock::now();
                    RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                    stats.ssaoCpuMs += ElapsedMs(start, end);
                    ++stats.fullscreenDrawCalls;
                    FrameStatsProvider::SetRendererStats(stats);
                });
            m_RenderGraph->AddPass(
                "SSAOBlurV",
                [ssaoBlur, ssao](RenderGraphBuilder& builder) {
                    builder.ReadTexture(ssaoBlur);
                    builder.WriteColor(ssao, RHILoadOp::Clear, RHIStoreOp::Store, {1, 1, 1, 1});
                },
                [this](GpuCommandList& commands, const RenderGraphResources&) {
                    const auto start = std::chrono::steady_clock::now();
                    m_PostProcessPass->DrawSSAOBlurVertical(commands);
                    const auto end = std::chrono::steady_clock::now();
                    RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                    stats.ssaoCpuMs += ElapsedMs(start, end);
                    ++stats.fullscreenDrawCalls;
                    FrameStatsProvider::SetRendererStats(stats);
                });
        };
        RGTextureHandle composite;
        const bool compositeToBackbuffer = m_PostProcessPass->IsCompositeToBackbuffer();
        RGTextureHandle backBuffer;
        GpuTextureView* backBufferView = nullptr;
        if (!compositeToBackbuffer) {
            composite = m_RenderGraph->ImportTexture("Composite", postResources.composite, postResources.compositeRtv,
                                                     postResources.compositeState, RHIResourceState::ShaderResource);
        } else {
            backBufferView = m_FrameContext->GetCurrentBackBufferView();
            if (!backBufferView || !backBufferView->texture) {
                Logger::Error("[Renderer] RHI returned no current backbuffer view");
                endFrameOnFailure();
                return;
            }
            auto backBufferSharedView = std::shared_ptr<GpuTextureView>(backBufferView->texture, backBufferView);
            backBuffer = m_RenderGraph->ImportTexture("BackBuffer", backBufferView->texture, backBufferSharedView,
                                                      RHIResourceState::RenderTarget, RHIResourceState::RenderTarget);
        }

        RGTextureHandle compositeInput = sceneColor;
        std::shared_ptr<GpuTextureView> compositeOverride;
        if (useDeferred) {
            if (!m_GBufferPass->PrepareGraphResources() || !m_DeferredLightingPass->PrepareGraphResources()) {
                endFrameOnFailure();
                return;
            }
            const auto gbufferResources = m_GBufferPass->GetGraphResources();
            const auto deferredResources = m_DeferredLightingPass->GetGraphResources();
            const auto gbufferAlbedo =
                m_RenderGraph->ImportTexture("GBufferAlbedo", gbufferResources.albedo, gbufferResources.albedoRtv,
                                             gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto gbufferNormal =
                m_RenderGraph->ImportTexture("GBufferNormal", gbufferResources.normal, gbufferResources.normalRtv,
                                             gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto gbufferMaterial =
                m_RenderGraph->ImportTexture("GBufferMaterial", gbufferResources.material, gbufferResources.materialRtv,
                                             gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto gbufferEmissive =
                m_RenderGraph->ImportTexture("GBufferEmissive", gbufferResources.emissive, gbufferResources.emissiveRtv,
                                             gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto gbufferVelocity =
                m_RenderGraph->ImportTexture("GBufferVelocity", gbufferResources.velocity, gbufferResources.velocityRtv,
                                             gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto deferredSceneColor = m_RenderGraph->ImportTexture(
                "DeferredSceneColor", deferredResources.sceneColor, deferredResources.sceneColorRtv,
                deferredResources.initialState, RHIResourceState::ShaderResource);
            compositeInput = deferredSceneColor;
            compositeOverride = deferredResources.sceneColorSrv;

            m_DeferredLightingPass->SetGBufferInput(gbufferResources.albedoSrv, gbufferResources.normalSrv,
                                                    gbufferResources.materialSrv, gbufferResources.emissiveSrv);
            m_DeferredLightingPass->SetDepthInput(postResources.sceneDepthSrv);
            m_DeferredLightingPass->SetLightingInput(sceneLights);
            if (shadowsEnabled) {
                m_DeferredLightingPass->SetShadowInput(
                    m_ShadowPass->GetLightViewProj(), m_ShadowPass->IsDirectionalShadowEnabled(),
                    m_ShadowPass->GetShadowMapTexture(), m_ShadowPass->GetSpotLightViewProj(),
                    m_ShadowPass->GetSpotShadowIndex(), m_ShadowPass->GetSpotShadowMapTexture(),
                    m_ShadowPass->GetPointShadowPosition(), m_ShadowPass->GetPointShadowRange(),
                    m_ShadowPass->GetPointShadowIndex(), m_ShadowPass->GetPointShadowMapTexture(),
                    cascadeCount > 0 ? cascades : nullptr, cascadeCount, m_ShadowPass->GetCascadeSplits());
            } else {
                const Mat4 identity = Mat4::Identity();
                m_DeferredLightingPass->SetShadowInput(identity, false, nullptr, identity, -1, nullptr, Vec3::Zero(),
                                                       1.0f, -1, nullptr, nullptr, 0, nullptr);
            }
            m_DeferredLightingPass->SetEnvironmentInput(m_EnvironmentPass->GetEnvironmentCubemap(),
                                                        m_EnvironmentPass->GetSH2BufferView());

            RGTextureHandle modernHiZ;
            if (modernFrameReady) {
                m_ModernDeferredPipeline->AddRayTracingBuildPass(*m_RenderGraph);
                m_ModernDeferredPipeline->AddDepthPrepass(*m_RenderGraph, sceneDepth);
                const Mat4 compatibilityViewProjection = m_ModernDeferredPipeline->GetCurrentViewProjection();
                const Mat4 compatibilityPreviousViewProjection = m_ModernDeferredPipeline->GetPreviousViewProjection();
                m_RenderGraph->AddPass(
                    "GBufferCompatibility",
                    [gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferEmissive, gbufferVelocity,
                     sceneDepth](RenderGraphBuilder& builder) {
                        // This is the first GBuffer writer. Clearing here lets compatibility depth become part of the
                        // same HiZ hierarchy used for Modern occlusion, SSGI and SSR; the indirect pass loads these
                        // attachments afterwards and fills Standard-material samples without erasing them.
                        builder.WriteColor(gbufferAlbedo, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 0});
                        builder.WriteColor(gbufferNormal, RHILoadOp::Clear, RHIStoreOp::Store,
                                           {0.5f, 0.5f, 1.0f, 1.0f});
                        builder.WriteColor(gbufferMaterial, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0.5f, 1.0f, 0});
                        builder.WriteColor(gbufferEmissive, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 0});
                        builder.WriteColor(gbufferVelocity, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 0});
                        builder.WriteDepth(sceneDepth, RHILoadOp::Load, RHIStoreOp::Store, 1.0f);
                    },
                    [this, &scene, &camera, compatibilityViewProjection,
                     compatibilityPreviousViewProjection](GpuCommandList& commands, const RenderGraphResources&) {
                        const auto start = std::chrono::steady_clock::now();
                        m_GBufferPass->ExecuteCompatibilityOnly(commands, scene, camera, compatibilityViewProjection,
                                                                compatibilityPreviousViewProjection);
                        const auto end = std::chrono::steady_clock::now();
                        RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                        stats.mainCpuMs += ElapsedMs(start, end);
                        FrameStatsProvider::SetRendererStats(stats);
                    });
                modernHiZ =
                    m_ModernDeferredPipeline->AddHiZPasses(*m_RenderGraph, sceneDepth, postResources.sceneDepthSrv);
                m_ModernDeferredPipeline->AddHiZOcclusionCulling(*m_RenderGraph, modernHiZ);
                m_ModernDeferredPipeline->AddGBufferPass(*m_RenderGraph, gbufferAlbedo, gbufferNormal, gbufferMaterial,
                                                         gbufferEmissive, gbufferVelocity, sceneDepth);
            } else {
                m_RenderGraph->AddPass(
                    "GBuffer",
                    [gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferEmissive, gbufferVelocity,
                     sceneDepth](RenderGraphBuilder& builder) {
                        builder.WriteColor(gbufferAlbedo, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 0});
                        builder.WriteColor(gbufferNormal, RHILoadOp::Clear, RHIStoreOp::Store,
                                           {0.5f, 0.5f, 1.0f, 1.0f});
                        builder.WriteColor(gbufferMaterial, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0.5f, 1.0f, 0});
                        builder.WriteColor(gbufferEmissive, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 0});
                        builder.WriteColor(gbufferVelocity, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 0});
                        builder.WriteDepth(sceneDepth, RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
                    },
                    [this, &scene, &camera](GpuCommandList& commands, const RenderGraphResources&) {
                        const auto start = std::chrono::steady_clock::now();
                        m_GBufferPass->Execute(commands, scene, camera);
                        const auto end = std::chrono::steady_clock::now();
                        RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                        stats.mainCpuMs += ElapsedMs(start, end);
                        FrameStatsProvider::SetRendererStats(stats);
                    });
            }

            if (modernFrameReady && frameSsaoEnabled &&
                (m_ModernDeferredPipeline->GetRayTracingEffectiveMask() & ModernRayTracingAO) == 0)
                addSsaoPasses();

            if (modernFrameReady) {
                m_ModernDeferredPipeline->SetDirectionalShadowInput(
                    shadowsEnabled && m_ShadowPass->IsDirectionalShadowEnabled() && !rayTracedDirectionalShadow,
                    directionalShadowSrv, cascadeCount > 0 ? cascades : nullptr, cascadeCount,
                    m_ShadowPass->GetCascadeSplits(), sceneLights.directionalShadowIntensity);
                RGTextureHandle modernDirectionalShadow = directionalShadow;
                if (rayTracedDirectionalShadow) {
                    const auto rtShadow = m_ModernDeferredPipeline->AddRayTracedShadowPass(
                        *m_RenderGraph, sceneDepth, postResources.sceneDepthSrv, gbufferNormal,
                        gbufferResources.normalSrv);
                    if (rtShadow.IsValid())
                        modernDirectionalShadow = rtShadow;
                }
                compositeInput = m_ModernDeferredPipeline->AddClusteredLightingPasses(
                    *m_RenderGraph, camera, gbufferAlbedo, gbufferResources.albedoSrv, gbufferNormal,
                    gbufferResources.normalSrv, gbufferMaterial, gbufferResources.materialSrv, gbufferEmissive,
                    gbufferResources.emissiveSrv, sceneDepth, postResources.sceneDepthSrv, environmentCube,
                    environmentCubeSrv, environmentSH, environmentSHSrv, modernDirectionalShadow);
                compositeOverride = m_ModernDeferredPipeline->GetHdrSrv();
                compositeInput = m_ModernDeferredPipeline->AddScreenSpaceEffects(
                    *m_RenderGraph, compositeInput, compositeOverride, sceneDepth, postResources.sceneDepthSrv,
                    gbufferAlbedo, gbufferResources.albedoSrv, gbufferNormal, gbufferResources.normalSrv,
                    gbufferMaterial, gbufferResources.materialSrv, gbufferVelocity, gbufferResources.velocitySrv,
                    modernHiZ, ssao, postResources.ssaoSrv, frameSsaoEnabled,
                    ResolveModernScreenSpaceDebugMode(m_DebugView));
                compositeOverride = m_ModernDeferredPipeline->GetEffectsHdrSrv();
            } else {
                m_RenderGraph->AddPass(
                    "DeferredLighting",
                    [deferredSceneColor, gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferEmissive, sceneDepth,
                     shadowsEnabled, directionalShadow, spotShadow, pointShadow, environmentGraphReady, environmentCube,
                     environmentSH](RenderGraphBuilder& builder) {
                        builder.ReadTexture(gbufferAlbedo);
                        builder.ReadTexture(gbufferNormal);
                        builder.ReadTexture(gbufferMaterial);
                        builder.ReadTexture(gbufferEmissive);
                        builder.ReadTexture(sceneDepth);
                        if (shadowsEnabled) {
                            builder.ReadTexture(directionalShadow);
                            builder.ReadTexture(spotShadow);
                            builder.ReadTexture(pointShadow);
                        }
                        if (environmentGraphReady) {
                            builder.ReadTexture(environmentCube);
                            builder.ReadBuffer(environmentSH);
                        }
                        builder.WriteColor(deferredSceneColor, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 1});
                    },
                    [this, &scene, &camera](GpuCommandList& commands, const RenderGraphResources&) {
                        const auto start = std::chrono::steady_clock::now();
                        m_DeferredLightingPass->Execute(commands, scene, camera);
                        const auto end = std::chrono::steady_clock::now();
                        RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                        stats.mainCpuMs += ElapsedMs(start, end);
                        ++stats.fullscreenDrawCalls;
                        FrameStatsProvider::SetRendererStats(stats);
                    });
            }

            m_RenderGraph->AddPass(
                "ForwardTransparent",
                [compositeInput, sceneDepth, shadowsEnabled, directionalShadow, spotShadow, pointShadow,
                 environmentGraphReady, environmentCube, environmentSH](RenderGraphBuilder& builder) {
                    if (shadowsEnabled) {
                        builder.ReadTexture(directionalShadow);
                        builder.ReadTexture(spotShadow);
                        builder.ReadTexture(pointShadow);
                    }
                    if (environmentGraphReady) {
                        builder.ReadTexture(environmentCube);
                        builder.ReadBuffer(environmentSH);
                    }
                    builder.WriteColor(compositeInput, RHILoadOp::Load, RHIStoreOp::Store);
                    builder.WriteDepth(sceneDepth, RHILoadOp::Load, RHIStoreOp::Store, 1.0f);
                },
                [this, &scene, &camera, modernFrameReady](GpuCommandList& commands, const RenderGraphResources&) {
                    const auto start = std::chrono::steady_clock::now();
                    const Mat4* viewProjection =
                        modernFrameReady ? &m_ModernDeferredPipeline->GetCurrentViewProjection() : nullptr;
                    m_MainPass->ExecuteTransparentOnly(commands, scene, camera, viewProjection);
                    const auto end = std::chrono::steady_clock::now();
                    RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                    stats.mainCpuMs += ElapsedMs(start, end);
                    const auto& mainStats = m_MainPass->GetLastStats();
                    stats.mainDrawCalls += mainStats.drawCalls;
                    stats.subMeshCount = mainStats.submittedSubMeshes;
                    stats.bindGroupCreates += mainStats.bindGroupCreates;
                    stats.textureUploads += mainStats.textureUploads;
                    stats.textureUploadBytes += mainStats.textureUploadBytes;
                    stats.textureUploadMs += mainStats.textureUploadMs;
                    FrameStatsProvider::SetRendererStats(stats);
                });
            if (modernFrameReady) {
                // Transparent rendering targets the effects HDR texture. Temporal AA and compute post run after it.
                compositeInput = m_ModernDeferredPipeline->AddTemporalPostProcess(
                    *m_RenderGraph, compositeInput, compositeOverride, sceneDepth, postResources.sceneDepthSrv,
                    gbufferNormal, gbufferResources.normalSrv, gbufferVelocity, gbufferResources.velocitySrv,
                    ResolveModernScreenSpaceDebugMode(m_DebugView));
                compositeOverride = m_ModernDeferredPipeline->GetFinalPostSrv();
            }
        } else {
            m_RenderGraph->AddPass(
                "Main",
                [sceneColor, sceneDepth, shadowsEnabled, directionalShadow, spotShadow, pointShadow,
                 environmentGraphReady, environmentCube, environmentSH](RenderGraphBuilder& builder) {
                    if (shadowsEnabled) {
                        builder.ReadTexture(directionalShadow);
                        builder.ReadTexture(spotShadow);
                        builder.ReadTexture(pointShadow);
                    }
                    if (environmentGraphReady) {
                        builder.ReadTexture(environmentCube);
                        builder.ReadBuffer(environmentSH);
                    }
                    builder.WriteColor(sceneColor, RHILoadOp::Clear, RHIStoreOp::Store, {0.0f, 0.0f, 0.0f, 1.0f});
                    builder.WriteDepth(sceneDepth, RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
                },
                [this, &scene, &camera](GpuCommandList& commands, const RenderGraphResources&) {
                    const auto start = std::chrono::steady_clock::now();
                    m_MainPass->Execute(commands, scene, camera);
                    const auto end = std::chrono::steady_clock::now();
                    RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                    stats.mainCpuMs += ElapsedMs(start, end);
                    const auto& mainStats = m_MainPass->GetLastStats();
                    stats.mainDrawCalls = mainStats.drawCalls;
                    stats.subMeshCount = mainStats.submittedSubMeshes;
                    stats.bindGroupCreates += mainStats.bindGroupCreates;
                    stats.textureUploads += mainStats.textureUploads;
                    stats.textureUploadBytes += mainStats.textureUploadBytes;
                    stats.textureUploadMs += mainStats.textureUploadMs;
                    FrameStatsProvider::SetRendererStats(stats);
                });
        }
        if (!modernFrameReady && frameSsaoEnabled)
            addSsaoPasses();
        const bool compositeSsaoEnabled = frameSsaoEnabled && !modernFrameReady;
        m_RenderGraph->AddPass(
            "Composite",
            [compositeInput, ssao, compositeSsaoEnabled, compositeToBackbuffer, composite,
             backBuffer](RenderGraphBuilder& builder) {
                builder.ReadTexture(compositeInput);
                if (compositeSsaoEnabled)
                    builder.ReadTexture(ssao);
                if (compositeToBackbuffer) {
                    builder.WriteColor(backBuffer, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 1});
                } else {
                    builder.WriteColor(composite, RHILoadOp::Clear, RHIStoreOp::Store, {0, 0, 0, 1});
                }
            },
            [this, &scene, compositeOverride](GpuCommandList& commands, const RenderGraphResources&) {
                const auto start = std::chrono::steady_clock::now();
                GpuTextureView* sceneColorOverride = compositeOverride.get();
                if (m_PostProcessPass->IsCompositeToBackbuffer()) {
                    if (sceneColorOverride) {
                        m_PostProcessPass->DrawCompositeToCurrentTarget(commands, scene, sceneColorOverride);
                    } else {
                        m_PostProcessPass->DrawCompositeToCurrentTarget(commands, scene);
                    }
                } else {
                    if (sceneColorOverride) {
                        m_PostProcessPass->DrawCompositeOffscreen(commands, scene, sceneColorOverride);
                    } else {
                        m_PostProcessPass->DrawCompositeOffscreen(commands, scene);
                    }
                }
                const auto end = std::chrono::steady_clock::now();
                RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                stats.compositeCpuMs += ElapsedMs(start, end);
                ++stats.fullscreenDrawCalls;
                FrameStatsProvider::SetRendererStats(stats);
            });
        const bool screenUIRequested =
            HasRendererFeature(m_FeatureMask, RendererFeatureMask::ScreenUI) && m_UIDrawList && !m_UIDrawList->Empty();
        const RHIFormat screenUIColorFormat =
            compositeToBackbuffer ? backBufferView->texture->desc.format : postResources.composite->desc.format;
        const bool screenUIReady = screenUIRequested && m_ScreenUIPass->Prepare(*m_UIDrawList, screenUIColorFormat);
        if (screenUIReady) {
            m_RenderGraph->AddPass(
                "ScreenUI",
                [compositeToBackbuffer, composite, backBuffer](RenderGraphBuilder& builder) {
                    if (compositeToBackbuffer) {
                        builder.WriteColor(backBuffer, RHILoadOp::Load, RHIStoreOp::Store);
                    } else {
                        builder.WriteColor(composite, RHILoadOp::Load, RHIStoreOp::Store);
                    }
                },
                [this, screenUIColorFormat](GpuCommandList& commands, const RenderGraphResources&) {
                    m_ScreenUIPass->Execute(commands, *m_UIDrawList, screenUIColorFormat);
                });
        }
    } else {
        GpuTextureView* backBufferView = m_FrameContext->GetCurrentBackBufferView();
        if (!backBufferView || !backBufferView->texture) {
            Logger::Error("[Renderer] RHI returned no current backbuffer view for main pass");
            endFrameOnFailure();
            return;
        }
        auto backBufferSharedView = std::shared_ptr<GpuTextureView>(backBufferView->texture, backBufferView);
        const auto backBuffer =
            m_RenderGraph->ImportTexture("BackBuffer", backBufferView->texture, backBufferSharedView,
                                         RHIResourceState::RenderTarget, RHIResourceState::RenderTarget);
        m_RenderGraph->AddPass(
            "Main",
            [backBuffer, shadowsEnabled, directionalShadow, spotShadow, pointShadow, environmentGraphReady,
             environmentCube, environmentSH](RenderGraphBuilder& builder) {
                if (shadowsEnabled) {
                    builder.ReadTexture(directionalShadow);
                    builder.ReadTexture(spotShadow);
                    builder.ReadTexture(pointShadow);
                }
                if (environmentGraphReady) {
                    builder.ReadTexture(environmentCube);
                    builder.ReadBuffer(environmentSH);
                }
                builder.WriteColor(backBuffer, RHILoadOp::Clear, RHIStoreOp::Store, {0.12f, 0.12f, 0.18f, 1.0f});
            },
            [this, &scene, &camera](GpuCommandList& commands, const RenderGraphResources&) {
                const auto start = std::chrono::steady_clock::now();
                m_MainPass->Execute(commands, scene, camera);
                const auto end = std::chrono::steady_clock::now();
                RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                stats.mainCpuMs += ElapsedMs(start, end);
                const auto& mainStats = m_MainPass->GetLastStats();
                stats.mainDrawCalls = mainStats.drawCalls;
                stats.subMeshCount = mainStats.submittedSubMeshes;
                stats.bindGroupCreates += mainStats.bindGroupCreates;
                stats.textureUploads += mainStats.textureUploads;
                stats.textureUploadBytes += mainStats.textureUploadBytes;
                stats.textureUploadMs += mainStats.textureUploadMs;
                FrameStatsProvider::SetRendererStats(stats);
            });
        const bool screenUIRequested =
            HasRendererFeature(m_FeatureMask, RendererFeatureMask::ScreenUI) && m_UIDrawList && !m_UIDrawList->Empty();
        const RHIFormat screenUIColorFormat = backBufferView->texture->desc.format;
        const bool screenUIReady = screenUIRequested && m_ScreenUIPass->Prepare(*m_UIDrawList, screenUIColorFormat);
        if (screenUIReady) {
            m_RenderGraph->AddPass(
                "ScreenUI",
                [backBuffer](RenderGraphBuilder& builder) {
                    builder.WriteColor(backBuffer, RHILoadOp::Load, RHIStoreOp::Store);
                },
                [this, screenUIColorFormat](GpuCommandList& commands, const RenderGraphResources&) {
                    m_ScreenUIPass->Execute(commands, *m_UIDrawList, screenUIColorFormat);
                });
        }
    }
    const auto publishGraphStats = [&]() {
        const auto& graph = m_RenderGraph->GetResourceStats();
        RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
        stats.transientRequestedBytes = graph.transientRequestedBytes;
        stats.transientAllocatedBytes = graph.transientAllocatedBytes;
        stats.transientReusedBytes = graph.transientReusedBytes;
        stats.renderGraphPooledBytes = graph.pooledBytes;
        stats.renderGraphPoolEvictedBytes = graph.poolEvictedBytes;
        stats.transientResources = graph.transientTextures + graph.transientBuffers;
        stats.transientDescriptors = graph.transientDescriptors;
        stats.renderGraphPoolEvictions = graph.poolEvictions;
        stats.transientBudgetExceeded = graph.transientBudgetExceeded;
        if (m_PipelineDiagnostics.resolvedPipeline == ResolvedRenderPipeline::ModernDeferred &&
            m_ModernDeferredPipeline) {
            const auto& modern = m_ModernDeferredPipeline->GetStats();
            stats.gpuSceneUploadBytes = modern.gpuSceneUploadBytes;
            stats.gpuScenePrepareCpuMs = modern.gpuScenePrepareCpuMs;
            stats.gpuSceneMaterialResolves = modern.materialResolves;
            stats.gpuSceneMaterialCacheHits = modern.materialCacheHits;
            stats.gpuSceneTexturedMaterials = modern.texturedMaterials;
            stats.gpuSceneCandidates = modern.candidateObjects;
            stats.gpuFrustumVisible = modern.indirectDrawCount;
            stats.gpuHiZOccluded = modern.indirectDrawCount > modern.hizVisibleDrawCount
                                       ? modern.indirectDrawCount - modern.hizVisibleDrawCount
                                       : 0;
            stats.indirectDrawCount = modern.indirectDrawCount;
            stats.clusterCount = modern.clusterCount;
            stats.clusterOverflow = modern.clusterOverflow;
            stats.localLightCount = modern.localLights;
            stats.rayTracingRequestedMask = modern.rayTracingRequestedMask;
            stats.rayTracingEffectiveMask = modern.rayTracingEffectiveMask;
            stats.rayTracingBlasCount = modern.rayTracingBlasCount;
            stats.rayTracingTlasInstanceCount = modern.rayTracingTlasInstanceCount;
            stats.rayTracingAccelerationStructureBytes = modern.rayTracingAccelerationStructureBytes;
            stats.rayTracingBuildCpuMs = modern.rayTracingBuildCpuMs;
            stats.rayTracingTlasUpdated = modern.rayTracingTlasUpdated;
            stats.rayTracingFallbackReason = m_ModernDeferredPipeline->GetRayTracingFallbackReason();
            stats.bindlessResourcesCapacity = m_Device->GetCapabilities().maxBindlessResources;
            stats.bindlessResourcesUsed = static_cast<uint32_t>(
                (std::min<uint64_t>)(FrameStatsProvider::GetResourceStats().liveNativeDescriptorSlots, UINT32_MAX));
            stats.historyResetReason = m_ModernDeferredPipeline->GetHistoryResetReason();
        }
        FrameStatsProvider::SetRendererStats(stats);
    };
    const auto graphPrepareStart = std::chrono::steady_clock::now();
    if (!m_RenderGraph->Prepare()) {
        publishGraphStats();
        Logger::Error("[Renderer] RenderGraph preparation failed: ", m_RenderGraph->GetLastError());
        endFrameOnFailure();
        return;
    }
    const auto graphExecuteStart = std::chrono::steady_clock::now();
    RendererFrameStats graphTimingStats = FrameStatsProvider::GetRendererStats();
    graphTimingStats.renderGraphPrepareCpuMs = ElapsedMs(graphPrepareStart, graphExecuteStart);
    graphTimingStats.renderGraphBuildCpuMs = ElapsedMs(submissionStart, graphExecuteStart);
    FrameStatsProvider::SetRendererStats(graphTimingStats);
    if (!m_RenderGraph->Execute(*commandList)) {
        publishGraphStats();
        Logger::Error("[Renderer] RenderGraph execution failed: ", m_RenderGraph->GetLastError());
        endFrameOnFailure();
        return;
    }
    if (m_ModernDeferredPipeline)
        m_ModernDeferredPipeline->CommitTemporalFrame();
    graphTimingStats = FrameStatsProvider::GetRendererStats();
    graphTimingStats.renderGraphExecuteCpuMs = ElapsedMs(graphExecuteStart, std::chrono::steady_clock::now());
    FrameStatsProvider::SetRendererStats(graphTimingStats);
    if (timestampPool) {
        commandList->WriteTimestamp(timestampPool.get(), 1);
        commandList->ResolveTimestamps(timestampPool.get(), 0, 2);
        m_FrameTimestampRecorded[timestampSlot] = true;
    }
    publishGraphStats();
    if (useOffscreen) {
        if (shadowsEnabled)
            m_ShadowPass->MarkGraphResourcesShaderResource();
        m_EnvironmentPass->MarkGraphResourcesShaderResource();
        if (useDeferred) {
            m_GBufferPass->MarkGraphResourcesShaderResource();
            m_DeferredLightingPass->MarkGraphResourcesShaderResource();
        }
        m_PostProcessPass->MarkGraphResourcesShaderResource(!m_PostProcessPass->IsCompositeToBackbuffer());
    } else {
        if (shadowsEnabled)
            m_ShadowPass->MarkGraphResourcesShaderResource();
        m_EnvironmentPass->MarkGraphResourcesShaderResource();
    }
    RendererFrameStats rendererStats = FrameStatsProvider::GetRendererStats();
    // Submission is the RenderGraph execution/command-recording phase. Graph construction and
    // RenderExtract preparation are reported separately so Present and preparation stalls do not
    // contaminate the CPU submission performance gate.
    rendererStats.renderSubmissionCpuMs = rendererStats.renderGraphExecuteCpuMs;
    rendererStats.drawCalls =
        rendererStats.shadowDrawCalls + rendererStats.mainDrawCalls + rendererStats.fullscreenDrawCalls;
    FrameStatsProvider::SetRendererStats(rendererStats);
    if (present) {
        m_FrameContext->EndFrame();
    }
}

void Renderer::SetOutputOffscreen(bool enabled) {
    m_OutputOffscreen = enabled;
    if (m_PostProcessPass) {
        m_PostProcessPass->SetCompositeToBackbuffer(!enabled);
    }
}

void Renderer::SetFeatureMask(RendererFeatureMask mask) {
    if (m_FeatureMask == mask)
        return;
    const bool shadowsWereEnabled = HasRendererFeature(m_FeatureMask, RendererFeatureMask::Shadows);
    const bool shadowsAreEnabled = HasRendererFeature(mask, RendererFeatureMask::Shadows);
    m_FeatureMask = mask;
    if (shadowsWereEnabled && !shadowsAreEnabled && m_ShadowPass)
        m_ShadowPass->ReleaseGraphResources();
    if (!HasRendererFeature(mask, RendererFeatureMask::SSAO) && m_PostProcessPass)
        m_PostProcessPass->SetSSAOEnabled(false);
}

void Renderer::InvalidateTemporalHistory(const std::string& reason, bool resetObjectHistory) {
    if (m_ModernDeferredPipeline)
        m_ModernDeferredPipeline->InvalidateTemporalHistory(reason, resetObjectHistory);
}

GpuTextureView* Renderer::GetSceneColorView() const {
    if (m_PipelineDiagnostics.resolvedPipeline == ResolvedRenderPipeline::ModernDeferred && m_ModernDeferredPipeline) {
        if (m_DebugView == RendererDebugView::HDRLighting && m_ModernDeferredPipeline->GetHdrSrv())
            return m_ModernDeferredPipeline->GetHdrSrv().get();
        if (m_DebugView == RendererDebugView::HiZ && m_ModernDeferredPipeline->GetHiZDebugSrv())
            return m_ModernDeferredPipeline->GetHiZDebugSrv().get();
        if (m_DebugView == RendererDebugView::MotionVectors && m_GBufferPass) {
            const auto resources = m_GBufferPass->GetGraphResources();
            if (resources.velocitySrv)
                return resources.velocitySrv.get();
        }
        if (m_DebugView == RendererDebugView::SSGI && m_ModernDeferredPipeline->GetSSGIDebugSrv())
            return m_ModernDeferredPipeline->GetSSGIDebugSrv().get();
        if (m_DebugView == RendererDebugView::SSRConfidence && m_ModernDeferredPipeline->GetSSRDebugSrv())
            return m_ModernDeferredPipeline->GetSSRDebugSrv().get();
        if (m_DebugView == RendererDebugView::TAAHistoryAge && m_ModernDeferredPipeline->GetTAAHistoryAgeDebugSrv()) {
            return m_ModernDeferredPipeline->GetTAAHistoryAgeDebugSrv().get();
        }
        if (m_DebugView == RendererDebugView::TAARejectReason &&
            m_ModernDeferredPipeline->GetTAARejectReasonDebugSrv()) {
            return m_ModernDeferredPipeline->GetTAARejectReasonDebugSrv().get();
        }
        const uint32_t rayTracingMask = m_ModernDeferredPipeline->GetRayTracingEffectiveMask();
        if (m_DebugView == RendererDebugView::RTShadow && (rayTracingMask & ModernRayTracingShadow) != 0 &&
            m_ModernDeferredPipeline->GetRTShadowDebugSrv())
            return m_ModernDeferredPipeline->GetRTShadowDebugSrv().get();
        if (m_DebugView == RendererDebugView::RTAO && (rayTracingMask & ModernRayTracingAO) != 0 &&
            m_ModernDeferredPipeline->GetRTAODebugSrv())
            return m_ModernDeferredPipeline->GetRTAODebugSrv().get();
        if (m_DebugView == RendererDebugView::RTDiffuse && (rayTracingMask & ModernRayTracingDiffuse) != 0 &&
            m_ModernDeferredPipeline->GetRTDiffuseDebugSrv())
            return m_ModernDeferredPipeline->GetRTDiffuseDebugSrv().get();
        if (m_DebugView == RendererDebugView::RTReflection && (rayTracingMask & ModernRayTracingReflection) != 0 &&
            m_ModernDeferredPipeline->GetRTReflectionDebugSrv())
            return m_ModernDeferredPipeline->GetRTReflectionDebugSrv().get();
    }
    return m_PostProcessPass ? m_PostProcessPass->GetSceneColorView() : nullptr;
}
