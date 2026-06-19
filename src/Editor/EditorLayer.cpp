#include "Editor/EditorLayer.h"
#include "Core/PlatformEventBridge.h"
#include "Editor/EditorImGuiBackend.h"

#include "Assets/AssetManager.h"
#include "Core/Engine.h"
#include "Core/Logger.h"
#include "Core/Window.h"
#include "Editor/EditorPanel.h"
#include "Editor/EditorPanels.h"
#include "Editor/ProjectPublisher.h"
#include "Game/SceneRenderLayer.h"

#include <SDL3/SDL.h>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <ImGuizmo.h>
#include <imgui.h>
#endif

#include <algorithm>
#include <cstring>

namespace {
class EditorImGuiEventBridge final : public IPlatformEventBridge {
public:
    explicit EditorImGuiEventBridge(class EditorImGuiBackend* backend) : m_Backend(backend) {}
    void OnSDLEvent(const SDL_Event& event) override {
        if (m_Backend) m_Backend->ProcessSDLEvent(event);
    }
private:
    class EditorImGuiBackend* m_Backend = nullptr;
};
}




EditorLayer::EditorLayer(SceneRenderLayer* sceneLayer, IWindow* window, Engine* engine,
                         std::filesystem::path initialProject)
    : Layer("EditorLayer")
    , m_SceneLayer(sceneLayer)
    , m_Window(window)
    , m_Engine(engine)
    , m_InitialProject(std::move(initialProject)) {
    std::strncpy(m_ProjectName.data(), "NewProject", m_ProjectName.size() - 1);
}

void EditorLayer::OnAttach() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Window || !m_SceneLayer) return;
    m_RenderContext = m_SceneLayer->GetRenderContext();
    if (!m_RenderContext || !m_Window->GetSDLWindow()) {
        Logger::Error("[Editor] Missing window or render context");
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
    m_ImGuiBackend = std::make_unique<EditorImGuiBackend>(m_RenderContext, m_Window);
    if (!m_ImGuiBackend->Init()) {
        ImGui::DestroyContext();
        return;
    }
    m_Context = EditorContext(m_SceneLayer, m_RenderContext, m_Window, m_Engine);
    m_Context.SetImGuiBackend(m_ImGuiBackend.get());
    if (m_Engine) {
        m_ImGuiEventBridge = std::make_unique<EditorImGuiEventBridge>(m_ImGuiBackend.get());
        m_Engine->SetPlatformEventBridge(m_ImGuiEventBridge.get());
    }
    m_Context.SetCommandStack(&m_CommandStack);
    m_Context.SetAssetRegistry(&m_AssetRegistry);
    m_Context.SetProject(&m_Project);
    std::string workspaceError;
    if (!m_Workspace.Load(&workspaceError)) Logger::Warn("[Editor] ", workspaceError);
    m_ImGuiReady = true;

    if (!m_InitialProject.empty() && !OpenProject(m_InitialProject)) {
        Logger::Error("[Editor] ", m_ProjectError);
    }
#endif
}

bool EditorLayer::OpenProject(const std::filesystem::path& root) {
    if (m_ProjectOpen) return false;
    if (!m_Project.Open(root, false)) {
        m_ProjectError = m_Project.GetLastError().empty()
            ? "Failed to open project" : m_Project.GetLastError();
        return false;
    }
    AssetManager::Get().Clear();
    AssetManager::Get().SetProjectRoot(m_Project.GetRoot());
    m_Context.SetProjectRoot(m_Project.GetRoot());
    m_AssetRegistry.SetRoot(m_Project.GetContentRoot());
    m_AssetRegistry.Refresh();
    RegisterServices();
    RegisterPanels();
    m_ProjectOpen = true;
    m_ProjectError.clear();
    m_Workspace.AddRecentProject(m_Project.GetRoot());
    std::string workspaceError;
    if (!m_Workspace.Save(&workspaceError)) Logger::Warn("[Editor] ", workspaceError);

    std::filesystem::path scenePath;
    const auto& lastScene = m_Project.GetLastScenePath();
    if (!lastScene.empty() && std::filesystem::is_regular_file(lastScene)) {
        scenePath = lastScene;
    } else {
        std::string error;
        m_Project.GetConfig().ResolveStartupScene(scenePath, &error);
    }
    if (!scenePath.empty() && m_SceneLayer->LoadScene(scenePath.string())) {
        m_CommandStack.Clear();
        m_Context.GetSelection().Clear();
    }
    Logger::Info("[Editor] Opened project: ", m_Project.GetRoot().string());
    return true;
}

