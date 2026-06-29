#include "Renderer/Renderer.h"

#include "Renderer/GpuUploadQueue.h"
#include "Core/FrameStats.h"
#include "Core/Logger.h"
#include "Renderer/EnvironmentPass.h"
#include "Renderer/MainPass.h"
#include "Renderer/PostProcessPass.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/ShaderManager.h"
#include "Renderer/ShadowPass.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/ScreenUIPass.h"
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
    if (!m_Device || !m_FrameContext || !m_ShadowPass || !m_MainPass || !m_PostProcessPass) return;

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
    m_MainPass->SetHdrPassthrough(useOffscreen);
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
                FrameStatsProvider::SetRendererStats(stats);
            });
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
            [sceneColor, ssao, ssaoEnabled = postOptions.ssaoEnabled,
             compositeToBackbuffer, composite, backBuffer](RenderGraphBuilder& builder) {
                builder.ReadTexture(sceneColor);
                if (ssaoEnabled) builder.ReadTexture(ssao);
                if (compositeToBackbuffer) {
                    builder.WriteColor(backBuffer, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0, 0, 0, 1});
                } else {
                    builder.WriteColor(composite, RHILoadOp::Clear, RHIStoreOp::Store,
                                       {0, 0, 0, 1});
                }
            },
            [this, &scene](GpuCommandList& commands, const RenderGraphResources&) {
                const auto start = std::chrono::steady_clock::now();
                if (m_PostProcessPass->IsCompositeToBackbuffer()) {
                    m_PostProcessPass->DrawCompositeToCurrentTarget(commands, scene);
                } else {
                    m_PostProcessPass->DrawCompositeOffscreen(commands, scene);
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
