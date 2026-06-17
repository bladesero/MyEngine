#pragma once

#include "IRenderContext.h"

#include <cstddef>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct D3D12Buffer : GpuBuffer {
    ComPtr<ID3D12Resource> resource;
    uint32_t byteSize = 0;
};

struct D3D12VertexBuffer : D3D12Buffer {
    uint32_t stride = 0;
};

struct D3D12IndexBuffer : D3D12Buffer {
    DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
};

struct D3D12Shader : GpuShader {
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12PipelineState> alphaPipelineState;
    ComPtr<ID3D12PipelineState> depthOnlyPipelineState;
    ComPtr<ID3D12PipelineState> wireframePipelineState;
    ComPtr<ID3D12PipelineState> twoSidedPipelineState;
};

struct D3D12Texture : GpuTexture {
    ComPtr<ID3D12Resource> resource;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE sampCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE sampGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvCpu = {};
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> dsvFaces;
    uint32_t arraySize = 1;
    bool isCube = false;

    bool IsCube() const override { return isCube; }
};

// ============================================================================
// D3D12Context
// ============================================================================
class D3D12Context final : public IRenderContext {
public:
    static constexpr uint32_t kFrameCount = 2;
    static constexpr uint32_t kTextureSlotCount = 10;
    static constexpr uint32_t kOffscreenRtvCount = 96;
    static constexpr uint32_t kDsvDescriptorCount = 32;
    static constexpr uint32_t kDefaultConstantBufferCapacity = 1024 * 1024;
    static constexpr uint32_t kDefaultSrvDescriptorCount      = 256;
    static constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    static constexpr DXGI_FORMAT kDepthTypelessFormat = DXGI_FORMAT_R24G8_TYPELESS;

    D3D12Context();
    ~D3D12Context() override;

    bool Init(IWindow* window) override;
    void Shutdown() override;

    void BeginFrame(float r, float g, float b, float a = 1.0f) override;
    void EndFrame() override;
    bool IsDeviceLost() const override { return m_DeviceLost; }
    const std::string& GetLastDeviceError() const override { return m_LastDeviceError; }
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

    std::shared_ptr<GpuShader> CreateShaderFromBytecode(
        const void* vsBytecode,
        size_t vsSize,
        const void* psBytecode,
        size_t psSize,
        const VertexElement* layout,
        uint32_t layoutCount) override;

    void BindShader(GpuShader* shader) override;
    void BindVertexBuffer(GpuBuffer* buffer) override;
    void BindIndexBuffer(GpuBuffer* buffer) override;
    void SetVSConstants(const void* data, uint32_t byteSize) override;

    void Draw(uint32_t vertexCount, uint32_t startVertex = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                     uint32_t baseVertex = 0) override;
    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                       uint32_t startVertex = 0);
    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                              uint32_t startIndex = 0, uint32_t baseVertex = 0);

    void SetViewport(float x, float y, float w, float h) override;

    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) override;
    void BindPSTexture(uint32_t slot, GpuTexture* tex) override;
    void SetBlendMode(GpuBlendMode mode);

    std::shared_ptr<GpuTexture> CreateDepthTexture(int width, int height, bool cube,
                                                   uint32_t arraySize = 1);

    void PushRenderTarget(const D3D12_CPU_DESCRIPTOR_HANDLE* colorRtv,
                          D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);
    void PopRenderTarget();

    void BindDepthOnlyShader(GpuShader* shader);
    void SetRasterState(bool cullNone, bool wireframe);

    bool CreateMainDepthBuffer();
    D3D12_CPU_DESCRIPTOR_HANDLE AllocDsvSlot();
    D3D12_CPU_DESCRIPTOR_HANDLE AllocRtvSlot();
    D3D12_CPU_DESCRIPTOR_HANDLE GetMainColorRtv() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvCpu() const { return GetMainColorRtv(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetMainDsvHandle() const;

    D3D12_CPU_DESCRIPTOR_HANDLE AllocSrvSlot(D3D12_GPU_DESCRIPTOR_HANDLE& outGpu);
    D3D12_CPU_DESCRIPTOR_HANDLE AllocSampSlot(D3D12_GPU_DESCRIPTOR_HANDLE& outGpu);

    void ResetPostProcessDescriptorAllocators();
    void BindPSTextureDescriptors(uint32_t slot,
                                  D3D12_GPU_DESCRIPTOR_HANDLE srvGpu,
                                  D3D12_GPU_DESCRIPTOR_HANDLE sampGpu);
    std::shared_ptr<GpuShader> CreateFullscreenShaderFromBytecode(
        const void* vsBytecode,
        size_t vsSize,
        const void* psBytecode,
        size_t psSize,
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_UNKNOWN);

    ID3D12Device* GetDevice() const { return m_Device.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return m_CommandQueue.Get(); }
    ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return m_SrvHeap.Get(); }
    int GetNumFramesInFlight() const { return static_cast<int>(kFrameCount); }

    D3D12_CPU_DESCRIPTOR_HANDLE GetFontSrvCpuHandle() const {
        return m_FontSrvCpuHandle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE GetFontSrvGpuHandle() const {
        return m_FontSrvGpuHandle;
    }

    ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }
    void WaitForGpuIdle();

private:
    struct FrameResources {
        ComPtr<ID3D12CommandAllocator> commandAllocator;

        ComPtr<ID3D12Resource> constantBufferUpload;
        uint8_t* constantBufferMapped = nullptr;
        uint32_t constantBufferCapacity = 0;
        uint32_t constantBufferOffset = 0;

        uint64_t fenceValue = 0;
    };

    struct RenderTargetBinding {
        D3D12_CPU_DESCRIPTOR_HANDLE colorRtv = {};
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = {};
        bool hasColorTarget = false;
    };

private:
    friend class D3D12SwapChain;

    void WaitForFrame(uint32_t frameIndex);
    void TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);
    void TransitionToPresent(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);
    void PresentSwapChain(bool vsync);
    bool ResizeSwapChain(uint32_t width, uint32_t height);
    bool CheckDeviceResult(HRESULT hr, const char* operation);
    void EnsureDefaultResources();
    void ApplyBoundPipelineState();

    bool BuildShaderPipelines(D3D12Shader& shader,
                              const D3D12_SHADER_BYTECODE& vs,
                              const D3D12_SHADER_BYTECODE& ps,
                              const VertexElement* layout,
                              uint32_t layoutCount);

    static DXGI_FORMAT ToDxgiFormat(VertexFormat fmt);
    static D3D12_CPU_DESCRIPTOR_HANDLE OffsetHandle(D3D12_CPU_DESCRIPTOR_HANDLE h, uint32_t index, uint32_t inc);
    static D3D12_GPU_DESCRIPTOR_HANDLE OffsetHandle(D3D12_GPU_DESCRIPTOR_HANDLE h, uint32_t index, uint32_t inc);

