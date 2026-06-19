#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/RHITypes.h"

struct GpuBuffer : GpuResource {
    RHIBufferDesc desc;
    virtual ~GpuBuffer() = default;
};
