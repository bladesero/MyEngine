#pragma once

#include "Game/SceneLayer.h"
#include "Game/GameViewport.h"
#include "Game/SceneViewportController.h"
#include "Renderer/IRenderContext.h"
#include "UI/Core/UISystem.h"
#include "UI/Render/UIDrawList.h"

struct GpuTextureView;

class SceneRenderLayer : public SceneLayer {
public:
    SceneRenderLayer(IRenderContext* context, int viewportWidth, int viewportHeight);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float dt) override;
    void OnEvent(Event& event) override;
    void OnRender() override;

    void SetPresentEnabled(bool enabled);
    void SetViewportInputEnabled(bool enabled);
    void SetSceneViewportUsesSimulationScene(bool enabled);
    bool GetSceneViewportUsesSimulationScene() const { return m_SceneViewportUsesSimulationScene; }
    Scene& GetSceneViewportRenderScene();
    const Scene& GetSceneViewportRenderScene() const;

    IRenderContext* GetRenderContext() const { return m_RenderContext; }
    GpuTextureView* GetSceneColorView() const { return m_Viewport.GetOutputView(); }
    SceneViewport* GetSceneViewport() { return &m_Viewport; }
    const SceneViewport* GetSceneViewport() const { return &m_Viewport; }
    GameViewport* GetGameViewport() { return &m_GameViewport; }
    const GameViewport* GetGameViewport() const { return &m_GameViewport; }

    Camera& GetCamera() { return m_Viewport.GetCamera(); }
    const Camera& GetCamera() const { return m_Viewport.GetCamera(); }

    void GetViewportRect(int& outX, int& outY, int& outW, int& outH) const;
    bool BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const;

protected:
    void OnSceneLoaded() override;

private:
    IRenderContext* m_RenderContext = nullptr;
    SceneViewport m_Viewport;
    GameViewport m_GameViewport;
    UISystem m_UISystem;
    UIDrawList m_UIDrawList;
    bool m_PresentEnabled = true;
    bool m_SceneViewportUsesSimulationScene = false;
};
