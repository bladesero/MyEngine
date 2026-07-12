#pragma once

#include "Renderer/RHI/GpuCommandList.h"
#include "Renderer/RHI/GpuSwapChain.h"
#include "Renderer/RHI/GpuSync.h"
#include "Renderer/RHI/GpuTextureView.h"

#include <cstdint>
#include <string>

enum class RHIDeviceLossReason : uint8_t {
    None,
    Removed,
    Reset,
    Hung,
    DriverInternalError,
    OutOfMemory,
    Unknown
};

struct RHIDeviceLossInfo {
    RHIDeviceLossReason reason = RHIDeviceLossReason::None;
    int64_t nativeCode = 0;
    uint64_t deviceGeneration = 0;
    std::string diagnostic;
};

inline const char* RHIDeviceLossReasonName(RHIDeviceLossReason reason) {
    switch (reason) {
    case RHIDeviceLossReason::None: return "none";
    case RHIDeviceLossReason::Removed: return "removed";
    case RHIDeviceLossReason::Reset: return "reset";
    case RHIDeviceLossReason::Hung: return "hung";
    case RHIDeviceLossReason::DriverInternalError: return "driver_internal_error";
    case RHIDeviceLossReason::OutOfMemory: return "out_of_memory";
    default: return "unknown";
    }
}

class IRHIFrameContext {
public:
    virtual ~IRHIFrameContext() = default;

    virtual void BeginFrame(float r, float g, float b, float a = 1.0f) = 0;
    virtual void EndFrame() = 0;
    virtual bool IsDeviceLost() const { return false; }
    virtual const std::string& GetLastDeviceError() const {
        static const std::string empty;
        return empty;
    }
    virtual RHIDeviceLossInfo GetDeviceLossInfo() const {
        return IsDeviceLost()
            ? RHIDeviceLossInfo{RHIDeviceLossReason::Unknown, 0, 0, GetLastDeviceError()}
            : RHIDeviceLossInfo{};
    }
    virtual uint32_t GetFrameIndex() const { return 0; }
    virtual GpuCommandList* GetGraphicsCommandList() = 0;
    virtual GpuQueue* GetGraphicsQueue() { return nullptr; }
    virtual GpuSwapChain* GetSwapChain() { return nullptr; }
    virtual GpuTextureView* GetCurrentBackBufferView() { return nullptr; }
    virtual void SetVSyncEnabled(bool enabled) { (void)enabled; }
    virtual bool IsVSyncEnabled() const { return true; }
};
