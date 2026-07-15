#pragma once

#include "Renderer/Renderer.h"
#include "Renderer/RenderPath.h"

class Camera;
struct GpuTextureView;
class IRHIDevice;
class IRHIFrameContext;
class IRHIReadbackService;
class RenderViewport;
class Scene;
class UIDrawList;

class ViewportRenderExecution {
public:
    ViewportRenderExecution(IRHIDevice* device, IRHIFrameContext* frameContext, IRHIReadbackService* readbackService);

    void ResizeIfNeeded(int width, int height);
    void Render(Scene& scene, Camera& camera, const RenderViewport& viewport, bool presentToSwapchain,
                const UIDrawList* uiDrawList = nullptr);
    void ReleaseFrameResources();

    GpuTextureView* GetOutputView() const;
    void SetRenderPath(RenderPath path);
    RenderPath GetRenderPath() const;
    void SetFeatureMask(RendererFeatureMask mask);
    RendererFeatureMask GetFeatureMask() const;

private:
    void SetCommandViewport(const RenderViewport& viewport, bool presentToSwapchain);

    IRHIFrameContext* m_FrameContext = nullptr;
    Renderer m_Renderer;
    int m_RendererW = 0;
    int m_RendererH = 0;
};
