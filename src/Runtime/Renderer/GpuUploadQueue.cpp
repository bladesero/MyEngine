#include "Renderer/GpuUploadQueue.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>

struct GpuUploadQueue::Impl {
    std::mutex mutex;
    struct Pending { UploadTask task; uint64_t bytes = 0; GpuUploadFence sequence = 0; };
    std::deque<Pending> tasks;
    GpuUploadBudget defaultBudget;
    GpuUploadFence lastEnqueued = 0;
    GpuUploadFence completed = 0;
    GpuUploadQueueStats stats;
};

GpuUploadQueue& GpuUploadQueue::Get()
{
    static GpuUploadQueue queue;
    static Impl implementation;
    queue.m_Impl = &implementation;
    return queue;
}

GpuUploadFence GpuUploadQueue::Enqueue(UploadTask task, uint64_t estimatedBytes)
{
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!task) return impl->lastEnqueued;
    const GpuUploadFence sequence = ++impl->lastEnqueued;
    impl->tasks.push_back({std::move(task), estimatedBytes, sequence});
    ++impl->stats.enqueuedTasks;impl->stats.enqueuedBytes+=estimatedBytes;
    impl->stats.pendingTasks=impl->tasks.size();impl->stats.pendingBytes+=estimatedBytes;
    impl->stats.peakPendingTasks=std::max(impl->stats.peakPendingTasks,impl->stats.pendingTasks);
    impl->stats.peakPendingBytes=std::max(impl->stats.peakPendingBytes,impl->stats.pendingBytes);
    return sequence;
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
        if(processed>0&&bytes+impl->tasks.front().bytes>budget.maxBytes){++impl->stats.byteBudgetDeferrals;break;}
        pending=std::move(impl->tasks.front());impl->tasks.pop_front();
        impl->stats.pendingTasks=impl->tasks.size();
        impl->stats.pendingBytes=impl->stats.pendingBytes>=pending.bytes?
            impl->stats.pendingBytes-pending.bytes:0;
        }
        try {
            pending.task(device);
        } catch (...) {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->completed = std::max(impl->completed, pending.sequence);
            ++impl->stats.failedTasks;
            throw;
        }
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->completed = std::max(impl->completed, pending.sequence);
            ++impl->stats.processedTasks;impl->stats.processedBytes+=pending.bytes;
        }
        bytes+=pending.bytes;++processed;
        if(budget.maxMilliseconds>0.0&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()>=budget.maxMilliseconds){
            std::lock_guard<std::mutex> lock(impl->mutex);
            if(!impl->tasks.empty())++impl->stats.timeBudgetDeferrals;
            break;
        }
    }
    {std::lock_guard<std::mutex> lock(impl->mutex);
     if(processed>=budget.maxTasks&&!impl->tasks.empty())++impl->stats.taskBudgetDeferrals;}
    return processed;
}

size_t GpuUploadQueue::PendingCount() const
{
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->tasks.size();
}

GpuUploadQueueStats GpuUploadQueue::GetStats() const
{
    Impl* impl=Get().m_Impl;std::lock_guard<std::mutex> lock(impl->mutex);return impl->stats;
}

GpuUploadFence GpuUploadQueue::CaptureFence() const
{
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->lastEnqueued;
}

bool GpuUploadQueue::IsFenceComplete(GpuUploadFence fence) const
{
    Impl* impl = Get().m_Impl;
    std::lock_guard<std::mutex> lock(impl->mutex);
    return fence == 0 || impl->completed >= fence;
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
    impl->stats.pendingTasks=0;impl->stats.pendingBytes=0;
    impl->completed = impl->lastEnqueued;
}

void GpuUploadQueue::ResetStatistics()
{
    Impl* impl=Get().m_Impl;std::lock_guard<std::mutex> lock(impl->mutex);
    const size_t tasks=impl->tasks.size();uint64_t bytes=0;
    for(const auto& task:impl->tasks)bytes+=task.bytes;
    impl->stats={};impl->stats.pendingTasks=tasks;impl->stats.pendingBytes=bytes;
    impl->stats.peakPendingTasks=tasks;impl->stats.peakPendingBytes=bytes;
}
