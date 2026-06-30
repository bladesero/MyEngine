#pragma once

#include "Core/EngineMath.h"
#include "Renderer/RenderPass.h"
#include "Renderer/SceneLighting.h"

#include <array>
#include <memory>
#include <unordered_map>

struct ShaderHandle;

class DeferredLightingPass final : public RenderPass {
public:
    struct GraphResources {
        std::shared_ptr<GpuTexture> sceneColor;
        std::shared_ptr<GpuTextureView> sceneColorRtv;
        std::shared_ptr<GpuTextureView> sceneColorSrv;
        RHIResourceState initialState = RHIResourceState::Undefined;
    };

    explicit DeferredLightingPass(IRHIDevice* device);

    void Execute(GpuCommandList& commands, const Scene& scene,
                 const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;
    bool PrepareGraphResources();
    GraphResources GetGraphResources() const;
    void MarkGraphResourcesShaderResource();

    void SetGBufferInput(std::shared_ptr<GpuTextureView> albedo,
                         std::shared_ptr<GpuTextureView> normal,
                         std::shared_ptr<GpuTextureView> material,
                         std::shared_ptr<GpuTextureView> emissive);
    void SetDepthInput(std::shared_ptr<GpuTextureView> depth);
    void SetLightingInput(const SceneLightData& lights);
    void SetShadowInput(const Mat4& lightViewProj,
                        bool directionalShadowEnabled,
                        GpuTexture* shadowMap,
                        const Mat4& spotLightViewProj,
                        int spotShadowIndex,
                        GpuTexture* spotShadowMap,
                        const Vec3& pointShadowPosition,
                        float pointShadowRange,
                        int pointShadowIndex,
                        GpuTexture* pointShadowMap,
                        const Mat4* cascadeViewProj,
                        uint32_t cascadeCount,
                        const float* cascadeSplits);
    void SetEnvironmentInput(GpuTexture* environmentCubemap,
                             std::shared_ptr<GpuBufferView> sh2Buffer);

private:
    bool EnsureResources();
    GpuShader* GetOrCreateShader();
    GpuGraphicsPipeline* GetOrCreatePipeline();
    std::shared_ptr<GpuTextureView> GetTextureView(GpuTexture* texture);

    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    std::shared_ptr<GpuTexture> m_SceneColor;
    std::shared_ptr<GpuTextureView> m_SceneColorRtv;
    std::shared_ptr<GpuTextureView> m_SceneColorSrv;
    RHIResourceState m_SceneColorState = RHIResourceState::Undefined;

    std::shared_ptr<GpuTextureView> m_GBufferAlbedo;
    std::shared_ptr<GpuTextureView> m_GBufferNormal;
    std::shared_ptr<GpuTextureView> m_GBufferMaterial;
    std::shared_ptr<GpuTextureView> m_GBufferEmissive;
    std::shared_ptr<GpuTextureView> m_SceneDepth;
    std::unordered_map<GpuTexture*, std::shared_ptr<GpuTextureView>> m_TextureViews;

    std::shared_ptr<ShaderHandle> m_ShaderHandle;
    uint64_t m_ShaderVersion = 0;
    std::shared_ptr<GpuGraphicsPipeline> m_Pipeline;
    std::shared_ptr<GpuSampler> m_LinearSampler;
    std::shared_ptr<GpuSampler> m_PointSampler;
    std::shared_ptr<GpuSampler> m_ShadowSampler;

    SceneLightData m_Lights;
    Mat4 m_LightViewProj = Mat4::Identity();
    Mat4 m_LightViewProjCascade[3] = {Mat4::Identity(), Mat4::Identity(), Mat4::Identity()};
    float m_CascadeSplits[4] = {};
    Mat4 m_SpotLightViewProj = Mat4::Identity();
    bool m_DirectionalShadowEnabled = false;
    GpuTexture* m_ShadowMap = nullptr;
    GpuTexture* m_SpotShadowMap = nullptr;
    GpuTexture* m_PointShadowMap = nullptr;
    Vec3 m_PointShadowPosition = Vec3::Zero();
    float m_PointShadowRange = 1.0f;
    int m_SpotShadowIndex = -1;
    int m_PointShadowIndex = -1;
    GpuTexture* m_EnvironmentCubemap = nullptr;
    std::shared_ptr<GpuBufferView> m_EnvironmentSH2Buffer;
    bool m_LoggedMissingEnvironmentSH = false;
};
