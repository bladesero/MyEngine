#pragma once

#include "Core/EngineMath.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class Scene;

struct SceneSdfClipmapLevel {
    AABB bounds;
    float cellSize = 1.0f;
    float probeCellSize = 1.0f;
    uint32_t sdfOffset = 0;
    uint32_t voxelWordOffset = 0;
    uint32_t probeOffset = 0;
};

struct SceneSdfClipmapData {
    static constexpr uint32_t kLevelCount = 3;
    static constexpr uint32_t kSdfResolution = 64;
    static constexpr uint32_t kProbeResolution = 16;

    bool enabled = false;
    uint64_t sourceHash = 0;
    uint32_t contributorCount = 0;
    uint32_t rebuildCount = 0;
    std::array<SceneSdfClipmapLevel, kLevelCount> levels{};
    std::vector<float> sdf;
    std::vector<uint32_t> voxelWords;
    std::vector<std::array<float, 4>> metadata;
    std::vector<std::string> warnings;
};

class SceneSdfClipmapBuilder {
public:
    const SceneSdfClipmapData& Build(const Scene& scene);
    const SceneSdfClipmapData& GetData() const { return m_Data; }

private:
    SceneSdfClipmapData m_Data;
};
