#pragma once

#include "API/RuntimeApi.h"

#include "Core/EngineMath.h"
#include "UI/Rml/RmlAssetLoader.h"
#include "UI/Rml/RmlContextManager.h"
#include "UI/Rml/RmlInputAdapter.h"
#include "UI/Input/UIInputSystem.h"
#include "UI/Rml/RmlRenderInterface.h"
#include "UI/Render/UIDrawList.h"
#include "UI/UIEventBridge.h"
#include "UI/Core/RuntimeUIScreenStack.h"
#include "UI/Core/SubtitleSystem.h"

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <nlohmann/json.hpp>

struct Event;
class IRHIDevice;
class IRHIFrameContext;
class Scene;
namespace Rml {
class ElementDocument;
}

struct UISystemOverlayState {
    bool visible = false;
    bool error = false;
    float progress = 0.0f;
    std::string title;
    std::string detail;
    std::string primaryHint;
    std::string secondaryHint;
};

struct UISafeAreaInsets {
    float left = 0, top = 0, right = 0, bottom = 0;
};
struct UIAccessibilitySettings {
    float uiScale = 1.0f, subtitleScale = 1.0f;
    bool subtitles = true, reduceCameraShake = false, highContrast = false;
    std::string colorVisionMode = "none";
};
struct UISystemDiagnostics {
    int viewportWidth = 1, viewportHeight = 1, safeWidth = 1, safeHeight = 1;
    float effectiveScale = 1.0f;
    uint32_t loadedFontFaces = 0, failedFontFaces = 0;
    bool narrowLayout = false, safeAreaValid = true;
    bool projectRuntimeScreenActive = false;
    uint32_t runtimeScreenFallbacks = 0;
    std::string runtimeScreenDocument;
    std::string lastFontError;
};

class UIDataModel {
public:
    using Value = std::variant<bool, int, float, std::string, Vec2, Vec3, nlohmann::json>;

    void SetBool(const std::string& name, bool value) {
        m_Values[name] = value;
        m_Dirty = true;
    }
    void SetInt(const std::string& name, int value) {
        m_Values[name] = value;
        m_Dirty = true;
    }
    void SetFloat(const std::string& name, float value) {
        m_Values[name] = value;
        m_Dirty = true;
    }
    void SetString(const std::string& name, std::string value) {
        m_Values[name] = std::move(value);
        m_Dirty = true;
    }
    void SetVec2(const std::string& name, const Vec2& value) {
        m_Values[name] = value;
        m_Dirty = true;
    }
    void SetVec3(const std::string& name, const Vec3& value) {
        m_Values[name] = value;
        m_Dirty = true;
    }
    void SetJson(const std::string& name, nlohmann::json value) {
        m_Values[name] = std::move(value);
        m_Dirty = true;
    }
    const std::unordered_map<std::string, Value>& GetValues() const { return m_Values; }
    void MarkDirty() { m_Dirty = true; }
    bool ConsumeDirty() {
        const bool dirty = m_Dirty;
        m_Dirty = false;
        return dirty;
    }

private:
    std::unordered_map<std::string, Value> m_Values;
    bool m_Dirty = false;
};

class MYENGINE_RUNTIME_API UISystem {
public:
    UISystem();
    ~UISystem();

    bool Initialize(IRHIDevice* device, IRHIFrameContext* frameContext);
    void Shutdown();
    void Resize(int width, int height);
    bool SetAccessibilitySettings(const UIAccessibilitySettings&, std::string* error = nullptr);
    const UIAccessibilitySettings& GetAccessibilitySettings() const { return m_Accessibility; }
    bool SetSafeAreaInsets(const UISafeAreaInsets&, std::string* error = nullptr);
    const UISafeAreaInsets& GetSafeAreaInsets() const { return m_SafeArea; }
    const UISystemDiagnostics& GetDiagnostics() const { return m_Diagnostics; }

