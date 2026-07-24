#include "RuntimeModule.h"
#include "RuntimeComponentRegistration.h"

#include "Assets/ScriptAsset.h"
#include "Navigation/NavigationModule.h"
#include "Physics/PhysicsModule.h"
#include "Renderer/RenderBackendRegistry.h"
#include "Scripting/AngelScriptRuntime.h"
#include "UI/Core/UISystem.h"

#include <atomic>
#include <mutex>
#include <stdexcept>

#if defined(MYENGINE_PLATFORM_WINDOWS)
#include "Renderer/Backends/D3DCommon/D3DShaderCompilerBackend.h"
#include "Renderer/Backends/D3D11/D3D11Backend.h"
#include "Renderer/Backends/D3D12/D3D12Backend.h"
#if defined(MYENGINE_ENABLE_VULKAN)
#include "Renderer/Backends/Vulkan/VulkanBackend.h"
#endif
#elif defined(MYENGINE_PLATFORM_MACOS)
#include "Renderer/Backends/Metal/MetalBackend.h"
#endif

namespace {
std::once_flag g_RuntimeModuleInitialization;
std::atomic<uint32_t> g_RuntimeModuleInitializationCount{0};

void AttachUIScripting(UISystem& system, UIEventBridge& bridge) {
    AngelScriptRuntime::SetUIEventBridge(&bridge);
    AngelScriptRuntime::SetUISystem(&system);
}

void DetachUIScripting(UISystem& system, UIEventBridge& bridge) {
    AngelScriptRuntime::ClearUIEventBridge(&bridge);
    AngelScriptRuntime::ClearUISystem(&system);
}
} // namespace

void InitializeMyEngineRuntimeModules() {
    std::call_once(g_RuntimeModuleInitialization, [] {
        if (!AttachPhysicsSubsystem() || !AttachNavigationSubsystem())
            throw std::runtime_error("failed to attach Runtime scene subsystems");
        SetScriptAssetProcessor(&AngelScriptRuntime::PreprocessSource, &AngelScriptRuntime::DiscoverClasses);
        SetUIScriptingLifecycleCallbacks(&AttachUIScripting, &DetachUIScripting);
#if defined(MYENGINE_PLATFORM_WINDOWS)
        if (!RegisterD3DShaderCompilerBackend())
            throw std::runtime_error("failed to register the D3D shader compiler");
        if (!RegisterRenderBackend(RenderBackend::D3D11, &CreateD3D11Context) ||
            !RegisterRenderBackend(RenderBackend::D3D12, &CreateD3D12Context))
            throw std::runtime_error("failed to register Windows render backends");
#if defined(MYENGINE_ENABLE_VULKAN)
        if (!RegisterRenderBackend(RenderBackend::Vulkan, &CreateVulkanContext))
            throw std::runtime_error("failed to register Vulkan render backend");
#endif
#elif defined(MYENGINE_PLATFORM_MACOS)
        if (!RegisterRenderBackend(RenderBackend::Metal, &CreateMetalContext))
            throw std::runtime_error("failed to register Metal render backend");
#endif
        RegisterRuntimeComponentTypes();
        g_RuntimeModuleInitializationCount.fetch_add(1, std::memory_order_release);
    });
}

uint32_t GetMyEngineRuntimeInitializationCount() {
    return g_RuntimeModuleInitializationCount.load(std::memory_order_acquire);
}
