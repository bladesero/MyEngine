#include "Editor/EditorImGuiBackend.h"

#include "Core/Logger.h"
#include "Core/Window.h"
#include "Editor/EditorImGuiMetalBridge.h"
#if defined(MYENGINE_ENABLE_VULKAN)
#include "Editor/EditorImGuiVulkanBridge.h"
#endif
#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/RHI/RHITypes.h"

#if defined(MYENGINE_PLATFORM_WINDOWS)
#include <d3d11.h>
#include <d3d12.h>
#endif

#include <SDL3/SDL.h>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#if defined(MYENGINE_PLATFORM_WINDOWS)
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx12.h>
#endif
#endif

EditorImGuiBackend::EditorImGuiBackend(IEditorImGuiRHIInterop* interop, IWindow* window)
    : m_Interop(interop), m_Window(window) {}

EditorImGuiBackend::~EditorImGuiBackend() {
    Shutdown();
}

bool EditorImGuiBackend::Init() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Interop || !m_Window || !m_Window->GetSDLWindow()) {
        Logger::Error("[EditorImGuiBackend] Missing RHI interop or window");
        return false;
    }

    if (!SDL_GetCurrentVideoDriver()) {
        Logger::Error("[EditorImGuiBackend] SDL_GetCurrentVideoDriver() is null");
        return false;
    }

    Shutdown();

    const ImGuiBackendHandles handles = m_Interop->GetImGuiBackendHandles();

#if defined(MYENGINE_PLATFORM_WINDOWS)
#if defined(MYENGINE_ENABLE_VULKAN)
    if (handles.backend == RHIBackend::Vulkan) {
        if (!ImGui_ImplSDL3_InitForVulkan(m_Window->GetSDLWindow())) {
            Logger::Error("[EditorImGuiBackend] ImGui_ImplSDL3_InitForVulkan failed");
            return false;
        }
        if (!EditorImGuiVulkan_Init(handles)) {
            ImGui_ImplSDL3_Shutdown();
            Logger::Error("[EditorImGuiBackend] ImGui_ImplVulkan_Init failed");
            return false;
        }
        m_Interop->SetImGuiTextureInteropReady(true);
    } else
#endif
    if (handles.backend == RHIBackend::D3D12) {
        if (!ImGui_ImplSDL3_InitForD3D(m_Window->GetSDLWindow())) {
            Logger::Error("[EditorImGuiBackend] ImGui_ImplSDL3_InitForD3D failed");
            return false;
        }
        auto* device = static_cast<ID3D12Device*>(handles.device);
        auto* heap = static_cast<ID3D12DescriptorHeap*>(handles.srvHeap);
        const D3D12_CPU_DESCRIPTOR_HANDLE fontCpu{ handles.fontSrvCpuHandle };
        const D3D12_GPU_DESCRIPTOR_HANDLE fontGpu{ handles.fontSrvGpuHandle };
        if (!ImGui_ImplDX12_Init(device, handles.framesInFlight,
                                 DXGI_FORMAT_R8G8B8A8_UNORM, heap,
                                 fontCpu, fontGpu)) {
            ImGui_ImplSDL3_Shutdown();
            return false;
        }
    } else {
        // D3D11 or default
        if (!ImGui_ImplSDL3_InitForD3D(m_Window->GetSDLWindow())) {
            Logger::Error("[EditorImGuiBackend] ImGui_ImplSDL3_InitForD3D failed");
            return false;
        }
        auto* device = static_cast<ID3D11Device*>(handles.device);
        auto* ctx = static_cast<ID3D11DeviceContext*>(handles.deviceContext);
        if (!ImGui_ImplDX11_Init(device, ctx)) {
            ImGui_ImplSDL3_Shutdown();
            return false;
        }
    }
#elif defined(MYENGINE_PLATFORM_MACOS)
    if (handles.backend != RHIBackend::Metal || !handles.device) {
        Logger::Error("[EditorImGuiBackend] Missing Metal ImGui handles");
        return false;
    }
    if (!ImGui_ImplSDL3_InitForMetal(m_Window->GetSDLWindow())) {
        Logger::Error("[EditorImGuiBackend] ImGui_ImplSDL3_InitForMetal failed");
        return false;
    }
    if (!EditorImGuiMetal_Init(handles.device)) {
        ImGui_ImplSDL3_Shutdown();
        Logger::Error("[EditorImGuiBackend] ImGui_ImplMetal_Init failed");
        return false;
    }
