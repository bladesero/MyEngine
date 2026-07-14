#include "Core/RuntimeQualityDegradation.h"

#include <algorithm>
#include <mutex>

namespace {
std::mutex g_Mutex;
RuntimeQualityDegradationState g_State;
} // namespace
RuntimeQualityDegradationState RuntimeQualityDegradation::Get() {
    std::lock_guard<std::mutex> lock(g_Mutex);
    return g_State;
}
void RuntimeQualityDegradation::SetLevel(uint32_t level) {
    std::lock_guard<std::mutex> lock(g_Mutex);
    level = std::min(level, 2u);
    if (level != g_State.level) {
        g_State.level = level;
        ++g_State.transitions;
    }
}
void RuntimeQualityDegradation::SetFrameCounters(uint64_t pressured, uint64_t healthy) {
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_State.pressuredFrames = pressured;
    g_State.healthyFrames = healthy;
}
void RuntimeQualityDegradation::Reset() {
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_State = {};
}
