#pragma once

#include "Renderer/MaterialResourceCache.h"
#include "Renderer/MaterialSystem.h"
#include "Renderer/RenderPass.h"
#include "Renderer/SceneRenderCollector.h"
#include "Core/EngineMath.h"

#include <memory>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

class TextureAsset;
class MeshAsset;
class MaterialAsset;
class ForwardOpaquePass;
class ForwardTransparentPass;
class ForwardDrawExecutor;
class SkyPass;
struct ShaderHandle;

class MainPass final : public RenderPass {
public:
    struct Stats {
        uint32_t drawCalls = 0;
        uint32_t submittedSubMeshes = 0;
        uint32_t culledSubMeshes = 0;
        uint32_t bindGroupCreates = 0;
        uint32_t textureUploads = 0;
        uint64_t textureUploadBytes = 0;
        float textureUploadMs = 0.0f;
    };

    explicit MainPass(IRHIDevice* device);
    ~MainPass() override;

    void Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera) override;
    void ExecuteTransparentOnly(GpuCommandList& commands, const Scene& scene, const Camera& camera,
                                const Mat4* viewProjection = nullptr);
    void Resize(uint32_t width, uint32_t height) override;

    void SetHdrPassthrough(bool passthrough);
    void SetShadowInput(const Mat4& lightViewProj, const Vec3& lightDirection, bool directionalShadowEnabled,
                        GpuTexture* shadowMap, const Mat4& spotLightViewProj, int spotShadowIndex,
                        GpuTexture* spotShadowMap, const Vec3& pointShadowPosition, float pointShadowRange,
                        int pointShadowIndex, GpuTexture* pointShadowMap, const Mat4* cascadeViewProj,
                        uint32_t cascadeCount, const float* cascadeSplits);
    void SetCascadeShadowInput(const Mat4* cascadeViewProj, uint32_t cascadeCount, const float* cascadeSplits);
    void SetEnvironmentInput(GpuTexture* environmentCubemap, std::shared_ptr<GpuBufferView> sh2Buffer,
                             const float* sh2Coefficients);
    void SetProbeInput(std::shared_ptr<GpuTextureView> reflectionAtlas,
                       std::shared_ptr<GpuBufferView> reflectionMetadata,
                       std::shared_ptr<GpuBufferView> shVolumeMetadata, std::shared_ptr<GpuBufferView> shCoefficients,
                       uint32_t reflectionCount, uint32_t shVolumeCount, uint32_t reflectionMipCount);
    void SetSunDirection(const Vec3& direction);
    const Stats& GetLastStats() const { return m_LastStats; }

private:
    enum class ShaderMode {
        Unknown,
        Legacy,
        ShadowedPbr,
    };

    void EnsureMeshUploaded(MeshAsset* mesh);
    void EnsureTextureUploaded(TextureAsset* tex);
    std::shared_ptr<GpuTextureView> GetTextureView(GpuTexture* texture);
    std::shared_ptr<GpuSampler> GetSamplerForTexture(TextureAsset* texture);
    void EnsureNamedBindingDefaults();
    GpuShader* GetOrCreateShader();
    GpuShader* GetOrCreateSkyShader();
    GpuGraphicsPipeline* GetOrCreateMainPipeline(bool transparent, bool twoSided, bool wireframe);
    GpuGraphicsPipeline* GetOrCreateMaterialPipeline(const MaterialAsset& material);
    GpuGraphicsPipeline* GetOrCreateMaterialPipeline(const MaterialAsset& material,
                                                     const std::shared_ptr<GpuShader>& shader, BlendMode blendMode,
                                                     bool twoSided, bool wireframe);
    GpuGraphicsPipeline* GetOrCreateSkyPipeline();
    void RenderSky(const Camera& camera, GpuCommandList& cmd);
    bool CanReuseMaterialBindGroups() const;
    std::shared_ptr<GpuBindGroup> GetOrCreateMaterialBindGroup(GpuShader* shader, const MaterialAsset& material,
                                                               bool shadowedPbr,
                                                               const std::array<GpuTexture*, 9>& textures,
                                                               const std::array<TextureAsset*, 9>& textureAssets);

private:
    friend class SkyPass;
    friend class ForwardOpaquePass;
    friend class ForwardTransparentPass;
    friend class ForwardDrawExecutor;

    struct MaterialBindGroupCacheEntry {
        std::string signature;
        std::shared_ptr<GpuBindGroup> bindGroup;
    };

    ShaderMode m_ShaderMode = ShaderMode::Unknown;

    std::shared_ptr<ShaderHandle> m_MainShaderHandle;
    bool m_HdrPassthrough = true;
    std::shared_ptr<ShaderHandle> m_SkyShaderHandle;
    std::array<std::shared_ptr<GpuGraphicsPipeline>, 8> m_MainPipelines;
    std::unordered_map<const MaterialAsset*, std::shared_ptr<GpuGraphicsPipeline>> m_MaterialPipelines;
    std::shared_ptr<GpuGraphicsPipeline> m_SkyPipeline;
    uint64_t m_MainShaderVersion = 0;
    uint64_t m_SkyShaderVersion = 0;
    SceneRenderCollector m_SceneCollector;
    MaterialResourceCache m_ResourceCache;
    MaterialSystem m_MaterialSystem;
    std::unique_ptr<SkyPass> m_SkyPass;
    std::unique_ptr<ForwardOpaquePass> m_ForwardOpaquePass;
    std::unique_ptr<ForwardTransparentPass> m_ForwardTransparentPass;
    std::unordered_map<const MaterialAsset*, MaterialBindGroupCacheEntry> m_MaterialBindGroups;
    GpuTexture* m_ShadowMap = nullptr;
    GpuTexture* m_SpotShadowMap = nullptr;
    GpuTexture* m_PointShadowMap = nullptr;
    GpuTexture* m_EnvironmentCubemap = nullptr;
    std::shared_ptr<GpuBufferView> m_EnvironmentSH2Buffer;
    std::shared_ptr<GpuTextureView> m_ProbeReflectionAtlas;
    std::shared_ptr<GpuBufferView> m_ProbeReflectionMetadata;
    std::shared_ptr<GpuBufferView> m_ProbeSHVolumeMetadata;
    std::shared_ptr<GpuBufferView> m_ProbeSHCoefficients;
    uint32_t m_ProbeReflectionCount = 0;
    uint32_t m_ProbeSHVolumeCount = 0;
    uint32_t m_ProbeReflectionMipCount = 1;
    float m_EnvironmentSH2[9][4] = {};
    bool m_LoggedEnvironmentState = false;
    bool m_LoggedEnvironmentSHBindingFailure = false;
    Mat4 m_LightViewProj = Mat4::Identity();
    Mat4 m_LightViewProjCascade[3] = {Mat4::Identity(), Mat4::Identity(), Mat4::Identity()};
    float m_CascadeSplits[4] = {};
    Mat4 m_SpotLightViewProj = Mat4::Identity();
    Vec3 m_LightDirection = Vec3{-0.55f, -1.0f, -0.45f}.Normalized();
    Vec3 m_SunDirection = Vec3{0.35f, 0.72f, 0.25f}.Normalized();
    bool m_DirectionalShadowEnabled = false;
    Vec3 m_PointShadowPosition = Vec3::Zero();
    float m_PointShadowRange = 1.0f;
    int m_SpotShadowIndex = -1;
    int m_PointShadowIndex = -1;
    Stats m_LastStats;
};
