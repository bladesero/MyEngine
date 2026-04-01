#pragma once

#include "Core/Platform.h"
#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuCommandList.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/GpuSwapChain.h"
#include "Renderer/RHI/GpuTexture.h"
#include "Renderer/RHI/VertexLayout.h"

#include <cstdint>
#include <memory>
#include <string>

class IWindow;
struct ImDrawData;
union SDL_Event;

// --------------------------------------------------------------------------
// IRHIContext - platform-agnostic rendering hardware interface
// --------------------------------------------------------------------------
class IRHIContext {
public:
    virtual ~IRHIContext() = default;

    // Lifecycle ---------------------------------------------------------------
    virtual bool Init(IWindow* window) = 0;
    virtual void Shutdown()            = 0;

    // Per-frame ---------------------------------------------------------------
    virtual void BeginFrame(float r, float g, float b, float a = 1.0f) = 0;
    virtual void EndFrame()  = 0;   // swap chain present
    // Optional swapchain abstraction (nullptr when backend keeps it internal).
    virtual GpuSwapChain* GetSwapChain() { return nullptr; }
    virtual GpuCommandList* GetGraphicsCommandList() = 0;

    // ImGui backend hooks -----------------------------------------------------
    // Editor/UI layers call these backend-agnostic hooks. Concrete contexts
    // map them to imgui_impl_* implementations.
    virtual bool InitImGui(IWindow* window) {
        (void)window;
        return false;
    }
    virtual void ShutdownImGui() {}
    virtual void ProcessImGuiSDLEvent(const SDL_Event& event) {
        (void)event;
    }
    virtual void BeginImGuiFrame() {}
    virtual void RenderImGuiDrawData(ImDrawData* drawData) {
        (void)drawData;
    }

    // Resource creation -------------------------------------------------------
    virtual std::shared_ptr<GpuBuffer> CreateVertexBuffer(
        const void* data, uint32_t byteSize, uint32_t strideBytes) = 0;

    virtual std::shared_ptr<GpuBuffer> CreateIndexBuffer(
        const void* data, uint32_t byteSize) = 0;

    virtual std::shared_ptr<GpuShader> CreateShader(
        const std::string& shaderSource,
        const std::string& vsEntry,
        const std::string& psEntry,
        const VertexElement* layout,
        uint32_t layoutCount) = 0;

    virtual std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) = 0;

    // Compatibility immediate wrappers ---------------------------------------
    // Existing callers can keep using RenderContext-style calls, while new
    // RHI code can operate through GpuCommandList directly.
    virtual void BindShader(GpuShader* shader) {
        if (auto* cmd = GetGraphicsCommandList()) {
            cmd->BindShader(shader);
        }
    }

    virtual void BindVertexBuffer(GpuBuffer* buffer) {
        if (auto* cmd = GetGraphicsCommandList()) {
            cmd->BindVertexBuffer(buffer);
        }
    }

    virtual void BindIndexBuffer(GpuBuffer* buffer) {
        if (auto* cmd = GetGraphicsCommandList()) {
            cmd->BindIndexBuffer(buffer);
        }
    }

    virtual void SetVSConstants(const void* data, uint32_t byteSize) {
        if (auto* cmd = GetGraphicsCommandList()) {
            cmd->SetVSConstants(data, byteSize);
        }
    }

    virtual void Draw(uint32_t vertexCount, uint32_t startVertex = 0) {
        if (auto* cmd = GetGraphicsCommandList()) {
            cmd->Draw(vertexCount, startVertex);
        }
    }

    virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                             uint32_t baseVertex = 0) {
        if (auto* cmd = GetGraphicsCommandList()) {
            cmd->DrawIndexed(indexCount, startIndex, baseVertex);
        }
    }

    virtual void SetViewport(float x, float y, float w, float h) {
        if (auto* cmd = GetGraphicsCommandList()) {
            cmd->SetViewport(x, y, w, h);
        }
    }

    virtual void BindPSTexture(uint32_t slot, GpuTexture* tex) {
        if (auto* cmd = GetGraphicsCommandList()) {
            cmd->BindPSTexture(slot, tex);
        }
    }
};

// Backward-compatible name for existing code.
using IRenderContext = IRHIContext;

// Factory functions - guarded by platform so including this header on any
// platform does not implicitly require platform-specific link libraries.
#ifdef MYENGINE_PLATFORM_WINDOWS
std::unique_ptr<IRenderContext> CreateD3D11Context();
std::unique_ptr<IRenderContext> CreateD3D12Context();
#endif

#ifdef MYENGINE_PLATFORM_MACOS
std::unique_ptr<IRenderContext> CreateMetalContext();
#endif
