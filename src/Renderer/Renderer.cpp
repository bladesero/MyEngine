#include "Renderer/Renderer.h"

#include "Renderer/MainPass.h"
#include "Renderer/ShadowPass.h"

Renderer::Renderer(IRenderContext* context)
    : m_Context(context)
    , m_ShadowPass(std::make_unique<ShadowPass>(context))
    , m_MainPass(std::make_unique<MainPass>(context))
{}

Renderer::~Renderer() = default;

void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (m_ShadowPass) m_ShadowPass->Resize(width, height);
    if (m_MainPass) m_MainPass->Resize(width, height);
}

void Renderer::RenderScene(const Scene& scene, const Camera& camera, bool present)
{
    if (!m_Context || !m_ShadowPass || !m_MainPass) return;

    m_ShadowPass->Execute(scene, camera);

    m_MainPass->SetPresentEnabled(present);
    m_MainPass->SetShadowInput(
        m_ShadowPass->GetLightViewProj(),
        m_ShadowPass->GetLightDirection(),
        m_ShadowPass->GetShadowMapTexture());
    m_MainPass->Execute(scene, camera);
}
