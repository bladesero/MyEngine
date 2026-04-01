#include "Editor/EditorLayer.h"

#include "Scene/Scene.h"
#include "Scene/Actor.h"
#include "Core/Logger.h"
#include "Core/Platform.h"

#if defined(MYENGINE_ENABLE_IMGUI) && defined(MYENGINE_PLATFORM_WINDOWS)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <commdlg.h>
#endif

#if defined(MYENGINE_ENABLE_IMGUI)
namespace
{
    constexpr float kToolbarHeight      = 40.0f;
    constexpr float kOutlinerPanelWidth = 280.0f;
    constexpr float kInspectorWidth     = 320.0f;

    bool DrawVec3Editor(const char* label, Vec3& value, float speed)
    {
        float values[3] = { value.x, value.y, value.z };
        if (!ImGui::DragFloat3(label, values, speed)) {
            return false;
        }
        value = Vec3{ values[0], values[1], values[2] };
        return true;
    }
}

class EditorLayer::ImGuiPlatformEventBridge : public IPlatformEventBridge {
public:
    explicit ImGuiPlatformEventBridge(IRenderContext* renderContext)
        : m_RenderContext(renderContext) {}

    void OnSDLEvent(const SDL_Event& event) override {
        if (m_RenderContext) {
            m_RenderContext->ProcessImGuiSDLEvent(event);
        }
    }

private:
    IRenderContext* m_RenderContext = nullptr;
};
#endif

EditorLayer::EditorLayer(SceneRenderLayer* sceneLayer, IWindow* window, Engine* engine)
    : Layer("EditorLayer")
    , m_SceneLayer(sceneLayer)
    , m_Window(window)
    , m_Engine(engine)
{}

void EditorLayer::OnAttach()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Window || !m_SceneLayer) return;
    m_RenderContext = m_SceneLayer->GetRenderContext();
    if (!m_RenderContext) {
        Logger::Error("[Editor] Missing render context");
        return;
    }
    if (!m_Window->GetSDLWindow()) {
        Logger::Error("[Editor] ImGui requires SDLWindow (SDL_Window*)");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    if (!m_RenderContext->InitImGui(m_Window)) {
        Logger::Error("[Editor] Failed to initialize ImGui backend from render context");
        ImGui::DestroyContext();
        return;
    }

    // Hook raw SDL events for ImGui through platform bridge.
    if (m_Engine) {
        m_PlatformBridge = std::make_unique<ImGuiPlatformEventBridge>(m_RenderContext);
        m_Engine->SetPlatformEventBridge(m_PlatformBridge.get());
    }

    m_ImGuiReady = true;
#endif
}

void EditorLayer::OnDetach()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_Engine) {
        m_Engine->SetPlatformEventBridge(nullptr);
    }
    m_PlatformBridge.reset();
    if (m_ImGuiReady) {
        if (m_RenderContext) {
            m_RenderContext->ShutdownImGui();
        }
        ImGui::DestroyContext();
        m_ImGuiReady = false;
    }
    m_RenderContext = nullptr;
#endif
}

void EditorLayer::OnRender()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_SceneLayer || !m_ImGuiReady || !m_RenderContext) return;

    m_RenderContext->BeginImGuiFrame();

    ImGui::NewFrame();

    DrawToolbar();
    DrawSceneOutliner();
    DrawInspector();

    ImGui::Render();

    m_RenderContext->RenderImGuiDrawData(ImGui::GetDrawData());

    // Present AFTER UI overlay (SceneRenderLayer rendered with present disabled).
    m_RenderContext->EndFrame();
#endif
}

#if defined(MYENGINE_ENABLE_IMGUI) && defined(MYENGINE_PLATFORM_WINDOWS)
static std::string OpenSceneFileDialogWin32()
{
    char fileName[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = nullptr;
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = "Scene (*.json)\0*.json\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(fileName);
    }
    return {};
}

