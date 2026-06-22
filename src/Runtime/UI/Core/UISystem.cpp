#include "UI/Core/UISystem.h"

#include "Assets/AssetManager.h"
#include "Core/Event.h"
#include "Core/Logger.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "UI/Core/UICanvasComponent.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>

#include <algorithm>
#include <vector>

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
    if (!Rml::Initialise()) {
        Logger::Error("[UI] RmlUi initialization failed");
        return false;
    }
    if (!m_ContextManager.Create("MyEngineInGameUI", m_Width, m_Height)) {
        Logger::Error("[UI] Failed to create RmlUi context");
        Rml::Shutdown();
        return false;
    }
    m_Initialized = true;
    return true;
}

void UISystem::Shutdown()
{
    if (!m_Initialized) return;
    m_ContextManager.Destroy();
    Rml::Shutdown();
    m_LoadedFonts.clear();
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
            if (Rml::LoadFontFace(resolved)) {
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
        if (!canvas.GetDocument() && !canvas.GetDocumentPath().empty()) {
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
