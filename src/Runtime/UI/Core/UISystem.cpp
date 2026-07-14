#include "UI/Core/UISystem.h"
#include "Core/RuntimeAccessibility.h"
#include "Input/Input.h"

#include "Assets/AssetManager.h"
#include "Core/Event.h"
#include "Core/Logger.h"
#include "Core/RuntimeFileSystem.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scripting/AngelScriptRuntime.h"
#include "UI/Core/UIActorTreeBuilder.h"
#include "UI/Core/UICanvasComponent.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <vector>

namespace {

bool g_RmlCoreInitialized = false;
std::unordered_map<std::string, std::vector<unsigned char>> g_RmlFontData;

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string EscapeRmlText(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

std::string DataModelValueToText(const UIDataModel::Value& value) {
    return std::visit(
        [](const auto& item) -> std::string {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                return item ? "true" : "false";
            } else if constexpr (std::is_same_v<T, int>) {
                return std::to_string(item);
            } else if constexpr (std::is_same_v<T, float>) {
                std::ostringstream stream;
                stream << std::fixed << std::setprecision(1) << item;
                return stream.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return item;
            } else if constexpr (std::is_same_v<T, Vec2>) {
                return std::to_string(item.x) + ", " + std::to_string(item.y);
            } else if constexpr (std::is_same_v<T, Vec3>) {
                return std::to_string(item.x) + ", " + std::to_string(item.y) + ", " + std::to_string(item.z);
            } else {
                return item.dump();
            }
        },
        value);
}

bool TryGetFloat(const UIDataModel& model, const char* key, float& value) {
    const auto found = model.GetValues().find(key);
    if (found == model.GetValues().end())
        return false;
    if (const float* number = std::get_if<float>(&found->second)) {
        value = *number;
        return true;
    }
    if (const int* number = std::get_if<int>(&found->second)) {
        value = static_cast<float>(*number);
        return true;
    }
    return false;
}

bool TryParseFontFaceFromPath(const std::string& path, std::string& family, Rml::Style::FontWeight& weight) {
    const std::string stem = std::filesystem::path(path).stem().string();
    const std::size_t separator = stem.find_last_of("-_ ");
    if (separator == std::string::npos || separator == 0 || separator + 1 >= stem.size()) {
        return false;
    }

    const std::string suffix = ToLower(stem.substr(separator + 1));
    if (suffix == "regular") {
        weight = Rml::Style::FontWeight::Normal;
    } else if (suffix == "bold") {
        weight = Rml::Style::FontWeight::Bold;
    } else {
        return false;
    }

    family = stem.substr(0, separator);
    return !family.empty();
}

bool ReadBinaryFile(const std::string& path, std::vector<unsigned char>& bytes) {
    std::vector<uint8_t> raw;
    if (!RuntimeFileSystem::Get().ReadAllBytes(path, raw) || raw.empty())
        return false;
    bytes.assign(raw.begin(), raw.end());
    return true;
}

bool HasInputEnabledCanvas(Scene& scene) {
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive())
            return;
        auto* canvas = actor.GetComponent<UICanvasComponent>();
        if (!canvas || !canvas->IsEnabled())
            return;
        found = canvas->IsVisible() && canvas->IsInteractive() && canvas->GetInputMode() != UIInputMode::None;
    });
    return found;
}

bool IsValidColorVisionMode(const std::string& mode) {
    return mode == "none" || mode == "protanopia" || mode == "deuteranopia" || mode == "tritanopia";
}

std::string AccentColor(const UIAccessibilitySettings& value) {
    if (value.highContrast)
        return "#ffe45c";
    if (value.colorVisionMode == "protanopia")
        return "#4cc9ff";
    if (value.colorVisionMode == "deuteranopia")
        return "#5eb8ff";
    if (value.colorVisionMode == "tritanopia")
        return "#ff8ac8";
    return "#4ca6ff";
}

} // namespace

UISystem::UISystem() = default;

UISystem::~UISystem() {
    Shutdown();
}