void EditorLayer::RegisterServices() {
    if (m_ServicesRegistered) return;
    m_ServiceCollection.Add(m_LogService);
    m_ServiceCollection.Add(m_DialogService);
    m_ServiceCollection.Add(m_ImportService);
    m_ServiceCollection.Add(m_ShaderWatchService);
    m_ServiceCollection.AttachAll(m_Context);
    m_ServicesRegistered = true;
}

void EditorLayer::RegisterPanels() {
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "scene.new", "New", [this](EditorContext&) { NewScene(); },
        [](EditorContext& context) { return context.IsEditing(); }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "scene.open", "Open", [this](EditorContext&) { OpenSceneDialog(); },
        [](EditorContext& context) { return context.IsEditing(); }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "scene.save", "Save", [this](EditorContext&) { SaveScene(); }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "asset.import", "Import", [this](EditorContext&) { ImportAssetDialog(); }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "project.settings", "Project Settings",
        [this](EditorContext&) { OpenProjectSettings(); },
        [](EditorContext& context) { return context.IsEditing(); }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "project.setStartup", "Set Startup",
        [this](EditorContext&) { SetStartupScene(); },
        [](EditorContext& context) {
            auto* layer = context.GetSceneLayer();
            return context.IsEditing() && layer && layer->HasFilePath() && !layer->IsDirty();
        }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "project.publish", "Publish",
        [this](EditorContext&) { PublishProject(); },
        [](EditorContext& context) { return context.IsEditing(); }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "edit.undo", "Undo",
        [](EditorContext& context) {
            if (auto* stack = context.GetCommandStack()) stack->Undo(context);
        },
        [](EditorContext& context) {
            auto* stack = context.GetCommandStack();
            return context.IsEditing() && stack && stack->CanUndo();
        }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "edit.redo", "Redo",
        [](EditorContext& context) {
            if (auto* stack = context.GetCommandStack()) stack->Redo(context);
        },
        [](EditorContext& context) {
            auto* stack = context.GetCommandStack();
            return context.IsEditing() && stack && stack->CanRedo();
        }));
    m_Context.SetActionRegistry(&m_ActionRegistry);

    auto gizmo = std::make_shared<EditorGizmoState>();
    m_Panels.push_back(std::make_unique<ToolbarPanel>());
    m_Panels.push_back(std::make_unique<ViewportPanel>(gizmo));
    m_Panels.push_back(std::make_unique<SceneHierarchyPanel>());
    m_Panels.push_back(std::make_unique<InspectorPanel>(gizmo));
    m_Panels.push_back(std::make_unique<LogPanel>());
    m_Panels.push_back(std::make_unique<AssetBrowserPanel>());
    const auto& state = m_Project.GetState();
    const bool visibility[] = {state.showToolbar, state.showViewport,
        state.showSceneHierarchy, state.showInspector, state.showLog,
        state.showAssetBrowser};
    for (size_t index = 0; index < m_Panels.size(); ++index) {
        m_Panels[index]->SetVisible(visibility[index]);
        m_Panels[index]->OnAttach(m_Context);
    }
}

void EditorLayer::OnDetach() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_ProjectOpen && m_Panels.size() == 6) {
        auto& state = m_Project.GetState();
        state.showToolbar = m_Panels[0]->IsVisible();
        state.showViewport = m_Panels[1]->IsVisible();
        state.showSceneHierarchy = m_Panels[2]->IsVisible();
        state.showInspector = m_Panels[3]->IsVisible();
        state.showLog = m_Panels[4]->IsVisible();
        state.showAssetBrowser = m_Panels[5]->IsVisible();
        if (m_SceneLayer && m_SceneLayer->HasFilePath())
            m_Project.SetLastScenePath(m_SceneLayer->GetSceneFilePath());
        m_Project.SaveState();
    }
    for (auto it = m_Panels.rbegin(); it != m_Panels.rend(); ++it) (*it)->OnDetach();
    m_Panels.clear();
    m_ActionRegistry.Clear();
    m_Context.SetActionRegistry(nullptr);
    if (m_ServicesRegistered) m_ServiceCollection.DetachAll(m_Context);
    if (m_Engine) m_Engine->SetPlatformEventBridge(nullptr);
    m_ImGuiEventBridge.reset();
    if (m_ImGuiReady) {
        m_ImGuiBackend.reset();
        ImGui::DestroyContext();
        m_ImGuiReady = false;
    }
    m_RenderContext = nullptr;
