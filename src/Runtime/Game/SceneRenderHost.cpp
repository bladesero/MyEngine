#include "Game/SceneRenderHost.h"

#include "Camera/Camera.h"
#include "Game/SceneViewportController.h"
#include "Scene/Scene.h"

SceneRenderHost::SceneRenderHost(IRenderContext* context)
    : m_RenderContext(context)
    , m_Renderer(context, context, context)
{}

void SceneRenderHost::SetPresentEnabled(bool enabled)
{
    m_PresentEnabled = enabled;
    m_Renderer.SetOutputOffscreen(!enabled);
}

void SceneRenderHost::ResizeRendererIfNeeded(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    if (m_RendererW == width && m_RendererH == height) {
        return;
    }
    m_Renderer.Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    m_RendererW = width;
    m_RendererH = height;
}

void SceneRenderHost::OnWindowResize(int windowW, int windowH, const SceneViewportController& viewport)
{
    if (!m_RenderContext || windowW <= 0 || windowH <= 0) {
        return;
    }

    if (!viewport.UsesEditorViewport()) {
        int x = 0, y = 0, w = 0, h = 0;
        viewport.GetViewportRect(x, y, w, h);
        ResizeRendererIfNeeded(w, h);
    }

    if (GpuSwapChain* swapChain = m_RenderContext->GetSwapChain()) {
        swapChain->Resize(static_cast<uint32_t>(windowW),
                          static_cast<uint32_t>(windowH));
    }
    SetCommandViewport(viewport);
}

void SceneRenderHost::Render(Scene& scene, Camera& camera, const SceneViewportController& viewport)
{
    if (!m_RenderContext) {
        return;
    }

    int x = 0, y = 0, w = 0, h = 0;
    viewport.GetViewportRect(x, y, w, h);
    ResizeRendererIfNeeded(w, h);
    SetCommandViewport(viewport);
    m_Renderer.RenderScene(scene, camera, m_PresentEnabled);
}

GpuTextureView* SceneRenderHost::GetSceneColorView() const
{
    return m_Renderer.GetSceneColorView();
}

void SceneRenderHost::SetCommandViewport(const SceneViewportController& viewport)
{
    if (!m_RenderContext) {
        return;
    }
    int x = 0, y = 0, w = 0, h = 0;
    viewport.GetViewportRect(x, y, w, h);
    if (w <= 0 || h <= 0) {
        return;
    }
    if (auto* commands = m_RenderContext->GetGraphicsCommandList()) {
        commands->SetViewport(static_cast<float>(x), static_cast<float>(y),
                              static_cast<float>(w), static_cast<float>(h));
    }
}