bool UISystem::Initialize(IRHIDevice* device, IRHIFrameContext* frameContext) {
    if (m_Initialized)
        return true;
    m_Device = device;
    m_FrameContext = frameContext;
    m_RenderInterface.SetDevice(device);
    Rml::SetSystemInterface(&m_AssetLoader);
    Rml::SetFileInterface(&m_AssetLoader);
    Rml::SetRenderInterface(&m_RenderInterface);
    if (!g_RmlCoreInitialized) {
        if (!Rml::Initialise()) {
            Logger::Error("[UI] RmlUi initialization failed");
            return false;
        }
        g_RmlCoreInitialized = true;
    }
    if (!m_ContextManager.Create("MyEngineInGameUI", m_Width, m_Height)) {
        Logger::Error("[UI] Failed to create RmlUi context");
        return false;
    }
    m_Initialized = true;
    m_Diagnostics.loadedFontFaces = 0;
    m_Diagnostics.failedFontFaces = 0;
    m_Diagnostics.lastFontError.clear();
    m_Diagnostics.runtimeScreenFallbacks = 0;
    m_Diagnostics.projectRuntimeScreenActive = false;
    m_Diagnostics.runtimeScreenDocument.clear();
    LoadEngineFallbackFonts();
    ApplyViewportMetrics();
    CreateSystemOverlayDocument();
    CreateRuntimeScreenDocument();
    CreateSubtitleDocument();
    AngelScriptRuntime::SetUIEventBridge(GetActiveEventBridge());
    AngelScriptRuntime::SetUISystem(this);
    return true;
}

void UISystem::Shutdown() {
    if (!m_Initialized)
        return;
    AngelScriptRuntime::ClearUIEventBridge(GetActiveEventBridge());
    AngelScriptRuntime::ClearUISystem(this);
    m_ContextManager.Destroy();
    m_SystemOverlayDocument = nullptr;
    m_RuntimeScreenDocument = nullptr;
    m_RuntimeScreenUsingCustomDocument = false;
    m_SubtitleDocument = nullptr;
    m_Subtitles.Clear();
    m_RuntimeScreen = {};
    m_LoadedFonts.clear();
    m_ActorTreeSignatures.clear();
    m_ExternalEventBridge = nullptr;
    m_FallbackFontsAttempted = false;
    m_Initialized = false;
    m_Device = nullptr;
    m_FrameContext = nullptr;
}

void UISystem::Resize(int width, int height) {
    if (width <= 0 || height <= 0)
        return;
    m_Width = width;
    m_Height = height;
    m_ContextManager.Resize(width, height);
    ApplyViewportMetrics();
}

bool UISystem::SetAccessibilitySettings(const UIAccessibilitySettings& value, std::string* error) {
    if (!std::isfinite(value.uiScale) || value.uiScale < 0.5f || value.uiScale > 2.0f ||
        !std::isfinite(value.subtitleScale) || value.subtitleScale < 0.5f || value.subtitleScale > 2.0f ||
        !IsValidColorVisionMode(value.colorVisionMode)) {
        if (error)
            *error = "invalid UI accessibility settings";
        return false;
    }
    m_Accessibility = value;
    RuntimeAccessibility::SetReduceCameraShake(value.reduceCameraShake);
    ApplyViewportMetrics();
    ApplySystemOverlay();
    ApplyRuntimeScreen();
    ApplySubtitle();
    if (error)
        error->clear();
    return true;
}

bool UISystem::SetSafeAreaInsets(const UISafeAreaInsets& value, std::string* error) {
    const bool finite = std::isfinite(value.left) && std::isfinite(value.top) && std::isfinite(value.right) &&
                        std::isfinite(value.bottom);
    if (!finite || value.left < 0 || value.top < 0 || value.right < 0 || value.bottom < 0 ||
        value.left + value.right >= 0.8f || value.top + value.bottom >= 0.8f) {
        if (error)
            *error = "invalid normalized UI safe-area insets";
        return false;
    }
    m_SafeArea = value;
    ApplyViewportMetrics();
    ApplySystemOverlay();
    ApplyRuntimeScreen();
    if (error)
        error->clear();
    return true;
}

void UISystem::ApplyViewportMetrics() {
    m_Diagnostics.viewportWidth = m_Width;
    m_Diagnostics.viewportHeight = m_Height;
    m_Diagnostics.effectiveScale = m_Accessibility.uiScale;
    m_Diagnostics.safeWidth =
        std::max(1, static_cast<int>(std::round(m_Width * (1.0f - m_SafeArea.left - m_SafeArea.right))));
    m_Diagnostics.safeHeight =
        std::max(1, static_cast<int>(std::round(m_Height * (1.0f - m_SafeArea.top - m_SafeArea.bottom))));
    m_Diagnostics.safeAreaValid = m_Diagnostics.safeWidth >= 240 && m_Diagnostics.safeHeight >= 160;
    m_Diagnostics.narrowLayout = m_Diagnostics.safeWidth / std::max(0.5f, m_Accessibility.uiScale) < 640;
    if (Rml::Context* context = m_ContextManager.GetContext())
        context->SetDensityIndependentPixelRatio(m_Accessibility.uiScale);
}

