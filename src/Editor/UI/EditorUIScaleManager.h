#pragma once

#include "Editor/UI/EditorFontManager.h"

#include <algorithm>
#include <filesystem>
#include <utility>

class EditorImGuiBackend;
class IWindow;

namespace Editor::UI {

struct EditorUIScaleSettings {
    float userScale = 1.0f;

    static constexpr float kMinUserScale = 0.75f;
    static constexpr float kMaxUserScale = 2.0f;

    static float ClampUserScale(float value) { return std::clamp(value, kMinUserScale, kMaxUserScale); }
};

class EditorUIScaleManager {
public:
    void Initialize(IWindow* window, float userScale = 1.0f);
    bool BeginFrame(EditorImGuiBackend* backend);

    bool SetUserScale(float userScale);
    void SetPlatformScaleForTesting(float platformScale);
    void MarkFontAtlasDirty() { m_FontManager.MarkDirty(); }
    void SetFontRoot(std::filesystem::path root) { m_FontManager.SetFontRoot(std::move(root)); }

    float GetPlatformScale() const { return m_PlatformScale; }
    float GetUserScale() const { return m_UserScale; }
    float GetEffectiveScale() const { return m_EffectiveScale; }
    bool WasFontAtlasRebuilt() const { return m_FontManager.WasRebuilt(); }
    const EditorFontManager& GetFontManager() const { return m_FontManager; }
    EditorFontManager& GetFontManager() { return m_FontManager; }

    static float ClampUserScale(float value) { return EditorUIScaleSettings::ClampUserScale(value); }
    static float ComputeEffectiveScale(float platformScale, float userScale);

private:
    float QueryPlatformScale() const;
    bool ApplyScale(float platformScale, float userScale);

    IWindow* m_Window = nullptr;
    float m_PlatformScale = 1.0f;
    float m_UserScale = 1.0f;
    float m_EffectiveScale = 1.0f;
    bool m_UseTestingPlatformScale = false;
    EditorFontManager m_FontManager;
};

} // namespace Editor::UI
