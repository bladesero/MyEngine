#pragma once

#include "Renderer/IRenderContext.h"

#include <cstddef>
#include <functional>

class GpuUploadQueue {
public:
    using UploadTask = std::function<void(IRenderContext&)>;

    static GpuUploadQueue& Get();

    GpuUploadQueue(const GpuUploadQueue&) = delete;
    GpuUploadQueue& operator=(const GpuUploadQueue&) = delete;

    void Enqueue(UploadTask task);
    size_t Process(IRenderContext& context, size_t maxTasks = static_cast<size_t>(-1));
    size_t PendingCount() const;
    void Clear();

private:
    GpuUploadQueue() = default;

    struct Impl;
    Impl* m_Impl = nullptr;
};