void UISystem::LoadEngineFallbackFonts() {
    if (m_FallbackFontsAttempted)
        return;
    m_FallbackFontsAttempted = true;
    for (const std::string& path : {"Content/UI/Fonts/LatoLatin-Regular.ttf", "Content/UI/Fonts/LatoLatin-Bold.ttf"}) {
        if (g_RmlFontData.count(path) != 0) {
            m_LoadedFonts[path] = true;
            ++m_Diagnostics.loadedFontFaces;
            continue;
        }
        const std::string resolved = AssetManager::Get().ResolvePath(path);
        std::string family;
        Rml::Style::FontWeight weight = Rml::Style::FontWeight::Auto;
        bool loaded = false;
        if (TryParseFontFaceFromPath(path, family, weight)) {
            std::vector<unsigned char> bytes;
            if (ReadBinaryFile(resolved, bytes)) {
                auto& stored = g_RmlFontData[path];
                stored = std::move(bytes);
                loaded = Rml::LoadFontFace(Rml::Span<const Rml::byte>(stored.data(), stored.size()), family,
                                           Rml::Style::FontStyle::Normal, weight);
            }
        }
        if (loaded) {
            m_LoadedFonts[path] = true;
            ++m_Diagnostics.loadedFontFaces;
        } else {
            ++m_Diagnostics.failedFontFaces;
            m_Diagnostics.lastFontError = path;
            Logger::Warn("[UI] Engine fallback font unavailable: ", path);
        }
    }
}

void UISystem::LoadCanvasFonts(Scene& scene) {
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive())
            return;
        auto* canvas = actor.GetComponent<UICanvasComponent>();
        if (!canvas || !canvas->IsEnabled())
            return;
        for (const std::string& fontPath : canvas->GetDefaultFontPaths()) {
            if (fontPath.empty() || m_LoadedFonts.count(fontPath) != 0)
                continue;
            // Rml font faces are process-global and retain the supplied memory.
            // Never replace the backing vector when another UISystem already loaded it.
            if (g_RmlFontData.count(fontPath) != 0) {
                m_LoadedFonts[fontPath] = true;
                continue;
            }
            const std::string resolved = AssetManager::Get().ResolvePath(fontPath);
            bool loaded = false;
            std::string family;
            Rml::Style::FontWeight weight = Rml::Style::FontWeight::Auto;
            if (TryParseFontFaceFromPath(fontPath, family, weight)) {
                std::vector<unsigned char> fontBytes;
                if (ReadBinaryFile(resolved, fontBytes)) {
                    auto& storedBytes = g_RmlFontData[fontPath];
                    storedBytes = std::move(fontBytes);
                    const Rml::Span<const Rml::byte> fontData(storedBytes.data(), storedBytes.size());
                    loaded = Rml::LoadFontFace(fontData, family, Rml::Style::FontStyle::Normal, weight);
                    if (!loaded) {
                        g_RmlFontData.erase(fontPath);
                    }
                }
            }

            if (!loaded) {
                loaded = Rml::LoadFontFace(resolved);
            }

            if (loaded) {
                m_LoadedFonts[fontPath] = true;
                ++m_Diagnostics.loadedFontFaces;
            } else {
                ++m_Diagnostics.failedFontFaces;
                m_Diagnostics.lastFontError = fontPath;
                Logger::Warn("[UI] Failed to load font: ", fontPath);
            }
        }
    });
}

void UISystem::EnsureCanvasDocuments(Scene& scene) {
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context)
        return;
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive())
            return;
        auto* component = actor.GetComponent<UICanvasComponent>();
        if (!component || !component->IsEnabled())
            return;
        UICanvas& canvas = component->GetCanvas();
        canvas.SetContext(context);
        if (component->GetSourceMode() == UICanvasSourceMode::ActorTree) {
            const std::size_t signature = UIActorTreeBuilder::ComputeSignature(actor);
            const auto found = m_ActorTreeSignatures.find(actor.GetID());
            if (!canvas.GetDocument() || found == m_ActorTreeSignatures.end() || found->second != signature) {
                std::string rml;
                std::string error;
                if (UIActorTreeBuilder::BuildDocument(actor, *component, rml, &error)) {
                    const std::string sourceURL = "generated://ui_actor_" + std::to_string(actor.GetID()) + ".rml";
                    if (canvas.LoadDocumentFromMemory(rml, sourceURL)) {
                        m_ActorTreeSignatures[actor.GetID()] = signature;
                    }
                } else if (!error.empty()) {
                    Logger::Warn("[UI] Failed to build UI actor tree: ", error);
                }
            }
        } else if (!canvas.GetDocument() && !canvas.GetDocumentPath().empty()) {
            canvas.LoadDocument(canvas.GetDocumentPath());
        }
    });
}

