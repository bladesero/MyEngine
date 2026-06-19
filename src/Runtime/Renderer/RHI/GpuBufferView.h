#pragma once

#include "Renderer/RHI/GpuBuffer.h"

#include <memory>

struct GpuBufferView : GpuResource {
    std::shared_ptr<GpuBuffer> buffer;
    RHIBufferViewDesc desc;
    virtual ~GpuBufferView() = default;
};

