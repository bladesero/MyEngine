#pragma once

#include "Core/Platform.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/RHI/IRHIFrameContext.h"
#include "Renderer/RHI/IRHIReadbackService.h"
#include "Renderer/RHI/IEditorImGuiRHIInterop.h"

#include <memory>

class IWindow;

// Compatibility facade for existing code. New renderer code should depend on
// the smallest split interface it needs: IRHIDevice, IRHIFrameContext,
// IRHIReadbackService, or IEditorImGuiRHIInterop.
class IRHIContext : public IRHIDevice,
                    public IRHIFrameContext,
                    public IRHIReadbackService,
                    public IEditorImGuiRHIInterop {
public:
    virtual ~IRHIContext() = default;

    virtual bool Init(IWindow* window) = 0;
    virtual void Shutdown() = 0;
};

// Backward-compatible name for existing code.
using IRenderContext = IRHIContext;

// Factory functions - guarded by platform so including this header on any
// platform does not implicitly require platform-specific link libraries.
#ifdef MYENGINE_PLATFORM_WINDOWS
std::unique_ptr<IRenderContext> CreateD3D11Context();
std::unique_ptr<IRenderContext> CreateD3D12Context();
#if defined(MYENGINE_ENABLE_VULKAN)
std::unique_ptr<IRenderContext> CreateVulkanContext();
#endif
#endif

#ifdef MYENGINE_PLATFORM_MACOS
std::unique_ptr<IRenderContext> CreateMetalContext();
#endif