private:
    bool m_IsRecording = false;
    bool m_ImGuiInitialized = false;
    bool m_DeviceLost = false;
    bool m_DepthOnlyBound = false;
    bool m_CullNone = false;
    bool m_Wireframe = false;
    uint32_t m_RenderFrameIndex = 0;
    uint32_t m_SwapChainWidth = 0;
    uint32_t m_SwapChainHeight = 0;
    std::string m_LastDeviceError;

    ComPtr<ID3D12Device>              m_Device;
    ComPtr<ID3D12CommandQueue>       m_CommandQueue;
    ComPtr<IDXGISwapChain3>          m_SwapChain;
    ComPtr<ID3D12GraphicsCommandList> m_CommandList;

    ComPtr<ID3D12DescriptorHeap>     m_RtvHeap;
    ComPtr<ID3D12DescriptorHeap>     m_OffscreenRtvHeap;
    ComPtr<ID3D12Resource>          m_BackBuffers[kFrameCount];
    D3D12_CPU_DESCRIPTOR_HANDLE      m_RtvHandles[kFrameCount] = {};
    uint32_t                         m_RtvDescriptorSize = 0;
    uint32_t                         m_OffscreenRtvDescriptorSize = 0;
    uint32_t                         m_NextRtvSlot = kFrameCount;
    uint32_t                         m_NextOffscreenRtvSlot = 0;
    DXGI_FORMAT                      m_RtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    ComPtr<ID3D12DescriptorHeap>     m_DsvHeap;
    ComPtr<ID3D12Resource>           m_MainDepthBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_MainDsvHandle = {};
    uint32_t                         m_DsvDescriptorSize = 0;
    uint32_t                         m_NextDsvSlot = 0;

    ComPtr<ID3D12DescriptorHeap>     m_SrvHeap;
    uint32_t                         m_SrvDescriptorSize = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_FontSrvCpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_FontSrvGpuHandle = {};
    uint32_t                         m_NextSrvSlot = 1;

    static constexpr uint32_t kDefaultSamplerDescriptorCount = 64;
    ComPtr<ID3D12DescriptorHeap>     m_SamplerHeap;
    uint32_t                         m_SamplerDescriptorSize = 0;
    uint32_t                         m_NextSampSlot = 0;

    ComPtr<ID3D12Resource>           m_DefaultTexture;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_DefaultTexSrvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_DefaultTexSrvGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE      m_DefaultSampCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_DefaultSampGpu = {};

    ComPtr<ID3D12CommandAllocator>    m_UploadCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_UploadCommandList;

    FrameResources                   m_Frames[kFrameCount];

    ComPtr<ID3D12Fence>             m_Fence;
    HANDLE                          m_FenceEvent = nullptr;
    uint64_t                         m_NextFenceValue = 1;

    D3D12_VIEWPORT                   m_Viewport{};
    D3D12_RECT                       m_ScissorRect{};
    bool                             m_HasViewport = false;
    std::unique_ptr<GpuSwapChain>    m_SwapChainInterface;
    std::unique_ptr<GpuCommandList>  m_GraphicsCommandList;
    D3D12Shader*                     m_BoundShader = nullptr;
    GpuBlendMode                     m_BlendMode = GpuBlendMode::Opaque;
    RenderTargetBinding              m_CurrentRenderTarget;
    std::vector<RenderTargetBinding> m_RenderTargetStack;
};

std::unique_ptr<IRenderContext> CreateD3D12Context();
