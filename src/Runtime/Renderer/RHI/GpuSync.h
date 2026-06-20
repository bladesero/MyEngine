#pragma once

#include <cstdint>
#include <vector>

class GpuCommandList;

class GpuFence {
public:
    virtual ~GpuFence() = default;
    virtual uint64_t GetCompletedValue() const = 0;
    virtual bool Wait(uint64_t value, uint32_t timeoutMs = UINT32_MAX) = 0;
};

class GpuQueue {
public:
    virtual ~GpuQueue() = default;
    // Immediate backends submit recorded work at their frame boundary. A null
    // command list signals after all work already submitted to this queue.
    virtual bool Submit(GpuCommandList* commandList, GpuFence* signalFence,
                        uint64_t signalValue) = 0;
    virtual bool Wait(GpuFence* fence, uint64_t value) = 0;
};

class GpuTimestampQueryPool {
public:
    virtual ~GpuTimestampQueryPool() = default;
    virtual uint32_t GetCount() const = 0;
    virtual uint64_t GetFrequency() const = 0;
    virtual bool ReadResults(uint32_t first, uint32_t count,
                             std::vector<uint64_t>& ticks) = 0;
};
