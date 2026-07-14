#pragma once

#include <cstdint>

// Logical allocation domains for budgeting / aggregation.
enum class AllocTag : uint32_t { Unknown = 0, General, Scene, Asset, Render, FrameScratch, Pool, Test, Count_ };

inline const char* AllocTagToString(AllocTag tag) {
    switch (tag) {
    case AllocTag::General:
        return "General";
    case AllocTag::Scene:
        return "Scene";
    case AllocTag::Asset:
        return "Asset";
    case AllocTag::Render:
        return "Render";
    case AllocTag::FrameScratch:
        return "FrameScratch";
    case AllocTag::Pool:
        return "Pool";
    case AllocTag::Test:
        return "Test";
    default:
        return "Unknown";
    }
}
