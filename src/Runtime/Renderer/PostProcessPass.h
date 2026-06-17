#pragma once

#include "Renderer/RenderPass.h"
#include "Core/EngineMath.h"

#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;

struct ShaderHandle;

class PostProcessPass final : public RenderPass {
public:
    explicit PostProcessPass(IRenderContext* context);
    ~PostProcessPass() override;

    void Execute(const Scene& scene, const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;

    void BeginOffscreen();
    void RenderSSAO(const Scene& scene, const Camera& camera);
    void RenderBloom(const Scene& scene);
    void EndOffscreenAndComposite(const Scene& scene);

    ID3D11ShaderResourceView* GetOffscreenSRV() const { return m_OffscreenSRV.Get(); }
    ID3D11ShaderResourceView* GetSceneColorSrv() const {
        return (!m_CompositeToBackbuffer && m_CompositeSRV)
            ? m_CompositeSRV.Get()
            : m_OffscreenSRV.Get();
    }
    void* GetSceneColorTextureHandle() const;
    void SetCompositeToBackbuffer(bool enabled) { m_CompositeToBackbuffer = enabled; }

private:
    void EnsureOffscreenRT(uint32_t w, uint32_t h);
    void EnsureSSAOResources(uint32_t w, uint32_t h);
    void ReleaseOffscreenRT();
    GpuShader* GetOrCreateFXAAShader();
    GpuShader* GetOrCreateSSAOShader();
    GpuShader* GetOrCreateSSAOBlurShader();
    void CreateNoiseTexture();

    // Offscreen color
    ComPtr<ID3D11Texture2D>          m_OffscreenTex;
    ComPtr<ID3D11RenderTargetView>   m_OffscreenRTV;
    ComPtr<ID3D11ShaderResourceView> m_OffscreenSRV;

    // Composited scene color used by editor offscreen previews.
    ComPtr<ID3D11Texture2D>          m_CompositeTex;
    ComPtr<ID3D11RenderTargetView>   m_CompositeRTV;
    ComPtr<ID3D11ShaderResourceView> m_CompositeSRV;

    // Offscreen depth (readable for SSAO)
    ComPtr<ID3D11Texture2D>          m_OffscreenDepthTex;
    ComPtr<ID3D11DepthStencilView>   m_OffscreenDSV;
    ComPtr<ID3D11ShaderResourceView> m_OffscreenDepthSRV;

    // SSAO render target
    ComPtr<ID3D11Texture2D>          m_SSAOTex;
    ComPtr<ID3D11RenderTargetView>   m_SSAORTV;
    ComPtr<ID3D11ShaderResourceView> m_SSAOSRV;

    // SSAO blur target (ping-pong)
    ComPtr<ID3D11Texture2D>          m_SSAOBlurTex;
    ComPtr<ID3D11RenderTargetView>   m_SSAOBlurRTV;
    ComPtr<ID3D11ShaderResourceView> m_SSAOBlurSRV;

    // Noise texture for SSAO random rotation
    ComPtr<ID3D11Texture2D>          m_NoiseTex;
    ComPtr<ID3D11ShaderResourceView> m_NoiseSRV;

    // Samplers
    ComPtr<ID3D11SamplerState>       m_ComposeSampler;
    ComPtr<ID3D11SamplerState>       m_PointClampSampler;
    ComPtr<ID3D11SamplerState>       m_NoiseSampler;

    // D3D12 editor offscreen color.
    ComPtr<ID3D12Resource>           m_D3D12OffscreenTex;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_D3D12OffscreenRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE      m_D3D12OffscreenSRV = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_D3D12OffscreenSRVGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE      m_D3D12OffscreenSampler = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_D3D12OffscreenSamplerGpu = {};
    std::shared_ptr<GpuTexture>      m_D3D12OffscreenDepth;
    D3D12_RESOURCE_STATES            m_D3D12OffscreenState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ComPtr<ID3D12Resource>           m_D3D12CompositeTex;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_D3D12CompositeRTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE      m_D3D12CompositeSRV = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_D3D12CompositeSRVGpu = {};
    D3D12_RESOURCE_STATES            m_D3D12CompositeState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ComPtr<ID3D12Resource>           m_D3D12WhiteTex;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_D3D12WhiteSRV = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_D3D12WhiteSRVGpu = {};
    std::shared_ptr<GpuTexture>      m_D3D12WhiteTexture;
    std::shared_ptr<GpuShader>       m_D3D12FXAAShader;

    uint32_t m_OffscreenWidth  = 0;
    uint32_t m_OffscreenHeight = 0;

    // Saved backbuffer state
    ComPtr<ID3D11RenderTargetView>   m_SavedRTV;
    ComPtr<ID3D11DepthStencilView>   m_SavedDSV;
    D3D11_VIEWPORT                   m_SavedViewport = {};
    bool                             m_ViewportSaved = false;
    bool                             m_CompositeToBackbuffer = true;

    // Shader handles
    std::shared_ptr<ShaderHandle> m_FXAAHandle;
    std::shared_ptr<ShaderHandle> m_SSAOHandle;
    std::shared_ptr<ShaderHandle> m_SSAOBlurHandle;
};