    void Update(Scene& scene, float dt);
    bool ProcessEvent(Event& event);
    bool ProcessEvent(Scene& scene, Event& event, const UIInputViewport& viewport);
    void CollectDrawData(Scene& scene, UIDrawList& drawList);
    void SetSystemOverlay(UISystemOverlayState state);
    const UISystemOverlayState& GetSystemOverlay() const { return m_SystemOverlay; }
    bool IsSystemOverlayDocumentLoaded() const { return m_SystemOverlayDocument != nullptr; }
    void SetRuntimeScreen(RuntimeUIScreenView view);
    const RuntimeUIScreenView& GetRuntimeScreen() const { return m_RuntimeScreen; }
    bool ProcessRuntimeScreenPointer(Event& event, const UIInputViewport& viewport, size_t& outIndex,
                                     bool& outActivate);
    bool ShowSubtitle(SubtitleCue cue, std::string* error = nullptr);
    void ClearSubtitles();
    const SubtitleState& GetSubtitleState() const { return m_Subtitles.GetState(); }
    bool IsSubtitlePresented() const { return m_Accessibility.subtitles && m_Subtitles.GetState().visible; }

    UIDataModel& CreateDataModel(const std::string& name);
    UIEventBridge& GetEventBridge() { return *GetActiveEventBridge(); }
    void SetEventBridge(UIEventBridge* eventBridge) { m_ExternalEventBridge = eventBridge; }
    void MarkActorTreeDirty(uint64_t canvasActorID);

private:
    void EnsureCanvasDocuments(Scene& scene);
    void LoadCanvasFonts(Scene& scene);
    void ApplyDataModels(Scene& scene);
    void CreateSystemOverlayDocument();
    void ApplySystemOverlay();
    void CreateRuntimeScreenDocument();
    void ApplyRuntimeScreen();
    void CreateSubtitleDocument();
    void ApplySubtitle();
    void ApplyViewportMetrics();
    void LoadEngineFallbackFonts();
    UIEventBridge* GetActiveEventBridge();
    static UIDataModel* ResolveDataModel(void* user, const std::string& name);

    IRHIDevice* m_Device = nullptr;
    IRHIFrameContext* m_FrameContext = nullptr;
    bool m_Initialized = false;
    int m_Width = 1;
    int m_Height = 1;
    UIAccessibilitySettings m_Accessibility;
    UISafeAreaInsets m_SafeArea;
    UISystemDiagnostics m_Diagnostics;
    bool m_FallbackFontsAttempted = false;
    RmlAssetLoader m_AssetLoader;
    RmlRenderInterface m_RenderInterface;
    RmlContextManager m_ContextManager;
    RmlInputAdapter m_InputAdapter;
    UIInputSystem m_UIInputSystem;
    UIEventBridge m_EventBridge;
    UIEventBridge* m_ExternalEventBridge = nullptr;
    std::unordered_map<std::string, UIDataModel> m_DataModels;
    std::unordered_map<std::string, bool> m_LoadedFonts;
    std::unordered_map<uint64_t, std::size_t> m_ActorTreeSignatures;
    UISystemOverlayState m_SystemOverlay;
    Rml::ElementDocument* m_SystemOverlayDocument = nullptr;
    RuntimeUIScreenView m_RuntimeScreen;
    Rml::ElementDocument* m_RuntimeScreenDocument = nullptr;
    bool m_RuntimeScreenUsingCustomDocument = false;
    SubtitleSystem m_Subtitles;
    Rml::ElementDocument* m_SubtitleDocument = nullptr;
};

using UIScriptingAttachCallback = void (*)(UISystem&, UIEventBridge&);
using UIScriptingDetachCallback = void (*)(UISystem&, UIEventBridge&);

// Scripting installs these callbacks from the Runtime composition root. UI
// only publishes lifecycle and event-bridge contracts.
void SetUIScriptingLifecycleCallbacks(UIScriptingAttachCallback attach, UIScriptingDetachCallback detach);
