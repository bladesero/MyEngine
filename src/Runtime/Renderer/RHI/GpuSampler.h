#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/RHITypes.h"

struct GpuSampler : GpuResource {
    GpuSampler(){CommitAccounting(GpuResourceAccountingClass::Descriptor,0,1);}
    RHISamplerDesc desc;
    virtual ~GpuSampler() = default;
};
