#pragma once

#include "IRenderContext.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;

// ============================================================================
// D3D12Context
// ----------------------------------------------------------------------------
// Minimal D3D12 implementation that matches the needs of Renderer.cpp:
//   - Create buffers from raw bytes
//   - Compile HLSL string into VS/PS + input layout
//   - Per-draw VS constants (cbuffer b0)
//   - Draw / DrawIndexed
//   - Clear + Present
//
// Also exposes some native handles so EditorLayer can initialize ImGui
// (imgui_impl_dx12) and render into the *same* in-progress command list.
// ============================================================================
class D3D12Context final : public IRenderContext {
public:
    static constexpr uint32_t kFrameCount = 2;
    static constexpr uint32_t kDefaultConstantBufferCapacity = 1024 * 1024; // 1MB per frame
    static constexpr uint32_t kDefaultSrvDescriptorCount      = 256;

    D3D12Context();
    ~D3D12Context() override;

    bool Init(IWindow* window) override;
    void Shutdown() override;

    void BeginFrame(float r, float g, float b, float a = 1.0f) override;
    void EndFrame() override;
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

    void BindShader(GpuShader* shader) override;
    void BindVertexBuffer(GpuBuffer* buffer) override;
    void BindIndexBuffer(GpuBuffer* buffer) override;
    void SetVSConstants(const void* data, uint32_t byteSize) override;

    void Draw(uint32_t vertexCount, uint32_t startVertex = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                     uint32_t baseVertex = 0) override;

    void SetViewport(float x, float y, float w, float h) override;

    // Texture upload (stub – D3D12 texture path not yet implemented)
    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) override;
    void BindPSTexture(uint32_t slot, GpuTexture* tex) override;

    // -------------------------------------------------------------------------
    // Native handles for Editor (ImGui DX12 backend).
    // -------------------------------------------------------------------------
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

    // Must be the command list currently being recorded (between BeginFrame
    // and EndFrame).
    ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }

private:
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
    };

    struct FrameResources {
        ComPtr<ID3D12CommandAllocator> commandAllocator;

        ComPtr<ID3D12Resource> constantBufferUpload;
        uint8_t* constantBufferMapped = nullptr;
        uint32_t constantBufferCapacity = 0;
        uint32_t constantBufferOffset = 0;

        uint64_t fenceValue = 0;
    };

private:
    friend class D3D12SwapChain;

    void WaitForFrame(uint32_t frameIndex);
    void TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);
    void TransitionToPresent(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex);
    void PresentSwapChain(bool vsync);
    bool ResizeSwapChain(uint32_t width, uint32_t height);

    static DXGI_FORMAT ToDxgiFormat(VertexFormat fmt);
    static D3D12_CPU_DESCRIPTOR_HANDLE OffsetHandle(D3D12_CPU_DESCRIPTOR_HANDLE h, uint32_t index, uint32_t inc);
    static D3D12_GPU_DESCRIPTOR_HANDLE OffsetHandle(D3D12_GPU_DESCRIPTOR_HANDLE h, uint32_t index, uint32_t inc);

private:
    bool m_IsRecording = false;
    bool m_ImGuiInitialized = false;
    uint32_t m_RenderFrameIndex = 0;
    uint32_t m_SwapChainWidth = 0;
    uint32_t m_SwapChainHeight = 0;

    // D3D core objects
    ComPtr<ID3D12Device>              m_Device;
    ComPtr<ID3D12CommandQueue>       m_CommandQueue;
    ComPtr<IDXGISwapChain3>          m_SwapChain;
    ComPtr<ID3D12GraphicsCommandList> m_CommandList;

    // Swapchain render targets
    ComPtr<ID3D12DescriptorHeap>     m_RtvHeap;
    ComPtr<ID3D12Resource>          m_BackBuffers[kFrameCount];
    D3D12_CPU_DESCRIPTOR_HANDLE      m_RtvHandles[kFrameCount] = {};
    uint32_t                         m_RtvDescriptorSize = 0;
    DXGI_FORMAT                      m_RtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Descriptors (ImGui needs an SRV heap even if we only render fonts)
    ComPtr<ID3D12DescriptorHeap>     m_SrvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE      m_FontSrvCpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE      m_FontSrvGpuHandle = {};

    // Per-frame resources
    FrameResources                   m_Frames[kFrameCount];

    // Fences
    ComPtr<ID3D12Fence>             m_Fence;
    HANDLE                          m_FenceEvent = nullptr;
    uint64_t                         m_NextFenceValue = 1;

    // Pipeline / viewport state
    D3D12_VIEWPORT                   m_Viewport{};
    bool                             m_HasViewport = false;
    std::unique_ptr<GpuSwapChain>    m_SwapChainInterface;
    std::unique_ptr<GpuCommandList>  m_GraphicsCommandList;
};

std::unique_ptr<IRenderContext> CreateD3D12Context();

