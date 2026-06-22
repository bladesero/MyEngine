#pragma once

#include "Camera/Camera.h"
#include "Math/Ray.h"

class SceneViewportController {
public:
    void Initialize(int width, int height);
    void OnUpdate(float dt);
    void OnWindowResize(int width, int height);

    void SetEditorViewportRect(int x, int y, int width, int height);
    void SetInputEnabled(bool enabled);
    void GetViewportRect(int& outX, int& outY, int& outW, int& outH) const;

    bool BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const;

    Camera& GetCamera() { return m_Camera; }
    const Camera& GetCamera() const { return m_Camera; }
    bool UsesEditorViewport() const { return m_UseEditorViewport; }

private:
    Camera m_Camera;
    int m_VpX = 0;
    int m_VpY = 0;
    int m_VpW = 0;
    int m_VpH = 0;
    bool m_RmbDown = false;
    bool m_UseEditorViewport = false;
    bool m_InputEnabled = true;
};
