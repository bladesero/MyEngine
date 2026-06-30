#include "Editor/EditorLayer.h"
#include "Core/PlatformEventBridge.h"
#include "Editor/EditorImGuiBackend.h"

#include "Assets/AssetManager.h"
#include "Core/Engine.h"
#include "Core/Logger.h"
#include "Core/Window.h"
#include "Editor/EditorPanel.h"
#include "Editor/EditorPanels.h"
#include "Editor/EditorShortcutMap.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorNotifications.h"
#include "Editor/UI/EditorStyleTokens.h"
#include "Editor/UI/EditorWidgets.h"
#include "Editor/ProjectPublisher.h"
#include "Game/SceneRenderLayer.h"
#include "Input/Input.h"
#include "Project/PublishTargets.h"
#include "Renderer/ShaderManager.h"

#include <SDL3/SDL.h>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <ImGuizmo.h>
#include <imgui.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <vector>

namespace EditorWidgets = Editor::UI::EditorWidgets;

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

bool CaptureShortcutChord(EditorShortcutChord& chord)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGuiIO& io = ImGui::GetIO();
    const auto makeChord = [&](ImGuiKey key) {
        chord = {};
        chord.ctrl = io.KeyCtrl;
        chord.shift = io.KeyShift;
        chord.alt = io.KeyAlt;
        chord.super = io.KeySuper;
        chord.key = static_cast<int>(key);
        return true;
    };
    std::vector<ImGuiKey> keys;
    for (int key = ImGuiKey_A; key <= ImGuiKey_Z; ++key) keys.push_back(static_cast<ImGuiKey>(key));
    for (int key = ImGuiKey_0; key <= ImGuiKey_9; ++key) keys.push_back(static_cast<ImGuiKey>(key));
    for (int key = ImGuiKey_F1; key <= ImGuiKey_F12; ++key) keys.push_back(static_cast<ImGuiKey>(key));
    keys.insert(keys.end(), {
        ImGuiKey_Space, ImGuiKey_Enter, ImGuiKey_Escape, ImGuiKey_Delete,
        ImGuiKey_Backspace, ImGuiKey_Tab, ImGuiKey_Insert, ImGuiKey_Home,
        ImGuiKey_End, ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_UpArrow,
        ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
        ImGuiKey_Comma, ImGuiKey_Period
    });
    for (ImGuiKey key : keys) {
        if (ImGui::IsKeyPressed(key, false)) return makeChord(key);
    }
#else
    (void)chord;
#endif
    return false;
}

std::filesystem::path FindEditorFontRoot()
{
    if (const char* basePath = SDL_GetBasePath()) {
        const std::filesystem::path candidate =
            std::filesystem::path(basePath) / "EngineContent" / "Editor" / "Fonts";
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec) && !ec) return candidate;
    }
    const std::filesystem::path candidate =
        std::filesystem::current_path() / "EngineContent" / "Editor" / "Fonts";
    return candidate.lexically_normal();
}

}




EditorLayer::EditorLayer(SceneRenderLayer* sceneLayer, IWindow* window, Engine* engine,
                         std::filesystem::path initialProject,
                         EditorAutomationConfig automation)
    : Layer("EditorLayer")
    , m_SceneLayer(sceneLayer)
    , m_Window(window)
    , m_Engine(engine)
    , m_InitialProject(std::move(initialProject))
    , m_Automation(std::move(automation)) {
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
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;
    std::string workspaceError;
    if (!m_Workspace.Load(&workspaceError)) Logger::Warn("[Editor] ", workspaceError);
    else if (!workspaceError.empty()) Logger::Warn("[Editor] ", workspaceError);
    m_UIScaleManager.Initialize(m_Window, m_Workspace.GetUserUiScale());
    m_UIScaleManager.SetFontRoot(FindEditorFontRoot());
    m_ThemeManager.Initialize(m_Workspace.GetEditorThemeId());
    m_UIScaleManager.BeginFrame(nullptr);
    m_ThemeManager.Apply(m_UIScaleManager.GetEffectiveScale());
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
    m_Context.SetProfiler(&m_Profiler);
    m_Context.SetShortcutMap(&m_Workspace.GetShortcuts());
    m_AssetRegistry.SetProfiler(&m_Profiler);
    if (const char* basePath = SDL_GetBasePath()) {
        m_Workspace.SetTemplateRoot(std::filesystem::path(basePath) / "ProjectTemplates" / "Default");
    }
    m_ImGuiReady = true;

    if (m_Automation.createProjectRoot.empty() &&
        !m_InitialProject.empty() && !OpenProject(m_InitialProject)) {
        Logger::Error("[Editor] ", m_ProjectError);
    }
    m_AutomationPending = m_Automation.Enabled();
    if (m_AutomationPending && m_Engine) {
        m_Engine->SetExitCode(3);
    }
#endif
}

