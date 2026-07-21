#include "Game/ViewportRenderExecution.h"

#include "Camera/Camera.h"
#include "Game/RenderViewport.h"
#include "Renderer/RHI/IRHIFrameContext.h"
#include "Scene/Scene.h"
#include "UI/Render/UIDrawList.h"

ViewportRenderExecution::ViewportRenderExecution(IRHIDevice* device, IRHIFrameContext* frameContext,
                                                 IRHIReadbackService* readbackService)
    : m_FrameContext(frameContext), m_Renderer(device, frameContext, readbackService) {
}

void ViewportRenderExecution::ResizeIfNeeded(int width, int height) {
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

void ViewportRenderExecution::Render(Scene& scene, Camera& camera, const RenderViewport& viewport,
                                     bool presentToSwapchain, const UIDrawList* uiDrawList) {
    int x = 0, y = 0, w = 0, h = 0;
    viewport.GetViewportRect(x, y, w, h);
    if (w <= 0 || h <= 0) {
        return;
    }

    ResizeIfNeeded(w, h);
    SetCommandViewport(viewport, presentToSwapchain);
    m_Renderer.SetOutputOffscreen(!presentToSwapchain);
    m_Renderer.SetUIDrawList(uiDrawList);
    m_Renderer.RenderScene(scene, camera, presentToSwapchain);
    m_Renderer.SetUIDrawList(nullptr);
}

void ViewportRenderExecution::ReleaseFrameResources() {
    m_Renderer.ReleaseFrameResources();
}

GpuTextureView* ViewportRenderExecution::GetOutputView() const {
    return m_Renderer.GetSceneColorView();
}

void ViewportRenderExecution::SetRenderPath(RenderPath path) {
    m_Renderer.SetRenderPath(path);
}

RenderPath ViewportRenderExecution::GetRenderPath() const {
    return m_Renderer.GetRenderPath();
}

void ViewportRenderExecution::SetDeviceProfile(GraphicsDeviceProfile profile) {
    m_Renderer.SetDeviceProfile(profile);
}

GraphicsDeviceProfile ViewportRenderExecution::GetDeviceProfile() const {
    return m_Renderer.GetDeviceProfile();
}

void ViewportRenderExecution::SetHardwareRayTracingEnabled(bool enabled) {
    m_Renderer.SetHardwareRayTracingEnabled(enabled);
}

bool ViewportRenderExecution::IsHardwareRayTracingEnabled() const {
    return m_Renderer.IsHardwareRayTracingEnabled();
}

uint32_t ViewportRenderExecution::GetRayTracingRequestedMask() const {
    return m_Renderer.GetRayTracingRequestedMask();
}

uint32_t ViewportRenderExecution::GetRayTracingEffectiveMask() const {
    return m_Renderer.GetRayTracingEffectiveMask();
}

std::string ViewportRenderExecution::GetRayTracingFallbackReason() const {
    return m_Renderer.GetRayTracingFallbackReason();
}

const RenderPipelineDiagnostics& ViewportRenderExecution::GetPipelineDiagnostics() const {
    return m_Renderer.GetPipelineDiagnostics();
}

void ViewportRenderExecution::SetFeatureMask(RendererFeatureMask mask) {
    m_Renderer.SetFeatureMask(mask);
}

RendererFeatureMask ViewportRenderExecution::GetFeatureMask() const {
    return m_Renderer.GetFeatureMask();
}

void ViewportRenderExecution::SetDebugView(RendererDebugView view) {
    m_Renderer.SetDebugView(view);
}

RendererDebugView ViewportRenderExecution::GetDebugView() const {
    return m_Renderer.GetDebugView();
}

void ViewportRenderExecution::InvalidateTemporalHistory(const std::string& reason, bool resetObjectHistory) {
    m_Renderer.InvalidateTemporalHistory(reason, resetObjectHistory);
}

void ViewportRenderExecution::SetCommandViewport(const RenderViewport& viewport, bool presentToSwapchain) {
    if (!m_FrameContext) {
        return;
    }

    int x = 0, y = 0, w = 0, h = 0;
    viewport.GetViewportRect(x, y, w, h);
    if (w <= 0 || h <= 0) {
        return;
    }
    if (auto* commands = m_FrameContext->GetGraphicsCommandList()) {
        const float viewportX = presentToSwapchain ? static_cast<float>(x) : 0.0f;
        const float viewportY = presentToSwapchain ? static_cast<float>(y) : 0.0f;
        commands->SetViewport(viewportX, viewportY, static_cast<float>(w), static_cast<float>(h));
    }
}
