#include "UI/Core/UISystem.h"

#include "Assets/AssetManager.h"
#include "Core/Event.h"
#include "Core/Logger.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "UI/Core/UIActorTreeBuilder.h"
#include "UI/Core/UICanvasComponent.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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
    return true;
}

void UISystem::Shutdown()
{
    if (!m_Initialized) return;
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

void UISystem::Update(Scene& scene, float dt)
{
    (void)dt;
    if (!m_Initialized) return;
    LoadCanvasFonts(scene);
    EnsureCanvasDocuments(scene);
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
