#pragma once

#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/RHI/IRHIFrameContext.h"
#include "Renderer/RHI/IRHIReadbackService.h"
#include "Renderer/RenderPath.h"
#include "Renderer/RendererFeatures.h"
#include "Scene/Scene.h"
#include "Camera/Camera.h"

#include <memory>
#include <array>
#include <vector>

class ShadowPass;
class PostProcessPass;
class MainPass;
class EnvironmentPass;
class GBufferPass;
class DeferredLightingPass;
class ScreenUIPass;
class ModernDeferredPipeline;
class ProbeLightingSystem;
class RenderGraph;
class UIDrawList;

enum class RendererDebugView : uint8_t { Final, HDRLighting, HiZ, MotionVectors, SSGI, SSRConfidence };

// ============================================================================
// Renderer  minimal scene renderer for MeshRendererComponent
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
    void SetDebugView(RendererDebugView view) { m_DebugView = view; }
    RendererDebugView GetDebugView() const { return m_DebugView; }
    void ReleaseFrameResources();
    void SetRenderPath(RenderPath path);
    RenderPath GetRenderPath() const { return m_RenderPath; }
    void SetDeviceProfile(GraphicsDeviceProfile profile);
    GraphicsDeviceProfile GetDeviceProfile() const { return m_DeviceProfile; }
    const RenderPipelineDiagnostics& GetPipelineDiagnostics() const { return m_PipelineDiagnostics; }
    void SetFeatureMask(RendererFeatureMask mask);
    void InvalidateTemporalHistory(const std::string& reason, bool resetObjectHistory = false);
    RendererFeatureMask GetFeatureMask() const { return m_FeatureMask; }

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
    std::unique_ptr<ModernDeferredPipeline> m_ModernDeferredPipeline;
    std::unique_ptr<ProbeLightingSystem> m_ProbeLightingSystem;
    std::unique_ptr<RenderGraph> m_RenderGraph;
    RenderPath m_RenderPath = RenderPath::Forward;
    GraphicsDeviceProfile m_DeviceProfile = GraphicsDeviceProfile::Desktop;
    RenderPipelineDiagnostics m_PipelineDiagnostics;
    bool m_ModernImplementationReady = false;
    bool m_ModernInitializationAttempted = false;
    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    RendererFeatureMask m_FeatureMask = RendererFeatureMask::All;
    bool m_OutputOffscreen = false;
    const UIDrawList* m_UIDrawList = nullptr;
    RendererDebugView m_DebugView = RendererDebugView::Final;
    std::array<std::shared_ptr<GpuTimestampQueryPool>, 3> m_FrameTimestampPools{};
    std::array<bool, 3> m_FrameTimestampRecorded{};
    uint8_t m_ShaderPrewarmMask = 0;
    uint64_t m_ShaderPrewarmSceneGeneration = 0;
    bool m_SceneShaderPrewarmComplete = false;
    std::vector<std::string> m_SceneShaderPrewarmPaths;

    bool PrewarmStartupShaders(const Scene& scene);
    void EnsureModernPipeline();
    void RefreshPipelineDiagnostics();
};
