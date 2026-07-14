#include "Editor/EditorImGuiVulkanBridge.h"

#if defined(MYENGINE_PLATFORM_WINDOWS) && defined(MYENGINE_ENABLE_IMGUI)

#include "Core/Logger.h"

#include <vulkan/vulkan.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <cstring>

namespace {
struct VulkanLoaderData {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
};

PFN_vkCmdBeginRenderingKHR g_BeginRendering = nullptr;
PFN_vkCmdEndRenderingKHR g_EndRendering = nullptr;
VkDevice g_Device = VK_NULL_HANDLE;
VkQueue g_Queue = VK_NULL_HANDLE;
VkFormat g_ColorAttachmentFormat = VK_FORMAT_UNDEFINED;
bool g_LoggedMissingPlatformRenderer = false;

PFN_vkVoidFunction LoadVulkanFunction(const char* functionName, void* userData) {
    const auto* loaderData = static_cast<const VulkanLoaderData*>(userData);
    if (!loaderData)
        return nullptr;

    auto load = [loaderData](const char* name) -> PFN_vkVoidFunction {
        if (loaderData->device) {
            if (PFN_vkVoidFunction fn = vkGetDeviceProcAddr(loaderData->device, name)) {
                return fn;
            }
        }
        if (loaderData->instance) {
            return vkGetInstanceProcAddr(loaderData->instance, name);
        }
        return nullptr;
    };

    if (PFN_vkVoidFunction fn = load(functionName)) {
        return fn;
    }

    if (std::strcmp(functionName, "vkCmdBeginRenderingKHR") == 0) {
        if (PFN_vkVoidFunction fn = load("vkCmdBeginRendering")) {
            return fn;
        }
    }
    if (std::strcmp(functionName, "vkCmdEndRenderingKHR") == 0) {
        if (PFN_vkVoidFunction fn = load("vkCmdEndRendering")) {
            return fn;
        }
    }
    return nullptr;
}

void CheckVkResult(VkResult result) {
    if (result == VK_SUCCESS)
        return;
    Logger::Error("[EditorImGuiVulkanBridge] ImGui Vulkan backend VkResult=", static_cast<int>(result));
}
} // namespace

bool EditorImGuiVulkan_Init(const ImGuiBackendHandles& handles) {
    const auto& vk = handles.vulkan;
    VulkanLoaderData loaderData{static_cast<VkInstance>(vk.instance), static_cast<VkDevice>(vk.device)};
    g_BeginRendering =
        reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(LoadVulkanFunction("vkCmdBeginRenderingKHR", &loaderData));
    g_EndRendering =
        reinterpret_cast<PFN_vkCmdEndRenderingKHR>(LoadVulkanFunction("vkCmdEndRenderingKHR", &loaderData));
    if (!g_BeginRendering || !g_EndRendering) {
        Logger::Error("[EditorImGuiVulkanBridge] Vulkan dynamic rendering functions are unavailable");
        return false;
    }
    if (!ImGui_ImplVulkan_LoadFunctions(LoadVulkanFunction, &loaderData)) {
        Logger::Error("[EditorImGuiVulkanBridge] Failed to load Vulkan functions for ImGui");
        return false;
    }
    g_Device = static_cast<VkDevice>(vk.device);
    g_Queue = static_cast<VkQueue>(vk.queue);
    g_ColorAttachmentFormat = static_cast<VkFormat>(vk.colorFormat);
    g_LoggedMissingPlatformRenderer = false;

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = static_cast<VkInstance>(vk.instance);
    initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(vk.physicalDevice);
    initInfo.Device = static_cast<VkDevice>(vk.device);
    initInfo.QueueFamily = vk.queueFamily;
    initInfo.Queue = static_cast<VkQueue>(vk.queue);
    initInfo.DescriptorPool = static_cast<VkDescriptorPool>(vk.descriptorPool);
    initInfo.MinImageCount = vk.minImageCount;
    initInfo.ImageCount = vk.imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.CheckVkResultFn = CheckVkResult;
    initInfo.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &g_ColorAttachmentFormat;
    Logger::Info("[EditorImGuiVulkanBridge] Init colorFormat=", static_cast<uint32_t>(g_ColorAttachmentFormat),
                 " imageCount=", vk.imageCount, " minImageCount=", vk.minImageCount);
    return ImGui_ImplVulkan_Init(&initInfo);
}

void EditorImGuiVulkan_Shutdown() {
    ImGui_ImplVulkan_Shutdown();
    g_BeginRendering = nullptr;
    g_EndRendering = nullptr;
    g_Device = VK_NULL_HANDLE;
    g_Queue = VK_NULL_HANDLE;
    g_ColorAttachmentFormat = VK_FORMAT_UNDEFINED;
    g_LoggedMissingPlatformRenderer = false;
}

void EditorImGuiVulkan_NewFrame() {
    ImGui_ImplVulkan_NewFrame();
}

void EditorImGuiVulkan_RenderDrawData(ImDrawData* drawData, const ImGuiBackendHandles& handles) {
    const auto& vk = handles.vulkan;
    auto commandBuffer = static_cast<VkCommandBuffer>(vk.commandBuffer);
    auto imageView = static_cast<VkImageView>(vk.imageView);
    if (!drawData || !commandBuffer || !imageView || vk.width == 0 || vk.height == 0) {
        return;
    }

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = imageView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {vk.width, vk.height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    if (!g_BeginRendering || !g_EndRendering) {
        Logger::Error("[EditorImGuiVulkanBridge] Vulkan dynamic rendering functions are not loaded");
        return;
    }
    vkCmdBeginRendering(commandBuffer, &rendering);
    ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
    vkCmdEndRendering(commandBuffer);
}

void EditorImGuiVulkan_RenderPlatformWindows() {
    ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    if (!platformIO.Renderer_RenderWindow || !platformIO.Renderer_SwapBuffers) {
        if (!g_LoggedMissingPlatformRenderer) {
            Logger::Error("[EditorImGuiVulkanBridge] Vulkan platform viewport renderer callbacks are missing");
            g_LoggedMissingPlatformRenderer = true;
        }
        return;
    }
    if (g_Queue)
        vkQueueWaitIdle(g_Queue);
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    if (g_Queue)
        vkQueueWaitIdle(g_Queue);
}

bool EditorImGuiVulkan_CreateFontsTexture() {
    return ImGui_ImplVulkan_CreateFontsTexture();
}

void EditorImGuiVulkan_DestroyFontsTexture() {
    ImGui_ImplVulkan_DestroyFontsTexture();
}

void* EditorImGuiVulkan_CreateTexture(const ImGuiNativeTextureInfo& info) {
    if (info.backend != RHIBackend::Vulkan || !info.imageView || !info.sampler)
        return nullptr;
    return reinterpret_cast<void*>(ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(info.sampler),
                                                               static_cast<VkImageView>(info.imageView),
                                                               static_cast<VkImageLayout>(info.imageLayout)));
}

#endif
