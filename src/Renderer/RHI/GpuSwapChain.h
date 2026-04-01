#pragma once

#include <cstdint>

// Presentation interface for backbuffer management.
class GpuSwapChain {
public:
    virtual ~GpuSwapChain() = default;

    virtual void Present(bool vsync = true) = 0;
    virtual bool Resize(uint32_t width, uint32_t height) = 0;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
};
