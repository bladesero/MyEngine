#include "TestHarness.h"
#include "Core/Application.h"
#include "Project/PublishTargets.h"
#include "Renderer/IRenderContext.h"
#include "Renderer/RenderBackendRegistry.h"
#include "Renderer/ShaderCooker.h"
#include "RuntimeModule/RuntimeModule.h"

namespace {

bool TestLinuxVulkanRuntimeRegistration() {
#if defined(MYENGINE_PLATFORM_LINUX) && defined(MYENGINE_ENABLE_VULKAN)
    InitializeMyEngineRuntimeModules();
    if (!Check(std::string(PublishTargets::kDefaultCacheTargetId).rfind("linux", 0) == 0,
               "Linux development cache must not use a Windows/macOS publish namespace"))
        return false;
    if (!Check(kDefaultRenderBackend == RenderBackend::Vulkan, "Linux must default to its Vulkan backend") ||
        !Check(IsBackendCompiled(RenderBackend::Vulkan), "Linux Vulkan factory was not registered"))
        return false;
    auto context = CreateRenderContext(RenderBackend::Vulkan);
    if (!Check(context != nullptr, "Linux Vulkan context factory failed"))
        return false;
    return Check(context->GetBackend() == RHIBackend::Vulkan, "Linux factory returned a different RHI backend") &&
           Check(context->QueryEditorImGuiInterop() != nullptr, "Linux Vulkan editor interop is unavailable");
#else
    return true;
#endif
}

bool TestLinuxShaderCookTargets() {
    const std::vector<ShaderBackend> expected{ShaderBackend::Vulkan};
    return Check(ShaderCooker::BackendsForTargetPlatform("linux-x64") == expected &&
                     ShaderCooker::BackendsForTargetPlatform("linux-arm64") == expected &&
                     ShaderCooker::BackendsForTargetPlatform("linux") == expected,
                 "Linux imports and validation must cook Vulkan rather than a foreign D3D/Metal backend");
}

MYENGINE_REGISTER_TEST("Renderer", "TestLinuxVulkanRuntimeRegistration", TestLinuxVulkanRuntimeRegistration);
MYENGINE_REGISTER_TEST("Renderer", "TestLinuxShaderCookTargets", TestLinuxShaderCookTargets);

} // namespace