void UISystem::ApplyDataModels(Scene& scene) {
    if (m_DataModels.empty())
        return;

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive())
            return;
        auto* component = actor.GetComponent<UICanvasComponent>();
        if (!component || !component->IsEnabled() || !component->IsVisible())
            return;
        Rml::ElementDocument* document = component->GetCanvas().GetDocument();
        if (!document)
            return;

        for (const auto& [modelName, model] : m_DataModels) {
            (void)modelName;
            for (const auto& [key, value] : model.GetValues()) {
                Rml::Element* element = document->GetElementById(key);
                if (!element)
                    continue;
                if (const bool* visible = std::get_if<bool>(&value)) {
                    element->SetProperty("display", *visible ? "block" : "none");
                    continue;
                }
                const std::string text = DataModelValueToText(value);
                element->SetInnerRML(EscapeRmlText(text));
                if (const std::string* stringValue = std::get_if<std::string>(&value)) {
                    element->SetProperty("display", stringValue->empty() ? "none" : "block");
                }
            }

            float health = 0.0f;
            float maxHealth = 0.0f;
            Rml::Element* healthFill = document->GetElementById("health-bar-fill");
            if (healthFill && TryGetFloat(model, "health", health) && TryGetFloat(model, "maxHealth", maxHealth) &&
                maxHealth > 0.0f) {
                const float percent = std::clamp(health / maxHealth, 0.0f, 1.0f) * 100.0f;
                healthFill->SetProperty("width", std::to_string(percent) + "%");
            }
        }
    });
}

void UISystem::Update(Scene& scene, float dt) {
    if (!m_Initialized)
        return;
    m_Subtitles.Update(dt);
    LoadCanvasFonts(scene);
    EnsureCanvasDocuments(scene);
    ApplyDataModels(scene);
    ApplySystemOverlay();
    ApplyRuntimeScreen();
    ApplySubtitle();
    if (Rml::Context* context = m_ContextManager.GetContext()) {
        context->Update();
    }
}

void UISystem::CreateSubtitleDocument() {
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context)
        return;
    static const char* source = R"(<rml><head><title>Subtitles</title><style>
body { margin:0; width:100%; height:100%; font-family:LatoLatin; color:#ffffff; }
#subtitle-root { position:absolute; left:10%; bottom:8%; width:80%;
 text-align:center; z-index:2147460000; }
#subtitle-card { display:inline-block; max-width:90%; padding:8px 14px;
 background-color:rgba(0,0,0,205); border:1px #52647d; }
#subtitle-speaker { color:#8fd0ff; font-size:18px; margin-bottom:3px; }
#subtitle-text { color:#ffffff; font-size:22px; }
</style></head><body><div id="subtitle-root"><div id="subtitle-card">
<div id="subtitle-speaker"></div><div id="subtitle-text"></div>
</div></div></body></rml>)";
    m_SubtitleDocument = context->LoadDocumentFromMemory(source, "generated://subtitles.rml");
    if (m_SubtitleDocument) {
        m_SubtitleDocument->Show();
        ApplySubtitle();
    }
}

void UISystem::ApplySubtitle() {
    if (!m_SubtitleDocument)
        return;
    const SubtitleState& state = m_Subtitles.GetState();
    if (Rml::Element* root = m_SubtitleDocument->GetElementById("subtitle-root")) {
        root->SetProperty("display", m_Accessibility.subtitles && state.visible ? "block" : "none");
        const int safeLeft = static_cast<int>(std::round(m_Width * m_SafeArea.left));
        const int safeBottom = static_cast<int>(std::round(m_Height * m_SafeArea.bottom));
        root->SetProperty("left", std::to_string(safeLeft + std::max(0, m_Diagnostics.safeWidth / 10)) + "px");
        root->SetProperty("bottom", std::to_string(safeBottom + std::max(8, m_Diagnostics.safeHeight / 16)) + "px");
        root->SetProperty("width", std::to_string(std::max(180, m_Diagnostics.safeWidth * 8 / 10)) + "px");
    }
    if (Rml::Element* card = m_SubtitleDocument->GetElementById("subtitle-card"))
        card->SetProperty("border-color", m_Accessibility.highContrast ? "#ffffff" : "#52647d");
    if (Rml::Element* speaker = m_SubtitleDocument->GetElementById("subtitle-speaker")) {
        speaker->SetInnerRML(EscapeRmlText(state.speaker));
        speaker->SetProperty("display", state.speaker.empty() ? "none" : "block");
        speaker->SetProperty("font-size", std::to_string(static_cast<int>(18 * m_Accessibility.subtitleScale)) + "px");
        speaker->SetProperty("color", AccentColor(m_Accessibility));
    }
    if (Rml::Element* text = m_SubtitleDocument->GetElementById("subtitle-text")) {
        text->SetInnerRML(EscapeRmlText(state.text));
        text->SetProperty("font-size", std::to_string(static_cast<int>(22 * m_Accessibility.subtitleScale)) + "px");
    }
}

