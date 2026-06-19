#pragma once

#include "Core/Platform.h"

#ifdef MYENGINE_PLATFORM_MACOS

#include "Renderer/IRenderContext.h"
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
    GpuCommandList* GetGraphicsCommandList() override;
    RHIBackend GetBackend() const override { return RHIBackend::Metal; }

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

    void BindShader      (GpuShader* shader);
    void BindVertexBuffer(GpuBuffer* buffer);
    void BindIndexBuffer (GpuBuffer* buffer);

    void SetVSConstants(const void* data, uint32_t byteSize);

    void Draw       (uint32_t vertexCount, uint32_t startVertex = 0);
    void DrawIndexed(uint32_t indexCount,  uint32_t startIndex  = 0,
                     uint32_t baseVertex   = 0);

    void SetViewport(float x, float y, float w, float h);
    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) override;
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
