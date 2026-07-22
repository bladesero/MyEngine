#pragma once

#include "DebugDraw/DebugDrawCommand.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct DebugDrawServiceStats {
    uint64_t submitted = 0;
    uint64_t dropped = 0;
    size_t pending = 0;
    size_t active = 0;
};

class DebugDrawService {
public:
    using Snapshot = std::shared_ptr<const std::vector<DebugDrawCommand>>;

    static constexpr size_t kMaxPendingCommands = 65536;
    static constexpr size_t kMaxActiveCommands = 65536;

    static DebugDrawService& Get();

    DebugDrawService(const DebugDrawService&) = delete;
    DebugDrawService& operator=(const DebugDrawService&) = delete;

    bool Submit(DebugDrawCommand command);
    Snapshot SnapshotForScene(const Scene& scene, uint64_t frameNumber, float totalSeconds);
    void Clear();
    void ClearScene(const Scene& scene);
    DebugDrawServiceStats GetStats() const;

private:
    DebugDrawService();
    ~DebugDrawService();

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
