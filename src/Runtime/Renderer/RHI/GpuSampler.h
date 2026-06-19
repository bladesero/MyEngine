#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/RHITypes.h"

struct GpuSampler : GpuResource {
    RHISamplerDesc desc;
    virtual ~GpuSampler() = default;
};
