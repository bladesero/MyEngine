#pragma once

#include "Renderer/RenderPass.h"
#include "Core/EngineMath.h"

#include <cstdint>
#include <memory>

struct ShaderHandle;

class PostProcessPass final : public RenderPass {
public:
    struct GraphResources {
        std::shared_ptr<GpuTexture> sceneColor;
        std::shared_ptr<GpuTextureView> sceneColorRtv;
        std::shared_ptr<GpuTextureView> sceneColorSrv;
        std::shared_ptr<GpuTexture> sceneDepth;
        std::shared_ptr<GpuTextureView> sceneDepthDsv;
        std::shared_ptr<GpuTextureView> sceneDepthSrv;
        std::shared_ptr<GpuTexture> ssao;
        std::shared_ptr<GpuTextureView> ssaoRtv;
        std::shared_ptr<GpuTextureView> ssaoSrv;
        std::shared_ptr<GpuTexture> ssaoBlur;
        std::shared_ptr<GpuTextureView> ssaoBlurRtv;
        std::shared_ptr<GpuTexture> composite;
        std::shared_ptr<GpuTextureView> compositeRtv;
        RHIResourceState sceneColorState = RHIResourceState::Undefined;
        RHIResourceState sceneDepthState = RHIResourceState::Undefined;
        RHIResourceState ssaoState = RHIResourceState::Undefined;
        RHIResourceState ssaoBlurState = RHIResourceState::Undefined;
        RHIResourceState compositeState = RHIResourceState::Undefined;
    };

    explicit PostProcessPass(IRHIDevice* device);

    void Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;
    bool PrepareGraphResources();
    GraphResources GetGraphResources() const;
    void MarkGraphResourcesShaderResource(bool compositeWritten);
    void BeginOffscreen(GpuCommandList& commands);
    void RenderSSAO(GpuCommandList& commands, const Scene& scene, const Camera& camera);
    void RenderBloom(GpuCommandList& commands, const Scene& scene);
    void EndOffscreenAndComposite(GpuCommandList& commands, const Scene& scene, GpuTextureView* backBufferView);
    void DrawSSAOOcclusion(GpuCommandList& commands, const Scene& scene, const Camera& camera,
                           const Mat4* projectionOverride = nullptr);
    void DrawSSAOBlurHorizontal(GpuCommandList& commands);
    void DrawSSAOBlurVertical(GpuCommandList& commands);
    void DrawCompositeOffscreen(GpuCommandList& commands, const Scene& scene);
    void DrawCompositeOffscreen(GpuCommandList& commands, const Scene& scene, GpuTextureView* sceneColorView);
    void DrawCompositeToBackbuffer(GpuCommandList& commands, const Scene& scene, GpuTextureView* backBufferView);
    void DrawCompositeToCurrentTarget(GpuCommandList& commands, const Scene& scene);
    void DrawCompositeToCurrentTarget(GpuCommandList& commands, const Scene& scene, GpuTextureView* sceneColorView);

    GpuTextureView* GetSceneColorView() const {
        return (!m_CompositeToBackbuffer && m_CompositeSrv) ? m_CompositeSrv.get() : m_SceneColorSrv.get();
    }
    void SetCompositeToBackbuffer(bool enabled) { m_CompositeToBackbuffer = enabled; }
    bool IsCompositeToBackbuffer() const { return m_CompositeToBackbuffer; }
    void SetSSAOEnabled(bool enabled);
    bool IsSSAOEnabled() const { return m_SSAOEnabled; }
    void SetSSAOScale(float scale);
    void SetInputPreprocessed(bool enabled) { m_InputPreprocessed = enabled; }

private:
    bool EnsureResources();
    void CloseSceneRendering(GpuCommandList& commands);
    void DrawFullscreen(GpuCommandList& commands, GpuGraphicsPipeline& pipeline, GpuBindGroup& bindings,
                        GpuTextureView& target, RHIResourceState& targetState, const ClearColor& clear);

    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    uint32_t m_SSAOWidth = 1;
    uint32_t m_SSAOHeight = 1;
    float m_SSAOScale = 1.0f;
    bool m_SSAOEnabled = true;
    bool m_CompositeToBackbuffer = true;
    bool m_SceneRendering = false;
    bool m_InputPreprocessed = false;
    bool m_LoggedCompositeBindingFailure = false;

    std::shared_ptr<GpuTexture> m_SceneColor;
    std::shared_ptr<GpuTextureView> m_SceneColorRtv;
    std::shared_ptr<GpuTextureView> m_SceneColorSrv;
    std::shared_ptr<GpuTexture> m_SceneDepth;
    std::shared_ptr<GpuTextureView> m_SceneDepthDsv;
    std::shared_ptr<GpuTextureView> m_SceneDepthSrv;
    std::shared_ptr<GpuTexture> m_SSAO;
    std::shared_ptr<GpuTextureView> m_SSAORtv;
    std::shared_ptr<GpuTextureView> m_SSAOSrv;
    std::shared_ptr<GpuTexture> m_SSAOBlur;
    std::shared_ptr<GpuTextureView> m_SSAOBlurRtv;
    std::shared_ptr<GpuTextureView> m_SSAOBlurSrv;
    std::shared_ptr<GpuTexture> m_Composite;
    std::shared_ptr<GpuTextureView> m_CompositeRtv;
    std::shared_ptr<GpuTextureView> m_CompositeSrv;
    std::shared_ptr<GpuTexture> m_Noise;
    std::shared_ptr<GpuTextureView> m_NoiseSrv;
    std::shared_ptr<GpuSampler> m_LinearClamp;
    std::shared_ptr<GpuSampler> m_PointClamp;
    std::shared_ptr<GpuSampler> m_NoiseSampler;
    std::shared_ptr<GpuShader> m_FXAAShader;
    std::shared_ptr<GpuShader> m_SSAOShader;
    std::shared_ptr<GpuShader> m_BlurShader;
    std::shared_ptr<ShaderHandle> m_FXAAHandle;
    std::shared_ptr<ShaderHandle> m_SSAOHandle;
    std::shared_ptr<ShaderHandle> m_BlurHandle;
    uint64_t m_FXAAVersion = 0;
    uint64_t m_SSAOVersion = 0;
    uint64_t m_BlurVersion = 0;
    std::shared_ptr<GpuGraphicsPipeline> m_FXAABackbufferPipeline;
    std::shared_ptr<GpuGraphicsPipeline> m_FXAAOffscreenPipeline;
    std::shared_ptr<GpuGraphicsPipeline> m_SSAOPipeline;
    std::shared_ptr<GpuGraphicsPipeline> m_BlurPipeline;
    RHIResourceState m_SceneColorState = RHIResourceState::Undefined;
    RHIResourceState m_SceneDepthState = RHIResourceState::Undefined;
    RHIResourceState m_SSAOState = RHIResourceState::Undefined;
    RHIResourceState m_SSAOBlurState = RHIResourceState::Undefined;
    RHIResourceState m_CompositeState = RHIResourceState::Undefined;
};
