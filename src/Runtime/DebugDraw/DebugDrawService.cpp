#include "DebugDraw/DebugDrawService.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <mutex>
#include <unordered_map>

struct DebugDrawService::Impl {
    struct PendingCommand {
        DebugDrawCommand command;
    };

    struct ActiveCommand {
        DebugDrawCommand command;
        uint64_t activatedFrame = 0;
        float expiresAt = 0.0f;
    };

    mutable std::mutex mutex;
    std::deque<PendingCommand> pending;
    std::vector<ActiveCommand> active;
    std::vector<DebugDrawCommand> sealed;
    std::unordered_map<uint64_t, Snapshot> sceneSnapshots;
    std::atomic<uint64_t> nextSequence{1};
    uint64_t sealedFrame = std::numeric_limits<uint64_t>::max();
    DebugDrawServiceStats stats;

    void SealFrame(uint64_t frameNumber, float totalSeconds) {
        if (sealedFrame == frameNumber)
            return;

        active.erase(std::remove_if(active.begin(), active.end(),
                                    [frameNumber, totalSeconds](const ActiveCommand& item) {
                                        if (!item.command.scene.IsAlive())
                                            return true;
                                        if (item.command.durationSeconds <= 0.0f)
                                            return item.activatedFrame < frameNumber;
                                        return totalSeconds >= item.expiresAt;
                                    }),
                     active.end());

        while (!pending.empty()) {
            PendingCommand item = std::move(pending.front());
            pending.pop_front();
            if (!item.command.scene.IsAlive())
                continue;
            if (active.size() >= kMaxActiveCommands) {
                ++stats.dropped;
                continue;
            }
            ActiveCommand activated;
            activated.command = std::move(item.command);
            activated.activatedFrame = frameNumber;
            activated.expiresAt = totalSeconds + (std::max)(0.0f, activated.command.durationSeconds);
            active.push_back(std::move(activated));
        }

        std::sort(active.begin(), active.end(), [](const ActiveCommand& lhs, const ActiveCommand& rhs) {
            return lhs.command.sequence < rhs.command.sequence;
        });
        sealed.clear();
        sealed.reserve(active.size());
        for (const ActiveCommand& item : active)
            sealed.push_back(item.command);
        sceneSnapshots.clear();
        sealedFrame = frameNumber;
        stats.pending = pending.size();
        stats.active = active.size();
    }
};

DebugDrawService::DebugDrawService() : m_Impl(std::make_unique<Impl>()) {
}
DebugDrawService::~DebugDrawService() = default;

DebugDrawService& DebugDrawService::Get() {
    static DebugDrawService service;
    return service;
}

bool DebugDrawService::Submit(DebugDrawCommand command) {
    if (!command.scene.IsAlive() || command.sceneGeneration == 0 ||
        command.scene.GetGeneration() != command.sceneGeneration) {
        return false;
    }
    command.durationSeconds = (std::max)(0.0f, command.durationSeconds);
    command.sequence = m_Impl->nextSequence.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    if (m_Impl->pending.size() >= kMaxPendingCommands) {
        ++m_Impl->stats.dropped;
        return false;
    }
    m_Impl->pending.push_back({std::move(command)});
    ++m_Impl->stats.submitted;
    m_Impl->stats.pending = m_Impl->pending.size();
    return true;
}

DebugDrawService::Snapshot DebugDrawService::SnapshotForScene(const Scene& scene, uint64_t frameNumber,
                                                              float totalSeconds) {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->SealFrame(frameNumber, totalSeconds);
    const uint64_t generation = scene.GetLifetimeGeneration();
    if (const auto found = m_Impl->sceneSnapshots.find(generation); found != m_Impl->sceneSnapshots.end())
        return found->second;

    auto commands = std::make_shared<std::vector<DebugDrawCommand>>();
    for (const DebugDrawCommand& command : m_Impl->sealed) {
        if (command.sceneGeneration == generation)
            commands->push_back(command);
    }
    Snapshot snapshot = commands;
    m_Impl->sceneSnapshots.emplace(generation, snapshot);
    return snapshot;
}

void DebugDrawService::Clear() {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->pending.clear();
    m_Impl->active.clear();
    m_Impl->sealed.clear();
    m_Impl->sceneSnapshots.clear();
    m_Impl->sealedFrame = std::numeric_limits<uint64_t>::max();
    m_Impl->stats.pending = 0;
    m_Impl->stats.active = 0;
}

void DebugDrawService::ClearScene(const Scene& scene) {
    const uint64_t generation = scene.GetLifetimeGeneration();
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    m_Impl->pending.erase(
        std::remove_if(m_Impl->pending.begin(), m_Impl->pending.end(),
                       [generation](const auto& item) { return item.command.sceneGeneration == generation; }),
        m_Impl->pending.end());
    m_Impl->active.erase(
        std::remove_if(m_Impl->active.begin(), m_Impl->active.end(),
                       [generation](const auto& item) { return item.command.sceneGeneration == generation; }),
        m_Impl->active.end());
    m_Impl->sealed.erase(std::remove_if(m_Impl->sealed.begin(), m_Impl->sealed.end(),
                                        [generation](const auto& item) { return item.sceneGeneration == generation; }),
                         m_Impl->sealed.end());
    m_Impl->sceneSnapshots.erase(generation);
    m_Impl->stats.pending = m_Impl->pending.size();
    m_Impl->stats.active = m_Impl->active.size();
}

DebugDrawServiceStats DebugDrawService::GetStats() const {
    std::lock_guard<std::mutex> lock(m_Impl->mutex);
    DebugDrawServiceStats stats = m_Impl->stats;
    stats.pending = m_Impl->pending.size();
    stats.active = m_Impl->active.size();
    return stats;
}
