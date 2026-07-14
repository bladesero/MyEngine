#pragma once

#include "IRenderContext.h"
#include "Renderer/RHI/IEditorImGuiRHIInterop.h"

#include <cstddef>
#include <string>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// --------------------------------------------------------------------------
// D3D11 concrete GpuBuffer / GpuShader
// --------------------------------------------------------------------------
struct D3D11Buffer : GpuBuffer {
    ComPtr<ID3D11Buffer> buffer;
    uint32_t stride = 0;
    std::vector<uint8_t> updateShadow;
};

struct D3D11IndexBuffer : GpuBuffer {
    ComPtr<ID3D11Buffer> buffer;
    DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
};

struct D3D11Shader : GpuShader {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11ComputeShader> cs;
};

struct D3D11BufferView : GpuBufferView {
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
};

struct D3D11Texture : GpuTexture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState> sampler;
    bool isCube = false;

    bool IsCube() const override { return isCube; }
};

struct D3D11TextureView : GpuTextureView {
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11UnorderedAccessView> uav;

    void* GetImGuiTextureId() override { return srv.Get(); }
};

struct D3D11Sampler : GpuSampler {
    ComPtr<ID3D11SamplerState> sampler;
};

struct D3D11GraphicsPipeline : GpuGraphicsPipeline {
    ComPtr<ID3D11BlendState> blendState;
    ComPtr<ID3D11RasterizerState> rasterizerState;
    ComPtr<ID3D11DepthStencilState> depthStencilState;
};

// --------------------------------------------------------------------------
// D3D11Context
// --------------------------------------------------------------------------
class D3D11Context : public IRenderContext, public IEditorImGuiRHIInterop {
public:
    D3D11Context();
    ~D3D11Context() override;

    bool Init(IWindow* window) override;
    void Shutdown() override;
    RHIDeviceIdentity GetDeviceIdentity() const override { return m_DeviceIdentity; }

    void BeginFrame(float r, float g, float b, float a) override;
    void EndFrame() override;
    bool IsDeviceLost() const override { return m_DeviceLost; }
    const std::string& GetLastDeviceError() const override { return m_LastDeviceError; }
    RHIDeviceLossInfo GetDeviceLossInfo() const override { return m_DeviceLossInfo; }
    GpuSwapChain* GetSwapChain() override;
    GpuCommandList* GetGraphicsCommandList() override;
    GpuQueue* GetGraphicsQueue() override { return m_GraphicsQueue.get(); }
    GpuTextureView* GetCurrentBackBufferView() override { return m_BackBufferView.get(); }
    void SetVSyncEnabled(bool enabled) override { m_VSyncEnabled = enabled; }
    bool IsVSyncEnabled() const override { return m_VSyncEnabled; }
    RHIBackend GetBackend() const override { return RHIBackend::D3D11; }
    IEditorImGuiRHIInterop* QueryEditorImGuiInterop() override { return this; }
    ImGuiBackendHandles GetImGuiBackendHandles() override;

    std::shared_ptr<GpuBuffer> CreateVertexBuffer(const void* data, uint32_t byteSize, uint32_t strideBytes) override;

    std::shared_ptr<GpuBuffer> CreateIndexBuffer(const void* data, uint32_t byteSize) override;
    std::shared_ptr<GpuBuffer> CreateBuffer(const RHIBufferDesc& desc, const void* initialData = nullptr) override;
    std::shared_ptr<GpuBufferView> CreateBufferView(const std::shared_ptr<GpuBuffer>& buffer,
                                                    const RHIBufferViewDesc& desc) override;

    std::shared_ptr<GpuShader> CreateShader(const std::string& hlslSource, const std::string& vsEntry,
                                            const std::string& psEntry, const VertexElement* layout,
                                            uint32_t layoutCount) override;

    std::shared_ptr<GpuShader> CreateShaderFromBytecode(const void* vsBytecode, size_t vsSize, const void* psBytecode,
                                                        size_t psSize, const VertexElement* layout,
                                                        uint32_t layoutCount) override;
    std::shared_ptr<GpuShader> CreateComputeShaderFromBytecode(const void* bytecode, size_t byteSize) override;

