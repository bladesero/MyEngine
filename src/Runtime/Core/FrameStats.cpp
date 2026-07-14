#include "Core/FrameStats.h"

#include <mutex>

namespace {
std::mutex g_FrameStatsMutex;
RendererFrameStats g_RendererStats;
RuntimeResourceFrameStats g_ResourceStats;
} // namespace

RendererFrameStats FrameStatsProvider::GetRendererStats() {
    std::lock_guard<std::mutex> lock(g_FrameStatsMutex);
    return g_RendererStats;
}

void FrameStatsProvider::SetRendererStats(const RendererFrameStats& value) {
    std::lock_guard<std::mutex> lock(g_FrameStatsMutex);
    g_RendererStats = value;
}

RuntimeResourceFrameStats FrameStatsProvider::GetResourceStats() {
    std::lock_guard<std::mutex> lock(g_FrameStatsMutex);
    return g_ResourceStats;
}

void FrameStatsProvider::SetResourceStats(const RuntimeResourceFrameStats& value) {
    std::lock_guard<std::mutex> lock(g_FrameStatsMutex);
    g_ResourceStats = value;
}
