#include "Renderer/GpuUploadQueue.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>

struct GpuUploadQueue::Impl {
    std::mutex mutex;
    struct Pending { UploadTask task; uint64_t bytes = 0; };
    std::deque<Pending> tasks;
    GpuUploadBudget defaultBudget;
};

GpuUploadQueue& GpuUploadQueue::Get()
{
    static GpuUploadQueue queue;
    static Impl implementation;
    queue.m_Impl = &implementation;
    return queue;
}

void GpuUploadQueue::Enqueue(UploadTask task, uint64_t estimatedBytes)
{
    if (!task) return;
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->tasks.push_back({std::move(task), estimatedBytes});
}

size_t GpuUploadQueue::Process(IRHIDevice& device, size_t maxTasks)
{
    GpuUploadBudget budget; budget.maxTasks=maxTasks;
    budget.maxBytes=static_cast<uint64_t>(-1); budget.maxMilliseconds=0.0;
    return Process(device,budget);
}

size_t GpuUploadQueue::Process(IRHIDevice& device, const GpuUploadBudget& budget)
{
    Impl* impl = Get().m_Impl;
    size_t processed=0; uint64_t bytes=0;
    const auto start=std::chrono::steady_clock::now();
    while(processed<budget.maxTasks){
        Impl::Pending pending;
        {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(impl->tasks.empty())break;
        if(processed>0&&bytes+impl->tasks.front().bytes>budget.maxBytes)break;
        pending=std::move(impl->tasks.front());impl->tasks.pop_front();
        }
        pending.task(device);bytes+=pending.bytes;++processed;
        if(budget.maxMilliseconds>0.0&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()>=budget.maxMilliseconds)break;
    }
    return processed;
}

size_t GpuUploadQueue::PendingCount() const
{
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->tasks.size();
}

void GpuUploadQueue::SetDefaultBudget(GpuUploadBudget budget)
{
    Impl* impl=Get().m_Impl;std::lock_guard<std::mutex> lock(impl->mutex);impl->defaultBudget=budget;
}

GpuUploadBudget GpuUploadQueue::GetDefaultBudget() const
{
    Impl* impl=Get().m_Impl;std::lock_guard<std::mutex> lock(impl->mutex);return impl->defaultBudget;
}

void GpuUploadQueue::Clear()
{
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->tasks.clear();
}