#else
    Logger::Error("[EditorImGuiBackend] No ImGui backend for this platform");
    return false;
#endif

#if defined(MYENGINE_PLATFORM_WINDOWS)
    if (handles.backend == RHIBackend::D3D11) {
        m_Interop->SetSwapChainResizeCallback([]() {
            ImGui_ImplDX11_InvalidateDeviceObjects();
        });
    }
#endif
    m_Initialized = true;
    return true;
#else
    return false;
#endif
}

void EditorImGuiBackend::Shutdown() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized) return;

    const ImGuiBackendHandles handles = m_Interop ? m_Interop->GetImGuiBackendHandles() : ImGuiBackendHandles{};
    const RHIBackend backend = handles.backend;
    if (ImGui::GetCurrentContext() &&
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        ImGui::DestroyPlatformWindows();
    }
#if defined(MYENGINE_PLATFORM_WINDOWS)
#if defined(MYENGINE_ENABLE_VULKAN)
    if (backend == RHIBackend::Vulkan) {
        if (m_Interop) m_Interop->SetImGuiTextureInteropReady(false);
        ClearVulkanTextureCache();
        EditorImGuiVulkan_Shutdown();
    } else
#endif
    if (backend == RHIBackend::D3D12) {
        ImGui_ImplDX12_Shutdown();
    } else {
        ImGui_ImplDX11_Shutdown();
    }
#elif defined(MYENGINE_PLATFORM_MACOS)
    if (backend == RHIBackend::Metal) {
        EditorImGuiMetal_Shutdown();
    }
#endif

    ImGui_ImplSDL3_Shutdown();
    m_Initialized = false;
    m_FontTextureRebuildPending = false;
#endif
}

void EditorImGuiBackend::ProcessSDLEvent(const SDL_Event& event) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized) return;
    ImGui_ImplSDL3_ProcessEvent(&event);
#endif
}

void EditorImGuiBackend::BeginFrame() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized) return;

    const ImGuiBackendHandles handles = m_Interop->GetImGuiBackendHandles();

#if defined(MYENGINE_PLATFORM_WINDOWS)
    if (m_FontTextureRebuildPending && RebuildFontTextureNow()) {
        m_FontTextureRebuildPending = false;
    }
#if defined(MYENGINE_ENABLE_VULKAN)
    if (handles.backend == RHIBackend::Vulkan) {
        EditorImGuiVulkan_NewFrame();
    } else
#endif
    if (handles.backend == RHIBackend::D3D12) {
        ImGui_ImplDX12_NewFrame();
    } else {
        // D3D11: restore backbuffer before ImGui captures the render target.
        // The render graph's EndRendering may have cleared OM render targets.
        if (handles.deviceContext && handles.backBufferRtvPtr) {
            auto* ctx = static_cast<ID3D11DeviceContext*>(handles.deviceContext);
            auto* rtv = static_cast<ID3D11RenderTargetView*>(handles.backBufferRtvPtr);
            auto* dsv = static_cast<ID3D11DepthStencilView*>(handles.backBufferDsvPtr);
            ctx->OMSetRenderTargets(1, &rtv, dsv);
        }
        ImGui_ImplDX11_NewFrame();
    }
#elif defined(MYENGINE_PLATFORM_MACOS)
    if (handles.backend == RHIBackend::Metal && handles.renderPassDescriptor) {
        EditorImGuiMetal_NewFrame(handles.renderPassDescriptor);
    }
#endif

    ImGui_ImplSDL3_NewFrame();
#endif
}

void EditorImGuiBackend::RenderDrawData(ImDrawData* drawData) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized || !drawData) return;

    const ImGuiBackendHandles handles = m_Interop->GetImGuiBackendHandles();

