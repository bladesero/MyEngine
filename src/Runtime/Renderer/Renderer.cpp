#include "Renderer/Renderer.h"

#include "Renderer/GpuUploadQueue.h"
#include "Renderer/D3D11Context.h"
#include "Renderer/D3D12Context.h"
#include "Renderer/EnvironmentPass.h"
#include "Renderer/MainPass.h"
#include "Renderer/PostProcessPass.h"
#include "Renderer/ShaderManager.h"
#include "Renderer/ShadowPass.h"

Renderer::Renderer(IRenderContext* context)
    : m_Context(context)
    , m_ShadowPass(std::make_unique<ShadowPass>(context))
    , m_EnvironmentPass(std::make_unique<EnvironmentPass>(context))
    , m_MainPass(std::make_unique<MainPass>(context))
    , m_PostProcessPass(std::make_unique<PostProcessPass>(context))
{
    ShaderManager::Get().SetContext(context);
}

Renderer::~Renderer() = default;

void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (m_ShadowPass) m_ShadowPass->Resize(width, height);
    if (m_EnvironmentPass) m_EnvironmentPass->Resize(width, height);
    if (m_MainPass) m_MainPass->Resize(width, height);
    if (m_PostProcessPass) m_PostProcessPass->Resize(width, height);
}

void Renderer::RenderScene(const Scene& scene, const Camera& camera, bool present)
{
    if (!m_Context || !m_ShadowPass || !m_MainPass || !m_PostProcessPass) return;

    GpuUploadQueue::Get().Process(*m_Context);

    m_Context->BeginFrame(0.12f, 0.12f, 0.18f);

    m_ShadowPass->Execute(scene, camera);
    m_EnvironmentPass->Execute(scene, camera);

    // HDR passthrough when PostProcessPass handles compositing (D3D11/D3D12).
    const bool useOffscreen =
        dynamic_cast<D3D11Context*>(m_Context) != nullptr ||
        dynamic_cast<D3D12Context*>(m_Context) != nullptr;
    m_MainPass->SetHdrPassthrough(useOffscreen);

    const uint32_t cascadeCount = m_ShadowPass->GetCascadeCount();
    Mat4 cascades[3] = {
        m_ShadowPass->GetCascadeViewProj(0),
        cascadeCount > 1 ? m_ShadowPass->GetCascadeViewProj(1)
                         : m_ShadowPass->GetCascadeViewProj(0),
        cascadeCount > 2 ? m_ShadowPass->GetCascadeViewProj(2)
                         : m_ShadowPass->GetCascadeViewProj(0)
    };

    m_MainPass->SetShadowInput(
        m_ShadowPass->GetLightViewProj(),
        m_ShadowPass->GetLightDirection(),
        m_ShadowPass->IsDirectionalShadowEnabled(),
        m_ShadowPass->GetShadowMapTexture(),
        m_ShadowPass->GetSpotLightViewProj(),
        m_ShadowPass->GetSpotShadowIndex(),
        m_ShadowPass->GetSpotShadowMapTexture(),
        m_ShadowPass->GetPointShadowPosition(),
        m_ShadowPass->GetPointShadowRange(),
        m_ShadowPass->GetPointShadowIndex(),
        m_ShadowPass->GetPointShadowMapTexture(),
        cascadeCount > 0 ? cascades : nullptr,
        cascadeCount,
        m_ShadowPass->GetCascadeSplits());
    m_MainPass->SetEnvironmentInput(
        m_EnvironmentPass->GetEnvironmentCubemap(),
        m_EnvironmentPass->GetSH2Buffer(),
        m_EnvironmentPass->GetSH2Coefficients());

    if (useOffscreen) {
        m_PostProcessPass->BeginOffscreen();
        m_MainPass->Execute(scene, camera);
        m_PostProcessPass->RenderSSAO(scene, camera);
        m_PostProcessPass->RenderBloom(scene);
        m_PostProcessPass->EndOffscreenAndComposite(scene);
    } else {
        m_MainPass->Execute(scene, camera);
    }

    if (present) {
        m_Context->EndFrame();
    }
}

void Renderer::SetEditorOffscreen(bool enabled)
{
    m_EditorOffscreen = enabled;
    if (m_PostProcessPass) {
        m_PostProcessPass->SetCompositeToBackbuffer(!enabled);
    }
}

void* Renderer::GetSceneColorTextureHandle() const
{
    return m_PostProcessPass ? m_PostProcessPass->GetSceneColorTextureHandle() : nullptr;
}