bool EditorLayer::OpenProject(const std::filesystem::path& root) {
    const auto openStart = std::chrono::steady_clock::now();
    if (m_ProjectOpen) return false;
    if (!m_Project.Open(root, false)) {
        m_ProjectError = m_Project.GetLastError().empty()
            ? "Failed to open project" : m_Project.GetLastError();
        return false;
    }
    auto& projectConfig = m_Project.GetConfig();
    if (projectConfig.GetPublishSettings().target != PublishTargets::kDefaultTargetId) {
        const std::string oldTarget = projectConfig.GetPublishSettings().target;
        projectConfig.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
        std::string saveError;
        if (projectConfig.Save(&saveError)) {
            Logger::Warn("[Editor] Updated publish target from '", oldTarget,
                         "' to '", PublishTargets::kDefaultTargetId, "' for this platform");
        } else {
            Logger::Warn("[Editor] Failed to update publish target for this platform: ", saveError);
        }
    }
    AssetManager::Get().Clear();
    AssetManager::Get().SetProjectRoot(m_Project.GetRoot());
    LoadProjectInputConfig();
    m_Context.SetProjectRoot(m_Project.GetRoot());
    m_Context.SetProfiler(&m_Profiler);
    m_AssetRegistry.SetRoot(m_Project.GetContentRoot());
    m_AssetRegistry.SetProfiler(&m_Profiler);
    m_AssetRegistry.Refresh();
    RegisterServices();
    RegisterPanels();
    m_LayoutManager.OpenProject(m_Project.GetRoot(), m_Project.GetState(), m_Panels);
    m_ProjectOpen = true;
    m_ProjectError.clear();
    if (!m_Project.GetLastWarning().empty()) {
        Logger::Warn("[Editor] ", m_Project.GetLastWarning());
        ShowProjectResult("Project opened with warning: " + m_Project.GetLastWarning(), true);
    }
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
    bool loadedScene = false;
    if (!scenePath.empty()) {
        const auto sceneLoadStart = std::chrono::steady_clock::now();
        loadedScene = m_SceneLayer->LoadScene(scenePath.string());
        const double sceneLoadMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - sceneLoadStart).count();
        m_Profiler.RecordEvent("Editor", "Scene Load", sceneLoadMs,
                               scenePath.generic_string());
    }
    if (loadedScene) {
        m_CommandStack.Clear();
        m_Context.GetSelection().Clear();
    }
    const double openMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - openStart).count();
    m_Profiler.RecordEvent("Editor", "OpenProject", openMs,
                           m_Project.GetRoot().string());
    Logger::Info("[Editor] Opened project: ", m_Project.GetRoot().string());
    return true;
}

void EditorLayer::RegisterServices() {
    if (m_ServicesRegistered) return;
    m_ServiceCollection.Add(m_LogService);
    m_ServiceCollection.Add(m_DialogService);
    m_ServiceCollection.Add(m_ImportService);
    m_ServiceCollection.Add(m_LuaScriptService);
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
        "project.settings", "Settings",
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
        "editor.runLua", "Run Lua",
        [](EditorContext& context) {
            auto* service = context.GetService<EditorLuaScriptService>();
            const std::string& selected = context.GetSelection().GetAssetPath();
            if (!service || selected.empty()) return;
            std::filesystem::path path(selected);
            if (!path.is_absolute()) path = context.GetContentRoot() / path;
            std::string error;
            if (!service->RunFile(path, &error)) {
                Logger::Error("[Editor] Run Lua failed: ", error);
            }
        },
        [](EditorContext& context) {
            const std::string& selected = context.GetSelection().GetAssetPath();
            if (!context.IsEditing() || selected.empty()) return false;
            std::filesystem::path path(selected);
            return path.extension() == ".lua" || path.extension() == ".LUA";
        }));
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
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "play.start", "Play",
        [](EditorContext& context) {
            if (auto* layer = context.GetSceneLayer()) layer->BeginPlay();
        },
        [](EditorContext& context) {
            auto* layer = context.GetSceneLayer();
            return layer && layer->IsEditing();
        }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "play.stop", "Stop",
        [](EditorContext& context) {
            if (auto* layer = context.GetSceneLayer()) layer->StopPlay();
            context.SetSceneViewMode(EditorWorldViewMode::EditorWorld);
        },
        [](EditorContext& context) {
            auto* layer = context.GetSceneLayer();
            return layer && !layer->IsEditing();
        }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "play.pause", "Pause",
        [](EditorContext& context) {
            if (auto* layer = context.GetSceneLayer()) layer->PausePlay();
        },
        [](EditorContext& context) {
            auto* layer = context.GetSceneLayer();
            return layer && layer->IsPlaying();
        }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "play.resume", "Resume",
        [](EditorContext& context) {
            if (auto* layer = context.GetSceneLayer()) layer->ResumePlay();
        },
        [](EditorContext& context) {
            auto* layer = context.GetSceneLayer();
            return layer && !layer->IsEditing() && !layer->IsPlaying();
        }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "play.step", "Step",
        [](EditorContext& context) {
            if (auto* layer = context.GetSceneLayer()) layer->StepPlay();
        },
        [](EditorContext& context) {
            return context.GetSceneLayer() != nullptr;
        }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "shader.recompile", "Recompile",
        [](EditorContext&) { ShaderManager::Get().RecompileAll(); }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "layout.save", "Save Layout",
        [this](EditorContext&) { SaveEditorLayout(); }));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>(
        "layout.resetDefault", "Reset Layout",
        [this](EditorContext&) { ResetEditorLayoutToDefault(); }));
    m_Context.SetActionRegistry(&m_ActionRegistry);

    auto gizmo = std::make_shared<EditorGizmoState>();
    m_Panels.push_back(std::make_unique<ToolbarPanel>());
    m_Panels.push_back(std::make_unique<SceneViewportPanel>(gizmo));
    m_Panels.push_back(std::make_unique<GameViewportPanel>());
    m_Panels.push_back(std::make_unique<SceneHierarchyPanel>());
    m_Panels.push_back(std::make_unique<InspectorPanel>(gizmo));
    m_Panels.push_back(std::make_unique<LogPanel>());
    m_Panels.push_back(std::make_unique<ProfilerPanel>());
    m_Panels.push_back(std::make_unique<AssetBrowserPanel>());
    for (size_t index = 0; index < m_Panels.size(); ++index) {
        const auto& state = m_Project.GetState();
        m_Panels[index]->SetVisible(state.IsPanelVisible(m_Panels[index]->GetID()));
        m_Panels[index]->OnAttach(m_Context);
    }
}

