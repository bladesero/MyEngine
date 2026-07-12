#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Scene;

enum class WorldPhase { WorldFrameBegin, PreUpdate, FixedPrePhysics, FixedPhysics, FixedPostPhysics, Update, LateUpdate, RenderExtract, WorldFrameEnd, Count };
inline constexpr size_t kWorldPhaseCount = static_cast<size_t>(WorldPhase::Count);
const char* WorldPhaseName(WorldPhase phase);
struct WorldTickContext { Scene& scene; float deltaSeconds; uint64_t frameIndex; uint64_t fixedTickIndex; };
struct WorldSystemDescriptor {
    std::string stableName;
    WorldPhase phase = WorldPhase::Update;
    int order = 0;
    std::vector<std::string> runsAfter;
    std::vector<std::string> runsBefore;
    bool runWhenPaused = false;
    std::function<void(WorldTickContext&)> execute;
};
struct WorldSchedulerStats {
    uint32_t fixedTicks = 0;
    uint32_t droppedFixedTicks = 0;
    std::array<float, kWorldPhaseCount> phaseMilliseconds{};
};

class WorldFrameScheduler {
public:
    explicit WorldFrameScheduler(bool registerBuiltins = true,
                                 bool freezeAfterRegistration = true);
    bool RegisterSystem(WorldSystemDescriptor descriptor, std::string* error = nullptr);
    bool Freeze(std::string* error = nullptr);
    void Tick(Scene& scene, float unscaledDeltaSeconds, bool forceStep = false);
    void Reset();
    void SetFixedDelta(float value) { if (value > 0.0f) m_FixedDelta = value; }
    float GetFixedDelta() const { return m_FixedDelta; }
    const WorldSchedulerStats& GetStats() const { return m_Stats; }

private:
    bool ExecutePhase(WorldPhase phase, Scene& scene, float delta, bool paused);
    std::vector<WorldSystemDescriptor> m_Systems;
    std::unordered_map<WorldPhase, std::vector<size_t>> m_Order;
    bool m_Frozen = false;
    bool m_Executing = false;
    float m_Accumulator = 0.0f;
    float m_FixedDelta = 1.0f / 60.0f;
    uint64_t m_FrameIndex = 0;
    uint64_t m_FixedTickIndex = 0;
    WorldSchedulerStats m_Stats;
};
