#pragma once

#include "Core/EngineMath.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class MeshAsset;

struct MeshSdfVoxelData {
    static constexpr uint32_t kMediumResolution = 64;

    uint32_t resolution = kMediumResolution;
    AABB bounds;
    float cellSize = 0.0f;
    float sdfScale = 1.0f;
    std::vector<int16_t> sdf;
    std::vector<uint8_t> voxels;

    bool Valid() const;
    bool IsVoxelOccupied(uint32_t x, uint32_t y, uint32_t z) const;
};

struct MeshSdfVoxelBakeResult {
    bool succeeded = false;
    MeshSdfVoxelData data;
    std::vector<std::string> warnings;
    std::string error;
};

class MeshSdfVoxelBaker {
public:
    static MeshSdfVoxelBakeResult BakeMedium(const MeshAsset& mesh);
};

class MeshSdfVoxelXml {
public:
    static bool Save(const std::filesystem::path& path,
                     const MeshSdfVoxelData& data,
                     std::string* error = nullptr);
    static bool Load(const std::filesystem::path& path,
                     MeshSdfVoxelData& data,
                     std::string* error = nullptr);
};
