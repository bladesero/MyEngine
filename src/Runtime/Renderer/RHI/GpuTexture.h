#pragma once

#include "Renderer/RHI/GpuResource.h"

struct GpuTexture : GpuResource {
    virtual ~GpuTexture() = default;
};
