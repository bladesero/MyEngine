#pragma once

#include <cstdint>

struct FrameStats {
    uint64_t frameNumber = 0;
    float fps = 0.0f;
    float frameMs = 0.0f;
    float updateMs = 0.0f;
    float renderMs = 0.0f;
    float smoothedFrameMs = 0.0f;
};
