#include "Editor/EditorImGuiBackend.h"

#include "Core/Logger.h"
#include "Core/Window.h"
#include "Renderer/IRenderContext.h"
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

EditorImGuiBackend::EditorImGuiBackend(IRenderContext* context, IWindow* window)
    : m_Context(context), m_Window(window) {}

EditorImGuiBackend::~EditorImGuiBackend() {
    Shutdown();
}

bool EditorImGuiBackend::Init() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Context || !m_Window || !m_Window->GetSDLWindow()) {
        Logger::Error("[EditorImGuiBackend] Missing context or window");
        return false;
    }

    if (!SDL_GetCurrentVideoDriver()) {
        Logger::Error("[EditorImGuiBackend] SDL_GetCurrentVideoDriver() is null");
        return false;
    }

    Shutdown();

    const ImGuiBackendHandles handles = m_Context->GetImGuiBackendHandles();

#if defined(MYENGINE_PLATFORM_WINDOWS)
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
    // Metal backend placeholder
    Logger::Error("[EditorImGuiBackend] Metal ImGui backend not yet migrated");
    return false;
#else
    Logger::Error("[EditorImGuiBackend] No ImGui backend for this platform");
    return false;
#endif

    if (handles.backend == RHIBackend::D3D11) {
        m_Context->SetSwapChainResizeCallback([]() {
            ImGui_ImplDX11_InvalidateDeviceObjects();
        });
    }
    m_Initialized = true;
    return true;
#else
    return false;
#endif
}

void EditorImGuiBackend::Shutdown() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized) return;

    const RHIBackend backend = m_Context ? m_Context->GetBackend() : RHIBackend::Unknown;
#if defined(MYENGINE_PLATFORM_WINDOWS)
    if (backend == RHIBackend::D3D12) {
        ImGui_ImplDX12_Shutdown();
    } else {
        ImGui_ImplDX11_Shutdown();
    }
#endif

    ImGui_ImplSDL3_Shutdown();
    m_Initialized = false;
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

    const ImGuiBackendHandles handles = m_Context->GetImGuiBackendHandles();

#if defined(MYENGINE_PLATFORM_WINDOWS)
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
#endif

    ImGui_ImplSDL3_NewFrame();
#endif
}

void EditorImGuiBackend::RenderDrawData(ImDrawData* drawData) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Initialized || !drawData) return;

    const ImGuiBackendHandles handles = m_Context->GetImGuiBackendHandles();

#if defined(MYENGINE_PLATFORM_WINDOWS)
    if (handles.backend == RHIBackend::D3D12) {
        auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(handles.commandList);
        if (cmdList) ImGui_ImplDX12_RenderDrawData(drawData, cmdList);
    } else {
        ImGui_ImplDX11_RenderDrawData(drawData);
    }
#endif
#endif
}

void* EditorImGuiBackend::GetTextureId(GpuTextureView* view) {
    if (!view) return nullptr;
    return view->GetImGuiTextureId();
}
