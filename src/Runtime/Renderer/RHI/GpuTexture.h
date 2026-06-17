#pragma once

#include "Renderer/RHI/GpuResource.h"

struct GpuTexture : GpuResource {
    virtual ~GpuTexture() = default;
    virtual bool IsCube() const { return false; }
};
