#include "Editor/Backends/ImGui/EditorImGuiBackend.h"

#include "Core/Logger.h"
#include "Core/Window.h"
#include "Editor/Backends/Metal/EditorImGuiMetalBridge.h"
#if defined(MYENGINE_ENABLE_VULKAN)
#include "Editor/Backends/Vulkan/EditorImGuiVulkanBridge.h"
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

#if defined(MYENGINE_ENABLE_IMGUI)
namespace {
bool IsPlatformViewportRenderable(const ImGuiViewport* viewport) {
    if (!viewport || (viewport->Flags & ImGuiViewportFlags_IsMinimized) || viewport->Size.x <= 0.0f ||
        viewport->Size.y <= 0.0f) {
        return false;
    }

    const SDL_WindowID windowID = static_cast<SDL_WindowID>(reinterpret_cast<intptr_t>(viewport->PlatformHandle));
    SDL_Window* window = windowID != 0 ? SDL_GetWindowFromID(windowID) : nullptr;
    if (!window)
        return true;

    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    constexpr SDL_WindowFlags kNonRenderableFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED;
    return (flags & kNonRenderableFlags) == 0;
}

void RenderRenderablePlatformWindows() {
    // ImGui's default helper only filters minimized viewports. On D3D12 each
    // secondary viewport waits indefinitely on its swapchain frame-latency
    // handle, so submitting an SDL-hidden or occluded window can throttle the
    // entire Editor even though the main viewport has no GPU work to wait for.
    ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    ImVector<ImGuiViewport*> renderableViewports;
    for (int index = 1; index < platformIO.Viewports.Size; ++index) {
        ImGuiViewport* viewport = platformIO.Viewports[index];
        if (IsPlatformViewportRenderable(viewport))
            renderableViewports.push_back(viewport);
    }
    for (ImGuiViewport* viewport : renderableViewports) {
        if (platformIO.Platform_RenderWindow)
            platformIO.Platform_RenderWindow(viewport, nullptr);
        if (platformIO.Renderer_RenderWindow)
            platformIO.Renderer_RenderWindow(viewport, nullptr);
    }
    for (ImGuiViewport* viewport : renderableViewports) {
        if (platformIO.Platform_SwapBuffers)
            platformIO.Platform_SwapBuffers(viewport, nullptr);
        if (platformIO.Renderer_SwapBuffers)
            platformIO.Renderer_SwapBuffers(viewport, nullptr);
    }
}
} // namespace
#endif

#if defined(MYENGINE_ENABLE_IMGUI) && defined(MYENGINE_PLATFORM_WINDOWS)
namespace {
class D3D11ImmediateContextStateScope {
public:
    explicit D3D11ImmediateContextStateScope(ID3D11DeviceContext* context) : m_Context(context) {
        if (!m_Context)
            return;

        m_Context->OMGetRenderTargets(1, &m_RenderTarget, &m_DepthStencil);
        m_ViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        m_Context->RSGetViewports(&m_ViewportCount, m_Viewports);
        m_ScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        m_Context->RSGetScissorRects(&m_ScissorCount, m_Scissors);
    }

    ~D3D11ImmediateContextStateScope() {
        if (!m_Context)
            return;

        if (m_RenderTarget) {
            m_Context->OMSetRenderTargets(1, &m_RenderTarget, m_DepthStencil);
        } else {
            m_Context->OMSetRenderTargets(0, nullptr, m_DepthStencil);
        }
        m_Context->RSSetViewports(m_ViewportCount, m_Viewports);
        m_Context->RSSetScissorRects(m_ScissorCount, m_Scissors);

        if (m_RenderTarget)
            m_RenderTarget->Release();
        if (m_DepthStencil)
            m_DepthStencil->Release();
    }

    D3D11ImmediateContextStateScope(const D3D11ImmediateContextStateScope&) = delete;
    D3D11ImmediateContextStateScope& operator=(const D3D11ImmediateContextStateScope&) = delete;

private:
    ID3D11DeviceContext* m_Context = nullptr;
    ID3D11RenderTargetView* m_RenderTarget = nullptr;
    ID3D11DepthStencilView* m_DepthStencil = nullptr;
    UINT m_ViewportCount = 0;
    D3D11_VIEWPORT m_Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT m_ScissorCount = 0;
    D3D11_RECT m_Scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
};
} // namespace
#endif