bool UISystem::ShowSubtitle(SubtitleCue cue, std::string* error) {
    if (!m_Subtitles.Enqueue(std::move(cue), error))
        return false;
    ApplySubtitle();
    return true;
}

void UISystem::ClearSubtitles() {
    m_Subtitles.Clear();
    ApplySubtitle();
}

void UISystem::CreateSystemOverlayDocument() {
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context)
        return;
    static const char* source = R"(<rml><head><title>System status</title><style>
body { margin:0; width:100%; height:100%; font-family:LatoLatin; color:#f5f7fa; }
#system-root { position:absolute; left:0; top:0; width:100%; height:100%;
 background-color:rgba(7,10,16,210); z-index:2147480000; }
#system-card { position:absolute; left:20%; top:32%; width:60%; padding:28px;
 background-color:rgba(20,27,39,245); border:2px #52647d; }
#system-title { font-size:30px; margin-bottom:14px; }
#system-detail { font-size:18px; color:#c9d2df; margin-bottom:20px; }
#system-track { width:100%; height:10px; background-color:#273244; margin-bottom:18px; }
#system-progress { width:0%; height:10px; background-color:#4ca6ff; }
#system-primary { font-size:17px; color:#8fd0ff; }
#system-secondary { font-size:15px; color:#9aa8ba; margin-top:8px; }
</style></head><body><div id="system-root"><div id="system-card">
<div id="system-title"></div><div id="system-detail"></div>
<div id="system-track"><div id="system-progress"></div></div>
<div id="system-primary"></div><div id="system-secondary"></div>
</div></div></body></rml>)";
    m_SystemOverlayDocument = context->LoadDocumentFromMemory(source, "generated://system-overlay.rml");
    if (m_SystemOverlayDocument) {
        m_SystemOverlayDocument->Show();
        ApplySystemOverlay();
    }
}

void UISystem::ApplySystemOverlay() {
    if (!m_SystemOverlayDocument)
        return;
    Rml::Element* root = m_SystemOverlayDocument->GetElementById("system-root");
    if (root)
        root->SetProperty("display", m_SystemOverlay.visible ? "block" : "none");
    if (Rml::Element* card = m_SystemOverlayDocument->GetElementById("system-card")) {
        const int safeLeft = static_cast<int>(std::round(m_Width * m_SafeArea.left));
        const int safeTop = static_cast<int>(std::round(m_Height * m_SafeArea.top));
        const int cardWidth = std::max(180, m_Diagnostics.safeWidth * 8 / 10);
        card->SetProperty("left", std::to_string(safeLeft + (m_Diagnostics.safeWidth - cardWidth) / 2) + "px");
        card->SetProperty("top", std::to_string(safeTop + std::max(8, m_Diagnostics.safeHeight / 4)) + "px");
        card->SetProperty("width", std::to_string(cardWidth) + "px");
        card->SetProperty("border-color", m_Accessibility.highContrast ? "#ffffff" : "#52647d");
    }
    auto setText = [&](const char* id, const std::string& text) {
        if (Rml::Element* element = m_SystemOverlayDocument->GetElementById(id))
            element->SetInnerRML(EscapeRmlText(text));
    };
    setText("system-title", m_SystemOverlay.title);
    setText("system-detail", m_SystemOverlay.detail);
    setText("system-primary", m_SystemOverlay.primaryHint);
    setText("system-secondary", m_SystemOverlay.secondaryHint);
    if (Rml::Element* progress = m_SystemOverlayDocument->GetElementById("system-progress")) {
        const float value = std::clamp(m_SystemOverlay.progress, 0.0f, 1.0f) * 100.0f;
        progress->SetProperty("width", std::to_string(value) + "%");
        progress->SetProperty("background-color", m_SystemOverlay.error ? "#e56565" : AccentColor(m_Accessibility));
    }
}

