#pragma once

#include "IRenderContext.h"

#include <cstddef>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

class D3D12DeferredReleaseQueue;
class D3D12DescriptorPool;
class D3D12DescriptorLease;
struct D3D12Sampler;

struct D3D12Buffer : GpuBuffer {
    ~D3D12Buffer() override;
    ComPtr<ID3D12Resource> resource;
    std::shared_ptr<D3D12DeferredReleaseQueue> deferredReleaseQueue;
    uint32_t byteSize = 0;
};

struct D3D12VertexBuffer : D3D12Buffer {
    uint32_t stride = 0;
};

struct D3D12IndexBuffer : D3D12Buffer {
    DXGI_FORMAT format = DXGI_FORMAT_R32_UINT;
};

struct D3D12Shader : GpuShader {
    ~D3D12Shader() override;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12PipelineState> alphaPipelineState;
    ComPtr<ID3D12PipelineState> depthOnlyPipelineState;
    ComPtr<ID3D12PipelineState> wireframePipelineState;
    ComPtr<ID3D12PipelineState> twoSidedPipelineState;
    ComPtr<ID3D12RootSignature> computeRootSignature;
    ComPtr<ID3D12PipelineState> computePipelineState;
    std::shared_ptr<D3D12DeferredReleaseQueue> deferredReleaseQueue;
    bool hasBindlessTable = false;
};

struct D3D12GraphicsPipeline : GpuGraphicsPipeline {
    ~D3D12GraphicsPipeline() override;
    ComPtr<ID3D12PipelineState> pipelineState;
    std::shared_ptr<D3D12DeferredReleaseQueue> deferredReleaseQueue;
};

struct D3D12BufferView : GpuBufferView {
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = {};
    std::shared_ptr<D3D12DescriptorLease> srvLease;
    std::shared_ptr<D3D12DescriptorLease> uavLease;
};

struct D3D12Texture : GpuTexture {
    ~D3D12Texture() override;
    ComPtr<ID3D12Resource> resource;
    std::shared_ptr<D3D12DeferredReleaseQueue> deferredReleaseQueue;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE sampCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE sampGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvCpu = {};
    std::shared_ptr<D3D12DescriptorLease> srvLease;
    std::shared_ptr<D3D12DescriptorLease> sampLease;
    std::shared_ptr<D3D12DescriptorLease> dsvLease;
    std::shared_ptr<D3D12Sampler> sampler;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> dsvFaces;
    std::vector<std::shared_ptr<D3D12DescriptorLease>> dsvFaceLeases;
    uint32_t arraySize = 1;
    bool isCube = false;

    bool IsCube() const override { return isCube; }
};

struct D3D12TextureView : GpuTextureView {
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvCpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = {};
    std::shared_ptr<D3D12DescriptorLease> srvLease;
    std::shared_ptr<D3D12DescriptorLease> rtvLease;
    std::shared_ptr<D3D12DescriptorLease> dsvLease;
    std::shared_ptr<D3D12DescriptorLease> uavLease;

    void* GetImGuiTextureId() override { return reinterpret_cast<void*>(srvGpu.ptr); }
};

struct D3D12Sampler : GpuSampler {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
    std::shared_ptr<D3D12DescriptorLease> lease;
};

// ============================================================================
// D3D12Context
// ============================================================================
class D3D12Context final : public IRenderContext {
public:
    static constexpr uint32_t kFrameCount = 2;
    static constexpr uint32_t kTextureSlotCount = 10;
    static constexpr uint32_t kOffscreenRtvCount = 256;
    static constexpr uint32_t kDsvDescriptorCount = 32;
    static constexpr uint32_t kDefaultConstantBufferCapacity = 1024 * 1024;
    static constexpr uint32_t kDefaultSrvDescriptorCount      = 1024;
    static constexpr uint32_t kDefaultSamplerDescriptorCount  = 2048;
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
    GpuQueue* GetGraphicsQueue() override { return m_GraphicsQueue.get(); }
    GpuTextureView* GetCurrentBackBufferView() override {
        if (m_RenderFrameIndex >= kFrameCount) return nullptr;
        return m_BackBufferViews[m_RenderFrameIndex].get();
    }
    void SetVSyncEnabled(bool enabled) override { m_VSyncEnabled = enabled; }
    bool IsVSyncEnabled() const override { return m_VSyncEnabled; }
    RHIBackend GetBackend() const override { return RHIBackend::D3D12; }
    ImGuiBackendHandles GetImGuiBackendHandles() override;

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
    std::shared_ptr<GpuGraphicsPipeline> CreateGraphicsPipeline(
        const GraphicsPipelineDesc& desc) override;

