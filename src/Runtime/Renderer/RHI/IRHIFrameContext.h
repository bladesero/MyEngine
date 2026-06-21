#pragma once

#include "Renderer/RHI/GpuCommandList.h"
#include "Renderer/RHI/GpuSwapChain.h"
#include "Renderer/RHI/GpuSync.h"
#include "Renderer/RHI/GpuTextureView.h"

#include <cstdint>
#include <string>

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
    virtual uint32_t GetFrameIndex() const { return 0; }
    virtual GpuCommandList* GetGraphicsCommandList() = 0;
    virtual GpuQueue* GetGraphicsQueue() { return nullptr; }
    virtual GpuSwapChain* GetSwapChain() { return nullptr; }
    virtual GpuTextureView* GetCurrentBackBufferView() { return nullptr; }
};