void UISystem::SetSystemOverlay(UISystemOverlayState state) {
    m_SystemOverlay = std::move(state);
    ApplySystemOverlay();
}

void UISystem::CreateRuntimeScreenDocument() {
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context)
        return;
    if (m_RuntimeScreenDocument)
        m_RuntimeScreenDocument->Close();
    m_RuntimeScreenDocument = nullptr;
    m_RuntimeScreenUsingCustomDocument = false;
    m_Diagnostics.projectRuntimeScreenActive = false;
    m_Diagnostics.runtimeScreenDocument = "generated://runtime-screen.rml";
    static const char* source = R"(<rml><head><title>Runtime screen</title><style>
body { margin:0; width:100%; height:100%; font-family:LatoLatin; color:#f5f7fa; }
#runtime-root { position:absolute; left:0; top:0; width:100%; height:100%;
 background-color:rgba(5,8,14,190); z-index:2147470000; }
#runtime-card { position:absolute; left:30%; top:12%; width:40%; padding:20px;
 background-color:rgba(20,27,39,245); border:2px #52647d; }
#runtime-title { text-align:center; font-size:30px; margin-bottom:12px; }
.runtime-action { display:block; margin:3px; padding:7px; text-align:center;
 font-size:18px; background-color:#273244; border:2px #273244; }
.runtime-action.focused { background-color:#315d84; border-color:#8fd0ff; }
#runtime-input-hint { text-align:center; margin-top:10px; color:#9fb0c6; font-size:14px; }
</style></head><body><div id="runtime-root"><div id="runtime-card">
<div id="runtime-title"></div><div id="runtime-actions"></div><div id="runtime-input-hint"></div>
</div></div></body></rml>)";
    m_RuntimeScreenDocument = context->LoadDocumentFromMemory(source, "generated://runtime-screen.rml");
    if (m_RuntimeScreenDocument) {
        m_RuntimeScreenDocument->Show();
        ApplyRuntimeScreen();
    }
}

void UISystem::ApplyRuntimeScreen() {
    if (!m_RuntimeScreenDocument)
        return;
    Rml::Element* root = m_RuntimeScreenDocument->GetElementById("runtime-root");
    if (root)
        root->SetProperty("display", m_RuntimeScreen.stableName.empty() ? "none" : "block");
    if (Rml::Element* card = m_RuntimeScreenDocument->GetElementById("runtime-card")) {
        const int safeLeft = static_cast<int>(std::round(m_Width * m_SafeArea.left));
        const int safeTop = static_cast<int>(std::round(m_Height * m_SafeArea.top));
        const int cardWidth = m_Diagnostics.narrowLayout ? std::max(180, m_Diagnostics.safeWidth - 24)
                                                         : std::max(240, m_Diagnostics.safeWidth * 3 / 5);
        card->SetProperty("left", std::to_string(safeLeft + (m_Diagnostics.safeWidth - cardWidth) / 2) + "px");
        card->SetProperty("top", std::to_string(safeTop + std::max(8, m_Diagnostics.safeHeight / 12)) + "px");
        card->SetProperty("width", std::to_string(cardWidth) + "px");
        card->SetProperty("border-color", m_Accessibility.highContrast ? "#ffffff" : "#52647d");
    }
    if (Rml::Element* title = m_RuntimeScreenDocument->GetElementById("runtime-title"))
        title->SetInnerRML(EscapeRmlText(m_RuntimeScreen.title));
    if (m_RuntimeScreenUsingCustomDocument) {
        for (size_t index = 0; index < m_RuntimeScreen.actions.size(); ++index) {
            const auto& action = m_RuntimeScreen.actions[index];
            if (Rml::Element* element =
                    m_RuntimeScreenDocument->GetElementById("runtime-action-" + action.stableName)) {
                element->SetInnerRML(EscapeRmlText(action.label));
                element->SetClass("focused", index == m_RuntimeScreen.focusedIndex);
                if (index == m_RuntimeScreen.focusedIndex)
                    element->SetProperty("border-color", AccentColor(m_Accessibility));
            }
        }
    } else if (Rml::Element* actions = m_RuntimeScreenDocument->GetElementById("runtime-actions")) {
        std::string rml;
        for (size_t index = 0; index < m_RuntimeScreen.actions.size(); ++index) {
            rml += "<div id=\"runtime-action-" + std::to_string(index) + "\" class=\"runtime-action";
            if (index == m_RuntimeScreen.focusedIndex)
                rml += " focused";
            rml += "\">" + EscapeRmlText(m_RuntimeScreen.actions[index].label) + "</div>";
        }
        actions->SetInnerRML(rml);
        if (Rml::Element* focused = m_RuntimeScreenDocument->GetElementById(
                "runtime-action-" + std::to_string(m_RuntimeScreen.focusedIndex))) {
            focused->SetProperty("border-color", AccentColor(m_Accessibility));
            if (m_Accessibility.highContrast)
                focused->SetProperty("background-color", "#000000");
        }
    }
    if (Rml::Element* hint = m_RuntimeScreenDocument->GetElementById("runtime-input-hint")) {
        const bool gamepad = Input::GetLastActiveDevice() == InputDeviceKind::Gamepad;
        const nlohmann::json select = nlohmann::json::parse(
            Input::GetSourceGlyphJson(gamepad ? "Gamepad/South" : "Keyboard/Enter"), nullptr, false);
        const nlohmann::json back = nlohmann::json::parse(
            Input::GetSourceGlyphJson(gamepad ? "Gamepad/East" : "Keyboard/Escape"), nullptr, false);
        const std::string selectLabel = select.value("label", gamepad ? "South" : "Enter");
        const std::string backLabel = back.value("label", gamepad ? "East" : "Esc");
        hint->SetInnerRML(EscapeRmlText("Select: " + selectLabel + "   Back: " + backLabel));
    }
}