#if defined(MYENGINE_PLATFORM_WINDOWS)
#if defined(MYENGINE_ENABLE_VULKAN)
    if (handles.backend == RHIBackend::Vulkan) {
        EditorImGuiVulkan_RenderDrawData(drawData, handles);
    } else
#endif
    if (handles.backend == RHIBackend::D3D12) {
        auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(handles.commandList);
        if (cmdList) ImGui_ImplDX12_RenderDrawData(drawData, cmdList);
    } else {
        ImGui_ImplDX11_RenderDrawData(drawData);
    }
#elif defined(MYENGINE_PLATFORM_MACOS)
    if (handles.backend == RHIBackend::Metal &&
        handles.commandBuffer && handles.commandEncoder) {
        EditorImGuiMetal_RenderDrawData(
            drawData, handles.commandBuffer, handles.commandEncoder);
    }
#endif
#endif
}

void EditorImGuiBackend::RenderPlatformWindows() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized) return;
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0) return;

    const ImGuiBackendHandles handles = m_Interop ? m_Interop->GetImGuiBackendHandles() : ImGuiBackendHandles{};
#if defined(MYENGINE_PLATFORM_WINDOWS)
#if defined(MYENGINE_ENABLE_VULKAN)
    if (handles.backend == RHIBackend::Vulkan) {
        EditorImGuiVulkan_RenderPlatformWindows();
        return;
    }
#endif
#endif

    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
#endif
}

bool EditorImGuiBackend::RebuildFontTexture() {
    if (!m_Initialized || !m_Interop) return false;
    if (!SupportsRuntimeFontTextureRebuild()) return false;
    m_FontTextureRebuildPending = true;
    return true;
}

bool EditorImGuiBackend::SupportsRuntimeFontTextureRebuild() const {
#if defined(MYENGINE_ENABLE_IMGUI) && defined(MYENGINE_PLATFORM_WINDOWS)
    if (!m_Initialized || !m_Interop) return false;
    const ImGuiBackendHandles handles = m_Interop->GetImGuiBackendHandles();
    return handles.backend != RHIBackend::D3D12;
#else
    return false;
#endif
}

bool EditorImGuiBackend::RebuildFontTextureNow() {
#if defined(MYENGINE_ENABLE_IMGUI) && defined(MYENGINE_PLATFORM_WINDOWS)
    if (!m_Initialized || !m_Interop) return false;
    const ImGuiBackendHandles handles = m_Interop->GetImGuiBackendHandles();
#if defined(MYENGINE_ENABLE_VULKAN)
    if (handles.backend == RHIBackend::Vulkan) {
        EditorImGuiVulkan_DestroyFontsTexture();
        return EditorImGuiVulkan_CreateFontsTexture();
    }
#endif
    if (handles.backend == RHIBackend::D3D12) {
        ImGui_ImplDX12_InvalidateDeviceObjects();
        return ImGui_ImplDX12_CreateDeviceObjects();
    }
    ImGui_ImplDX11_InvalidateDeviceObjects();
    return ImGui_ImplDX11_CreateDeviceObjects();
#else
    return false;
#endif
}

void* EditorImGuiBackend::GetTextureId(GpuTextureView* view) {
    if (!view) return nullptr;
#if defined(MYENGINE_PLATFORM_WINDOWS) && defined(MYENGINE_ENABLE_VULKAN)
    if (m_Interop && m_Interop->GetImGuiBackendHandles().backend == RHIBackend::Vulkan) {
        const ImGuiNativeTextureInfo native = view->GetImGuiNativeTextureInfo();
        auto it = m_VulkanTextureCache.find(view);
        if (it != m_VulkanTextureCache.end() &&
            it->second.imageView == native.imageView &&
            it->second.sampler == native.sampler &&
            it->second.imageLayout == native.imageLayout) {
            return it->second.descriptor;
        }
        void* texture = EditorImGuiVulkan_CreateTexture(native);
        if (texture) {
            m_VulkanTextureCache[view] = {texture, native.imageView, native.sampler, native.imageLayout};
        }
        return texture;
    }
#endif
    return view->GetImGuiTextureId();
}

void EditorImGuiBackend::ClearVulkanTextureCache() {
    m_VulkanTextureCache.clear();
}
