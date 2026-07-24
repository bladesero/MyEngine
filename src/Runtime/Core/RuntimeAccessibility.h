#pragma once

#include "API/RuntimeApi.h"

class MYENGINE_RUNTIME_API RuntimeAccessibility {
public:
    static void SetReduceCameraShake(bool enabled);
    static bool GetReduceCameraShake();
    static float GetCameraShakeScale();
};
