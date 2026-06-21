#pragma once

#include "Renderer/RHI/GpuCommandList.h"
#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/RHI/IRHIReadbackService.h"
#include "Scene/Scene.h"
#include "Camera/Camera.h"

#include <cstdint>

// Base abstraction for renderer passes (shadow, main color, post, ...).
class RenderPass {
public:
    explicit RenderPass(IRHIDevice* device, IRHIReadbackService* readbackService = nullptr)
        : m_Device(device), m_ReadbackService(readbackService) {}
    virtual ~RenderPass() = default;

    virtual void Execute(GpuCommandList& commands, const Scene& scene,
                         const Camera& camera) = 0;
    virtual void Resize(uint32_t width, uint32_t height) {
        (void)width;
        (void)height;
    }

protected:
    IRHIDevice* Device() const { return m_Device; }
    IRHIReadbackService* ReadbackService() const { return m_ReadbackService; }

private:
    IRHIDevice* m_Device = nullptr;
    IRHIReadbackService* m_ReadbackService = nullptr;
};
