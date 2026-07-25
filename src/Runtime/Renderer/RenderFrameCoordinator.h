#pragma once

#include "API/RuntimeApi.h"

#include <cstdint>

class IRHIDevice;
class IRHIFrameContext;

// Owns the device-level frame boundary shared by all viewports rendered by one
// application. Individual Renderer instances keep their own graph and temporal
// state, but uploads, BeginFrame, and the final Present happen once.
class MYENGINE_RUNTIME_API RenderFrameCoordinator {
public:
    RenderFrameCoordinator(IRHIDevice* device, IRHIFrameContext* frameContext);
    ~RenderFrameCoordinator();
    RenderFrameCoordinator(const RenderFrameCoordinator&) = delete;
    RenderFrameCoordinator& operator=(const RenderFrameCoordinator&) = delete;

    // The coordinator resolves the engine frame number inside Runtime. Callers
    // can live in different DLL/EXE modules and must not supply module-local
    // copies of Time::FrameCount().
    void BeginFrame(float r = 0.12f, float g = 0.12f, float b = 0.18f, float a = 1.0f);
    void EndFrame();

private:
    IRHIDevice* m_Device = nullptr;
    IRHIFrameContext* m_FrameContext = nullptr;
    uint64_t m_FrameNumber = 0;
    bool m_FrameOpen = false;
};
