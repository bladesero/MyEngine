#include "Editor/EditorLayer.h"

#include "Scene/Scene.h"
#include "Scene/Actor.h"
#include "Core/Logger.h"
#include "Renderer/D3D11Context.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_sdl3.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif
#endif

#if defined(MYENGINE_ENABLE_IMGUI)
class EditorLayer::ImGuiPlatformEventBridge : public IPlatformEventBridge {
public:
    void OnSDLEvent(const SDL_Event& event) override {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
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
    IRenderContext* rc = m_SceneLayer->GetRenderContext();
    auto* d3d = dynamic_cast<D3D11Context*>(rc);
    if (!d3d) {
        Logger::Error("[Editor] ImGui requires D3D11Context");
        return;
    }
    if (!m_Window->GetSDLWindow()) {
        Logger::Error("[Editor] ImGui requires SDLWindow (SDL_Window*)");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Hook raw SDL events for ImGui through platform bridge.
    if (m_Engine) {
        m_PlatformBridge = std::make_unique<ImGuiPlatformEventBridge>();
        m_Engine->SetPlatformEventBridge(m_PlatformBridge.get());
    }

    ImGui_ImplSDL3_InitForD3D(m_Window->GetSDLWindow());
    ImGui_ImplDX11_Init(d3d->GetDevice(), d3d->GetDeviceContext());

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
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_ImGuiReady = false;
    }
#endif
}

void EditorLayer::OnRender()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_SceneLayer || !m_ImGuiReady) return;
    IRenderContext* rc = m_SceneLayer->GetRenderContext();
    auto* d3d = dynamic_cast<D3D11Context*>(rc);
    if (!d3d) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    DrawToolbar();
    DrawSceneOutliner();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Present AFTER UI overlay (SceneRenderLayer rendered with present disabled).
    rc->EndFrame();
#endif
}

#if defined(MYENGINE_ENABLE_IMGUI) && defined(_WIN32)
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
#endif

void EditorLayer::DrawToolbar()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 40.0f));

    ImGui::Begin("##Toolbar", nullptr, flags);

    if (ImGui::Button("New Scene")) {
        m_SceneLayer->NewScene("Untitled");
        m_Selected = nullptr;
    }
    ImGui::SameLine();

    if (ImGui::Button("Open Scene")) {
#if defined(_WIN32)
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
    ImGui::Begin("Scene Outliner");

    Scene& scene = m_SceneLayer->GetScene();
    auto   roots = scene.GetRootActors();

    for (Actor* actor : roots) {
        DrawActorNode(actor);
    }

    ImGui::End();
#endif
}