EditorImGuiBackend::EditorImGuiBackend(IEditorImGuiRHIInterop* interop, IWindow* window)
    : m_Interop(interop), m_Window(window) {
}

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
        auto* commandQueue = static_cast<ID3D12CommandQueue*>(handles.commandQueue);
        auto* heap = static_cast<ID3D12DescriptorHeap*>(handles.srvHeap);
        const D3D12_CPU_DESCRIPTOR_HANDLE fontCpu{handles.fontSrvCpuHandle};
        const D3D12_GPU_DESCRIPTOR_HANDLE fontGpu{handles.fontSrvGpuHandle};
        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device = device;
        initInfo.CommandQueue = commandQueue;
        initInfo.NumFramesInFlight = static_cast<int>(handles.framesInFlight);
        initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.SrvDescriptorHeap = heap;
        // MyEngine owns one persistent font SRV slot. Runtime font-atlas rebuild stays disabled for D3D12.
        initInfo.LegacySingleSrvCpuDescriptor = fontCpu;
        initInfo.LegacySingleSrvGpuDescriptor = fontGpu;
        if (!device || !commandQueue || !heap || !ImGui_ImplDX12_Init(&initInfo)) {
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
        m_Interop->SetSwapChainResizeCallback([]() { ImGui_ImplDX11_InvalidateDeviceObjects(); });
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
    if (!m_Initialized)
        return;

    const ImGuiBackendHandles handles = m_Interop ? m_Interop->GetImGuiBackendHandles() : ImGuiBackendHandles{};
    const RHIBackend backend = handles.backend;
    // Renderer backends own multi-viewport teardown. In particular, ImGui's DX12 backend must first detach the
    // special main-viewport frame data before its internal DestroyPlatformWindows call; invoking it here treats the
    // main viewport as a secondary swapchain and dereferences its intentionally-null command queue.
#if defined(MYENGINE_PLATFORM_WINDOWS)
#if defined(MYENGINE_ENABLE_VULKAN)
    if (backend == RHIBackend::Vulkan) {
        if (m_Interop)
            m_Interop->SetImGuiTextureInteropReady(false);
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
    if (!m_Initialized)
        return;
    ImGui_ImplSDL3_ProcessEvent(&event);
#endif
}

void EditorImGuiBackend::BeginFrame() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized)
        return;

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
            if (handles.width > 0 && handles.height > 0) {
                D3D11_VIEWPORT viewport = {};
                viewport.Width = static_cast<float>(handles.width);
                viewport.Height = static_cast<float>(handles.height);
                viewport.MinDepth = 0.0f;
                viewport.MaxDepth = 1.0f;
                ctx->RSSetViewports(1, &viewport);
                const D3D11_RECT scissor = {0, 0, static_cast<LONG>(handles.width), static_cast<LONG>(handles.height)};
                ctx->RSSetScissorRects(1, &scissor);
            }
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
    if (!m_Initialized || !drawData)
        return;

    const ImGuiBackendHandles handles = m_Interop->GetImGuiBackendHandles();

#if defined(MYENGINE_PLATFORM_WINDOWS)
#if defined(MYENGINE_ENABLE_VULKAN)
    if (handles.backend == RHIBackend::Vulkan) {
        EditorImGuiVulkan_RenderDrawData(drawData, handles);
    } else
#endif
        if (handles.backend == RHIBackend::D3D12) {
        auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(handles.commandList);
        if (cmdList)
            ImGui_ImplDX12_RenderDrawData(drawData, cmdList);
    } else {
        ImGui_ImplDX11_RenderDrawData(drawData);
    }
#elif defined(MYENGINE_PLATFORM_MACOS)
    if (handles.backend == RHIBackend::Metal && handles.commandBuffer && handles.commandEncoder) {
        EditorImGuiMetal_RenderDrawData(drawData, handles.commandBuffer, handles.commandEncoder);
    }
#endif
#endif
}

void EditorImGuiBackend::RenderPlatformWindows() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized)
        return;
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0)
        return;

    const ImGuiBackendHandles handles = m_Interop ? m_Interop->GetImGuiBackendHandles() : ImGuiBackendHandles{};
#if defined(MYENGINE_PLATFORM_WINDOWS)
#if defined(MYENGINE_ENABLE_VULKAN)
    if (handles.backend == RHIBackend::Vulkan) {
        EditorImGuiVulkan_RenderPlatformWindows();
        return;
    }
#endif
#endif

#if defined(MYENGINE_PLATFORM_WINDOWS)
    if (handles.backend == RHIBackend::D3D11) {
        D3D11ImmediateContextStateScope stateScope(static_cast<ID3D11DeviceContext*>(handles.deviceContext));
        ImGui::UpdatePlatformWindows();
        RenderRenderablePlatformWindows();
        return;
    }
#endif

    ImGui::UpdatePlatformWindows();
    RenderRenderablePlatformWindows();
#endif
}

bool EditorImGuiBackend::RebuildFontTexture() {
    if (!m_Initialized || !m_Interop)
        return false;
    if (!SupportsRuntimeFontTextureRebuild())
        return false;
    m_FontTextureRebuildPending = true;
    return true;
}

bool EditorImGuiBackend::SupportsRuntimeFontTextureRebuild() const {
#if defined(MYENGINE_ENABLE_IMGUI) && defined(MYENGINE_PLATFORM_WINDOWS)
    if (!m_Initialized || !m_Interop)
        return false;
    const ImGuiBackendHandles handles = m_Interop->GetImGuiBackendHandles();
    return handles.backend != RHIBackend::D3D12;
#else
    return false;
#endif
}

bool EditorImGuiBackend::RebuildFontTextureNow() {
#if defined(MYENGINE_ENABLE_IMGUI) && defined(MYENGINE_PLATFORM_WINDOWS)
    if (!m_Initialized || !m_Interop)
        return false;
    const ImGuiBackendHandles handles = m_Interop->GetImGuiBackendHandles();
#if defined(MYENGINE_ENABLE_VULKAN)
    if (handles.backend == RHIBackend::Vulkan) {
        // ImGui 1.92 renderer backends consume atlas texture updates from ImDrawData.
        return true;
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
    if (!view)
        return nullptr;
#if defined(MYENGINE_PLATFORM_WINDOWS) && defined(MYENGINE_ENABLE_VULKAN)
    if (m_Interop && m_Interop->GetImGuiBackendHandles().backend == RHIBackend::Vulkan) {
        const ImGuiNativeTextureInfo native = view->GetImGuiNativeTextureInfo();
        auto it = m_VulkanTextureCache.find(view);
        if (it != m_VulkanTextureCache.end() && it->second.imageView == native.imageView &&
            it->second.sampler == native.sampler && it->second.imageLayout == native.imageLayout) {
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
