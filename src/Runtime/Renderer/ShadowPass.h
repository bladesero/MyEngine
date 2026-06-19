#pragma once

#include "Renderer/RenderPass.h"
#include "Core/EngineMath.h"

#include <memory>

struct ShaderHandle;

class ShadowPass final : public RenderPass {
public:
    explicit ShadowPass(IRenderContext* context);

    void Execute(const Scene& scene, const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;

    const Mat4& GetLightViewProj() const { return m_LightViewProj; }
    const Vec3& GetLightDirection() const { return m_LightDirection; }
    bool IsDirectionalShadowEnabled() const { return m_DirectionalShadowEnabled; }
    GpuTexture* GetShadowMapTexture() const { return m_ShadowMapTexture.get(); }
    const Mat4& GetSpotLightViewProj() const { return m_SpotLightViewProj; }
    int GetSpotShadowIndex() const { return m_SpotShadowIndex; }
    GpuTexture* GetSpotShadowMapTexture() const { return m_SpotShadowMapTexture.get(); }
    const Vec3& GetPointShadowPosition() const { return m_PointShadowPosition; }
    float GetPointShadowRange() const { return m_PointShadowRange; }
    int GetPointShadowIndex() const { return m_PointShadowIndex; }
    GpuTexture* GetPointShadowMapTexture() const { return m_PointShadowMapTexture.get(); }

    uint32_t GetCascadeCount() const { return m_CascadeCount; }
    const Mat4& GetCascadeViewProj(uint32_t index) const;
    const float* GetCascadeSplits() const { return m_CascadeSplits; }

private:
    void UpdateLightMatrices(const Scene& scene, const Camera& camera);
    void EnsureShadowShader();

    bool EnsureShadowResources();

private:
    static constexpr uint32_t kDefaultShadowMapSize = 2048;
    static constexpr uint32_t kMaxCascades = 3;

    std::shared_ptr<ShaderHandle> m_ShadowShaderHandle;
    uint64_t m_ShadowShaderVersion = 0;
    Mat4 m_LightViewProj = Mat4::Identity();
    Mat4 m_LightViewProjCascade[4] = {};
    float m_CascadeSplits[4] = {};
    uint32_t m_CascadeCount = 0;
    Vec3 m_LightDirection = Vec3{ -0.55f, -1.0f, -0.45f }.Normalized();
    bool m_DirectionalShadowEnabled = false;
    Mat4 m_SpotLightViewProj = Mat4::Identity();
    int m_SpotShadowIndex = -1;
    Vec3 m_PointShadowPosition = Vec3::Zero();
    float m_PointShadowRange = 1.0f;
    int m_PointShadowIndex = -1;
    Mat4 m_PointLightViewProj[6] = {};
    uint32_t m_ShadowMapSize = kDefaultShadowMapSize;

    // Shared as GpuTexture for the main pass sampler binding path.
    std::shared_ptr<GpuTexture> m_ShadowMapTexture;
    std::shared_ptr<GpuTexture> m_SpotShadowMapTexture;
    std::shared_ptr<GpuTexture> m_PointShadowMapTexture;

    std::shared_ptr<GpuTextureView> m_ShadowCascadeViews[kMaxCascades];
    std::shared_ptr<GpuTextureView> m_SpotShadowView;
    std::shared_ptr<GpuTextureView> m_PointShadowViews[6];
    bool m_ShadowResourcesInShaderState = false;
};
