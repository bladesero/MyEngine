#pragma once

#include "Renderer/RHI/IRHIDevice.h"

#include <cstddef>
#include <functional>
#include <cstdint>

struct GpuUploadBudget {
    size_t maxTasks = static_cast<size_t>(-1);
    uint64_t maxBytes = 32ull * 1024ull * 1024ull;
    double maxMilliseconds = 4.0;
};

class GpuUploadQueue {
public:
    using UploadTask = std::function<void(IRHIDevice&)>;

    static GpuUploadQueue& Get();

    GpuUploadQueue(const GpuUploadQueue&) = delete;
    GpuUploadQueue& operator=(const GpuUploadQueue&) = delete;

    void Enqueue(UploadTask task, uint64_t estimatedBytes = 0);
    size_t Process(IRHIDevice& device, size_t maxTasks = static_cast<size_t>(-1));
    size_t Process(IRHIDevice& device, const GpuUploadBudget& budget);
    void SetDefaultBudget(GpuUploadBudget budget);
    GpuUploadBudget GetDefaultBudget() const;
    size_t PendingCount() const;
    void Clear();

private:
    GpuUploadQueue() = default;

    struct Impl;
    Impl* m_Impl = nullptr;
};
