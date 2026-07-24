#pragma once

#include "API/RuntimeApi.h"

#include "Renderer/RHI/IRHIDevice.h"

#include <cstddef>
#include <functional>
#include <cstdint>

struct GpuUploadBudget {
    size_t maxTasks = static_cast<size_t>(-1);
    uint64_t maxBytes = 32ull * 1024ull * 1024ull;
    double maxMilliseconds = 4.0;
};

struct GpuUploadQueueStats {
    size_t pendingTasks = 0;
    uint64_t pendingBytes = 0;
    size_t peakPendingTasks = 0;
    uint64_t peakPendingBytes = 0;
    uint64_t enqueuedTasks = 0;
    uint64_t enqueuedBytes = 0;
    uint64_t processedTasks = 0;
    uint64_t processedBytes = 0;
    uint64_t failedTasks = 0;
    uint64_t byteBudgetDeferrals = 0;
    uint64_t timeBudgetDeferrals = 0;
    uint64_t taskBudgetDeferrals = 0;
};

using GpuUploadFence = uint64_t;

class MYENGINE_RUNTIME_API GpuUploadQueue {
public:
    using UploadTask = std::function<void(IRHIDevice&)>;

    static GpuUploadQueue& Get();

    GpuUploadQueue(const GpuUploadQueue&) = delete;
    GpuUploadQueue& operator=(const GpuUploadQueue&) = delete;

    GpuUploadFence Enqueue(UploadTask task, uint64_t estimatedBytes = 0);
    size_t Process(IRHIDevice& device, size_t maxTasks = static_cast<size_t>(-1));
    size_t Process(IRHIDevice& device, const GpuUploadBudget& budget);
    void SetDefaultBudget(GpuUploadBudget budget);
    GpuUploadBudget GetDefaultBudget() const;
    size_t PendingCount() const;
    GpuUploadQueueStats GetStats() const;
    GpuUploadFence CaptureFence() const;
    bool IsFenceComplete(GpuUploadFence fence) const;
    void Clear();
    void ResetStatistics();

private:
    GpuUploadQueue() = default;

    struct Impl;
    Impl* m_Impl = nullptr;
};
