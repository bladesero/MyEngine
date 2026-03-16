#include "Time.h"

void Time::Reset() {
    s_Last = Clock::now();
    s_DeltaSeconds = 0.0f;
    s_TotalSeconds = 0.0f;
    s_FrameCount = 0;
}

void Time::Tick() {
    const auto now = Clock::now();
    const std::chrono::duration<float> delta = now - s_Last;
    s_Last = now;
    s_DeltaSeconds = delta.count();
    s_TotalSeconds += s_DeltaSeconds;
    ++s_FrameCount;
}
