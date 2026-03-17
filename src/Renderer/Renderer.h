#pragma once

#include "Renderer/IRenderContext.h"
#include "Renderer/MeshShader.h"
#include "Scene/Scene.h"
#include "Scene/MeshRendererComponent.h"
#include "Camera/Camera.h"

#include <memory>

// ============================================================================
// Renderer  –  minimal scene renderer for MeshRendererComponent
//
//  - Owns no window; works on top of an IRenderContext (D3D11 in this repo)
//  - Traverses Scene, finds actors with MeshRendererComponent and draws them
//  - Uses row-major, left-handed math (Mat4, Camera) and MeshShader.h
// ============================================================================

class Renderer {
public:
    explicit Renderer(IRenderContext* context)
        : m_Context(context) {}

    // Render all visible MeshRendererComponent in the scene from the camera.
    // If present == false, the caller is responsible for calling IRenderContext::EndFrame()
    // (useful for editor overlays like ImGui).
    void RenderScene(const Scene& scene, const Camera& camera, bool present = true);

private:
    void EnsureMeshUploaded(MeshAsset* mesh);
    GpuShader* GetOrCreateMeshShader();

    IRenderContext*              m_Context = nullptr;
    std::shared_ptr<GpuShader>   m_MeshShader;
};

