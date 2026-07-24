#pragma once

#include "API/RuntimeApi.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

enum class TaskPriority : uint8_t { High = 0, Normal = 1, Low = 2, Count };

class TaskCancelled final : public std::exception {
public:
    const char* what() const noexcept override { return "task cancelled"; }
};

class CancellationToken {
public:
    bool IsCancellationRequested() const { return m_Cancelled && m_Cancelled->load(std::memory_order_acquire); }
    void ThrowIfCancellationRequested() const {
        if (IsCancellationRequested())
            throw TaskCancelled{};
    }

private:
    friend class TaskService;
    friend class TaskScope;
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> cancelled) : m_Cancelled(std::move(cancelled)) {}
    std::shared_ptr<std::atomic<bool>> m_Cancelled;
};

struct TaskDescriptor {
    std::string stableName;
    TaskPriority priority = TaskPriority::Normal;
};

class TaskStateBase {
public:
    virtual ~TaskStateBase() = default;
    virtual void Cancel() = 0;
    virtual void Wait() const = 0;
    virtual bool IsReady() const = 0;
    std::string stableName;
};

template <typename T> class TaskState final : public TaskStateBase {
public:
    TaskState() : future(promise.get_future().share()) {}
    void Cancel() override { cancelled->store(true, std::memory_order_release); }
    void Wait() const override { future.wait(); }
    bool IsReady() const override { return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

    std::promise<T> promise;
    std::shared_future<T> future;
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
};

template <typename T> class TaskHandle {
public:
    TaskHandle() = default;
    bool Valid() const { return static_cast<bool>(m_State); }
    bool IsReady() const { return m_State && m_State->IsReady(); }
    void Cancel() const {
        if (m_State)
            m_State->Cancel();
    }
    void Wait() const {
        if (m_State)
            m_State->Wait();
    }
    T Get() const { return m_State->future.get(); }
    const std::string& GetStableName() const {
        static const std::string empty;
        return m_State ? m_State->stableName : empty;
    }

private:
    friend class TaskService;
    explicit TaskHandle(std::shared_ptr<TaskState<T>> state) : m_State(std::move(state)) {}
    std::shared_ptr<TaskState<T>> m_State;
};

template <> inline void TaskHandle<void>::Get() const {
    m_State->future.get();
}

class MYENGINE_RUNTIME_API TaskScope {
public:
    TaskScope() = default;
    ~TaskScope();
    TaskScope(const TaskScope&) = delete;
    TaskScope& operator=(const TaskScope&) = delete;

    void CancelAll();
    void CancelAndWait();
    void ReapCompleted();
    size_t TaskCount() const;

private:
    friend class TaskService;
    void Register(const std::shared_ptr<TaskStateBase>& state);
    mutable std::mutex m_Mutex;
    std::vector<std::shared_ptr<TaskStateBase>> m_Tasks;
};

struct TaskServiceStats {
    uint64_t submitted = 0;
    uint64_t completed = 0;
    uint64_t cancelled = 0;
    uint64_t failed = 0;
    size_t queued = 0;
    size_t workers = 0;
};

class MYENGINE_RUNTIME_API TaskService {
public:
    explicit TaskService(size_t workerCount = 0);
    ~TaskService();
    TaskService(const TaskService&) = delete;
    TaskService& operator=(const TaskService&) = delete;

    static TaskService& Get();

    template <typename Function>
    auto Submit(TaskScope& scope, TaskDescriptor descriptor, Function&& function)
        -> TaskHandle<std::invoke_result_t<Function, CancellationToken>>;

    void Shutdown();
    TaskServiceStats GetStats() const;

private:
    struct WorkItem {
        uint64_t sequence = 0;
        std::function<void()> execute;
    };
    void Enqueue(TaskPriority priority, WorkItem work, const std::shared_ptr<TaskStateBase>& state);
    void WorkerLoop();

    mutable std::mutex m_Mutex;
    std::condition_variable m_WorkAvailable;
    std::array<std::deque<WorkItem>, static_cast<size_t>(TaskPriority::Count)> m_Queues;
    std::vector<std::thread> m_Workers;
    std::vector<std::weak_ptr<TaskStateBase>> m_AllTasks;
    bool m_Stopping = false;
    uint64_t m_NextSequence = 1;
    TaskServiceStats m_Stats;
};

template <typename Function>
auto TaskService::Submit(TaskScope& scope, TaskDescriptor descriptor, Function&& function)
    -> TaskHandle<std::invoke_result_t<Function, CancellationToken>> {
    using Result = std::invoke_result_t<Function, CancellationToken>;
    auto state = std::make_shared<TaskState<Result>>();
    state->stableName = descriptor.stableName;
    scope.Register(state);
    auto work = [state, function = std::forward<Function>(function), this]() mutable {
        try {
            CancellationToken token(state->cancelled);
            token.ThrowIfCancellationRequested();
            if constexpr (std::is_void_v<Result>) {
                function(token);
                state->promise.set_value();
            } else {
                state->promise.set_value(function(token));
            }
            std::lock_guard<std::mutex> lock(m_Mutex);
            ++m_Stats.completed;
        } catch (const TaskCancelled&) {
            state->promise.set_exception(std::current_exception());
            std::lock_guard<std::mutex> lock(m_Mutex);
            ++m_Stats.cancelled;
        } catch (...) {
            state->promise.set_exception(std::current_exception());
            std::lock_guard<std::mutex> lock(m_Mutex);
            ++m_Stats.failed;
        }
    };
    try {
        Enqueue(descriptor.priority, {0, std::move(work)}, state);
    } catch (...) {
        state->promise.set_exception(std::current_exception());
    }
    return TaskHandle<Result>(std::move(state));
}
