#include "Game/SceneRenderLayer.h"

#include "Core/Event.h"
#include "Core/Logger.h"
#include "Game/DefaultSceneFactory.h"
#include "Renderer/IRenderContext.h"
#include "Renderer/RHI/GpuSwapChain.h"

SceneRenderLayer::SceneRenderLayer(IRenderContext* context,
                                   int viewportWidth, int viewportHeight)
    : SceneLayer("SceneRenderLayer")
    , m_RenderContext(context)
    , m_Viewport(context, context, context)
    , m_GameViewport(context, context, context)
{
    m_Viewport.Initialize(viewportWidth, viewportHeight);
    m_GameViewport.Initialize(viewportWidth, viewportHeight);
}

void SceneRenderLayer::SetPresentEnabled(bool enabled)
{
    m_PresentEnabled = enabled;
}

void SceneRenderLayer::SetSceneViewportUsesSimulationScene(bool enabled)
{
    m_SceneViewportUsesSimulationScene = enabled;
}

void SceneRenderLayer::SetRenderPath(RenderPath path)
{
    m_Viewport.SetRenderPath(path);
    m_GameViewport.SetRenderPath(path);
}

RenderPath SceneRenderLayer::GetRenderPath() const
{
    return m_Viewport.GetRenderPath();
}

void SceneRenderLayer::SetDDGIEnabled(bool enabled)
{
    m_Viewport.SetDDGIEnabled(enabled);
    m_GameViewport.SetDDGIEnabled(enabled);
}

bool SceneRenderLayer::IsDDGIEnabled() const
{
    return m_Viewport.IsDDGIEnabled();
}

void SceneRenderLayer::SetDDGIDebugView(DDGIDebugView view)
{
    m_Viewport.SetDDGIDebugView(view);
    m_GameViewport.SetDDGIDebugView(view);
}

DDGIDebugView SceneRenderLayer::GetDDGIDebugView() const
{
    return m_Viewport.GetDDGIDebugView();
}

Scene& SceneRenderLayer::GetSceneViewportRenderScene()
{
    return m_SceneViewportUsesSimulationScene && HasPlayWorld()
        ? GetSimulationScene()
        : GetEditorScene();
}

const Scene& SceneRenderLayer::GetSceneViewportRenderScene() const
{
    return m_SceneViewportUsesSimulationScene && HasPlayWorld()
        ? GetSimulationScene()
        : GetEditorScene();
}

void SceneRenderLayer::OnAttach()
{
    SceneLayer::OnAttach();
    int x = 0, y = 0, w = 0, h = 0;
    m_GameViewport.GetViewportRect(x, y, w, h);
    m_UIInputViewport = {x, y, w, h, 1.0f, 1.0f, true, true};
    m_UISystem.Resize(w, h);
    if (m_RenderContext) {
        m_UISystem.Initialize(m_RenderContext, m_RenderContext);
    }
    m_Viewport.GetViewportRect(x, y, w, h);
    Logger::Info("[SceneRenderLayer] attached (", w, "x", h, ")");
}

void SceneRenderLayer::OnDetach()
{
    m_UISystem.Shutdown();
    SceneLayer::OnDetach();
}

void SceneRenderLayer::OnUpdate(float dt)
{
    SceneLayer::OnUpdate(dt);
    m_Viewport.OnUpdate(dt);
    m_UISystem.Update(GetSimulationScene(), dt);
}

void SceneRenderLayer::OnEvent(Event& event)
{
    m_UISystem.ProcessEvent(GetSimulationScene(), event, m_UIInputViewport);
    if (event.handled) return;
    SceneLayer::OnEvent(event);
    if (event.type == EventType::WindowResize) {
        const int windowW = event.resize.width;
        const int windowH = event.resize.height;
        if (windowW <= 0 || windowH <= 0) return;
        m_Viewport.OnWindowResize(windowW, windowH);
        m_GameViewport.OnWindowResize(windowW, windowH);
        if (m_PresentEnabled) {
            m_UIInputViewport = {0, 0, windowW, windowH, 1.0f, 1.0f, true, true};
            m_UISystem.Resize(windowW, windowH);
        }
        if (m_RenderContext) {
            if (GpuSwapChain* swapChain = m_RenderContext->GetSwapChain()) {
                m_GameViewport.ReleaseFrameResources();
                swapChain->Resize(static_cast<uint32_t>(windowW),
                                  static_cast<uint32_t>(windowH));
            }
        }
    }
}

void SceneRenderLayer::OnSceneLoaded()
{
    SceneLayer::OnSceneLoaded();
    DefaultSceneFactory::PopulateIfEmpty(GetEditorScene());
}

void SceneRenderLayer::OnRender()
{
    m_UISystem.CollectDrawData(GetSimulationScene(), m_UIDrawList);
    if (m_PresentEnabled) {
        m_GameViewport.Render(GetSimulationScene(), true, &m_UIDrawList);
        return;
    }
    if (m_SceneViewportActive) {
        m_Viewport.Render(GetSceneViewportRenderScene(), false);
    }
    if (m_GameViewportActive) {
        m_GameViewport.Render(GetSimulationScene(), false, &m_UIDrawList);
    }
}

void SceneRenderLayer::SetViewportInputEnabled(bool enabled)
{
    m_Viewport.SetInputEnabled(enabled);
}

void SceneRenderLayer::SetUIInputViewport(const UIInputViewport& viewport)
{
    m_UIInputViewport = viewport;
    const int contextWidth = static_cast<int>(static_cast<float>(viewport.width) * viewport.scaleX);
    const int contextHeight = static_cast<int>(static_cast<float>(viewport.height) * viewport.scaleY);
    m_UISystem.Resize(contextWidth, contextHeight);
}

void SceneRenderLayer::GetViewportRect(int& outX, int& outY, int& outW, int& outH) const
{
    m_Viewport.GetViewportRect(outX, outY, outW, outH);
}

bool SceneRenderLayer::BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const
{
    return m_Viewport.BuildRayFromScreen(screenX, screenY, outRay);
}
