#pragma once

#include "Renderer/RenderPass.h"
#include "Core/EngineMath.h"

#include <memory>

#ifdef MYENGINE_PLATFORM_WINDOWS
#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

class ShadowPass final : public RenderPass {
public:
    explicit ShadowPass(IRenderContext* context);

    void Execute(const Scene& scene, const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;

    const Mat4& GetLightViewProj() const { return m_LightViewProj; }
    const Vec3& GetLightDirection() const { return m_LightDirection; }
    GpuTexture* GetShadowMapTexture() const { return m_ShadowMapTexture.get(); }

private:
    void UpdateLightMatrices(const Scene& scene);
    void EnsureShadowShader();

#ifdef MYENGINE_PLATFORM_WINDOWS
    bool EnsureShadowResourcesD3D11();
#endif

private:
    static constexpr uint32_t kDefaultShadowMapSize = 2048;

    std::shared_ptr<GpuShader> m_ShadowShader;
    Mat4 m_LightViewProj = Mat4::Identity();
    Vec3 m_LightDirection = Vec3{ -0.55f, -1.0f, -0.45f }.Normalized();
    uint32_t m_ShadowMapSize = kDefaultShadowMapSize;

    // Shared as GpuTexture for the main pass sampler binding path.
    std::shared_ptr<GpuTexture> m_ShadowMapTexture;

#ifdef MYENGINE_PLATFORM_WINDOWS
    ComPtr<ID3D11Texture2D>          m_ShadowDepthTexture;
    ComPtr<ID3D11DepthStencilView>   m_ShadowDSV;
    ComPtr<ID3D11ShaderResourceView> m_ShadowSRV;
    ComPtr<ID3D11SamplerState>       m_ShadowSampler;
    ComPtr<ID3D11RasterizerState>    m_ShadowRasterState;
#endif
};