static std::string SaveSceneFileDialogWin32()
{
    char fileName[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = nullptr;
    ofn.lpstrFile    = fileName;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrFilter  = "Scene (*.json)\0*.json\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt  = "json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn) == TRUE) {
        return std::string(fileName);
    }
    return {};
}
#endif

void EditorLayer::DrawToolbar()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kToolbarHeight));

    ImGui::Begin("##Toolbar", nullptr, flags);

    if (ImGui::Button("New Scene")) {
        m_SceneLayer->NewScene("Untitled");
        m_Selected = nullptr;
    }
    ImGui::SameLine();

    if (ImGui::Button("Open Scene")) {
#if defined(MYENGINE_PLATFORM_WINDOWS)
        std::string path = OpenSceneFileDialogWin32();
        if (!path.empty()) {
            if (!SceneSerializer::LoadFromFile(m_SceneLayer->GetScene(), path)) {
                Logger::Error("[Editor] Failed to open scene: ", path);
            } else {
                m_Selected = nullptr;
            }
        }
#else
        const char* path = "scene.json";
        if (!SceneSerializer::LoadFromFile(m_SceneLayer->GetScene(), path)) {
            Logger::Error("[Editor] Failed to open scene: ", path);
        } else {
            m_Selected = nullptr;
        }
#endif
    }
    ImGui::SameLine();

    if (ImGui::Button("Save Scene")) {
#if defined(_WIN32)
        std::string path = SaveSceneFileDialogWin32();
        if (!path.empty()) {
            if (!SceneSerializer::SaveToFile(m_SceneLayer->GetScene(), path)) {
                Logger::Error("[Editor] Failed to save scene: ", path);
            }
        }
#else
        const char* path = "scene.json";
        if (!SceneSerializer::SaveToFile(m_SceneLayer->GetScene(), path)) {
            Logger::Error("[Editor] Failed to save scene: ", path);
        }
#endif
    }

    ImGui::End();
#endif
}

void EditorLayer::DrawActorNode(Actor* actor)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!actor) return;

    const bool selected = (m_Selected == actor);
    const bool hasChildren = !actor->GetChildren().empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanFullWidth;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected)     flags |= ImGuiTreeNodeFlags_Selected;

    const bool open = ImGui::TreeNodeEx((void*)actor, flags, "%s", actor->GetName().c_str());
    if (ImGui::IsItemClicked()) {
        m_Selected = actor;
    }

    if (open && hasChildren) {
        for (Actor* child : actor->GetChildren()) {
            DrawActorNode(child);
        }
        ImGui::TreePop();
    }
#endif
}

void EditorLayer::DrawSceneOutliner()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + kToolbarHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kOutlinerPanelWidth, vp->WorkSize.y - kToolbarHeight), ImGuiCond_FirstUseEver);

    ImGui::Begin("Scene Outliner");

    Scene& scene = m_SceneLayer->GetScene();
    auto   roots = scene.GetRootActors();

    for (Actor* actor : roots) {
        DrawActorNode(actor);
    }

    ImGui::End();
#endif
}

void EditorLayer::DrawInspector()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x - kInspectorWidth, vp->WorkPos.y + kToolbarHeight),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kInspectorWidth, vp->WorkSize.y - kToolbarHeight), ImGuiCond_FirstUseEver);

    ImGui::Begin("Inspector");

    if (!m_Selected) {
        ImGui::TextDisabled("Select an actor to edit its transform.");
        ImGui::End();
        return;
    }

    ImGui::Text("Actor: %s", m_Selected->GetName().c_str());
    ImGui::Text("ID: %llu", static_cast<unsigned long long>(m_Selected->GetID()));
    ImGui::Separator();
    ImGui::TextUnformatted("Transform");

    Transform& transform = m_Selected->GetTransform();
    bool changed = false;
    changed |= DrawVec3Editor("Position", transform.position, 0.05f);
    changed |= DrawVec3Editor("Rotation", transform.rotation, 0.2f);
    changed |= DrawVec3Editor("Scale", transform.scale, 0.05f);

    if (changed) {
        m_SceneLayer->MarkDirty();
    }

    ImGui::End();
#endif
}
