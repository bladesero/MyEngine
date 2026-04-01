#pragma once

#include "Renderer/RHI/GpuResource.h"

struct GpuBuffer : GpuResource {
    virtual ~GpuBuffer() = default;
};
