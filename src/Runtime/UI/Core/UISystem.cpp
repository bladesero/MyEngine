#include "UI/Core/UISystem.h"

#include "Assets/AssetManager.h"
#include "Core/Event.h"
#include "Core/Logger.h"
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
#include <sstream>
#include <vector>

namespace {

bool g_RmlCoreInitialized = false;
std::unordered_map<std::string, std::vector<unsigned char>> g_RmlFontData;

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string EscapeRmlText(std::string value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

std::string DataModelValueToText(const UIDataModel::Value& value)
{
    return std::visit([](const auto& item) -> std::string {
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
            return std::to_string(item.x) + ", " + std::to_string(item.y) + ", " +
                std::to_string(item.z);
        } else {
            return item.dump();
        }
    }, value);
}

bool TryGetFloat(const UIDataModel& model, const char* key, float& value)
{
    const auto found = model.GetValues().find(key);
    if (found == model.GetValues().end()) return false;
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

bool TryParseFontFaceFromPath(const std::string& path,
                              std::string& family,
                              Rml::Style::FontWeight& weight)
{
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

bool ReadBinaryFile(const std::string& path, std::vector<unsigned char>& bytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), size));
}

bool HasInputEnabledCanvas(Scene& scene)
{
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive()) return;
        auto* canvas = actor.GetComponent<UICanvasComponent>();
        if (!canvas || !canvas->IsEnabled()) return;
        found = canvas->IsVisible() && canvas->IsInteractive() &&
            canvas->GetInputMode() != UIInputMode::None;
    });
    return found;
}

} // namespace

UISystem::UISystem() = default;

UISystem::~UISystem()
{
    Shutdown();
}

bool UISystem::Initialize(IRHIDevice* device, IRHIFrameContext* frameContext)
{
    if (m_Initialized) return true;
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
    AngelScriptRuntime::SetUIEventBridge(GetActiveEventBridge());
    AngelScriptRuntime::SetUISystem(this);
    return true;
}

void UISystem::Shutdown()
{
    if (!m_Initialized) return;
    AngelScriptRuntime::ClearUIEventBridge(GetActiveEventBridge());
    AngelScriptRuntime::ClearUISystem(this);
    m_ContextManager.Destroy();
    m_LoadedFonts.clear();
    m_ActorTreeSignatures.clear();
    m_ExternalEventBridge = nullptr;
    m_Initialized = false;
    m_Device = nullptr;
    m_FrameContext = nullptr;
}

void UISystem::Resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    m_Width = width;
    m_Height = height;
    m_ContextManager.Resize(width, height);
}

void UISystem::LoadCanvasFonts(Scene& scene)
{
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* canvas = actor.GetComponent<UICanvasComponent>();
        if (!canvas || !canvas->IsEnabled()) return;
        for (const std::string& fontPath : canvas->GetDefaultFontPaths()) {
            if (fontPath.empty() || m_LoadedFonts.count(fontPath) != 0) continue;
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
            } else {
                Logger::Warn("[UI] Failed to load font: ", fontPath);
            }
        }
    });
}

