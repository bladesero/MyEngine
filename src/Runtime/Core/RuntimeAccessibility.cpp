#include "Core/RuntimeAccessibility.h"

#include <atomic>

namespace { std::atomic<bool> g_ReduceCameraShake{false}; }

void RuntimeAccessibility::SetReduceCameraShake(bool enabled)
{
    g_ReduceCameraShake.store(enabled,std::memory_order_relaxed);
}

bool RuntimeAccessibility::GetReduceCameraShake()
{
    return g_ReduceCameraShake.load(std::memory_order_relaxed);
}

float RuntimeAccessibility::GetCameraShakeScale()
{
    return GetReduceCameraShake()?0.25f:1.0f;
}
