#pragma once

#include "Camera/Camera.h"
#include "Game/ViewportRenderExecution.h"
#include "Math/Ray.h"
#include "Renderer/RenderPath.h"

struct GpuTextureView;
class IRHIDevice;
class IRHIFrameContext;
class IRHIReadbackService;
class Scene;
class UIDrawList;

class RenderViewport {
public:
    RenderViewport(IRHIDevice* device, IRHIFrameContext* frameContext, IRHIReadbackService* readbackService);
    virtual ~RenderViewport() = default;

    virtual void Initialize(int width, int height);
    virtual void OnUpdate(float dt) { (void)dt; }
    virtual void OnWindowResize(int width, int height);
    virtual void ResolveFrameCamera(const Scene& scene) { (void)scene; }
    virtual void Render(Scene& scene, bool presentToSwapchain, const UIDrawList* uiDrawList = nullptr);

    void SetViewportRect(int x, int y, int width, int height);
    void SetInputEnabled(bool enabled);
    void GetViewportRect(int& outX, int& outY, int& outW, int& outH) const;
    bool BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const;
    GpuTextureView* GetOutputView() const;
    void ReleaseFrameResources();
    void SetRenderPath(RenderPath path);
    RenderPath GetRenderPath() const;
    void SetFeatureMask(RendererFeatureMask mask);
    RendererFeatureMask GetFeatureMask() const;

    virtual Camera& GetCamera() = 0;
    virtual const Camera& GetCamera() const = 0;

    bool UsesExternalViewportRect() const { return m_UseExternalViewportRect; }
    bool IsInputEnabled() const { return m_InputEnabled; }
    float GetAspect() const;

protected:
    void UpdateCameraAspect(Camera& camera);

private:
    ViewportRenderExecution m_RenderExecution;
    int m_VpX = 0;
    int m_VpY = 0;
    int m_VpW = 0;
    int m_VpH = 0;
    bool m_UseExternalViewportRect = false;
    bool m_InputEnabled = true;
};
