#pragma once

#include "Core/EngineMath.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Scene;

enum class WorldZoneTriggerMode {
    Distance,
    Portal,
    DistanceOrPortal,
    DistanceAndPortal,
};

enum class WorldZoneStreamState {
    Unloaded,
    Loading,
    Instantiating,
    Loaded,
    Failed,
};

struct WorldZoneStreamDescriptor {
    std::string stableName;
    std::string sourcePath;
    Vec3 boundsCenter = Vec3::Zero();
    Vec3 boundsHalfExtents = Vec3::Zero();
    float loadDistance = 50.0f;
    float unloadDistance = 65.0f;
    WorldZoneTriggerMode triggerMode = WorldZoneTriggerMode::Distance;
    bool portalInitiallyOpen = false;
    int priority = 0;
};

struct WorldZoneStreamEntryStats {
    std::string stableName;
    WorldZoneStreamState state = WorldZoneStreamState::Unloaded;
    bool desired = false;
    uint64_t lifetimeGeneration = 0;
    size_t instantiatedActors = 0;
    std::string lastError;
};

struct WorldZoneStreamerStats {
    size_t registeredZones = 0;
    size_t loadedZones = 0;
    size_t loadingZones = 0;
    size_t failedZones = 0;
    size_t instantiatedActorsThisFrame = 0;
    uint64_t actorBudgetBlockedFrames = 0;
};

class WorldZoneStreamer {
public:
    WorldZoneStreamer();
    ~WorldZoneStreamer();
    WorldZoneStreamer(const WorldZoneStreamer&) = delete;
    WorldZoneStreamer& operator=(const WorldZoneStreamer&) = delete;

    bool Register(WorldZoneStreamDescriptor descriptor, std::string* error = nullptr);
    bool Unregister(Scene& scene, const std::string& stableName);
    bool SetPortalOpen(const std::string& stableName, bool open);
    bool Retry(const std::string& stableName);
    void SetObserverPosition(const Vec3& position);
    const Vec3& GetObserverPosition() const;
    void SetBudgets(size_t maxActorsPerFrame, size_t maxTransitionsPerFrame);
    void Tick(Scene& scene, float deltaSeconds);
    void Reset(Scene& scene);

    WorldZoneStreamerStats GetStats() const;
    std::vector<WorldZoneStreamEntryStats> GetEntryStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
