#pragma once

#include "Renderer/IRenderContext.h"
#include "Scene/Scene.h"
#include "Camera/Camera.h"

#include <memory>

class ShadowPass;
class PostProcessPass;
class MainPass;
class EnvironmentPass;
class RenderGraph;

// ============================================================================
// Renderer  鈥? minimal scene renderer for MeshRendererComponent
//
//  - Owns no window; works on top of an IRenderContext (D3D11 in this repo)
//  - Traverses Scene, finds actors with MeshRendererComponent and draws them
//  - Uses row-major, left-handed math (Mat4, Camera) and MeshShader.h
// ============================================================================

class Renderer {
public:
    explicit Renderer(IRenderContext* context);
    ~Renderer();

    void Resize(uint32_t width, uint32_t height);

    // Render all visible MeshRendererComponent in the scene from the camera.
    // If present == false, the caller is responsible for calling IRenderContext::EndFrame()
    // (useful for editor overlays like ImGui).
    void RenderScene(const Scene& scene, const Camera& camera, bool present = true);

    void SetEditorOffscreen(bool enabled);
    GpuTextureView* GetSceneColorView() const;

private:
    IRenderContext*            m_Context = nullptr;
    std::unique_ptr<ShadowPass> m_ShadowPass;
    std::unique_ptr<EnvironmentPass> m_EnvironmentPass;
    std::unique_ptr<MainPass>   m_MainPass;
    std::unique_ptr<PostProcessPass> m_PostProcessPass;
    std::unique_ptr<RenderGraph> m_RenderGraph;
    bool m_EditorOffscreen = false;
};

