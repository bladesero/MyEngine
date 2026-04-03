#pragma once

// Named EngineTime.h (not Time.h) so MSVC on Windows does not pick this file
// when the standard library includes <time.h> (case-insensitive match).

#include <chrono>

class Time {
public:
    using Clock = std::chrono::steady_clock;

    static void Reset();
    static void Tick();

    static float DeltaSeconds() { return s_DeltaSeconds; }
    static float TotalSeconds() { return s_TotalSeconds; }
    static unsigned long long FrameCount() { return s_FrameCount; }

private:
    inline static Clock::time_point s_Last{};
    inline static float s_DeltaSeconds = 0.0f;
    inline static float s_TotalSeconds = 0.0f;
    inline static unsigned long long s_FrameCount = 0;
};
