#pragma once

#include "Core/Platform.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/RHI/IRHIFrameContext.h"
#include "Renderer/RHI/IRHIReadbackService.h"

#include <memory>
#include <cstdint>
#include <string>

struct RHIDeviceIdentity {
    std::string adapterName;
    std::string driverVersion;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint32_t subsystemId = 0;
    uint32_t revision = 0;
    uint64_t dedicatedVideoMemoryBytes = 0;
    bool softwareAdapter = false;
};

class IWindow;
class IEditorImGuiRHIInterop;

// Compatibility facade for existing code. New renderer code should depend on
// the smallest split interface it needs: IRHIDevice, IRHIFrameContext,
// or IRHIReadbackService. Editor-only ImGui integration is queried explicitly.
class IRHIContext : public IRHIDevice, public IRHIFrameContext, public IRHIReadbackService {
public:
    virtual ~IRHIContext() = default;

    virtual bool Init(IWindow* window) = 0;
    virtual void Shutdown() = 0;
    virtual RHIDeviceIdentity GetDeviceIdentity() const { return {}; }
    virtual IEditorImGuiRHIInterop* QueryEditorImGuiInterop() { return nullptr; }
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