#endif
}

void EditorLayer::OnUpdate(float deltaSeconds) {
    if (m_ServicesRegistered) m_ServiceCollection.UpdateAll(deltaSeconds);
    for (auto& panel : m_Panels) panel->OnUpdate(deltaSeconds);
    ProcessDialogResults();
    if (m_ProjectOpen) {
        if (Scene* scene = m_Context.GetScene()) m_Context.GetSelection().Validate(*scene);
    }
}

void EditorLayer::DrawProjectSelector() {
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({720.0f, 520.0f}, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("MyEngine Project Selector", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("Open an existing project");
    ImGui::InputText("Project folder", m_ProjectPath.data(), m_ProjectPath.size());
    if (ImGui::Button("Browse...")) m_DialogService.RequestOpenProjectFolder(m_Window);
    ImGui::SameLine();
    if (ImGui::Button("Open") && m_ProjectPath[0] != '\0') OpenProject(m_ProjectPath.data());

    ImGui::SeparatorText("Recent projects");
    for (const auto& recent : m_Workspace.GetRecentProjects()) {
        const std::string label = recent.string();
        if (ImGui::Selectable(label.c_str())) OpenProject(recent);
    }

    ImGui::SeparatorText("Create project");
    ImGui::InputText("Project name", m_ProjectName.data(), m_ProjectName.size());
    ImGui::TextDisabled("The project is created at the Project folder path above.");
    if (ImGui::Button("Create") && m_ProjectPath[0] != '\0') {
        std::string error;
        if (m_Workspace.CreateProject(m_ProjectPath.data(), m_ProjectName.data(), &error)) {
            OpenProject(m_ProjectPath.data());
        } else {
            m_ProjectError = std::move(error);
        }
    }
    if (!m_ProjectError.empty()) {
        ImGui::Spacing();
        ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, "%s", m_ProjectError.c_str());
    }
    ImGui::End();
#endif
}

void EditorLayer::OpenProjectSettings() {
    if (!m_ProjectOpen) return;
    const auto& config = m_Project.GetConfig();
    std::strncpy(m_ProjectName.data(), config.GetName().c_str(), m_ProjectName.size() - 1);
    m_ProjectName.back() = '\0';
    std::strncpy(m_PublishOutput.data(),
                 config.GetPublishSettings().outputDirectory.c_str(),
                 m_PublishOutput.size() - 1);
    m_PublishOutput.back() = '\0';
    m_ProjectSettingsRequested = true;
}

void EditorLayer::ShowProjectResult(std::string message, bool error) {
    m_ProjectResult = std::move(message);
    m_ProjectResultIsError = error;
    m_ProjectResultRequested = true;
}

void EditorLayer::DrawProjectSettings() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_ProjectSettingsRequested) {
        ImGui::OpenPopup("Project Settings");
        m_ProjectSettingsRequested = false;
    }
    ImGui::SetNextWindowSize({620.0f, 0.0f}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Project Settings", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;

    const auto& config = m_Project.GetConfig();
    ImGui::InputText("Project name", m_ProjectName.data(), m_ProjectName.size());
    ImGui::InputText("Output directory", m_PublishOutput.data(), m_PublishOutput.size());
    ImGui::LabelText("Target", "%s", "windows-x64");
    ImGui::LabelText("Startup scene", "%s",
                     config.GetStartupScene().empty()
                         ? "<not set>" : config.GetStartupScene().c_str());
    ImGui::TextDisabled("Use Set Startup to assign the currently saved scene.");
    ImGui::Separator();
    if (ImGui::Button("Save")) {
        auto& editable = m_Project.GetConfig();
        const ProjectConfig previous = editable;
        editable.SetName(m_ProjectName.data());
        editable.GetPublishSettings().outputDirectory = m_PublishOutput.data();
        editable.GetPublishSettings().target = "windows-x64";
        std::string error;
        if (editable.Save(&error)) {
            Logger::Info("[Editor] Project settings saved");
            ImGui::CloseCurrentPopup();
            ShowProjectResult("Project settings saved.", false);
        } else {
            editable = previous;
            ShowProjectResult("Failed to save project settings: " + error, true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
#endif
}

void EditorLayer::DrawProjectResult() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_ProjectResultRequested) {
        ImGui::OpenPopup("Project Result");
        m_ProjectResultRequested = false;
    }
    ImGui::SetNextWindowSize({620.0f, 0.0f}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Project Result", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) return;
    const ImVec4 color = m_ProjectResultIsError
        ? ImVec4{1.0f, 0.35f, 0.3f, 1.0f}
        : ImVec4{0.45f, 0.9f, 0.55f, 1.0f};
    ImGui::PushTextWrapPos(580.0f);
    ImGui::TextColored(color, "%s", m_ProjectResult.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
#endif
}

void EditorLayer::OnRender() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiReady || !m_RenderContext) return;
    if (m_ImGuiBackend) m_ImGuiBackend->BeginFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    ImGuizmo::AllowAxisFlip(false);
    if (m_ProjectOpen) {
        for (auto& panel : m_Panels) panel->OnImGui();
        DrawProjectSettings();
        DrawProjectResult();
    } else {
        DrawProjectSelector();
    }
    ImGui::Render();
    if (m_ImGuiBackend) m_ImGuiBackend->RenderDrawData(ImGui::GetDrawData());
    m_RenderContext->EndFrame();
#endif
}

void EditorLayer::ProcessDialogResults() {
    EditorDialogResult result;
    if (!m_DialogService.ConsumeResult(result) || result.path.empty()) return;
    if (result.operation == EditorFileOperation::OpenProjectFolder) {
        std::strncpy(m_ProjectPath.data(), result.path.c_str(), m_ProjectPath.size() - 1);
        m_ProjectPath.back() = '\0';
        return;
    }
    if (!m_ProjectOpen) return;
    if (result.operation == EditorFileOperation::OpenScene) {
        if (m_SceneLayer->LoadScene(result.path)) {
            m_Context.GetSelection().Clear();
            m_CommandStack.Clear();
            m_Project.SetLastScenePath(result.path);
        } else Logger::Error("[Editor] Failed to open scene: ", result.path);
    } else if (result.operation == EditorFileOperation::SaveScene) {
        if (!m_SceneLayer->SaveSceneAs(result.path))
            Logger::Error("[Editor] Failed to save scene: ", result.path);
        else m_Project.SetLastScenePath(result.path);
    } else if (result.operation == EditorFileOperation::ImportAsset) {
        m_ImportService.Import(result.path);
    }
}

void EditorLayer::NewScene() {
    if (!m_Context.IsEditing()) return;
    m_SceneLayer->NewScene();
    m_Context.GetSelection().Clear();
    m_CommandStack.Clear();
}

void EditorLayer::OpenSceneDialog() {
    if (m_Context.IsEditing()) m_DialogService.RequestOpenScene(m_Window);
}

void EditorLayer::SaveScene() {
    if (!m_SceneLayer) return;
    if (m_SceneLayer->HasFilePath()) m_SceneLayer->SaveScene();
    else m_DialogService.RequestSaveScene(m_Window);
}

void EditorLayer::ImportAssetDialog() {
    m_DialogService.RequestImportAsset(m_Window);
}

void EditorLayer::SetStartupScene() {
    if (!m_SceneLayer || !m_SceneLayer->HasFilePath() || m_SceneLayer->IsDirty()) return;
    std::string error;
    auto& config = m_Project.GetConfig();
    if (!config.SetStartupScene(m_SceneLayer->GetSceneFilePath(), &error) ||
        !config.Save(&error)) {
        Logger::Error("[Editor] Failed to set startup scene: ", error);
        return;
    }
    Logger::Info("[Editor] Startup scene: ", config.GetStartupScene());
}

void EditorLayer::PublishProject() {
    if (!m_ProjectOpen) return;
    if (m_SceneLayer && m_SceneLayer->IsDirty()) {
        const std::string message = "Publish rejected: save the current scene before publishing.";
        Logger::Error("[Editor] ", message);
        ShowProjectResult(message, true);
        return;
    }
    PublishReport report;
    std::string error;
    const char* basePath = SDL_GetBasePath();
    if (!basePath || !ProjectPublisher::Publish(
            m_Project.GetConfig(), basePath ? basePath : "", report, &error)) {
        Logger::Error("[Editor] Publish failed: ", error);
        ShowProjectResult("Publish failed: " + error, true);
        return;
    }
    Logger::Info("[Editor] Published ", report.cookedFiles.size(),
                 " assets (", report.contentBytes, " bytes) to ",
                 report.outputDirectory.string());
    ShowProjectResult(
        "Published " + std::to_string(report.cookedFiles.size()) +
        " files (" + std::to_string(report.contentBytes) + " bytes) to:\n" +
        report.outputDirectory.string(), false);
}