void UISystem::SetRuntimeScreen(RuntimeUIScreenView view) {
    const bool documentChanged = view.documentPath != m_RuntimeScreen.documentPath;
    m_RuntimeScreen = std::move(view);
    if (m_Initialized && documentChanged) {
        if (m_RuntimeScreenDocument) {
            m_RuntimeScreenDocument->Close();
            m_RuntimeScreenDocument = nullptr;
        }
        m_RuntimeScreenUsingCustomDocument = false;
        Rml::Context* context = m_ContextManager.GetContext();
        if (context && !m_RuntimeScreen.documentPath.empty()) {
            Rml::ElementDocument* candidate = context->LoadDocument(m_RuntimeScreen.documentPath);
            bool valid =
                candidate && candidate->GetElementById("runtime-root") && candidate->GetElementById("runtime-title");
            for (const auto& action : m_RuntimeScreen.actions)
                valid = valid && candidate && candidate->GetElementById("runtime-action-" + action.stableName);
            if (valid) {
                m_RuntimeScreenDocument = candidate;
                m_RuntimeScreenUsingCustomDocument = true;
                m_Diagnostics.projectRuntimeScreenActive = true;
                m_Diagnostics.runtimeScreenDocument = m_RuntimeScreen.documentPath;
                candidate->Show();
            } else {
                if (candidate)
                    candidate->Close();
                ++m_Diagnostics.runtimeScreenFallbacks;
                Logger::Warn("[RuntimeUI] Project screen contract invalid; using standard fallback: ",
                             m_RuntimeScreen.documentPath);
            }
        }
        if (!m_RuntimeScreenDocument)
            CreateRuntimeScreenDocument();
    }
    ApplyRuntimeScreen();
}

bool UISystem::ProcessRuntimeScreenPointer(Event& event, const UIInputViewport& viewport, size_t& outIndex,
                                           bool& outActivate) {
    outIndex = 0;
    outActivate = false;
    if (!m_Initialized || !m_RuntimeScreenDocument || m_RuntimeScreen.stableName.empty() || !viewport.enabled ||
        !viewport.hovered || (event.type != EventType::MouseMove && event.type != EventType::MouseButtonUp))
        return false;
    const int windowX = event.type == EventType::MouseMove ? event.mouseMove.x : event.mouseButton.x;
    const int windowY = event.type == EventType::MouseMove ? event.mouseMove.y : event.mouseButton.y;
    if (windowX < viewport.x || windowY < viewport.y || windowX >= viewport.x + viewport.width ||
        windowY >= viewport.y + viewport.height)
        return false;
    const float x = static_cast<float>(windowX - viewport.x) * viewport.scaleX;
    const float y = static_cast<float>(windowY - viewport.y) * viewport.scaleY;
    for (size_t index = 0; index < m_RuntimeScreen.actions.size(); ++index) {
        Rml::Element* element = m_RuntimeScreenDocument->GetElementById(
            "runtime-action-" +
            (m_RuntimeScreenUsingCustomDocument ? m_RuntimeScreen.actions[index].stableName : std::to_string(index)));
        if (!element || !element->IsVisible(true) || !element->IsPointWithinElement(Rml::Vector2f(x, y)))
            continue;
        outIndex = index;
        outActivate = event.type == EventType::MouseButtonUp && event.mouseButton.button == 1;
        event.handled = true;
        return true;
    }
    // Generated standard screens remain operable before Rml has usable font
    // metrics (for example the first frame or headless tests). These constants
    // mirror the centered card and compact action rows in the generated RCSS.
    const float contextWidth = static_cast<float>(viewport.width) * viewport.scaleX;
    const float contextHeight = static_cast<float>(viewport.height) * viewport.scaleY;
    const float actionLeft = contextWidth * 0.30f + 20.0f;
    const float actionRight = contextWidth * 0.70f - 20.0f;
    const float actionTop = contextHeight * 0.20f;
    const float actionRowHeight = 40.0f;
    if (x >= actionLeft && x <= actionRight && y >= actionTop) {
        const size_t index = static_cast<size_t>((y - actionTop) / actionRowHeight);
        if (index < m_RuntimeScreen.actions.size()) {
            outIndex = index;
            outActivate = event.type == EventType::MouseButtonUp && event.mouseButton.button == 1;
            event.handled = true;
            return true;
        }
    }
    return false;
}

