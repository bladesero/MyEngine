#pragma once

#include "Core/Platform.h"
#include "Core/PlatformEventBridge.h"
#include "Renderer/RHI/IEditorImGuiRHIInterop.h"
#include "Renderer/RHI/GpuTextureView.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

class IWindow;
struct ImDrawData;
union SDL_Event;
// ==========================================================================
// EditorImGuiBackend - Editor-side ImGui backend that owns all ImGui init,
// frame, and draw lifecycle.  Uses native handles from the render context
// to talk to D3D11 / D3D12 / Metal backends.
// ==========================================================================
class EditorImGuiBackend {
public:
    EditorImGuiBackend(IEditorImGuiRHIInterop* interop, IWindow* window);
    ~EditorImGuiBackend();

    bool Init();
    void Shutdown();
    void ProcessSDLEvent(const SDL_Event& event);
    void BeginFrame();
    void RenderDrawData(ImDrawData* drawData);
    void RenderPlatformWindows();
    bool RebuildFontTexture();
    bool SupportsRuntimeFontTextureRebuild() const;
    void* GetTextureId(GpuTextureView* view);

    bool IsInitialized() const { return m_Initialized; }

private:
    struct CachedVulkanTexture {
        void* descriptor = nullptr;
        void* imageView = nullptr;
        void* sampler = nullptr;
        uint32_t imageLayout = 0;
    };

    bool RebuildFontTextureNow();
    void ClearVulkanTextureCache();

    IEditorImGuiRHIInterop* m_Interop = nullptr;
    IWindow* m_Window = nullptr;
    std::unordered_map<GpuTextureView*, CachedVulkanTexture> m_VulkanTextureCache;
    bool m_Initialized = false;
    bool m_FontTextureRebuildPending = false;
};
