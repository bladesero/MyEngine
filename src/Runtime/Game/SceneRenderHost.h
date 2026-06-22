#pragma once

#include "Renderer/IRenderContext.h"
#include "Renderer/Renderer.h"

class Camera;
class Scene;
class SceneViewportController;

class SceneRenderHost {
public:
    explicit SceneRenderHost(IRenderContext* context);

    void SetPresentEnabled(bool enabled);
    void ResizeRendererIfNeeded(int width, int height);
    void OnWindowResize(int windowW, int windowH, const SceneViewportController& viewport);
    void Render(Scene& scene, Camera& camera, const SceneViewportController& viewport);

    GpuTextureView* GetSceneColorView() const;
    IRenderContext* GetRenderContext() const { return m_RenderContext; }

private:
    void SetCommandViewport(const SceneViewportController& viewport);

    IRenderContext* m_RenderContext = nullptr;
    Renderer m_Renderer;
    int m_RendererW = 0;
    int m_RendererH = 0;
    bool m_PresentEnabled = true;
};
