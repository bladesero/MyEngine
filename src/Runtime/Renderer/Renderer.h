#pragma once

#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/RHI/IRHIFrameContext.h"
#include "Renderer/RHI/IRHIReadbackService.h"
#include "Renderer/RenderPath.h"
#include "Scene/Scene.h"
#include "Camera/Camera.h"

#include <memory>

class ShadowPass;
class PostProcessPass;
class MainPass;
class EnvironmentPass;
class GBufferPass;
class DeferredLightingPass;
class ScreenUIPass;
class RenderGraph;
class UIDrawList;

// ============================================================================
// Renderer  鈥? minimal scene renderer for MeshRendererComponent
//
//  - Owns no window; works on top of split RHI device/frame/readback services
//  - Traverses Scene, finds actors with MeshRendererComponent and draws them
//  - Uses row-major, left-handed math (Mat4, Camera) and MeshShader.h
// ============================================================================

class Renderer {
public:
    Renderer(IRHIDevice* device, IRHIFrameContext* frameContext, IRHIReadbackService* readbackService);
    ~Renderer();

    void Resize(uint32_t width, uint32_t height);

    // Render all visible MeshRendererComponent in the scene from the camera.
    // If present == false, the caller is responsible for ending the RHI frame
    // (useful for editor overlays like ImGui).
    void RenderScene(const Scene& scene, const Camera& camera, bool present = true);
    void SetUIDrawList(const UIDrawList* drawList) { m_UIDrawList = drawList; }

    void SetOutputOffscreen(bool enabled);
    GpuTextureView* GetSceneColorView() const;
    void ReleaseFrameResources();
    void SetRenderPath(RenderPath path) { m_RenderPath = path; }
    RenderPath GetRenderPath() const { return m_RenderPath; }

private:
    IRHIDevice* m_Device = nullptr;
    IRHIFrameContext* m_FrameContext = nullptr;
    IRHIReadbackService* m_ReadbackService = nullptr;
    std::unique_ptr<ShadowPass> m_ShadowPass;
    std::unique_ptr<EnvironmentPass> m_EnvironmentPass;
    std::unique_ptr<MainPass> m_MainPass;
    std::unique_ptr<GBufferPass> m_GBufferPass;
    std::unique_ptr<DeferredLightingPass> m_DeferredLightingPass;
    std::unique_ptr<PostProcessPass> m_PostProcessPass;
    std::unique_ptr<ScreenUIPass> m_ScreenUIPass;
    std::unique_ptr<RenderGraph> m_RenderGraph;
    RenderPath m_RenderPath = RenderPath::Forward;
    bool m_OutputOffscreen = false;
    const UIDrawList* m_UIDrawList = nullptr;
};
