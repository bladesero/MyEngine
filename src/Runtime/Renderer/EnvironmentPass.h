#pragma once

#include "Renderer/RenderPass.h"

#include <array>
#include <memory>

#ifdef MYENGINE_PLATFORM_WINDOWS
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>
#endif

#ifdef MYENGINE_PLATFORM_WINDOWS
struct D3D11Texture;
#endif

class EnvironmentPass final : public RenderPass {
public:
    explicit EnvironmentPass(IRenderContext* context);
    ~EnvironmentPass() override;

    void Execute(const Scene& scene, const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;

    GpuTexture* GetEnvironmentCubemap() const;
    GpuTexture* GetSH2Buffer() const;
    const float* GetSH2Coefficients() const { return &m_SH2[0][0]; }

private:
    bool EnsureD3D11Resources();
    void ExecuteD3D11();
    bool EnsureD3D12Resources();
    void ExecuteD3D12();

private:
    static constexpr int kCubeSize = 64;
    static constexpr int kCubeMipLevels = 7;
    float m_SH2[9][4] = {};
    std::shared_ptr<GpuTexture> m_D3D12Environment;
    std::shared_ptr<GpuTexture> m_D3D12SH2Buffer;

#ifdef MYENGINE_PLATFORM_WINDOWS
    struct D3D11EnvironmentTexture;
    std::shared_ptr<D3D11EnvironmentTexture> m_D3D11Environment;
    std::shared_ptr<D3D11Texture> m_D3D11SH2Buffer;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_D3D11FaceRTV[6];
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_D3D11SHCompute;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_D3D11SHBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_D3D11SHUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_D3D11SHReadback;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_D3D11LinearClamp;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_D3D12Unused;
    D3D12_CPU_DESCRIPTOR_HANDLE m_D3D12FaceRTV[6] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_D3D12MipFaceRTV[kCubeMipLevels][6] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_D3D12SHUAVCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_D3D12SHUAVGpu = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_D3D12SHReadback;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_D3D12ComputeRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_D3D12ComputePSO;
    std::shared_ptr<GpuShader> m_D3D12AtmosphereShader;
    std::shared_ptr<GpuShader> m_D3D12MipmapShader;
    D3D12_RESOURCE_STATES m_D3D12EnvState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_D3D12SHState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
#endif
};
