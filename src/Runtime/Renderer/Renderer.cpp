#include "Renderer/Renderer.h"

#include "Renderer/GpuUploadQueue.h"
#include "Core/FrameStats.h"
#include "Core/Logger.h"
#include "Renderer/EnvironmentPass.h"
#include "Renderer/DDGIPass.h"
#include "Renderer/DeferredLightingPass.h"
#include "Renderer/GBufferPass.h"
#include "Renderer/LightComponent.h"
#include "Renderer/MainPass.h"
#include "Renderer/PostProcessPass.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/ShaderManager.h"
#include "Renderer/ShadowPass.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/ScreenUIPass.h"
#include "Renderer/SceneLighting.h"
#include "Scene/Actor.h"
#include "UI/Render/UIDrawList.h"

#include <chrono>

namespace {
struct PostProcessRuntimeOptions {
    bool ssaoEnabled = false;
    float ssaoScale = 1.0f;
};

PostProcessRuntimeOptions CollectPostProcessOptions(const Scene& scene)
{
    PostProcessRuntimeOptions options;
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive()) return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled()) return;
        options.ssaoEnabled = post->GetSSAOIntensity() > 0.0f;
        options.ssaoScale = post->GetSSAOScale();
        found = true;
    });
    return options;
}

Vec3 CollectEnvironmentSunDirection(const Scene& scene)
{
    Vec3 sunDirection = EnvironmentPass::DefaultSunDirection();
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive()) return;
        auto* light = actor.GetComponent<LightComponent>();
        if (!light || !light->IsEnabled() ||
            light->GetLightType() != LightType::Directional) {
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

float ElapsedMs(std::chrono::steady_clock::time_point start,
                std::chrono::steady_clock::time_point end)
{
    return std::chrono::duration<float, std::milli>(end - start).count();
}
}

Renderer::Renderer(IRHIDevice* device, IRHIFrameContext* frameContext,
                   IRHIReadbackService* readbackService)
    : m_Device(device)
    , m_FrameContext(frameContext)
    , m_ReadbackService(readbackService)
    , m_ShadowPass(std::make_unique<ShadowPass>(device))
    , m_EnvironmentPass(std::make_unique<EnvironmentPass>(device, readbackService))
    , m_MainPass(std::make_unique<MainPass>(device))
    , m_GBufferPass(std::make_unique<GBufferPass>(device))
    , m_DeferredLightingPass(std::make_unique<DeferredLightingPass>(device))
    , m_DDGIPass(std::make_unique<DDGIPass>(device))
    , m_PostProcessPass(std::make_unique<PostProcessPass>(device))
    , m_ScreenUIPass(std::make_unique<ScreenUIPass>(device))
    , m_RenderGraph(device ? std::make_unique<RenderGraph>(*device) : nullptr)
{
    ShaderManager::Get().SetDevice(device);
}

Renderer::~Renderer() = default;

void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (m_ShadowPass) m_ShadowPass->Resize(width, height);
    if (m_EnvironmentPass) m_EnvironmentPass->Resize(width, height);
    if (m_MainPass) m_MainPass->Resize(width, height);
    if (m_GBufferPass) m_GBufferPass->Resize(width, height);
    if (m_DeferredLightingPass) m_DeferredLightingPass->Resize(width, height);
    if (m_PostProcessPass) m_PostProcessPass->Resize(width, height);
    if (m_ScreenUIPass) m_ScreenUIPass->Resize(width, height);
}

void Renderer::ReleaseFrameResources()
{
    if (m_RenderGraph) m_RenderGraph->Reset();
}

