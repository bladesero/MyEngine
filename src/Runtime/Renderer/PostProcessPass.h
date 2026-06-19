#pragma once

#include "Renderer/RenderPass.h"
#include "Core/EngineMath.h"

#include <cstdint>
#include <memory>

struct ShaderHandle;

class PostProcessPass final : public RenderPass {
public:
    explicit PostProcessPass(IRenderContext* context);

    void Execute(const Scene& scene, const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;
    void BeginOffscreen();
    void RenderSSAO(const Scene& scene, const Camera& camera);
    void RenderBloom(const Scene& scene);
    void EndOffscreenAndComposite(const Scene& scene);

    GpuTextureView* GetSceneColorView() const {
        return (!m_CompositeToBackbuffer && m_CompositeSrv)
            ? m_CompositeSrv.get() : m_SceneColorSrv.get();
    }
    void SetCompositeToBackbuffer(bool enabled) { m_CompositeToBackbuffer = enabled; }

private:
    bool EnsureResources();
    void CloseSceneRendering();
    void DrawFullscreen(GpuCommandList& commands, GpuGraphicsPipeline& pipeline,
                        GpuBindGroup& bindings, GpuTextureView& target,
                        RHIResourceState& targetState, const ClearColor& clear);

    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    bool m_CompositeToBackbuffer = true;
    bool m_SceneRendering = false;

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