void EditorLayer::OnDetach() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_ProjectOpen) {
        auto& state = m_Project.GetState();
        for (const auto& panel : m_Panels) {
            if (panel) state.SetPanelVisible(panel->GetID(), panel->IsVisible());
        }
        m_LayoutManager.SaveCurrentLayout(state);
        if (m_SceneLayer && m_SceneLayer->HasFilePath())
            m_Project.SetLastScenePath(m_SceneLayer->GetSceneFilePath());
        m_Project.SaveState();
        m_LayoutManager.CloseProject();
    }
    for (auto it = m_Panels.rbegin(); it != m_Panels.rend(); ++it) (*it)->OnDetach();
    m_Panels.clear();
    m_ActionRegistry.Clear();
    m_Context.SetActionRegistry(nullptr);
    m_Context.SetShortcutMap(nullptr);
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
    for (auto& panel : m_Panels) {
        if (panel && (panel->IsVisible() || panel->ShouldUpdateWhenHidden())) {
            panel->OnUpdate(deltaSeconds);
        }
    }
    ProcessDialogResults();
    if (m_AutomationPending) RunAutomation();
    if (m_ProjectOpen) {
        m_Context.RefreshSceneViewMode();
        if (Scene* scene = m_Context.GetInspectorScene()) {
            const auto world = m_Context.GetSelection().GetPrimaryObject().GetWorldKind();
            m_Context.GetSelection().Validate(*scene, world);
        }
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
    std::strncpy(m_InputConfigPath.data(),
                 config.GetInputSettings().config.c_str(),
                 m_InputConfigPath.size() - 1);
    m_InputConfigPath.back() = '\0';
    const std::string& backend = config.GetGraphicsSettings().backend;
    m_GraphicsBackendIndex =
#if defined(MYENGINE_ENABLE_VULKAN)
        backend == "vulkan" ? 2 :
#endif
        (backend == "d3d12" ? 1 : 0);
    m_ProjectSettingsRequested = true;
}

void EditorLayer::ShowProjectResult(std::string message, bool error) {
    m_ProjectResult = std::move(message);
    m_ProjectResultIsError = error;
    m_ProjectResultRequested = true;
}

void EditorLayer::LoadProjectInputConfig() {
    std::string error;
    std::filesystem::path inputConfig;
    if (!m_Project.GetConfig().ResolveInputConfigPath(inputConfig, false, &error)) {
        Logger::Warn("[Editor] Input config path invalid: ", error,
                     "; using default input map");
        Input::SetDefaultActionMap();
        return;
    }
    if (!std::filesystem::is_regular_file(inputConfig)) {
        Logger::Warn("[Editor] Input config not found: ", inputConfig.string(),
                     "; using default input map");
        Input::SetDefaultActionMap();
        return;
    }
    if (!Input::LoadActionMapFromFile(inputConfig, &error)) {
        Logger::Warn("[Editor] Failed to load input config: ", error,
                     "; using default input map");
        return;
    }
    Logger::Info("[Editor] Loaded input config: ", inputConfig.string());
}

