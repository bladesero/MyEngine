#pragma once

#include <algorithm>
#include <cstdint>

namespace ClusterLightListLayout {

inline constexpr uint32_t kTileSize = 32;
inline constexpr uint32_t kDepthSlices = 24;
inline constexpr uint32_t kMaxLightsPerCluster = 128;

constexpr uint32_t TileCount(uint32_t dimension) {
    return (dimension + kTileSize - 1u) / kTileSize;
}

constexpr uint64_t ClusterCount(uint32_t width, uint32_t height) {
    return static_cast<uint64_t>(TileCount(width)) * TileCount(height) * kDepthSlices;
}

constexpr uint64_t LightIndexCapacity(uint32_t width, uint32_t height) {
    return ClusterCount(width, height) * kMaxLightsPerCluster;
}

constexpr uint64_t BaseIndex(uint32_t clusterIndex) {
    return static_cast<uint64_t>(clusterIndex) * kMaxLightsPerCluster;
}

constexpr uint32_t StoredLightCount(uint32_t intersectingLightCount) {
    return (std::min)(intersectingLightCount, kMaxLightsPerCluster);
}

constexpr uint32_t OverflowLightCount(uint32_t intersectingLightCount) {
    return intersectingLightCount > kMaxLightsPerCluster ? intersectingLightCount - kMaxLightsPerCluster : 0u;
}

} // namespace ClusterLightListLayout
