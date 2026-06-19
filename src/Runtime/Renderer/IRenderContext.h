#pragma once

#include "Core/Platform.h"
#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuCommandList.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/GpuSwapChain.h"
#include "Renderer/RHI/GpuTexture.h"
#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/RHI/GpuSampler.h"
#include "Renderer/RHI/VertexLayout.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/RHI/GpuReadback.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class IWindow;
struct ImDrawData;
union SDL_Event;

// --------------------------------------------------------------------------
// IRHIContext - platform-agnostic rendering hardware interface
// --------------------------------------------------------------------------
class IRHIContext : public IRHIDevice {
public:
    virtual ~IRHIContext() = default;

    // Lifecycle ---------------------------------------------------------------
    virtual bool Init(IWindow* window) = 0;
    virtual void Shutdown()            = 0;

    // Per-frame ---------------------------------------------------------------
    virtual void BeginFrame(float r, float g, float b, float a = 1.0f) = 0;
    virtual void EndFrame()  = 0;   // swap chain present
    virtual bool IsDeviceLost() const { return false; }
    virtual const std::string& GetLastDeviceError() const {
        static const std::string empty;
        return empty;
    }
    // Optional swapchain abstraction (nullptr when backend keeps it internal).
    virtual GpuSwapChain* GetSwapChain() { return nullptr; }
    virtual GpuTextureView* GetCurrentBackBufferView() { return nullptr; }
    virtual GpuCommandList* GetGraphicsCommandList() = 0;
    RHIBackend GetBackend() const override { return RHIBackend::Unknown; }
    virtual std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(
        const std::shared_ptr<GpuBuffer>&) { return nullptr; }

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

    // Precompiled DXBC from dxc (vs_5_0 / ps_5_0). Default: unsupported (e.g. Metal).
    virtual std::shared_ptr<GpuShader> CreateShaderFromBytecode(
        const void* vsBytecode,
        size_t vsSize,
        const void* psBytecode,
        size_t psSize,
        const VertexElement* layout,
        uint32_t layoutCount) {
        (void)vsBytecode;
        (void)vsSize;
        (void)psBytecode;
        (void)psSize;
        (void)layout;
        (void)layoutCount;
        return nullptr;
    }

    virtual std::shared_ptr<GpuTexture> UploadTexture2D(
        const void* rgba8Data, int width, int height) = 0;

    virtual std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc&) {
        return nullptr;
    }
    virtual std::shared_ptr<GpuTextureView> CreateTextureView(
        const std::shared_ptr<GpuTexture>&, const RHITextureViewDesc&) {
        return nullptr;
    }
    virtual std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc&) {
        return nullptr;
    }
    std::shared_ptr<GpuBindGroup> CreateBindGroup(
        const std::shared_ptr<GpuShader>& shader) {
        return shader ? std::make_shared<GpuBindGroup>(shader) : nullptr;
    }
    virtual void* GetImGuiTextureId(GpuTextureView*) { return nullptr; }

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
