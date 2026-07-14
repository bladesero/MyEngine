#include "TaskService.h"

#include <algorithm>

TaskScope::~TaskScope() { CancelAndWait(); }

void TaskScope::Register(const std::shared_ptr<TaskStateBase>& state) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Tasks.push_back(state);
}

void TaskScope::CancelAll() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    for (const auto& task : m_Tasks) task->Cancel();
}

void TaskScope::CancelAndWait() {
    std::vector<std::shared_ptr<TaskStateBase>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        tasks.swap(m_Tasks);
    }
    for (const auto& task : tasks) task->Cancel();
    for (const auto& task : tasks) task->Wait();
}

void TaskScope::ReapCompleted() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Tasks.erase(std::remove_if(m_Tasks.begin(), m_Tasks.end(),
        [](const auto& task) { return task->IsReady(); }), m_Tasks.end());
}

size_t TaskScope::TaskCount() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Tasks.size();
}

TaskService::TaskService(size_t workerCount) {
    if (workerCount == 0) {
        const unsigned hardware = std::thread::hardware_concurrency();
        workerCount = std::min<size_t>(8,
            std::max<size_t>(1, hardware > 1 ? hardware - 1 : 1));
    }
    m_Stats.workers = workerCount;
    m_Workers.reserve(workerCount);
    for (size_t index = 0; index < workerCount; ++index)
        m_Workers.emplace_back([this] { WorkerLoop(); });
}

TaskService::~TaskService() { Shutdown(); }

TaskService& TaskService::Get() {
    static TaskService service;
    return service;
}

void TaskService::Enqueue(TaskPriority priority, WorkItem work,
                          const std::shared_ptr<TaskStateBase>& state) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Stopping) throw std::runtime_error("TaskService is shutting down");
        work.sequence = m_NextSequence++;
        m_Queues[static_cast<size_t>(priority)].push_back(std::move(work));
        m_AllTasks.push_back(state);
        ++m_Stats.submitted;
        ++m_Stats.queued;
    }
    m_WorkAvailable.notify_one();
}

void TaskService::WorkerLoop() {
    for (;;) {
        WorkItem work;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_WorkAvailable.wait(lock, [this] {
                if (m_Stopping) return true;
                for (const auto& queue : m_Queues) if (!queue.empty()) return true;
                return false;
            });
            size_t queueIndex = m_Queues.size();
            for (size_t index = 0; index < m_Queues.size(); ++index) {
                if (!m_Queues[index].empty()) { queueIndex = index; break; }
            }
            if (queueIndex == m_Queues.size()) {
                if (m_Stopping) return;
                continue;
            }
            work = std::move(m_Queues[queueIndex].front());
            m_Queues[queueIndex].pop_front();
            --m_Stats.queued;
        }
        work.execute();
    }
}

void TaskService::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Stopping && m_Workers.empty()) return;
        m_Stopping = true;
        for (auto& weak : m_AllTasks) if (auto task = weak.lock()) task->Cancel();
    }
    m_WorkAvailable.notify_all();
    for (std::thread& worker : m_Workers) if (worker.joinable()) worker.join();
    m_Workers.clear();
}

TaskServiceStats TaskService::GetStats() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    TaskServiceStats stats = m_Stats;
    stats.workers = m_Workers.size();
    return stats;
}
