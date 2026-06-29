#pragma once

#include "Core/Platform.h"

#ifdef MYENGINE_PLATFORM_MACOS

#include "Renderer/IRenderContext.h"
#include <cstddef>
#include <memory>

// Forward declarations – avoids pulling in Objective-C headers from C++ code.
struct SDL_Window;

// ============================================================================
// MetalContext  –  Metal rendering back-end for macOS.
//
//  Implements IRenderContext on top of Apple Metal via SDL3's Metal view
//  helpers (SDL_Metal_CreateView / SDL_Metal_GetLayer).
//
//  ObjC/Metal types are fully hidden behind a pImpl so that this header
//  can be safely included from plain C++ translation units.
// ============================================================================
class MetalContext : public IRenderContext {
public:
    MetalContext();
    ~MetalContext() override;

    // IRenderContext –--------------------------------------------------------
    bool Init(IWindow* window)  override;
    void Shutdown()             override;

    void BeginFrame(float r, float g, float b, float a = 1.0f) override;
    void EndFrame()  override;
    GpuSwapChain* GetSwapChain() override;
    GpuTextureView* GetCurrentBackBufferView() override;
    GpuCommandList* GetGraphicsCommandList() override;
    RHIBackend GetBackend() const override { return RHIBackend::Metal; }
    ImGuiBackendHandles GetImGuiBackendHandles() override;

    std::shared_ptr<GpuBuffer> CreateVertexBuffer(
        const void* data, uint32_t byteSize, uint32_t strideBytes) override;

    std::shared_ptr<GpuBuffer> CreateIndexBuffer(
        const void* data, uint32_t byteSize) override;

    // mslSource: Metal Shading Language string.
    // vsEntry / psEntry: function names inside the MSL source.
    std::shared_ptr<GpuShader> CreateShader(
        const std::string& mslSource,
        const std::string& vsEntry,
        const std::string& psEntry,
        const VertexElement* layout,
        uint32_t            layoutCount) override;
    std::shared_ptr<GpuShader> CreateShaderFromBytecode(
        const void* vsBytecode,
        size_t vsSize,
        const void* psBytecode,
        size_t psSize,
        const VertexElement* layout,
        uint32_t layoutCount) override;
    std::shared_ptr<GpuShader> CreateComputeShaderFromBytecode(
        const void* bytecode, size_t byteSize) override;

    void BindShader      (GpuShader* shader);
    void BindVertexBuffer(GpuBuffer* buffer);
    void BindIndexBuffer (GpuBuffer* buffer);

    void SetVSConstants(const void* data, uint32_t byteSize);

    void Draw       (uint32_t vertexCount, uint32_t startVertex = 0);
    void DrawIndexed(uint32_t indexCount,  uint32_t startIndex  = 0,
                     uint32_t baseVertex   = 0);
    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                       uint32_t startVertex = 0);
    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                              uint32_t startIndex = 0,
                              uint32_t baseVertex = 0);

    void SetViewport(float x, float y, float w, float h);
    void BeginRendering(const RenderingInfo& info);
    void EndRendering();
    std::shared_ptr<GpuGraphicsPipeline> CreateGraphicsPipeline(
        const GraphicsPipelineDesc& desc) override;
    std::shared_ptr<GpuComputePipeline> CreateComputePipeline(
        const ComputePipelineDesc& desc) override;
    void SetGraphicsPipeline(GpuGraphicsPipeline* pipeline);
    void SetComputePipeline(GpuComputePipeline* pipeline);
    void SetBindGroup(GpuBindGroup* group);
    void Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1);
    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) override;
    std::shared_ptr<GpuTexture> UploadTexture(
        const RHITextureDesc& desc, const RHITextureSubresourceData* data,
        uint32_t subresourceCount) override;
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override;
    std::shared_ptr<GpuTextureView> CreateTextureView(
        const std::shared_ptr<GpuTexture>& texture, const RHITextureViewDesc& desc) override;
    std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc& desc) override;
    void BindPSTexture(uint32_t slot, GpuTexture* tex);

    // Metal-specific accessors.
    // Returned as void* to keep ObjC types out of this header.
    void* GetDevice()              const;  // id<MTLDevice>
    void* GetCommandBuffer()       const;  // id<MTLCommandBuffer>
    void* GetCommandEncoder()      const;  // id<MTLRenderCommandEncoder>
    void* GetRenderPassDescriptor() const; // MTLRenderPassDescriptor*

private:
    friend class MetalSwapChain;

    void PresentSwapChain(bool vsync);
    bool ResizeSwapChain(uint32_t width, uint32_t height);

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
    std::unique_ptr<GpuSwapChain> m_SwapChainInterface;
    std::unique_ptr<GpuCommandList> m_GraphicsCommandList;
};

#endif // MYENGINE_PLATFORM_MACOS
