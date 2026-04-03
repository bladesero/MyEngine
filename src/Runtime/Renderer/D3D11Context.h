#pragma once

#include "IRenderContext.h"

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
};

struct D3D11Texture : GpuTexture {
    ComPtr<ID3D11Texture2D>          texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState>       sampler;
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
    GpuSwapChain* GetSwapChain() override;
    GpuCommandList* GetGraphicsCommandList() override;
    bool InitImGui(IWindow* window) override;
    void ShutdownImGui() override;
    void ProcessImGuiSDLEvent(const SDL_Event& event) override;
    void BeginImGuiFrame() override;
    void RenderImGuiDrawData(ImDrawData* drawData) override;

    std::shared_ptr<GpuBuffer> CreateVertexBuffer(
        const void* data, uint32_t byteSize, uint32_t strideBytes) override;

    std::shared_ptr<GpuBuffer> CreateIndexBuffer(
        const void* data, uint32_t byteSize) override;

    std::shared_ptr<GpuShader> CreateShader(
        const std::string& hlslSource,
        const std::string& vsEntry,
        const std::string& psEntry,
        const VertexElement* layout,
        uint32_t layoutCount) override;

    void BindShader(GpuShader* shader)       override;
    void BindVertexBuffer(GpuBuffer* buffer) override;
    void BindIndexBuffer(GpuBuffer* buffer) override;
    void SetVSConstants(const void* data, uint32_t byteSize) override;
    void Draw(uint32_t vertexCount, uint32_t startVertex)    override;
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                     uint32_t baseVertex = 0) override;
    void SetViewport(float x, float y, float w, float h)     override;

    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) override;
    void BindPSTexture(uint32_t slot, GpuTexture* tex) override;

    // Native handles (needed by editor overlays such as ImGui).
    ID3D11Device*        GetDevice() const        { return m_Device.Get(); }
    ID3D11DeviceContext* GetDeviceContext() const { return m_Context.Get(); }
    IDXGISwapChain*      GetNativeSwapChain() const { return m_SwapChain.Get(); }

private:
    friend class D3D11SwapChain;

    void CreateConstantBuffer(uint32_t byteSize);
    void PresentSwapChain(bool vsync);
    bool ResizeSwapChain(uint32_t width, uint32_t height);

    ComPtr<ID3D11Device>           m_Device;
    ComPtr<ID3D11DeviceContext>    m_Context;
    ComPtr<IDXGISwapChain>         m_SwapChain;
    ComPtr<ID3D11RenderTargetView> m_RTV;
    ComPtr<ID3D11Texture2D>        m_Depth;
    ComPtr<ID3D11DepthStencilView> m_DSV;
    ComPtr<ID3D11Buffer>           m_CBuffer;      // per-draw VS/PS constants
    uint32_t                       m_CBufferSize = 0;
    uint32_t                       m_SwapChainWidth = 0;
    uint32_t                       m_SwapChainHeight = 0;
    bool                           m_ImGuiInitialized = false;
    std::unique_ptr<GpuSwapChain>  m_SwapChainInterface;
    std::unique_ptr<GpuCommandList> m_GraphicsCommandList;
};