    void BindShader(GpuShader* shader);
    void BindVertexBuffer(GpuBuffer* buffer);
    void BindIndexBuffer(GpuBuffer* buffer);
    void SetVSConstants(const void* data, uint32_t byteSize);

    void Draw(uint32_t vertexCount, uint32_t startVertex = 0);
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                     uint32_t baseVertex = 0);
    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                       uint32_t startVertex = 0);
    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                              uint32_t startIndex = 0, uint32_t baseVertex = 0);
    void DrawIndirect(GpuBuffer* arguments, uint64_t offset, bool indexed);

    void SetViewport(float x, float y, float w, float h);

    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) override;
    bool UpdateBuffer(const std::shared_ptr<GpuBuffer>&, uint64_t,
                      const void*, uint64_t) override;
    std::shared_ptr<GpuTexture> UploadTexture(
        const RHITextureDesc&, const RHITextureSubresourceData*, uint32_t) override;
    RHIDeviceCapabilities GetCapabilities() const override;
    bool IsFormatSupported(RHIFormat, RHIResourceUsage) const override;
    std::shared_ptr<GpuFence> CreateFence(uint64_t initialValue = 0) override;
    std::shared_ptr<GpuTimestampQueryPool> CreateTimestampQueryPool(uint32_t count) override;
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override;
    std::shared_ptr<GpuTextureView> CreateTextureView(
        const std::shared_ptr<GpuTexture>& texture, const RHITextureViewDesc& desc) override;
    std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc& desc) override;
    std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(
        const std::shared_ptr<GpuBuffer>& buffer) override;
    std::shared_ptr<GpuTextureReadbackTicket> ReadbackTextureAsync(
        const std::shared_ptr<GpuTexture>&, const RHITextureRegion&) override;
    void BindPSTexture(uint32_t slot, GpuTexture* tex);
    void SetBlendMode(GpuBlendMode mode);

    std::shared_ptr<GpuTexture> CreateDepthTexture(int width, int height, bool cube,
                                                   uint32_t arraySize = 1);

    void PushRenderTarget(const D3D12_CPU_DESCRIPTOR_HANDLE* colorRtv,
                          D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);
    void PushRenderTargets(uint32_t colorCount, const D3D12_CPU_DESCRIPTOR_HANDLE* colorRtvs,
                           D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);
    void PopRenderTarget();

    void BindDepthOnlyShader(GpuShader* shader);
    void SetRasterState(bool cullNone, bool wireframe);

    bool CreateMainDepthBuffer();
    D3D12_CPU_DESCRIPTOR_HANDLE AllocDsvSlot(
        std::shared_ptr<D3D12DescriptorLease>* lease = nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE AllocRtvSlot(
        std::shared_ptr<D3D12DescriptorLease>* lease = nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE GetMainColorRtv() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvCpu() const { return GetMainColorRtv(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetMainDsvHandle() const;

    D3D12_CPU_DESCRIPTOR_HANDLE AllocSrvSlot(
        D3D12_GPU_DESCRIPTOR_HANDLE& outGpu,
        std::shared_ptr<D3D12DescriptorLease>* lease = nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE AllocSampSlot(
        D3D12_GPU_DESCRIPTOR_HANDLE& outGpu,
        std::shared_ptr<D3D12DescriptorLease>* lease = nullptr);

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
    DXGI_FORMAT GetRtvFormat() const { return m_RtvFormat; }

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

    struct SamplerCacheKey {
        RHISamplerDesc desc;

        bool operator==(const SamplerCacheKey& other) const {
            return desc.filter == other.desc.filter &&
                   desc.addressU == other.desc.addressU &&
                   desc.addressV == other.desc.addressV &&
                   desc.addressW == other.desc.addressW;
        }
    };

    struct SamplerCacheKeyHash {
        size_t operator()(const SamplerCacheKey& key) const {
            size_t hash = static_cast<size_t>(key.desc.filter);
            hash = (hash * 131u) ^ static_cast<size_t>(key.desc.addressU);
            hash = (hash * 131u) ^ static_cast<size_t>(key.desc.addressV);
            hash = (hash * 131u) ^ static_cast<size_t>(key.desc.addressW);
            return hash;
        }
    };

private:
    friend class D3D12SwapChain;

    void WaitForFrame(uint32_t frameIndex);
    void TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);
    void TransitionToPresent(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);
    void PresentSwapChain(bool vsync);
    bool ResizeSwapChain(uint32_t width, uint32_t height);
    bool RecreateFrameCommandList(ID3D12CommandAllocator* allocator);
    bool CheckDeviceResult(HRESULT hr, const char* operation);
    void ReportDeviceRemovedReason(const char* operation);
    void DumpDredDiagnostics();
    bool CanUseDevice(const char* operation);
    void EnsureDefaultResources();
    void ApplyBoundPipelineState();
    bool UploadBufferData(ID3D12Resource* destination,
                          ID3D12Resource* uploadBuffer,
                          uint64_t byteSize,
                          D3D12_RESOURCE_STATES finalState);

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
    bool m_FrameCommandListClosed = true;
    bool m_DeviceLost = false;
    bool m_VSyncEnabled = true;
    bool m_DredDumped = false;
    bool m_DeviceLossSuppressionLogged = false;
    bool m_DepthOnlyBound = false;
    bool m_CullNone = false;
    bool m_Wireframe = false;
    uint32_t m_RenderFrameIndex = 0;
    uint32_t m_SwapChainWidth = 0;
    uint32_t m_SwapChainHeight = 0;
    std::string m_LastDeviceError;

    std::shared_ptr<D3D12DeferredReleaseQueue> m_DeferredReleaseQueue;
    std::shared_ptr<D3D12DescriptorPool> m_SrvDescriptorPool;
    std::shared_ptr<D3D12DescriptorPool> m_SamplerDescriptorPool;
    std::shared_ptr<D3D12DescriptorPool> m_RtvDescriptorPool;
    std::shared_ptr<D3D12DescriptorPool> m_DsvDescriptorPool;

    ComPtr<ID3D12Device>              m_Device;
    ComPtr<ID3D12CommandQueue>       m_CommandQueue;
    ComPtr<ID3D12CommandSignature>   m_DrawIndirectSignature;
    ComPtr<ID3D12CommandSignature>   m_DrawIndexedIndirectSignature;
    ComPtr<IDXGISwapChain3>          m_SwapChain;
    ComPtr<ID3D12GraphicsCommandList> m_CommandList;

    ComPtr<ID3D12DescriptorHeap>     m_RtvHeap;
    ComPtr<ID3D12DescriptorHeap>     m_OffscreenRtvHeap;
    ComPtr<ID3D12Resource>          m_BackBuffers[kFrameCount];
    std::shared_ptr<D3D12TextureView> m_BackBufferViews[kFrameCount];
    D3D12_CPU_DESCRIPTOR_HANDLE      m_RtvHandles[kFrameCount] = {};
    uint32_t                         m_RtvDescriptorSize = 0;
    uint32_t                         m_OffscreenRtvDescriptorSize = 0;
    uint32_t                         m_NextRtvSlot = kFrameCount;
    uint32_t                         m_NextOffscreenRtvSlot = 0;
    DXGI_FORMAT                      m_RtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    ComPtr<ID3D12DescriptorHeap>     m_DsvHeap;
    ComPtr<ID3D12Resource>           m_MainDepthBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_MainDsvHandle = {};
    std::shared_ptr<D3D12DescriptorLease> m_MainDsvLease;
    uint32_t                         m_DsvDescriptorSize = 0;
    uint32_t                         m_NextDsvSlot = 0;

    ComPtr<ID3D12DescriptorHeap>     m_SrvHeap;
    uint32_t                         m_SrvDescriptorSize = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_FontSrvCpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_FontSrvGpuHandle = {};
    uint32_t                         m_NextSrvSlot = 1;

    ComPtr<ID3D12DescriptorHeap>     m_SamplerHeap;
    uint32_t                         m_SamplerDescriptorSize = 0;
    uint32_t                         m_NextSampSlot = 0;
    uint32_t                         m_UniqueSamplerDescriptorCount = 0;
    std::unordered_map<SamplerCacheKey, std::weak_ptr<D3D12Sampler>, SamplerCacheKeyHash> m_SamplerCache;

    ComPtr<ID3D12Resource>           m_DefaultTexture;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_DefaultTexSrvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_DefaultTexSrvGpu = {};
    std::shared_ptr<D3D12DescriptorLease> m_DefaultTexSrvLease;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_DefaultSampCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_DefaultSampGpu = {};
    std::shared_ptr<D3D12DescriptorLease> m_DefaultSampLease;
    std::shared_ptr<D3D12Sampler>    m_DefaultSampler;

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
    std::shared_ptr<GpuQueue>        m_GraphicsQueue;
    D3D12Shader*                     m_BoundShader = nullptr;
    GpuBlendMode                     m_BlendMode = GpuBlendMode::Opaque;
    RenderTargetBinding              m_CurrentRenderTarget;
    std::vector<RenderTargetBinding> m_RenderTargetStack;
};

std::unique_ptr<IRenderContext> CreateD3D12Context();
