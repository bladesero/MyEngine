#pragma once

#include "Core/Platform.h"
#include "Core/PlatformEventBridge.h"
#include "Renderer/RHI/IEditorImGuiRHIInterop.h"
#include "Renderer/RHI/GpuTextureView.h"

#include <memory>

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
    bool RebuildFontTexture();
    bool SupportsRuntimeFontTextureRebuild() const;
    void* GetTextureId(GpuTextureView* view);

    bool IsInitialized() const { return m_Initialized; }

private:
    bool RebuildFontTextureNow();

    IEditorImGuiRHIInterop* m_Interop = nullptr;
    IWindow* m_Window = nullptr;
    bool m_Initialized = false;
    bool m_FontTextureRebuildPending = false;
};
