#include "Editor/EditorImGuiVulkanBridge.h"

#if defined(MYENGINE_PLATFORM_WINDOWS) && defined(MYENGINE_ENABLE_IMGUI)

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

bool EditorImGuiVulkan_Init(const ImGuiBackendHandles& handles)
{
    const auto& vk = handles.vulkan;
    VkFormat colorFormat = static_cast<VkFormat>(vk.colorFormat);
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
    initInfo.PipelineRenderingCreateInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    return ImGui_ImplVulkan_Init(&initInfo);
}

void EditorImGuiVulkan_Shutdown()
{
    ImGui_ImplVulkan_Shutdown();
}

void EditorImGuiVulkan_NewFrame()
{
    ImGui_ImplVulkan_NewFrame();
}

void EditorImGuiVulkan_RenderDrawData(ImDrawData* drawData, const ImGuiBackendHandles& handles)
{
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
    vkCmdBeginRendering(commandBuffer, &rendering);
    ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
    vkCmdEndRendering(commandBuffer);
}

bool EditorImGuiVulkan_CreateFontsTexture()
{
    return ImGui_ImplVulkan_CreateFontsTexture();
}

void EditorImGuiVulkan_DestroyFontsTexture()
{
    ImGui_ImplVulkan_DestroyFontsTexture();
}

void* EditorImGuiVulkan_CreateTexture(const ImGuiNativeTextureInfo& info)
{
    if (info.backend != RHIBackend::Vulkan || !info.imageView || !info.sampler) return nullptr;
    return reinterpret_cast<void*>(ImGui_ImplVulkan_AddTexture(
        static_cast<VkSampler>(info.sampler),
        static_cast<VkImageView>(info.imageView),
        static_cast<VkImageLayout>(info.imageLayout)));
}

#endif
