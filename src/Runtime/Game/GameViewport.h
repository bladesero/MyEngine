#pragma once

#include "Game/RenderViewport.h"

class CameraComponent;
class IRHIDevice;
class IRHIFrameContext;
class IRHIReadbackService;
class Scene;

class GameViewport final : public RenderViewport {
public:
    GameViewport(IRHIDevice* device,
                 IRHIFrameContext* frameContext,
                 IRHIReadbackService* readbackService);

    void Initialize(int width, int height) override;
    void ResolveFrameCamera(const Scene& scene) override;

    Camera& GetCamera() override { return m_Camera; }
    const Camera& GetCamera() const override { return m_Camera; }

    bool HasMainCamera() const { return m_HasMainCamera; }
    const CameraComponent* GetMainCameraComponent() const { return m_MainCamera; }

    static const CameraComponent* FindMainCamera(const Scene& scene);

private:
    void UseFallbackCamera();

    Camera m_Camera;
    const CameraComponent* m_MainCamera = nullptr;
    bool m_HasMainCamera = false;
};
