#include "Renderer/GpuUploadQueue.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

struct GpuUploadQueue::Impl {
    std::mutex mutex;
    std::deque<UploadTask> tasks;
};

GpuUploadQueue& GpuUploadQueue::Get()
{
    static GpuUploadQueue queue;
    static Impl implementation;
    queue.m_Impl = &implementation;
    return queue;
}

void GpuUploadQueue::Enqueue(UploadTask task)
{
    if (!task) return;
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->tasks.push_back(std::move(task));
}

size_t GpuUploadQueue::Process(IRHIDevice& device, size_t maxTasks)
{
    Impl* impl = Get().m_Impl;
    std::vector<UploadTask> ready;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        const size_t count = std::min(maxTasks, impl->tasks.size());
        ready.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            ready.push_back(std::move(impl->tasks.front()));
            impl->tasks.pop_front();
        }
    }

    for (UploadTask& task : ready) task(device);
    return ready.size();
}

size_t GpuUploadQueue::PendingCount() const
{
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->tasks.size();
}

void GpuUploadQueue::Clear()
{
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->tasks.clear();
}
