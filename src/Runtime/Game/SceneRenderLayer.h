#pragma once

#include "Game/SceneLayer.h"
#include "Game/SceneRenderHost.h"
#include "Game/SceneViewportController.h"

class SceneRenderLayer : public SceneLayer {
public:
    SceneRenderLayer(IRenderContext* context, int viewportWidth, int viewportHeight);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    void OnEvent(Event& event) override;
    void OnRender() override;

    void SetPresentEnabled(bool enabled);
    void SetEditorViewportRect(int x, int y, int width, int height);
    void SetViewportInputEnabled(bool enabled);

    IRenderContext* GetRenderContext() const { return m_RenderHost.GetRenderContext(); }
    GpuTextureView* GetSceneColorView() const { return m_RenderHost.GetSceneColorView(); }
    SceneViewportController* GetSceneViewport() { return &m_Viewport; }
    const SceneViewportController* GetSceneViewport() const { return &m_Viewport; }
    SceneRenderHost* GetSceneRenderHost() { return &m_RenderHost; }
    const SceneRenderHost* GetSceneRenderHost() const { return &m_RenderHost; }

    Camera& GetCamera() { return m_Viewport.GetCamera(); }
    const Camera& GetCamera() const { return m_Viewport.GetCamera(); }

    void GetViewportRect(int& outX, int& outY, int& outW, int& outH) const;
    bool BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const;

protected:
    void OnSceneLoaded() override;

private:
    SceneViewportController m_Viewport;
    SceneRenderHost m_RenderHost;
};
