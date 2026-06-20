#pragma once

#include "Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <vector>

class GpuReadbackTicket {
public:
    virtual ~GpuReadbackTicket() = default;
    virtual bool IsReady() const = 0;
    virtual bool Read(std::vector<uint8_t>& data) = 0;
    virtual uint32_t GetSize() const = 0;
};

class GpuTextureReadbackTicket : public GpuReadbackTicket {
public:
    virtual uint32_t GetRowPitch() const = 0;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual RHIFormat GetFormat() const = 0;
};
