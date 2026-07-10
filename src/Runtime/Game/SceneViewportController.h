#pragma once

#include "Game/RenderViewport.h"

class IRHIDevice;
class IRHIFrameContext;
class IRHIReadbackService;

enum class SceneViewDirection {
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom
};

class SceneViewport : public RenderViewport {
public:
    SceneViewport(IRHIDevice* device,
                  IRHIFrameContext* frameContext,
                  IRHIReadbackService* readbackService);

    void Initialize(int width, int height) override;
    void OnUpdate(float dt) override;
    void OnWindowResize(int width, int height) override;
    void SetViewportRect(int x, int y, int width, int height);
    void SetInputEnabled(bool enabled);

    Camera& GetCamera() override { return m_Camera; }
    const Camera& GetCamera() const override { return m_Camera; }

    void FrameDirection(SceneViewDirection direction,
                        const Vec3& target,
                        float distance = 10.0f);
    void FrameTarget(const Vec3& target, float radius = 1.0f);
    void OrbitAroundFocus(const Vec3& target, float yawDegrees, float pitchDegrees);
    void ToggleProjectionMode();
    void SetProjectionMode(ProjectionMode mode);
    bool IsOrthographic() const;

private:
    void ApplyOrthographicForCurrentAspect();

    Camera m_Camera;
    bool m_RmbDown = false;
    float m_OrthographicWidth = 10.0f;
};

using SceneViewportController = SceneViewport;