void EditorLayer::CreateDefaultInputConfig() {
    auto& config = m_Project.GetConfig();
    const ProjectConfig previous = config;
    std::string error;
    if (!config.SetInputConfigPath(m_InputConfigPath.data(), &error)) {
        config = previous;
        ShowProjectResult("Invalid input config path: " + error, true);
        return;
    }
    std::filesystem::path inputConfig;
    if (!config.ResolveInputConfigPath(inputConfig, false, &error)) {
        config = previous;
        ShowProjectResult("Failed to resolve input config path: " + error, true);
        return;
    }
    if (!InputActionMap::WriteDefaultFile(inputConfig, &error)) {
        config = previous;
        ShowProjectResult("Failed to create input config: " + error, true);
        return;
    }
    if (!config.Save(&error)) {
        config = previous;
        ShowProjectResult("Failed to save project settings: " + error, true);
        return;
    }
    LoadProjectInputConfig();
    ShowProjectResult("Default input config created.", false);
}

void EditorLayer::DrawProjectSettings() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_ProjectSettingsRequested) {
        ImGui::OpenPopup("Settings");
        m_ProjectSettingsRequested = false;
    }
    ImGui::SetNextWindowSize({760.0f, 520.0f}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_NoCollapse)) return;

    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (ImGui::BeginTabItem("Project")) {
            DrawProjectSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graphics")) {
            DrawGraphicsSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gameplay Input")) {
            DrawGameplayInputSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shortcuts")) {
            DrawShortcutSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Appearance")) {
            DrawAppearanceSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Layout")) {
            DrawLayoutSettingsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
#endif
}

void EditorLayer::DrawLayoutSettingsTab() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::LabelText("Default config", "%s", m_LayoutManager.GetConfigPath().string().c_str());
    if (!m_LayoutManager.GetLastWarning().empty()) {
        EditorWidgets::InlineMessage(EditorWidgets::MessageType::Warning,
                                     m_LayoutManager.GetLastWarning().c_str());
    }
    if (ImGui::Button("Save Current")) SaveEditorLayout();
    ImGui::SameLine();
    if (ImGui::Button("Reset To Default")) ResetEditorLayoutToDefault();
    ImGui::SameLine();
    if (ImGui::Button("Reveal Config Path")) RevealEditorLayoutConfig();
    ImGui::Separator();
    ImGui::TextDisabled("Layout is stored in editor state and does not dirty the scene.");
#endif
}

