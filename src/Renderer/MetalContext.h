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

    void BindShader      (GpuShader* shader) override;
    void BindVertexBuffer(GpuBuffer* buffer) override;
    void BindIndexBuffer (GpuBuffer* buffer) override;

    void SetVSConstants(const void* data, uint32_t byteSize) override;

    void Draw       (uint32_t vertexCount, uint32_t startVertex = 0) override;
    void DrawIndexed(uint32_t indexCount,  uint32_t startIndex  = 0,
                     uint32_t baseVertex   = 0)                      override;

    void SetViewport(float x, float y, float w, float h) override;

    // Metal-specific accessors used by the ImGui Metal back-end.
    // Returned as void* to keep ObjC types out of this header.
    void* GetDevice()              const;  // id<MTLDevice>
    void* GetCommandBuffer()       const;  // id<MTLCommandBuffer>
    void* GetCommandEncoder()      const;  // id<MTLRenderCommandEncoder>
    void* GetRenderPassDescriptor() const; // MTLRenderPassDescriptor*

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

#endif // MYENGINE_PLATFORM_MACOS