bool UISystem::ProcessEvent(Event& event) {
    if (!m_Initialized || event.handled)
        return false;
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context)
        return false;
    const bool consumed = m_InputAdapter.ProcessEvent(*context, event);
    if (consumed)
        event.handled = true;
    return consumed;
}

bool UISystem::ProcessEvent(Scene& scene, Event& event, const UIInputViewport& viewport) {
    if (!m_Initialized || event.handled)
        return false;
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context)
        return false;
    if (!HasInputEnabledCanvas(scene))
        return false;
    EnsureCanvasDocuments(scene);

    Event localEvent = event;
    bool insideViewport = true;
    if (event.type == EventType::MouseMove || event.type == EventType::MouseButtonDown ||
        event.type == EventType::MouseButtonUp) {
        const int x = event.type == EventType::MouseMove ? event.mouseMove.x : event.mouseButton.x;
        const int y = event.type == EventType::MouseMove ? event.mouseMove.y : event.mouseButton.y;
        insideViewport =
            x >= viewport.x && y >= viewport.y && x < viewport.x + viewport.width && y < viewport.y + viewport.height;
        if (insideViewport) {
            const float sx = viewport.scaleX != 0.0f ? viewport.scaleX : 1.0f;
            const float sy = viewport.scaleY != 0.0f ? viewport.scaleY : 1.0f;
            const int localX = static_cast<int>((x - viewport.x) * sx);
            const int localY = static_cast<int>((y - viewport.y) * sy);
            if (event.type == EventType::MouseMove) {
                localEvent.mouseMove.x = localX;
                localEvent.mouseMove.y = localY;
            } else {
                localEvent.mouseButton.x = localX;
                localEvent.mouseButton.y = localY;
            }
        }
    }

    const bool mouseEvent = event.type == EventType::MouseMove || event.type == EventType::MouseButtonDown ||
                            event.type == EventType::MouseButtonUp || event.type == EventType::MouseWheel;
    bool consumed = false;
    if (mouseEvent) {
        consumed = m_UIInputSystem.ProcessEvent(scene, *context, event, viewport, *GetActiveEventBridge(),
                                                &UISystem::ResolveDataModel, this);
        if (consumed) {
            event.handled = true;
            return true;
        }
    }
    if (viewport.enabled && viewport.hovered && insideViewport) {
        const bool rmlConsumed = m_InputAdapter.ProcessEvent(*context, localEvent);
        consumed = rmlConsumed;
    }
    if (consumed)
        event.handled = true;
    return consumed;
}

void UISystem::CollectDrawData(Scene& scene, UIDrawList& drawList) {
    if (!m_Initialized) {
        drawList.Clear();
        return;
    }
    EnsureCanvasDocuments(scene);
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context) {
        drawList.Clear();
        return;
    }
    m_RenderInterface.BeginFrame(drawList);
    context->Render();
    m_RenderInterface.EndFrame();
}

UIDataModel& UISystem::CreateDataModel(const std::string& name) {
    return m_DataModels[name];
}

void UISystem::MarkActorTreeDirty(uint64_t canvasActorID) {
    m_ActorTreeSignatures.erase(canvasActorID);
}

UIEventBridge* UISystem::GetActiveEventBridge() {
    return m_ExternalEventBridge ? m_ExternalEventBridge : &m_EventBridge;
}

UIDataModel* UISystem::ResolveDataModel(void* user, const std::string& name) {
    auto* self = static_cast<UISystem*>(user);
    return self ? &self->CreateDataModel(name) : nullptr;
}
