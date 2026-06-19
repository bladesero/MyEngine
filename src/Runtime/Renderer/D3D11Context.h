#pragma once

#include "IRenderContext.h"

#include <cstddef>
#include <string>

#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// --------------------------------------------------------------------------
// D3D11 concrete GpuBuffer / GpuShader
// --------------------------------------------------------------------------
struct D3D11Buffer : GpuBuffer {
    ComPtr<ID3D11Buffer> buffer;
    uint32_t             stride = 0;
};

struct D3D11IndexBuffer : GpuBuffer {
    ComPtr<ID3D11Buffer> buffer;
    DXGI_FORMAT          format = DXGI_FORMAT_R32_UINT;
};

struct D3D11Shader : GpuShader {
    ComPtr<ID3D11VertexShader>  vs;
    ComPtr<ID3D11PixelShader>   ps;
    ComPtr<ID3D11InputLayout>   inputLayout;
    ComPtr<ID3D11ComputeShader> cs;
};

struct D3D11BufferView : GpuBufferView {
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};

struct D3D11Texture : GpuTexture {
    ComPtr<ID3D11Texture2D>          texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState>       sampler;
    bool isCube = false;

    bool IsCube() const override { return isCube; }
};

struct D3D11TextureView : GpuTextureView {
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};

struct D3D11Sampler : GpuSampler {
    ComPtr<ID3D11SamplerState> sampler;
};

// --------------------------------------------------------------------------
// D3D11Context
// --------------------------------------------------------------------------
class D3D11Context : public IRenderContext {
public:
    D3D11Context();
    ~D3D11Context() override;

    bool Init(IWindow* window) override;
    void Shutdown()            override;

    void BeginFrame(float r, float g, float b, float a) override;
    void EndFrame()  override;
    bool IsDeviceLost() const override { return m_DeviceLost; }
    const std::string& GetLastDeviceError() const override { return m_LastDeviceError; }
    GpuSwapChain* GetSwapChain() override;
    GpuCommandList* GetGraphicsCommandList() override;
    GpuTextureView* GetCurrentBackBufferView() override { return m_BackBufferView.get(); }
    RHIBackend GetBackend() const override { return RHIBackend::D3D11; }
    bool InitImGui(IWindow* window) override;
    void ShutdownImGui() override;
    void ProcessImGuiSDLEvent(const SDL_Event& event) override;
    void BeginImGuiFrame() override;
    void RenderImGuiDrawData(ImDrawData* drawData) override;

    std::shared_ptr<GpuBuffer> CreateVertexBuffer(
        const void* data, uint32_t byteSize, uint32_t strideBytes) override;

    std::shared_ptr<GpuBuffer> CreateIndexBuffer(
        const void* data, uint32_t byteSize) override;
    std::shared_ptr<GpuBuffer> CreateBuffer(
        const RHIBufferDesc& desc, const void* initialData = nullptr) override;
    std::shared_ptr<GpuBufferView> CreateBufferView(
        const std::shared_ptr<GpuBuffer>& buffer, const RHIBufferViewDesc& desc) override;

    std::shared_ptr<GpuShader> CreateShader(
        const std::string& hlslSource,
        const std::string& vsEntry,
        const std::string& psEntry,
        const VertexElement* layout,
        uint32_t layoutCount) override;

    std::shared_ptr<GpuShader> CreateShaderFromBytecode(
        const void* vsBytecode,
        size_t vsSize,
        const void* psBytecode,
        size_t psSize,
        const VertexElement* layout,
        uint32_t layoutCount) override;
    std::shared_ptr<GpuShader> CreateComputeShaderFromBytecode(
        const void* bytecode, size_t byteSize) override;

    void BindShader(GpuShader* shader);
    void BindVertexBuffer(GpuBuffer* buffer);
    void BindIndexBuffer(GpuBuffer* buffer);
    void SetVSConstants(const void* data, uint32_t byteSize);
    void Draw(uint32_t vertexCount, uint32_t startVertex);
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                     uint32_t baseVertex = 0);
    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                       uint32_t startVertex = 0);
    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                              uint32_t startIndex = 0, uint32_t baseVertex = 0);
    void SetViewport(float x, float y, float w, float h);

    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) override;
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override;
    std::shared_ptr<GpuTextureView> CreateTextureView(
        const std::shared_ptr<GpuTexture>& texture, const RHITextureViewDesc& desc) override;
    std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc& desc) override;
    void* GetImGuiTextureId(GpuTextureView* view) override;
    std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(
        const std::shared_ptr<GpuBuffer>& buffer) override;
    void BindPSTexture(uint32_t slot, GpuTexture* tex);
    void SetBlendMode(GpuBlendMode mode);
    void SetRasterState(bool twoSided, bool wireframe);

    // Native handles (needed by editor overlays such as ImGui).
    ID3D11Device*        GetDevice() const        { return m_Device.Get(); }
    ID3D11DeviceContext* GetDeviceContext() const { return m_Context.Get(); }
    IDXGISwapChain*      GetNativeSwapChain() const { return m_SwapChain.Get(); }

private:
    friend class D3D11SwapChain;

    void CreateConstantBuffer(uint32_t byteSize);
    void PresentSwapChain(bool vsync);
    bool ResizeSwapChain(uint32_t width, uint32_t height);
    bool CheckDeviceResult(HRESULT hr, const char* operation);

    ComPtr<ID3D11Device>           m_Device;
    ComPtr<ID3D11DeviceContext>    m_Context;
    ComPtr<IDXGISwapChain>         m_SwapChain;
    ComPtr<ID3D11RenderTargetView> m_RTV;
    std::shared_ptr<D3D11TextureView> m_BackBufferView;
    ComPtr<ID3D11Texture2D>        m_Depth;
    ComPtr<ID3D11DepthStencilView> m_DSV;
    ComPtr<ID3D11Buffer>           m_CBuffer;      // per-draw VS/PS constants
    ComPtr<ID3D11BlendState>       m_OpaqueBlendState;
    ComPtr<ID3D11BlendState>       m_AlphaBlendState;
    ComPtr<ID3D11RasterizerState>  m_RasterSolidCullBack;
    ComPtr<ID3D11RasterizerState>  m_RasterSolidCullNone;
    ComPtr<ID3D11RasterizerState>  m_RasterWireCullBack;
    ComPtr<ID3D11RasterizerState>  m_RasterWireCullNone;
    uint32_t                       m_CBufferSize = 0;
    uint32_t                       m_SwapChainWidth = 0;
    uint32_t                       m_SwapChainHeight = 0;
    bool                           m_ImGuiInitialized = false;
    bool                           m_DeviceLost = false;
    std::string                    m_LastDeviceError;
    std::unique_ptr<GpuSwapChain>  m_SwapChainInterface;
    std::unique_ptr<GpuCommandList> m_GraphicsCommandList;
};
