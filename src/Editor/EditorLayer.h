#pragma once

#include "Core/Layer.h"
#include "Core/PlatformEventBridge.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorDialogService.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorLogService.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorShaderWatchService.h"
#include "Editor/EditorService.h"

#include <memory>
#include <vector>

class EditorPanel;
class EditorService;
class Engine;
class IWindow;
class SceneRenderLayer;

class EditorLayer final : public Layer {
public:
    EditorLayer(SceneRenderLayer* sceneLayer, IWindow* window, Engine* engine);
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaSeconds) override;
    void OnEvent(Event& event) override { (void)event; }
    void OnRender() override;

private:
    class ImGuiPlatformEventBridge;
    void RegisterServices();
    void RegisterPanels();
    void ProcessDialogResults();
    void NewScene();
    void OpenSceneDialog();
    void SaveScene();
    void ImportAssetDialog();

    SceneRenderLayer* m_SceneLayer=nullptr;
    IWindow* m_Window=nullptr;
    Engine* m_Engine=nullptr;
    IRenderContext* m_RenderContext=nullptr;
    EditorContext m_Context;
    EditorCommandStack m_CommandStack;
    EditorAssetRegistry m_AssetRegistry;
    EditorProject m_Project;
    EditorLogService m_LogService;
    EditorDialogService m_DialogService;
    EditorImportService m_ImportService;
    EditorShaderWatchService m_ShaderWatchService;
    EditorServiceCollection m_ServiceCollection;
    EditorActionRegistry m_ActionRegistry;
    std::vector<std::unique_ptr<EditorPanel>> m_Panels;
    std::unique_ptr<ImGuiPlatformEventBridge> m_PlatformBridge;
    bool m_ImGuiReady=false;
};
