#pragma once

#include <cstdint>

struct RuntimeQualityDegradationState {
    uint32_t level=0;
    uint64_t transitions=0;
    uint64_t pressuredFrames=0;
    uint64_t healthyFrames=0;
};

class RuntimeQualityDegradation {
public:
    static RuntimeQualityDegradationState Get();
    static void SetLevel(uint32_t level);
    static void SetFrameCounters(uint64_t pressured,uint64_t healthy);
    static void Reset();
};
