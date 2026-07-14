#pragma once

#include "Renderer/RHI/GpuBuffer.h"

#include <memory>

struct GpuBufferView : GpuResource {
    GpuBufferView() { CommitAccounting(GpuResourceAccountingClass::Descriptor, 0, 1); }
    std::shared_ptr<GpuBuffer> buffer;
    RHIBufferViewDesc desc;
    virtual ~GpuBufferView() = default;
};
