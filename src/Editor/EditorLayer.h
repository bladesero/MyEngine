#pragma once

#include "Core/Layer.h"
#include "Core/PlatformEventBridge.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorDialogService.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorLogService.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorShaderWatchService.h"
#include "Editor/EditorService.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/EditorImGuiBackend.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class EditorPanel;
class Engine;
class IWindow;
class SceneRenderLayer;
struct PublishReport;

struct EditorAutomationConfig {
    std::filesystem::path createProjectRoot;
    std::string projectName;
    bool publishProject = false;

    bool Enabled() const {
        return !createProjectRoot.empty() || publishProject;
    }
};

class EditorLayer final : public Layer {
public:
    EditorLayer(SceneRenderLayer* sceneLayer, IWindow* window, Engine* engine,
                std::filesystem::path initialProject = {},
                EditorAutomationConfig automation = {});
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaSeconds) override;
    void OnEvent(Event& event) override { (void)event; }
    void OnRender() override;

private:
    bool OpenProject(const std::filesystem::path& root);
    void DrawProjectSelector();
    void DrawProjectSettings();
    void DrawProjectResult();
    void OpenProjectSettings();
    void ShowProjectResult(std::string message, bool error);
    void RegisterServices();
    void RegisterPanels();
    void ProcessDialogResults();
    void NewScene();
    void OpenSceneDialog();
    void SaveScene();
    void ImportAssetDialog();
    void SetStartupScene();
    void PublishProject();
    bool PublishProjectInternal(PublishReport* report = nullptr,
                                std::string* error = nullptr,
                                bool showResult = true);
    void RunAutomation();
    void FailAutomation(const std::string& message);

    SceneRenderLayer* m_SceneLayer = nullptr;
    IWindow* m_Window = nullptr;
    Engine* m_Engine = nullptr;
    IRenderContext* m_RenderContext = nullptr;
    EditorContext m_Context;
    EditorCommandStack m_CommandStack;
    EditorAssetRegistry m_AssetRegistry;
    EditorProject m_Project;
    EditorWorkspace m_Workspace;
    EditorLogService m_LogService;
    EditorDialogService m_DialogService;
    EditorImportService m_ImportService;
    EditorShaderWatchService m_ShaderWatchService;
    EditorServiceCollection m_ServiceCollection;
    EditorActionRegistry m_ActionRegistry;
    std::vector<std::unique_ptr<EditorPanel>> m_Panels;
    std::unique_ptr<EditorImGuiBackend> m_ImGuiBackend;
    std::unique_ptr<IPlatformEventBridge> m_ImGuiEventBridge;
    std::filesystem::path m_InitialProject;
    EditorAutomationConfig m_Automation;
    std::array<char, 1024> m_ProjectPath{};
    std::array<char, 128> m_ProjectName{};
    std::array<char, 1024> m_PublishOutput{};
    std::string m_ProjectError;
    std::string m_ProjectResult;
    bool m_ProjectSettingsRequested = false;
    bool m_ProjectResultRequested = false;
    bool m_ProjectResultIsError = false;
    bool m_ProjectOpen = false;
    bool m_AutomationPending = false;
    bool m_ServicesRegistered = false;
    bool m_ImGuiReady = false;
};
