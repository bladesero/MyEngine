#pragma once

#include "Renderer/RenderPass.h"
#include "Core/EngineMath.h"

#include <memory>
#include <array>
#include <unordered_map>

class TextureAsset;
class MeshAsset;
class MaterialAsset;
struct ShaderHandle;

class MainPass final : public RenderPass {
public:
    explicit MainPass(IRHIDevice* device);

    void Execute(GpuCommandList& commands, const Scene& scene,
                 const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;


    void SetHdrPassthrough(bool passthrough);
    void SetShadowInput(const Mat4& lightViewProj,
                        const Vec3& lightDirection,
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
    void SetCascadeShadowInput(const Mat4* cascadeViewProj, uint32_t cascadeCount,
                               const float* cascadeSplits);
    void SetEnvironmentInput(GpuTexture* environmentCubemap,
                             std::shared_ptr<GpuBufferView> sh2Buffer,
                             const float* sh2Coefficients);

private:
    enum class ShaderMode {
        Unknown,
        Legacy,
        ShadowedPbr,
    };

    void EnsureMeshUploaded(MeshAsset* mesh);
    void EnsureTextureUploaded(TextureAsset* tex);
    std::shared_ptr<GpuTextureView> GetTextureView(GpuTexture* texture);
    void EnsureNamedBindingDefaults();
    GpuShader* GetOrCreateShader();
    GpuShader* GetOrCreateSkyShader();
    GpuGraphicsPipeline* GetOrCreateMainPipeline(bool transparent,
                                                 bool twoSided,
                                                 bool wireframe);
    GpuGraphicsPipeline* GetOrCreateMaterialPipeline(const MaterialAsset& material);
    GpuGraphicsPipeline* GetOrCreateSkyPipeline();
    void RenderSky(const Camera& camera, GpuCommandList& cmd);

private:
    ShaderMode m_ShaderMode = ShaderMode::Unknown;


    std::shared_ptr<ShaderHandle> m_MainShaderHandle;
    bool m_HdrPassthrough = true;
    std::shared_ptr<ShaderHandle> m_SkyShaderHandle;
    std::array<std::shared_ptr<GpuGraphicsPipeline>, 8> m_MainPipelines;
    std::unordered_map<const MaterialAsset*, std::shared_ptr<GpuGraphicsPipeline>> m_MaterialPipelines;
    std::shared_ptr<GpuGraphicsPipeline> m_SkyPipeline;
    uint64_t m_MainShaderVersion = 0;
    uint64_t m_SkyShaderVersion = 0;
    std::unordered_map<TextureAsset*, std::shared_ptr<GpuTexture>> m_TexCache;
    std::unordered_map<GpuTexture*, std::shared_ptr<GpuTextureView>> m_TextureViews;
    std::shared_ptr<GpuTexture> m_DefaultTexture;
    std::shared_ptr<GpuTextureView> m_DefaultTextureView;
    std::shared_ptr<GpuSampler> m_LinearSampler;
    std::shared_ptr<GpuSampler> m_ShadowSampler;
    GpuTexture* m_ShadowMap = nullptr;
    GpuTexture* m_SpotShadowMap = nullptr;
    GpuTexture* m_PointShadowMap = nullptr;
    GpuTexture* m_EnvironmentCubemap = nullptr;
    std::shared_ptr<GpuBufferView> m_EnvironmentSH2Buffer;
    float m_EnvironmentSH2[9][4] = {};
    Mat4 m_LightViewProj = Mat4::Identity();
    Mat4 m_LightViewProjCascade[3] = { Mat4::Identity(), Mat4::Identity(), Mat4::Identity() };
    float m_CascadeSplits[4] = {};
    Mat4 m_SpotLightViewProj = Mat4::Identity();
    Vec3 m_LightDirection = Vec3{ -0.55f, -1.0f, -0.45f }.Normalized();
    bool m_DirectionalShadowEnabled = false;
    Vec3 m_PointShadowPosition = Vec3::Zero();
    float m_PointShadowRange = 1.0f;
    int m_SpotShadowIndex = -1;
    int m_PointShadowIndex = -1;
};