void UISystem::EnsureCanvasDocuments(Scene& scene)
{
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context) return;
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* component = actor.GetComponent<UICanvasComponent>();
        if (!component || !component->IsEnabled()) return;
        UICanvas& canvas = component->GetCanvas();
        canvas.SetContext(context);
        if (component->GetSourceMode() == UICanvasSourceMode::ActorTree) {
            const std::size_t signature = UIActorTreeBuilder::ComputeSignature(actor);
            const auto found = m_ActorTreeSignatures.find(actor.GetID());
            if (!canvas.GetDocument() || found == m_ActorTreeSignatures.end() ||
                found->second != signature) {
                std::string rml;
                std::string error;
                if (UIActorTreeBuilder::BuildDocument(actor, *component, rml, &error)) {
                    const std::string sourceURL = "generated://ui_actor_" +
                        std::to_string(actor.GetID()) + ".rml";
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

void UISystem::ApplyDataModels(Scene& scene)
{
    if (m_DataModels.empty()) return;

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* component = actor.GetComponent<UICanvasComponent>();
        if (!component || !component->IsEnabled() || !component->IsVisible()) return;
        Rml::ElementDocument* document = component->GetCanvas().GetDocument();
        if (!document) return;

        for (const auto& [modelName, model] : m_DataModels) {
            (void)modelName;
            for (const auto& [key, value] : model.GetValues()) {
                Rml::Element* element = document->GetElementById(key);
                if (!element) continue;
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
            if (healthFill && TryGetFloat(model, "health", health) &&
                TryGetFloat(model, "maxHealth", maxHealth) && maxHealth > 0.0f) {
                const float percent = std::clamp(health / maxHealth, 0.0f, 1.0f) * 100.0f;
                healthFill->SetProperty("width", std::to_string(percent) + "%");
            }
        }
    });
}

void UISystem::Update(Scene& scene, float dt)
{
    (void)dt;
    if (!m_Initialized) return;
    LoadCanvasFonts(scene);
    EnsureCanvasDocuments(scene);
    ApplyDataModels(scene);
    if (Rml::Context* context = m_ContextManager.GetContext()) {
        context->Update();
    }
}

bool UISystem::ProcessEvent(Event& event)
{
    if (!m_Initialized || event.handled) return false;
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context) return false;
    const bool consumed = m_InputAdapter.ProcessEvent(*context, event);
    if (consumed) event.handled = true;
    return consumed;
}

bool UISystem::ProcessEvent(Scene& scene, Event& event, const UIInputViewport& viewport)
{
    if (!m_Initialized || event.handled) return false;
    Rml::Context* context = m_ContextManager.GetContext();
    if (!context) return false;
    if (!HasInputEnabledCanvas(scene)) return false;
    EnsureCanvasDocuments(scene);

    Event localEvent = event;
    bool insideViewport = true;
    if (event.type == EventType::MouseMove ||
        event.type == EventType::MouseButtonDown ||
        event.type == EventType::MouseButtonUp) {
        const int x = event.type == EventType::MouseMove ? event.mouseMove.x : event.mouseButton.x;
        const int y = event.type == EventType::MouseMove ? event.mouseMove.y : event.mouseButton.y;
        insideViewport = x >= viewport.x && y >= viewport.y &&
            x < viewport.x + viewport.width && y < viewport.y + viewport.height;
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

    const bool mouseEvent = event.type == EventType::MouseMove ||
        event.type == EventType::MouseButtonDown ||
        event.type == EventType::MouseButtonUp ||
        event.type == EventType::MouseWheel;
    bool consumed = false;
    if (mouseEvent) {
        consumed = m_UIInputSystem.ProcessEvent(
            scene, *context, event, viewport, *GetActiveEventBridge(), &UISystem::ResolveDataModel, this);
        if (consumed) {
            event.handled = true;
            return true;
        }
    }
    if (viewport.enabled && viewport.hovered && insideViewport) {
        const bool rmlConsumed = m_InputAdapter.ProcessEvent(*context, localEvent);
        consumed = rmlConsumed;
    }
    if (consumed) event.handled = true;
    return consumed;
}

void UISystem::CollectDrawData(Scene& scene, UIDrawList& drawList)
{
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

UIDataModel& UISystem::CreateDataModel(const std::string& name)
{
    return m_DataModels[name];
}

void UISystem::MarkActorTreeDirty(uint64_t canvasActorID)
{
    m_ActorTreeSignatures.erase(canvasActorID);
}

UIEventBridge* UISystem::GetActiveEventBridge()
{
    return m_ExternalEventBridge ? m_ExternalEventBridge : &m_EventBridge;
}

UIDataModel* UISystem::ResolveDataModel(void* user, const std::string& name)
{
    auto* self = static_cast<UISystem*>(user);
    return self ? &self->CreateDataModel(name) : nullptr;
}