void EditorLayer::DrawAppearanceSettingsTab() {
#if defined(MYENGINE_ENABLE_IMGUI)
    float userScale = m_Workspace.GetUserUiScale();
    ImGui::Text("Platform DPI scale: %.2f", m_UIScaleManager.GetPlatformScale());
    ImGui::Text("Effective UI scale: %.2f", m_UIScaleManager.GetEffectiveScale());
    ImGui::LabelText("Font root", "%s",
                     m_UIScaleManager.GetFontManager().GetFontRoot().string().c_str());
    if (!m_UIScaleManager.GetFontManager().GetLastWarning().empty()) {
        EditorWidgets::InlineMessage(
            Editor::UI::EditorNotificationType::Warning,
            m_UIScaleManager.GetFontManager().GetLastWarning().c_str());
    }
    if (ImGui::SliderFloat("UI Scale", &userScale,
                           Editor::UI::EditorUIScaleSettings::kMinUserScale,
                           Editor::UI::EditorUIScaleSettings::kMaxUserScale, "%.2f")) {
        m_Workspace.SetUserUiScale(userScale);
        m_UIScaleManager.SetUserScale(userScale);
        m_UIScaleManager.MarkFontAtlasDirty();
        m_ThemeManager.Apply(m_UIScaleManager.GetEffectiveScale());
        std::string error;
        if (!m_Workspace.Save(&error)) Logger::Warn("[Editor] ", error);
    }

    static constexpr const char* kThemes[] = {"Dark"};
    int themeIndex = 0;
    if (ImGui::Combo("Theme", &themeIndex, kThemes, 1)) {
        m_Workspace.SetEditorThemeId("dark");
        m_ThemeManager.SetThemeID(m_Workspace.GetEditorThemeId());
        m_ThemeManager.Apply(m_UIScaleManager.GetEffectiveScale());
        std::string error;
        if (!m_Workspace.Save(&error)) Logger::Warn("[Editor] ", error);
    }
    if (ImGui::Button("Reset Appearance")) {
        m_Workspace.SetUserUiScale(1.0f);
        m_Workspace.SetEditorThemeId("dark");
        m_UIScaleManager.SetUserScale(m_Workspace.GetUserUiScale());
        m_UIScaleManager.MarkFontAtlasDirty();
        m_ThemeManager.SetThemeID(m_Workspace.GetEditorThemeId());
        m_ThemeManager.Apply(m_UIScaleManager.GetEffectiveScale());
        std::string error;
        if (m_Workspace.Save(&error)) ShowProjectResult("Appearance reset.", false);
        else ShowProjectResult("Failed to save appearance: " + error, true);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Appearance is stored in workspace preferences and does not dirty the scene.");
#endif
}

void EditorLayer::DrawProjectSettingsTab() {
#if defined(MYENGINE_ENABLE_IMGUI)
    const auto& config = m_Project.GetConfig();
    ImGui::InputText("Project name", m_ProjectName.data(), m_ProjectName.size());
    ImGui::InputText("Output directory", m_PublishOutput.data(), m_PublishOutput.size());
    ImGui::LabelText("Target", "%s", PublishTargets::kDefaultTargetLabel);
    ImGui::LabelText("Startup scene", "%s",
                     config.GetStartupScene().empty()
                         ? "<not set>" : config.GetStartupScene().c_str());
    ImGui::TextDisabled("Use Set Startup to assign the currently saved scene.");
    if (ImGui::Button("Save")) {
        auto& editable = m_Project.GetConfig();
        const ProjectConfig previous = editable;
        editable.SetName(m_ProjectName.data());
        editable.GetPublishSettings().outputDirectory = m_PublishOutput.data();
        editable.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
        std::string error;
        if (!editable.SetInputConfigPath(m_InputConfigPath.data(), &error)) {
            editable = previous;
            ShowProjectResult("Invalid input config path: " + error, true);
            ImGui::EndPopup();
            return;
        }
        if (editable.Save(&error)) {
            Logger::Info("[Editor] Project settings saved");
            LoadProjectInputConfig();
            ShowProjectResult("Project settings saved.", false);
        } else {
            editable = previous;
            ShowProjectResult("Failed to save project settings: " + error, true);
        }
    }
#endif
}

void EditorLayer::DrawGraphicsSettingsTab() {
#if defined(MYENGINE_ENABLE_IMGUI)
#if defined(MYENGINE_ENABLE_VULKAN)
    static constexpr const char* kBackends[] = {"DirectX 11", "DirectX 12", "Vulkan"};
    static constexpr int kBackendCount = 3;
#else
    static constexpr const char* kBackends[] = {"DirectX 11", "DirectX 12"};
    static constexpr int kBackendCount = 2;
    if (m_GraphicsBackendIndex >= kBackendCount) m_GraphicsBackendIndex = 0;
#endif
    ImGui::Combo("Backend", &m_GraphicsBackendIndex, kBackends,
                 kBackendCount);
    const RHIBackend active = m_RenderContext ? m_RenderContext->GetBackend() : RHIBackend::Unknown;
    const char* activeLabel = active == RHIBackend::Vulkan ? "Vulkan" :
                              active == RHIBackend::D3D12 ? "DirectX 12" :
                              active == RHIBackend::D3D11 ? "DirectX 11" : "Unknown";
    ImGui::LabelText("Active backend", "%s", activeLabel);
    ImGui::LabelText("Apply", "%s", "next launch");
    if (ImGui::Button("Save Graphics")) {
        auto& editable = m_Project.GetConfig();
        const ProjectConfig previous = editable;
        editable.GetGraphicsSettings().backend =
#if defined(MYENGINE_ENABLE_VULKAN)
            m_GraphicsBackendIndex == 2 ? "vulkan" :
#endif
            (m_GraphicsBackendIndex == 1 ? "d3d12" : "d3d11");
        std::string error;
        if (editable.Save(&error)) {
            Logger::Info("[Editor] Graphics settings saved");
            ShowProjectResult("Graphics settings saved.", false);
        } else {
            editable = previous;
            ShowProjectResult("Failed to save graphics settings: " + error, true);
        }
    }
#endif
}

void EditorLayer::SaveEditorLayout() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ProjectOpen) return;
    auto& state = m_Project.GetState();
    for (const auto& panel : m_Panels) {
        if (panel) state.SetPanelVisible(panel->GetID(), panel->IsVisible());
    }
    m_LayoutManager.SaveCurrentLayout(state);
    if (m_Project.SaveState()) ShowProjectResult("Editor layout saved.", false);
    else ShowProjectResult("Failed to save editor layout.", true);
#endif
}

