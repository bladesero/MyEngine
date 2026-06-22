#include "Game/SceneRenderLayer.h"

#include "Core/Event.h"
#include "Core/Logger.h"
#include "Game/DefaultSceneFactory.h"

SceneRenderLayer::SceneRenderLayer(IRenderContext* context,
                                   int viewportWidth, int viewportHeight)
    : SceneLayer("SceneRenderLayer")
    , m_RenderHost(context)
{
    m_Viewport.Initialize(viewportWidth, viewportHeight);
}

void SceneRenderLayer::SetPresentEnabled(bool enabled)
{
    m_RenderHost.SetPresentEnabled(enabled);
}

void SceneRenderLayer::OnAttach()
{
    SceneLayer::OnAttach();
    int x = 0, y = 0, w = 0, h = 0;
    m_Viewport.GetViewportRect(x, y, w, h);
    m_RenderHost.ResizeRendererIfNeeded(w, h);
    Logger::Info("[SceneRenderLayer] attached (", w, "x", h, ")");
}

void SceneRenderLayer::OnDetach()
{
    SceneLayer::OnDetach();
}

void SceneRenderLayer::OnUpdate(float dt)
{
    SceneLayer::OnUpdate(dt);
    m_Viewport.OnUpdate(dt);
}

void SceneRenderLayer::OnEvent(Event& event)
{
    SceneLayer::OnEvent(event);
    if (event.type == EventType::WindowResize) {
        const int windowW = event.resize.width;
        const int windowH = event.resize.height;
        if (windowW <= 0 || windowH <= 0) return;
        m_Viewport.OnWindowResize(windowW, windowH);
        m_RenderHost.OnWindowResize(windowW, windowH, m_Viewport);
    }
}

void SceneRenderLayer::OnSceneLoaded()
{
    SceneLayer::OnSceneLoaded();
    DefaultSceneFactory::PopulateIfEmpty(GetScene());
}

void SceneRenderLayer::OnRender()
{
    m_RenderHost.Render(GetScene(), m_Viewport.GetCamera(), m_Viewport);
}

void SceneRenderLayer::SetEditorViewportRect(int x, int y, int width, int height)
{
    m_Viewport.SetEditorViewportRect(x, y, width, height);
}

void SceneRenderLayer::SetViewportInputEnabled(bool enabled)
{
    m_Viewport.SetInputEnabled(enabled);
}

void SceneRenderLayer::GetViewportRect(int& outX, int& outY, int& outW, int& outH) const
{
    m_Viewport.GetViewportRect(outX, outY, outW, outH);
}

bool SceneRenderLayer::BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const
{
    return m_Viewport.BuildRayFromScreen(screenX, screenY, outRay);
}
