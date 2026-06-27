#pragma once

#include "Renderer/RHI/RHITypes.h"

#include <cstdint>

struct ImGuiVulkanFrameInfo {
    void* instance = nullptr;
    void* physicalDevice = nullptr;
    void* device = nullptr;
    void* queue = nullptr;
    void* descriptorPool = nullptr;
    void* commandBuffer = nullptr;
    void* imageView = nullptr;
    uint32_t queueFamily = 0;
    uint32_t imageCount = 0;
    uint32_t minImageCount = 0;
    uint32_t colorFormat = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ImGuiBackendHandles {
    RHIBackend backend = RHIBackend::Unknown;
    ImGuiVulkanFrameInfo vulkan;
    void* device = nullptr;            // ID3D11Device* / ID3D12Device* / MTLDevice*
    void* deviceContext = nullptr;     // ID3D11DeviceContext* (D3D11 only)
    void* instance = nullptr;          // VkInstance (Vulkan only)
    void* physicalDevice = nullptr;    // VkPhysicalDevice (Vulkan only)
    void* queue = nullptr;             // VkQueue (Vulkan only)
    void* descriptorPool = nullptr;    // VkDescriptorPool (Vulkan only)
    void* imageView = nullptr;         // current VkImageView (Vulkan only)
    uint32_t framesInFlight = 0;       // D3D12 only
    uint32_t queueFamily = 0;          // Vulkan only
    uint32_t imageCount = 0;           // Vulkan only
    uint32_t minImageCount = 0;        // Vulkan only
    uint32_t colorFormat = 0;          // VkFormat (Vulkan only)
    uint32_t width = 0;                // current framebuffer width (Vulkan only)
    uint32_t height = 0;               // current framebuffer height (Vulkan only)
    void* srvHeap = nullptr;           // D3D12 ID3D12DescriptorHeap*
    uint64_t fontSrvCpuHandle = 0;     // D3D12 font SRV CPU handle
    uint64_t fontSrvGpuHandle = 0;     // D3D12 font SRV GPU handle
    void* backBufferRtvPtr = nullptr;  // D3D11 ID3D11RenderTargetView*
    void* backBufferDsvPtr = nullptr;  // D3D11 ID3D11DepthStencilView*
    void* commandList = nullptr;       // D3D12 ID3D12GraphicsCommandList*
    void* commandBuffer = nullptr;     // Metal id<MTLCommandBuffer>
    void* commandEncoder = nullptr;    // Metal id<MTLRenderCommandEncoder>
    void* renderPassDescriptor = nullptr; // Metal MTLRenderPassDescriptor*
};

class IEditorImGuiRHIInterop {
public:
    using SwapChainResizeCallback = void(*)();

    virtual ~IEditorImGuiRHIInterop() = default;
    virtual ImGuiBackendHandles GetImGuiBackendHandles() { return {}; }
    virtual void SetSwapChainResizeCallback(SwapChainResizeCallback) {}
    virtual void SetImGuiTextureInteropReady(bool) {}
};
