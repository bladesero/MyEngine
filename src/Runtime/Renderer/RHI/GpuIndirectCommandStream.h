#pragma once

#include "Renderer/RHI/GpuResource.h"

#include <cstdint>

struct GpuBuffer;

// Backend-neutral executable indexed-indirect stream. Metal specializes this
// with an MTLIndirectCommandBuffer; D3D12/Vulkan retain native counted draws.
struct GpuIndexedIndirectCommandStream : GpuResource {
    uint32_t capacity = 0;
    GpuBuffer* arguments = nullptr;
    GpuBuffer* count = nullptr;
    GpuBuffer* indexBuffer = nullptr;
    uint64_t argumentOffset = 0;
    uint64_t countOffset = 0;
    uint32_t maxDrawCount = 0;
    uint32_t stride = 0;
};
