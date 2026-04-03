#pragma once

// Base class for all GPU-side opaque resources.
struct GpuResource {
    virtual ~GpuResource() = default;
};
