#include "Animation/AnimatorController.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioEngine.h"
#include "Camera/Camera.h"
#include "Core/Sha256.h"
#include "DebugDraw/DebugDrawService.h"
#include "Game/RuntimeResourceBudget.h"
#include "Gameplay/GameplayComponents.h"
#include "Input/Input.h"
#include "Miscs/IconsManager.h"
#include "Math/Mat4Inverse.h"
#include "Navigation/NavigationWorld.h"
#include "Physics/RigidBodyComponent.h"
#include "Project/RuntimePerformanceProfile.h"
#include "Renderer/RHI/RHIResourceStats.h"
#include "Renderer/RenderBackendRegistry.h"
#include "RuntimeModule/RuntimeModule.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Scene.h"
#include "Scripting/AngelScriptRuntime.h"
#include "UI/Core/RuntimeUIScreenConfig.h"

#include <cstdint>

namespace {

template <typename T> void Touch(T value) {
    volatile T sink = value;
    (void)sink;
}

} // namespace

int main() {
    Touch(&Mat4Invert);
    Touch(&Sha256::HashFile);
    Touch(&RuntimePerformanceProfile::FromText);
    Touch(&Input::SetDefaultActionMap);
    Touch(&AssetManager::Get);
    Touch(&RHIResourceStatsProvider::GetStats);
    Touch(&RigidBodyComponent::Serialize);
    Touch(&NavigationWorld::Bake);
    Touch(&AudioBusName);
    Touch(&Camera::SetPerspective);
    Touch(&AnimatorController::Serialize);
    Touch(&RuntimeUIScreenConfig::FromText);
    Touch(&DebugDrawService::Get);
    Touch(&ParseRenderBackend);
    Touch(&HealthComponent::Serialize);
    Touch(&RuntimeResourceBudgetController::Configure);
    Touch(&AngelScriptRuntime::SetUIEventBridge);
    Touch(&IconsManager::Get);
    Touch(&CreateRenderContext);

    InitializeMyEngineRuntimeModules();
    InitializeMyEngineRuntimeModules();
    if (GetMyEngineRuntimeInitializationCount() != 1)
        return 1;
    if (!HasScenePhysicsSubsystemFactory() || !HasSceneNavigationSubsystemFactory())
        return 2;
    if (!ComponentRegistry::Get().IsRegistered("MeshRenderer"))
        return 3;
    Scene scene;
    (void)scene.GetPhysicsWorld();
    (void)scene.GetNavigationWorld();
#ifdef MYENGINE_PLATFORM_WINDOWS
    if (!IsBackendCompiled(RenderBackend::D3D11) || !IsBackendCompiled(RenderBackend::D3D12))
        return 4;
#elif defined(MYENGINE_PLATFORM_MACOS)
    if (!IsBackendCompiled(RenderBackend::Metal))
        return 4;
#endif
#ifdef MYENGINE_ENABLE_VULKAN
    if (!IsBackendCompiled(RenderBackend::Vulkan))
        return 5;
#endif
    return 0;
}
