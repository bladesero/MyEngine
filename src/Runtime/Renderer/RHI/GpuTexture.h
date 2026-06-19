#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/RHITypes.h"

struct GpuTexture : GpuResource {
    RHITextureDesc desc;
    virtual ~GpuTexture() = default;
    virtual bool IsCube() const { return false; }
};