    void BindShader(GpuShader* shader);
    void BindVertexBuffer(GpuBuffer* buffer);
    void BindIndexBuffer(GpuBuffer* buffer);
    void SetVSConstants(const void* data, uint32_t byteSize);
    void Draw(uint32_t vertexCount, uint32_t startVertex);
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, uint32_t baseVertex = 0);
    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex = 0);
    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex = 0,
                              uint32_t baseVertex = 0);
    void SetViewport(float x, float y, float w, float h);

    std::shared_ptr<GpuTexture> UploadTexture2D(const void* rgba8Data, int width, int height) override;
    bool UpdateBuffer(const std::shared_ptr<GpuBuffer>&, uint64_t, const void*, uint64_t) override;
    std::shared_ptr<GpuTexture> UploadTexture(const RHITextureDesc&, const RHITextureSubresourceData*,
                                              uint32_t) override;
    RHIDeviceCapabilities GetCapabilities() const override;
    bool IsFormatSupported(RHIFormat, RHIResourceUsage) const override;
    std::shared_ptr<GpuFence> CreateFence(uint64_t initialValue = 0) override;
    std::shared_ptr<GpuTimestampQueryPool> CreateTimestampQueryPool(uint32_t count) override;
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override;
    std::shared_ptr<GpuTextureView> CreateTextureView(const std::shared_ptr<GpuTexture>& texture,
                                                      const RHITextureViewDesc& desc) override;
    std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc& desc) override;
    std::shared_ptr<GpuGraphicsPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(const std::shared_ptr<GpuBuffer>& buffer) override;
    std::shared_ptr<GpuTextureReadbackTicket> ReadbackTextureAsync(const std::shared_ptr<GpuTexture>&,
                                                                   const RHITextureRegion&) override;
    void BindPSTexture(uint32_t slot, GpuTexture* tex);
    void SetBlendMode(GpuBlendMode mode);
    void SetRasterState(bool twoSided, bool wireframe);

    // Called by EditorImGuiBackend to invalidate ImGui RTV cache on resize.
    using SwapChainResizeCallback = void (*)();
    void SetSwapChainResizeCallback(SwapChainResizeCallback cb) override { m_ResizeCallback = cb; }

    // Native handles (needed by editor overlays such as ImGui).
    ID3D11Device* GetDevice() const { return m_Device.Get(); }
    ID3D11DeviceContext* GetDeviceContext() const { return m_Context.Get(); }
    IDXGISwapChain* GetNativeSwapChain() const { return m_SwapChain.Get(); }

private:
    friend class D3D11SwapChain;

    void CreateConstantBuffer(uint32_t byteSize);
    void PresentSwapChain(bool vsync);
    bool ResizeSwapChain(uint32_t width, uint32_t height);
    bool CheckDeviceResult(HRESULT hr, const char* operation);

    ComPtr<ID3D11Device> m_Device;
    ComPtr<ID3D11DeviceContext> m_Context;
    RHIDeviceIdentity m_DeviceIdentity;
    ComPtr<IDXGISwapChain> m_SwapChain;
    ComPtr<ID3D11RenderTargetView> m_RTV;
    std::shared_ptr<D3D11TextureView> m_BackBufferView;
    ComPtr<ID3D11Texture2D> m_Depth;
    ComPtr<ID3D11DepthStencilView> m_DSV;
    ComPtr<ID3D11Buffer> m_CBuffer; // per-draw VS/PS constants
    ComPtr<ID3D11BlendState> m_OpaqueBlendState;
    ComPtr<ID3D11BlendState> m_AlphaBlendState;
    ComPtr<ID3D11RasterizerState> m_RasterSolidCullBack;
    ComPtr<ID3D11RasterizerState> m_RasterSolidCullNone;
    ComPtr<ID3D11RasterizerState> m_RasterWireCullBack;
    ComPtr<ID3D11RasterizerState> m_RasterWireCullNone;
    uint32_t m_CBufferSize = 0;
    uint32_t m_SwapChainWidth = 0;
    uint32_t m_SwapChainHeight = 0;
    bool m_DeviceLost = false;
    bool m_VSyncEnabled = true;
    std::string m_LastDeviceError;
    RHIDeviceLossInfo m_DeviceLossInfo;
    uint64_t m_DeviceGeneration = 0;
    SwapChainResizeCallback m_ResizeCallback = nullptr;
    std::unique_ptr<GpuSwapChain> m_SwapChainInterface;
    std::unique_ptr<GpuCommandList> m_GraphicsCommandList;
    std::shared_ptr<GpuQueue> m_GraphicsQueue;
};