void EditorLayer::ResetEditorLayoutToDefault() {
    if (!m_ProjectOpen) return;
    auto& state = m_Project.GetState();
    for (const auto& panel : m_Panels) {
        if (!panel) continue;
        panel->SetVisible(true);
        state.SetPanelVisible(panel->GetID(), true);
    }
    m_LayoutManager.ResetToDefault(state);
    if (m_Project.SaveState()) ShowProjectResult("Editor layout reset to default.", false);
    else ShowProjectResult("Failed to save reset editor layout.", true);
}

void EditorLayer::RevealEditorLayoutConfig() {
    if (!m_ProjectOpen) return;
    ShowProjectResult("Default editor layout config:\n" +
                      m_LayoutManager.GetConfigPath().string(), false);
}

void EditorLayer::DrawGameplayInputSettingsTab() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::InputText("Input config", m_InputConfigPath.data(), m_InputConfigPath.size());
    if (ImGui::Button("Create Default Input Config")) CreateDefaultInputConfig();
    ImGui::SameLine();
    if (ImGui::Button("Reload Input Config")) {
        auto& editable = m_Project.GetConfig();
        const ProjectConfig previous = editable;
        std::string error;
        if (!editable.SetInputConfigPath(m_InputConfigPath.data(), &error)) {
            editable = previous;
            ShowProjectResult("Invalid input config path: " + error, true);
        } else if (!editable.Save(&error)) {
            editable = previous;
            ShowProjectResult("Failed to save project settings: " + error, true);
        } else {
            LoadProjectInputConfig();
            ShowProjectResult("Input config reloaded.", false);
        }
    }
#endif
}

void EditorLayer::DrawShortcutSettingsTab() {
#if defined(MYENGINE_ENABLE_IMGUI)
    EditorShortcutMap& shortcuts = m_Workspace.GetShortcuts();
    if (ImGui::Button("Reset All Defaults")) {
        shortcuts.ResetDefaults();
        m_ShortcutCaptureError.clear();
        m_CapturingShortcutAction.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Shortcuts")) {
        std::string error;
        if (m_Workspace.Save(&error)) ShowProjectResult("Shortcuts saved.", false);
        else ShowProjectResult("Failed to save shortcuts: " + error, true);
    }
    if (!m_ShortcutCaptureError.empty()) {
        ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, "%s", m_ShortcutCaptureError.c_str());
    }

    if (ImGui::BeginTable("ShortcutTable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Shortcut");
        ImGui::TableSetupColumn("Conflict");
        ImGui::TableSetupColumn("Edit");
        ImGui::TableHeadersRow();

        for (EditorAction* action : m_ActionRegistry.GetOrderedActions()) {
            if (!action) continue;
            const std::string actionId = action->GetID();
            const EditorShortcutChord* chord = shortcuts.FindShortcut(actionId);
            const std::string chordText = chord ? EditorShortcutMap::FormatChord(*chord) : std::string{};
            const std::string conflict = chord ? shortcuts.FindConflict(actionId, *chord) : std::string{};
            const bool capturing = m_CapturingShortcutAction == actionId;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(action->GetLabel());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(action->GetID());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(capturing ? "Press shortcut..." :
                (chordText.empty() ? "<none>" : chordText.c_str()));
            ImGui::TableSetColumnIndex(3);
            if (!conflict.empty()) {
                ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, "%s", conflict.c_str());
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(4);
            const std::string recordId = "Record##" + actionId;
            const std::string clearId = "Clear##" + actionId;
            const std::string resetId = "Reset##" + actionId;
            if (ImGui::Button(recordId.c_str())) {
                m_CapturingShortcutAction = actionId;
                m_ShortcutCaptureError.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button(clearId.c_str())) {
                shortcuts.ClearShortcut(actionId);
                if (m_CapturingShortcutAction == actionId) m_CapturingShortcutAction.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button(resetId.c_str())) {
                EditorShortcutMap defaults = EditorShortcutMap::CreateDefault();
                if (const EditorShortcutChord* defaultChord = defaults.FindShortcut(actionId)) {
                    shortcuts.SetShortcut(actionId, *defaultChord);
                } else {
                    shortcuts.ClearShortcut(actionId);
                }
                if (m_CapturingShortcutAction == actionId) m_CapturingShortcutAction.clear();
            }
        }
        ImGui::EndTable();
    }

    if (!m_CapturingShortcutAction.empty()) {
        EditorShortcutChord captured;
        if (CaptureShortcutChord(captured)) {
            if (captured.key == static_cast<int>(ImGuiKey_Escape) &&
                !captured.ctrl && !captured.shift && !captured.alt && !captured.super) {
                m_CapturingShortcutAction.clear();
                m_ShortcutCaptureError.clear();
            } else {
                const std::string conflict = shortcuts.FindConflict(m_CapturingShortcutAction, captured);
                shortcuts.SetShortcut(m_CapturingShortcutAction, captured);
                m_ShortcutCaptureError = conflict.empty()
                    ? std::string{}
                    : "Shortcut also assigned to " + conflict;
                m_CapturingShortcutAction.clear();
            }
        }
    }