void Renderer::RenderScene(const Scene& scene, const Camera& camera, bool present)
{
    FrameStatsProvider::SetRendererStats({});
    if (!m_Device || !m_FrameContext || !m_ShadowPass || !m_MainPass ||
        !m_GBufferPass || !m_DeferredLightingPass || !m_DDGIPass ||
        !m_PostProcessPass) return;

    GpuUploadQueue::Get().Process(*m_Device);

    m_FrameContext->BeginFrame(0.12f, 0.12f, 0.18f);
    const auto endFrameOnFailure = [this, present]() {
        if (present) m_FrameContext->EndFrame();
    };
    GpuCommandList* commandList = m_FrameContext->GetGraphicsCommandList();
    if (!commandList) {
        endFrameOnFailure();
        return;
    }

    m_RenderGraph->Reset();
    const Vec3 environmentSunDirection = CollectEnvironmentSunDirection(scene);
    m_EnvironmentPass->SetSunDirection(environmentSunDirection);
    m_MainPass->SetSunDirection(environmentSunDirection);
    if (!m_ShadowPass->PrepareGraphResources(scene, camera)) {
        Logger::Error("[Renderer] ShadowPass failed to prepare graph resources");
        endFrameOnFailure();
        return;
    }
    const auto shadowResources = m_ShadowPass->GetGraphResources();
    const auto directionalShadow = m_RenderGraph->ImportTexture(
        "DirectionalShadow", shadowResources.directional,
        shadowResources.directionalCascadeViews[0], shadowResources.initialState,
        RHIResourceState::ShaderResource);
    const auto spotShadow = m_RenderGraph->ImportTexture(
        "SpotShadow", shadowResources.spot, shadowResources.spotView,
        shadowResources.initialState, RHIResourceState::ShaderResource);
    const auto pointShadow = m_RenderGraph->ImportTexture(
        "PointShadow", shadowResources.point, shadowResources.pointViews[0],
        shadowResources.initialState, RHIResourceState::ShaderResource);

    m_RenderGraph->AddPass("Shadow",
        [directionalShadow, spotShadow, pointShadow](RenderGraphBuilder& builder) {
            for (uint32_t cascade = 0; cascade < 3; ++cascade) {
                builder.WriteDepth(directionalShadow, RGTextureSubresource{0, 1, cascade, 1},
                                   RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
            }
            builder.WriteDepth(spotShadow, RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
            for (uint32_t face = 0; face < 6; ++face) {
                builder.WriteDepth(pointShadow, RGTextureSubresource{0, 1, face, 1},
                                   RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
            }
        },
        [this, &scene, &camera](GpuCommandList& commands, const RenderGraphResources&) {
            (void)camera;
            const auto start = std::chrono::steady_clock::now();
            m_ShadowPass->ExecuteGraphManaged(commands, scene);
            const auto end = std::chrono::steady_clock::now();
            RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
            stats.shadowCpuMs += ElapsedMs(start, end);
            const auto& shadowStats = m_ShadowPass->GetLastStats();
            stats.shadowDrawCalls = shadowStats.drawCalls;
            stats.bindGroupCreates += shadowStats.bindGroupCreates;
            FrameStatsProvider::SetRendererStats(stats);
        }, RenderGraph::PassFlags::ManualRenderingScope |
           RenderGraph::PassFlags::ManualResourceTransitions);
    const bool environmentGraphReady = m_EnvironmentPass->PrepareGraphResources();
    RGTextureHandle environmentCube;
    RGBufferHandle environmentSH;
    if (environmentGraphReady) {
        const auto environmentResources = m_EnvironmentPass->GetGraphResources();
        environmentCube = m_RenderGraph->ImportTexture(
            "EnvironmentCube", environmentResources.environment,
            environmentResources.environmentView, environmentResources.environmentInitialState,
            RHIResourceState::ShaderResource);
        environmentSH = m_RenderGraph->ImportBuffer(
            "EnvironmentSH2", environmentResources.shBuffer,
            environmentResources.shInitialState, RHIResourceState::ShaderResource);
        m_RenderGraph->AddPass("Environment",
            [environmentCube, environmentSH, generated = environmentResources.generated](
                RenderGraphBuilder& builder) {
                if (generated) {
                    builder.ReadTexture(environmentCube);
                    builder.ReadBuffer(environmentSH);
                } else {
                    builder.WriteColor(environmentCube, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0.0f, 0.0f, 0.0f, 1.0f});
                    builder.ReadWriteUAV(environmentSH);
                }
            },
            [this](GpuCommandList& commands, const RenderGraphResources&) {
                m_EnvironmentPass->ExecuteGraphManaged(commands);
            }, RenderGraph::PassFlags::ManualRenderingScope |
               RenderGraph::PassFlags::ManualResourceTransitions);
    }
    const bool backendSupportsPostProcess = m_Device->GetBackend() == RHIBackend::D3D11 ||
                                            m_Device->GetBackend() == RHIBackend::D3D12 ||
                                            m_Device->GetBackend() == RHIBackend::Metal ||
                                            m_Device->GetBackend() == RHIBackend::Vulkan;
    const bool useOffscreen = backendSupportsPostProcess || m_OutputOffscreen;
    const bool useDeferred = useOffscreen && m_RenderPath == RenderPath::Deferred;
    m_MainPass->SetHdrPassthrough(useOffscreen);
    const SceneLightData sceneLights = CollectSceneLights(scene);
    const PostProcessRuntimeOptions postOptions = CollectPostProcessOptions(scene);
    m_PostProcessPass->SetSSAOEnabled(postOptions.ssaoEnabled);
    m_PostProcessPass->SetSSAOScale(postOptions.ssaoScale);
    const uint32_t cascadeCount = m_ShadowPass->GetCascadeCount();
    Mat4 cascades[3] = {
        m_ShadowPass->GetCascadeViewProj(0),
        cascadeCount > 1 ? m_ShadowPass->GetCascadeViewProj(1) : m_ShadowPass->GetCascadeViewProj(0),
        cascadeCount > 2 ? m_ShadowPass->GetCascadeViewProj(2) : m_ShadowPass->GetCascadeViewProj(0)};
    m_MainPass->SetShadowInput(
        m_ShadowPass->GetLightViewProj(), m_ShadowPass->GetLightDirection(),
        m_ShadowPass->IsDirectionalShadowEnabled(), m_ShadowPass->GetShadowMapTexture(),
        m_ShadowPass->GetSpotLightViewProj(), m_ShadowPass->GetSpotShadowIndex(),
        m_ShadowPass->GetSpotShadowMapTexture(), m_ShadowPass->GetPointShadowPosition(),
        m_ShadowPass->GetPointShadowRange(), m_ShadowPass->GetPointShadowIndex(),
        m_ShadowPass->GetPointShadowMapTexture(), cascadeCount > 0 ? cascades : nullptr,
        cascadeCount, m_ShadowPass->GetCascadeSplits());
    m_MainPass->SetEnvironmentInput(
        m_EnvironmentPass->GetEnvironmentCubemap(), m_EnvironmentPass->GetSH2BufferView(),
        m_EnvironmentPass->GetSH2Coefficients());
    if (useOffscreen) {
        if (!m_PostProcessPass->PrepareGraphResources()) {
            endFrameOnFailure();
            return;
        }
        const auto postResources = m_PostProcessPass->GetGraphResources();
        const auto sceneColor = m_RenderGraph->ImportTexture(
            "SceneColor", postResources.sceneColor, postResources.sceneColorRtv,
            postResources.sceneColorState, RHIResourceState::ShaderResource);
        const auto sceneDepth = m_RenderGraph->ImportTexture(
            "SceneDepth", postResources.sceneDepth, postResources.sceneDepthDsv,
            postResources.sceneDepthState, RHIResourceState::ShaderResource);
        RGTextureHandle ssao;
        RGTextureHandle ssaoBlur;
        if (postOptions.ssaoEnabled) {
            ssao = m_RenderGraph->ImportTexture(
                "SSAO", postResources.ssao, postResources.ssaoRtv,
                postResources.ssaoState, RHIResourceState::ShaderResource);
            ssaoBlur = m_RenderGraph->ImportTexture(
                "SSAOBlur", postResources.ssaoBlur, postResources.ssaoBlurRtv,
                postResources.ssaoBlurState, RHIResourceState::ShaderResource);
        }
        RGTextureHandle composite;
        const bool compositeToBackbuffer = m_PostProcessPass->IsCompositeToBackbuffer();
        RGTextureHandle backBuffer;
        GpuTextureView* backBufferView = nullptr;
        if (!compositeToBackbuffer) {
            composite = m_RenderGraph->ImportTexture(
                "Composite", postResources.composite, postResources.compositeRtv,
                postResources.compositeState, RHIResourceState::ShaderResource);
        } else {
            backBufferView = m_FrameContext->GetCurrentBackBufferView();
            if (!backBufferView || !backBufferView->texture) {
                Logger::Error("[Renderer] RHI returned no current backbuffer view");
                endFrameOnFailure();
                return;
            }
            auto backBufferSharedView =
                std::shared_ptr<GpuTextureView>(backBufferView->texture, backBufferView);
            backBuffer = m_RenderGraph->ImportTexture(
                "BackBuffer", backBufferView->texture, backBufferSharedView,
                RHIResourceState::RenderTarget, RHIResourceState::RenderTarget);
        }

        RGTextureHandle compositeInput = sceneColor;
        if (useDeferred) {
            if (!m_GBufferPass->PrepareGraphResources() ||
                !m_DeferredLightingPass->PrepareGraphResources()) {
                endFrameOnFailure();
                return;
            }
            DDGIPass::GraphResources ddgiResources;
            if (m_DDGIEnabled &&
                !m_DDGIPass->PrepareGraphResources(scene, sceneLights)) {
                endFrameOnFailure();
                return;
            }
            if (m_DDGIEnabled) {
                ddgiResources = m_DDGIPass->GetGraphResources();
            }
            const auto gbufferResources = m_GBufferPass->GetGraphResources();
            const auto deferredResources = m_DeferredLightingPass->GetGraphResources();
            const auto gbufferAlbedo = m_RenderGraph->ImportTexture(
                "GBufferAlbedo", gbufferResources.albedo, gbufferResources.albedoRtv,
                gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto gbufferNormal = m_RenderGraph->ImportTexture(
                "GBufferNormal", gbufferResources.normal, gbufferResources.normalRtv,
                gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto gbufferMaterial = m_RenderGraph->ImportTexture(
                "GBufferMaterial", gbufferResources.material, gbufferResources.materialRtv,
                gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto gbufferEmissive = m_RenderGraph->ImportTexture(
                "GBufferEmissive", gbufferResources.emissive, gbufferResources.emissiveRtv,
                gbufferResources.initialState, RHIResourceState::ShaderResource);
            const auto deferredSceneColor = m_RenderGraph->ImportTexture(
                "DeferredSceneColor", deferredResources.sceneColor, deferredResources.sceneColorRtv,
                deferredResources.initialState, RHIResourceState::ShaderResource);
            RGBufferHandle ddgiMetadata;
            RGBufferHandle ddgiSdf;
            RGBufferHandle ddgiVoxels;
            RGBufferHandle ddgiProbeSH2;
            if (m_DDGIEnabled) {
                ddgiMetadata = m_RenderGraph->ImportBuffer(
                    "DDGIMetadata", ddgiResources.metadata,
                    ddgiResources.metadataState, RHIResourceState::ShaderResource);
                ddgiSdf = m_RenderGraph->ImportBuffer(
                    "SceneSdfClipmapSdf", ddgiResources.sdf,
                    ddgiResources.sdfState, RHIResourceState::ShaderResource);
                ddgiVoxels = m_RenderGraph->ImportBuffer(
                    "SceneSdfClipmapVoxels", ddgiResources.voxels,
                    ddgiResources.voxelState, RHIResourceState::ShaderResource);
                ddgiProbeSH2 = m_RenderGraph->ImportBuffer(
                    "DDGIProbeSH2", ddgiResources.probeSH2,
                    ddgiResources.probeState, RHIResourceState::ShaderResource);
            }
            compositeInput = deferredSceneColor;

            m_DeferredLightingPass->SetGBufferInput(
                gbufferResources.albedoSrv, gbufferResources.normalSrv,
                gbufferResources.materialSrv, gbufferResources.emissiveSrv);
            m_DeferredLightingPass->SetDepthInput(postResources.sceneDepthSrv);
            m_DeferredLightingPass->SetLightingInput(sceneLights);
            m_DeferredLightingPass->SetShadowInput(
                m_ShadowPass->GetLightViewProj(), m_ShadowPass->IsDirectionalShadowEnabled(),
                m_ShadowPass->GetShadowMapTexture(), m_ShadowPass->GetSpotLightViewProj(),
                m_ShadowPass->GetSpotShadowIndex(), m_ShadowPass->GetSpotShadowMapTexture(),
                m_ShadowPass->GetPointShadowPosition(), m_ShadowPass->GetPointShadowRange(),
                m_ShadowPass->GetPointShadowIndex(), m_ShadowPass->GetPointShadowMapTexture(),
                cascadeCount > 0 ? cascades : nullptr, cascadeCount,
                m_ShadowPass->GetCascadeSplits());
            m_DeferredLightingPass->SetEnvironmentInput(
                m_EnvironmentPass->GetEnvironmentCubemap(),
                m_EnvironmentPass->GetSH2BufferView());
            m_DeferredLightingPass->SetDDGIInput(
                ddgiResources.probeSH2Srv, ddgiResources.metadataView,
                m_DDGIEnabled && ddgiResources.enabled);
            m_DeferredLightingPass->SetDDGIDebugView(m_DDGIDebugView);

            if (m_DDGIEnabled && ddgiResources.enabled) {
                m_RenderGraph->AddPass("DDGI",
                    [ddgiMetadata, ddgiSdf, ddgiVoxels, ddgiProbeSH2](
                        RenderGraphBuilder& builder) {
                        builder.ReadBuffer(ddgiMetadata);
                        builder.ReadBuffer(ddgiSdf);
                        builder.ReadBuffer(ddgiVoxels);
                        builder.ReadWriteUAV(ddgiProbeSH2);
                    },
                    [this, &scene, &camera](GpuCommandList& commands,
                                            const RenderGraphResources&) {
                        m_DDGIPass->Execute(commands, scene, camera);
                    });
            }

            m_RenderGraph->AddPass("GBuffer",
                [gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferEmissive,
                 sceneDepth](RenderGraphBuilder& builder) {
                    builder.WriteColor(gbufferAlbedo, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0, 0, 0, 0});
                    builder.WriteColor(gbufferNormal, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0.5f, 0.5f, 1.0f, 1.0f});
                    builder.WriteColor(gbufferMaterial, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0, 0.5f, 1.0f, 0});
                    builder.WriteColor(gbufferEmissive, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0, 0, 0, 0});
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

            m_RenderGraph->AddPass("DeferredLighting",
                [deferredSceneColor, gbufferAlbedo, gbufferNormal, gbufferMaterial,
                 gbufferEmissive, sceneDepth, directionalShadow, spotShadow, pointShadow,
                 environmentGraphReady, environmentCube, environmentSH,
                 ddgiEnabled = m_DDGIEnabled && ddgiResources.enabled,
                 ddgiMetadata, ddgiProbeSH2](
                    RenderGraphBuilder& builder) {
                    builder.ReadTexture(gbufferAlbedo);
                    builder.ReadTexture(gbufferNormal);
                    builder.ReadTexture(gbufferMaterial);
                    builder.ReadTexture(gbufferEmissive);
                    builder.ReadTexture(sceneDepth);
                    builder.ReadTexture(directionalShadow);
                    builder.ReadTexture(spotShadow);
                    builder.ReadTexture(pointShadow);
                    if (environmentGraphReady) {
                        builder.ReadTexture(environmentCube);
                        builder.ReadBuffer(environmentSH);
                    }
                    if (ddgiEnabled) {
                        builder.ReadBuffer(ddgiMetadata);
                        builder.ReadBuffer(ddgiProbeSH2);
                    }
                    builder.WriteColor(deferredSceneColor, RHILoadOp::Clear,
                                       RHIStoreOp::Store, {0, 0, 0, 1});
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

            m_RenderGraph->AddPass("ForwardTransparent",
                [deferredSceneColor, sceneDepth](RenderGraphBuilder& builder) {
                    builder.WriteColor(deferredSceneColor, RHILoadOp::Load, RHIStoreOp::Store);
                    builder.WriteDepth(sceneDepth, RHILoadOp::Load, RHIStoreOp::Store, 1.0f);
                },
                [this, &scene, &camera](GpuCommandList& commands, const RenderGraphResources&) {
                    const auto start = std::chrono::steady_clock::now();
                    m_MainPass->ExecuteTransparentOnly(commands, scene, camera);
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
        } else {
            m_RenderGraph->AddPass("Main",
                [sceneColor, sceneDepth, directionalShadow, spotShadow, pointShadow,
                 environmentGraphReady, environmentCube, environmentSH](RenderGraphBuilder& builder) {
                    builder.ReadTexture(directionalShadow);
                    builder.ReadTexture(spotShadow);
                    builder.ReadTexture(pointShadow);
                    if (environmentGraphReady) {
                        builder.ReadTexture(environmentCube);
                        builder.ReadBuffer(environmentSH);
                    }
                    builder.WriteColor(sceneColor, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0.0f, 0.0f, 0.0f, 1.0f});
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
        if (postOptions.ssaoEnabled) {
            m_RenderGraph->AddPass("SSAO", [sceneDepth, ssao](RenderGraphBuilder& builder) {
                builder.ReadTexture(sceneDepth);
                builder.WriteColor(ssao, RHILoadOp::Clear, RHIStoreOp::Store, {1, 1, 1, 1});
            },
            [this, &scene, &camera](GpuCommandList& commands, const RenderGraphResources&) {
                const auto start = std::chrono::steady_clock::now();
                m_PostProcessPass->DrawSSAOOcclusion(commands, scene, camera);
                const auto end = std::chrono::steady_clock::now();
                RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
                stats.ssaoCpuMs += ElapsedMs(start, end);
                ++stats.fullscreenDrawCalls;
                FrameStatsProvider::SetRendererStats(stats);
            });
            m_RenderGraph->AddPass("SSAOBlurH", [ssao, ssaoBlur](RenderGraphBuilder& builder) {
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
            m_RenderGraph->AddPass("SSAOBlurV", [ssaoBlur, ssao](RenderGraphBuilder& builder) {
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
        }
        m_RenderGraph->AddPass("Composite",
            [compositeInput, ssao, ssaoEnabled = postOptions.ssaoEnabled,
             compositeToBackbuffer, composite, backBuffer](RenderGraphBuilder& builder) {
                builder.ReadTexture(compositeInput);
                if (ssaoEnabled) builder.ReadTexture(ssao);
                if (compositeToBackbuffer) {
                    builder.WriteColor(backBuffer, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0, 0, 0, 1});
                } else {
                    builder.WriteColor(composite, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0, 0, 0, 1});
                }
            },
            [this, &scene, useDeferred](GpuCommandList& commands, const RenderGraphResources&) {
                const auto start = std::chrono::steady_clock::now();
                GpuTextureView* sceneColorOverride = nullptr;
                if (useDeferred) {
                    sceneColorOverride =
                        m_DeferredLightingPass->GetGraphResources().sceneColorSrv.get();
                }
                if (m_PostProcessPass->IsCompositeToBackbuffer()) {
                    if (sceneColorOverride) {
                        m_PostProcessPass->DrawCompositeToCurrentTarget(
                            commands, scene, sceneColorOverride);
                    } else {
                        m_PostProcessPass->DrawCompositeToCurrentTarget(commands, scene);
                    }
                } else {
                    if (sceneColorOverride) {
                        m_PostProcessPass->DrawCompositeOffscreen(
                            commands, scene, sceneColorOverride);
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
        if (m_UIDrawList && !m_UIDrawList->Empty()) {
            m_RenderGraph->AddPass("ScreenUI",
                [compositeToBackbuffer, composite, backBuffer](RenderGraphBuilder& builder) {
                    if (compositeToBackbuffer) {
                        builder.WriteColor(backBuffer, RHILoadOp::Load, RHIStoreOp::Store);
                    } else {
                        builder.WriteColor(composite, RHILoadOp::Load, RHIStoreOp::Store);
                    }
                },
                [this](GpuCommandList& commands, const RenderGraphResources&) {
                    m_ScreenUIPass->Execute(commands, *m_UIDrawList);
                });
        }
    } else {
        GpuTextureView* backBufferView = m_FrameContext->GetCurrentBackBufferView();
        if (!backBufferView || !backBufferView->texture) {
            Logger::Error("[Renderer] RHI returned no current backbuffer view for main pass");
            endFrameOnFailure();
            return;
        }
        auto backBufferSharedView =
            std::shared_ptr<GpuTextureView>(backBufferView->texture, backBufferView);
        const auto backBuffer = m_RenderGraph->ImportTexture(
            "BackBuffer", backBufferView->texture, backBufferSharedView,
            RHIResourceState::RenderTarget, RHIResourceState::RenderTarget);
        m_RenderGraph->AddPass("Main",
            [backBuffer, directionalShadow, spotShadow, pointShadow,
             environmentGraphReady, environmentCube, environmentSH](RenderGraphBuilder& builder) {
                builder.ReadTexture(directionalShadow);
                builder.ReadTexture(spotShadow);
                builder.ReadTexture(pointShadow);
                if (environmentGraphReady) {
                    builder.ReadTexture(environmentCube);
                    builder.ReadBuffer(environmentSH);
                }
                builder.WriteColor(backBuffer, RHILoadOp::Clear, RHIStoreOp::Store,
                                   {0.12f, 0.12f, 0.18f, 1.0f});
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
        if (m_UIDrawList && !m_UIDrawList->Empty()) {
            m_RenderGraph->AddPass("ScreenUI",
                [backBuffer](RenderGraphBuilder& builder) {
                    builder.WriteColor(backBuffer, RHILoadOp::Load, RHIStoreOp::Store);
                },
                [this](GpuCommandList& commands, const RenderGraphResources&) {
                    m_ScreenUIPass->Execute(commands, *m_UIDrawList);
                });
        }
    }
    if (!m_RenderGraph->Execute(*commandList)) {
        Logger::Error("[Renderer] RenderGraph execution failed: ", m_RenderGraph->GetLastError());
        endFrameOnFailure();
        return;
    }
    if (useOffscreen) {
        m_ShadowPass->MarkGraphResourcesShaderResource();
        m_EnvironmentPass->MarkGraphResourcesShaderResource();
        if (useDeferred) {
            m_GBufferPass->MarkGraphResourcesShaderResource();
            m_DeferredLightingPass->MarkGraphResourcesShaderResource();
            if (m_DDGIEnabled) {
                m_DDGIPass->MarkGraphResourcesShaderResource();
            }
        }
        m_PostProcessPass->MarkGraphResourcesShaderResource(
            !m_PostProcessPass->IsCompositeToBackbuffer());
    } else {
        m_ShadowPass->MarkGraphResourcesShaderResource();
        m_EnvironmentPass->MarkGraphResourcesShaderResource();
    }
    RendererFrameStats rendererStats = FrameStatsProvider::GetRendererStats();
    rendererStats.drawCalls = rendererStats.shadowDrawCalls +
        rendererStats.mainDrawCalls + rendererStats.fullscreenDrawCalls;
    FrameStatsProvider::SetRendererStats(rendererStats);

    if (present) {
        m_FrameContext->EndFrame();
    }
}

void Renderer::SetOutputOffscreen(bool enabled)
{
    m_OutputOffscreen = enabled;
    if (m_PostProcessPass) {
        m_PostProcessPass->SetCompositeToBackbuffer(!enabled);
    }
}

GpuTextureView* Renderer::GetSceneColorView() const
{
    return m_PostProcessPass ? m_PostProcessPass->GetSceneColorView() : nullptr;
}
