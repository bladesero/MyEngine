#pragma once

class RuntimeAccessibility {
public:
    static void SetReduceCameraShake(bool enabled);
    static bool GetReduceCameraShake();
    static float GetCameraShakeScale();
};
