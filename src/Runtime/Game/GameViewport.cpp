#include "Game/GameViewport.h"

#include "Camera/CameraComponent.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

GameViewport::GameViewport(IRHIDevice* device, IRHIFrameContext* frameContext, IRHIReadbackService* readbackService)
    : RenderViewport(device, frameContext, readbackService) {
}

void GameViewport::Initialize(int width, int height) {
    RenderViewport::Initialize(width, height);
    UseFallbackCamera();
}

void GameViewport::ResolveFrameCamera(const Scene& scene) {
    m_MainCamera = FindMainCamera(scene);
    m_HasMainCamera = m_MainCamera != nullptr;
    if (!m_MainCamera) {
        UseFallbackCamera();
        return;
    }

    const Actor* owner = m_MainCamera->GetOwner();
    if (!owner) {
        UseFallbackCamera();
        return;
    }
    m_Camera = m_MainCamera->BuildCamera(*owner, GetAspect());
}

const CameraComponent* GameViewport::FindMainCamera(const Scene& scene) {
    if (const uint64_t hintActorID = scene.GetMainCameraHintActorID()) {
        if (const Actor* hintedActor = scene.FindByID(hintActorID); hintedActor && hintedActor->IsActive()) {
            const auto* camera = hintedActor->GetComponent<CameraComponent>();
            if (camera && camera->IsEnabled())
                return camera;
        }
    }

    const CameraComponent* result = nullptr;
    scene.ForEach([&](Actor& actor) {
        if (result || !actor.IsActive())
            return;
        const auto* camera = actor.GetComponent<CameraComponent>();
        if (!camera || !camera->IsEnabled() || !camera->IsMainCamera())
            return;
        result = camera;
    });
    return result;
}

void GameViewport::UseFallbackCamera() {
    m_Camera.SetCameraMode(CameraMode::Fly);
    m_Camera.LookAt({0.0f, 2.0f, -6.0f}, {0.0f, 0.0f, 0.0f});
    m_Camera.SetPerspective(60.0f, GetAspect(), 0.1f, 1000.0f);
}