#endif
}

void EditorLayer::DispatchEditorShortcuts() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ProjectOpen || !m_Context.GetActionRegistry() || !m_Context.GetShortcutMap()) return;
    if (!m_CapturingShortcutAction.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    m_Context.GetShortcutMap()->Dispatch(*m_Context.GetActionRegistry(), m_Context);
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
    ImGui::PushTextWrapPos(580.0f);
    EditorWidgets::InlineMessage(
        m_ProjectResultIsError ? EditorWidgets::MessageType::Error
                               : EditorWidgets::MessageType::Success,
        m_ProjectResult.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
#endif
}

void EditorLayer::DrawMainMenuBar() {
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float scale = m_UIScaleManager.GetEffectiveScale();
    const float height = Editor::UI::ScaleToken(
        Editor::UI::EditorStyleTokens{}.menuBarHeight, scale);
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize({viewport->WorkSize.x, height});
    ImGui::SetNextWindowViewport(viewport->ID);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    if (ImGui::Begin("Editor Main Menu Bar", nullptr, flags) && ImGui::BeginMenuBar()) {
        const auto drawAction = [&](const char* actionID) {
            EditorAction* action = m_ActionRegistry.Find(actionID);
            if (!action) return;
            const auto iconForAction = [](const char* id) -> const char* {
                if (std::strcmp(id, "scene.new") == 0) return Editor::UI::EditorIcons::SceneNew;
                if (std::strcmp(id, "scene.open") == 0) return Editor::UI::EditorIcons::SceneOpen;
                if (std::strcmp(id, "scene.save") == 0) return Editor::UI::EditorIcons::SceneSave;
                if (std::strcmp(id, "project.settings") == 0) return Editor::UI::EditorIcons::ProjectSettings;
                if (std::strcmp(id, "project.setStartup") == 0) return Editor::UI::EditorIcons::ProjectStartup;
                if (std::strcmp(id, "project.publish") == 0) return Editor::UI::EditorIcons::ProjectPublish;
                if (std::strcmp(id, "edit.undo") == 0) return Editor::UI::EditorIcons::EditUndo;
                if (std::strcmp(id, "edit.redo") == 0) return Editor::UI::EditorIcons::EditRedo;
                if (std::strcmp(id, "shader.recompile") == 0) return Editor::UI::EditorIcons::ShaderRecompile;
                return Editor::UI::EditorIcons::File;
            };
            std::string shortcutText;
            if (const EditorShortcutChord* chord =
                    m_Workspace.GetShortcuts().FindShortcut(actionID)) {
                if (chord->IsValid()) shortcutText = EditorShortcutMap::FormatChord(*chord);
            }
            const bool enabled = action->CanExecute(m_Context);
            const char* icon = iconForAction(actionID);
            const std::string label = std::string(Editor::UI::EditorIcons::FallbackFor(icon)) +
                " " + action->GetLabel();
            if (ImGui::MenuItem(label.c_str(),
                                shortcutText.empty() ? nullptr : shortcutText.c_str(),
                                false, enabled)) {
                m_ActionRegistry.Execute(actionID, m_Context);
            }
        };

        if (ImGui::BeginMenu("File")) {
            drawAction("scene.new");
            drawAction("scene.open");
            drawAction("scene.save");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project")) {
            drawAction("project.settings");
            drawAction("project.setStartup");
            drawAction("project.publish");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            drawAction("edit.undo");
            drawAction("edit.redo");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Build/Debug")) {
            drawAction("shader.recompile");
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
#endif
}

float EditorLayer::DrawStatusBar() {
#if defined(MYENGINE_ENABLE_IMGUI)
    return m_StatusBar.Draw(m_Context, m_ProjectOpen ? &m_Project : nullptr,
                            m_RenderContext, m_Engine,
                            m_UIScaleManager.GetEffectiveScale());
#else
    return 0.0f;
#endif
}

std::string EditorLayer::GetSelectedStatusText() const {
    return Editor::UI::EditorStatusBar::FormatSelectedText(m_Context);
}

std::string EditorLayer::GetBackendStatusText() const {
    return Editor::UI::EditorStatusBar::FormatBackendText(m_RenderContext);
}

void EditorLayer::OnRender() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiReady || !m_RenderContext) return;
    const bool scaleOrFontChanged = m_UIScaleManager.BeginFrame(m_ImGuiBackend.get());
    if (scaleOrFontChanged) m_ThemeManager.Apply(m_UIScaleManager.GetEffectiveScale());
    if (m_ImGuiBackend) m_ImGuiBackend->BeginFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    ImGuizmo::AllowAxisFlip(false);
    DispatchEditorShortcuts();
    if (m_ProjectOpen) {
        const float menuHeight = Editor::UI::ScaleToken(
            Editor::UI::EditorStyleTokens{}.menuBarHeight,
            m_UIScaleManager.GetEffectiveScale());
        DrawMainMenuBar();
        const float statusHeight = DrawStatusBar();
        m_LayoutManager.BeginDockSpace(m_Panels, menuHeight, statusHeight);
        if (m_SceneLayer) {
            m_SceneLayer->SetSceneViewportActive(false);
            m_SceneLayer->SetGameViewportActive(false);
        }
        for (auto& panel : m_Panels) panel->OnImGui();
        DrawProjectSettings();
        DrawProjectResult();
    } else {
        DrawProjectSelector();
    }
    ImGui::Render();
    if (m_ImGuiBackend) m_ImGuiBackend->RenderDrawData(ImGui::GetDrawData());
    const bool renderPlatformWindowsAfterMainFrame =
        m_RenderContext->GetBackend() == RHIBackend::Vulkan;
    if (m_ImGuiBackend && !renderPlatformWindowsAfterMainFrame)
        m_ImGuiBackend->RenderPlatformWindows();
    m_RenderContext->EndFrame();
    if (m_ImGuiBackend && renderPlatformWindowsAfterMainFrame)
        m_ImGuiBackend->RenderPlatformWindows();
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
            m_Context.SetSceneViewMode(EditorWorldViewMode::EditorWorld);
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
    m_Context.SetSceneViewMode(EditorWorldViewMode::EditorWorld);
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
    PublishProjectInternal();
}

bool EditorLayer::PublishProjectInternal(PublishReport* report, std::string* error,
                                         bool showResult) {
    if (error) error->clear();
    if (!m_ProjectOpen) {
        if (error) *error = "project is not open";
        return false;
    }
    if (m_SceneLayer && m_SceneLayer->IsDirty()) {
        const std::string message = "Publish rejected: save the current scene before publishing.";
        Logger::Error("[Editor] ", message);
        if (showResult) ShowProjectResult(message, true);
        if (error) *error = message;
        return false;
    }
    PublishReport localReport;
    std::string localError;
    const char* basePath = SDL_GetBasePath();
    const std::filesystem::path binaryDirectory = basePath ? basePath : "";
    const std::filesystem::path engineContentDirectory = binaryDirectory / "EngineContent";
    const bool succeeded = basePath && ProjectPublisher::Publish(
        m_Project.GetConfig(), binaryDirectory, engineContentDirectory,
        localReport, &localError);
    if (!succeeded) {
        Logger::Error("[Editor] Publish failed: ", localError);
        if (showResult) ShowProjectResult("Publish failed: " + localError, true);
        if (error) *error = std::move(localError);
        return false;
    }
    Logger::Info("[Editor] Published ", localReport.cookedFiles.size(),
                 " assets (", localReport.contentBytes, " bytes) to ",
                 localReport.outputDirectory.string());
    if (showResult) {
        ShowProjectResult(
            "Published " + std::to_string(localReport.cookedFiles.size()) +
            " files (" + std::to_string(localReport.contentBytes) + " bytes) to:\n" +
            localReport.outputDirectory.string(), false);
    }
    if (report) *report = std::move(localReport);
    return true;
}

void EditorLayer::FailAutomation(const std::string& message) {
    Logger::Error("[EditorAutomation] ", message);
    m_AutomationPending = false;
    if (m_Engine) {
        m_Engine->SetExitCode(2);
        m_Engine->RequestQuit();
    }
}

void EditorLayer::RunAutomation() {
    m_AutomationPending = false;
    if (!m_Automation.createProjectRoot.empty() && !m_ProjectOpen) {
        const std::string projectName = m_Automation.projectName.empty()
            ? m_Automation.createProjectRoot.filename().string()
            : m_Automation.projectName;
        std::string createError;
        if (!m_Workspace.CreateProject(m_Automation.createProjectRoot, projectName, &createError)) {
            FailAutomation("create project failed: " + createError);
            return;
        }
        if (!OpenProject(m_Automation.createProjectRoot)) {
            FailAutomation("open created project failed: " + m_ProjectError);
            return;
        }
    }
    if (!m_ProjectOpen && !m_InitialProject.empty()) {
        if (!OpenProject(m_InitialProject)) {
            FailAutomation("open project failed: " + m_ProjectError);
            return;
        }
    }
    if (m_Automation.publishProject) {
        std::string publishError;
        if (!PublishProjectInternal(nullptr, &publishError, false)) {
            FailAutomation("publish project failed: " + publishError);
            return;
        }
    }
    if (m_Engine) {
        m_Engine->SetExitCode(0);
        m_Engine->RequestQuit();
    }
}
