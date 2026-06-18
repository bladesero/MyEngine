#include "Editor/EditorLayer.h"

#include "Core/Engine.h"
#include "Core/Logger.h"
#include "Core/Window.h"
#include "Editor/EditorPanel.h"
#include "Editor/EditorPanels.h"
#include "Editor/EditorService.h"
#include "Game/SceneRenderLayer.h"

#include <SDL3/SDL.h>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <ImGuizmo.h>
#endif

#include <filesystem>

class EditorLayer::ImGuiPlatformEventBridge final:public IPlatformEventBridge {
public:explicit ImGuiPlatformEventBridge(IRenderContext* context):m_Context(context){}
    void OnSDLEvent(const SDL_Event& event)override{if(m_Context)m_Context->ProcessImGuiSDLEvent(event);}
private:IRenderContext* m_Context=nullptr;
};

EditorLayer::EditorLayer(SceneRenderLayer* sceneLayer,IWindow* window,Engine* engine)
    :Layer("EditorLayer"),m_SceneLayer(sceneLayer),m_Window(window),m_Engine(engine){}

void EditorLayer::OnAttach(){
#if defined(MYENGINE_ENABLE_IMGUI)
    if(!m_Window||!m_SceneLayer)return;m_RenderContext=m_SceneLayer->GetRenderContext();if(!m_RenderContext||!m_Window->GetSDLWindow()){Logger::Error("[Editor] Missing window or render context");return;}
    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::StyleColorsDark();ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());if(!m_RenderContext->InitImGui(m_Window)){ImGui::DestroyContext();return;}
    if(m_Engine){m_PlatformBridge=std::make_unique<ImGuiPlatformEventBridge>(m_RenderContext);m_Engine->SetPlatformEventBridge(m_PlatformBridge.get());}
    m_Project.Open(std::filesystem::current_path());m_Context=EditorContext(m_SceneLayer,m_RenderContext,m_Window,m_Engine);m_Context.SetProjectRoot(m_Project.GetRoot());m_Context.SetCommandStack(&m_CommandStack);m_Context.SetAssetRegistry(&m_AssetRegistry);m_Context.SetProject(&m_Project);m_AssetRegistry.SetRoot(m_Project.GetContentRoot());m_AssetRegistry.Refresh();RegisterServices();RegisterPanels();m_ImGuiReady=true;
#endif
}

void EditorLayer::RegisterServices(){m_ServiceCollection.Add(m_LogService);m_ServiceCollection.Add(m_DialogService);m_ServiceCollection.Add(m_ImportService);m_ServiceCollection.Add(m_ShaderWatchService);m_ServiceCollection.AttachAll(m_Context);}
void EditorLayer::RegisterPanels(){
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>("scene.new","New",[this](EditorContext&){NewScene();},[](EditorContext& context){return context.IsEditing();}));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>("scene.open","Open",[this](EditorContext&){OpenSceneDialog();},[](EditorContext& context){return context.IsEditing();}));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>("scene.save","Save",[this](EditorContext&){SaveScene();}));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>("asset.import","Import",[this](EditorContext&){ImportAssetDialog();}));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>("edit.undo","Undo",[](EditorContext& context){if(auto* stack=context.GetCommandStack())stack->Undo(context);},[](EditorContext& context){auto* stack=context.GetCommandStack();return context.IsEditing()&&stack&&stack->CanUndo();}));
    m_ActionRegistry.Register(std::make_unique<LambdaEditorAction>("edit.redo","Redo",[](EditorContext& context){if(auto* stack=context.GetCommandStack())stack->Redo(context);},[](EditorContext& context){auto* stack=context.GetCommandStack();return context.IsEditing()&&stack&&stack->CanRedo();}));
    m_Context.SetActionRegistry(&m_ActionRegistry);
    auto gizmo=std::make_shared<EditorGizmoState>();m_Panels.push_back(std::make_unique<ToolbarPanel>());m_Panels.push_back(std::make_unique<ViewportPanel>(gizmo));m_Panels.push_back(std::make_unique<SceneHierarchyPanel>());m_Panels.push_back(std::make_unique<InspectorPanel>(gizmo));m_Panels.push_back(std::make_unique<LogPanel>());m_Panels.push_back(std::make_unique<AssetBrowserPanel>());
    const auto& state=m_Project.GetState();const bool visibility[]={state.showToolbar,state.showViewport,state.showSceneHierarchy,state.showInspector,state.showLog,state.showAssetBrowser};for(size_t index=0;index<m_Panels.size();++index){m_Panels[index]->SetVisible(visibility[index]);m_Panels[index]->OnAttach(m_Context);}
}

void EditorLayer::OnDetach(){
#if defined(MYENGINE_ENABLE_IMGUI)
    if(m_Panels.size()==6){auto& state=m_Project.GetState();state.showToolbar=m_Panels[0]->IsVisible();state.showViewport=m_Panels[1]->IsVisible();state.showSceneHierarchy=m_Panels[2]->IsVisible();state.showInspector=m_Panels[3]->IsVisible();state.showLog=m_Panels[4]->IsVisible();state.showAssetBrowser=m_Panels[5]->IsVisible();}if(m_SceneLayer&&m_SceneLayer->HasFilePath())m_Project.SetLastScenePath(m_SceneLayer->GetSceneFilePath());m_Project.SaveState();
    for(auto it=m_Panels.rbegin();it!=m_Panels.rend();++it)(*it)->OnDetach();m_Panels.clear();m_ActionRegistry.Clear();m_Context.SetActionRegistry(nullptr);m_ServiceCollection.DetachAll(m_Context);if(m_Engine)m_Engine->SetPlatformEventBridge(nullptr);m_PlatformBridge.reset();if(m_ImGuiReady){m_RenderContext->ShutdownImGui();ImGui::DestroyContext();m_ImGuiReady=false;}m_RenderContext=nullptr;
#endif
}

void EditorLayer::OnUpdate(float deltaSeconds){m_ServiceCollection.UpdateAll(deltaSeconds);for(auto& panel:m_Panels)panel->OnUpdate(deltaSeconds);ProcessDialogResults();if(Scene* scene=m_Context.GetScene())m_Context.GetSelection().Validate(*scene);}
void EditorLayer::OnRender(){
#if defined(MYENGINE_ENABLE_IMGUI)
    if(!m_ImGuiReady||!m_RenderContext)return;m_RenderContext->BeginImGuiFrame();ImGui::NewFrame();ImGuizmo::BeginFrame();ImGuizmo::AllowAxisFlip(false);for(auto& panel:m_Panels)panel->OnImGui();ImGui::Render();m_RenderContext->RenderImGuiDrawData(ImGui::GetDrawData());m_RenderContext->EndFrame();
#endif
}

void EditorLayer::ProcessDialogResults(){EditorDialogResult result;if(!m_DialogService.ConsumeResult(result)||result.path.empty())return;if(result.operation==EditorFileOperation::OpenScene){if(m_SceneLayer->LoadScene(result.path)){m_Context.GetSelection().Clear();m_CommandStack.Clear();m_Project.SetLastScenePath(result.path);}else Logger::Error("[Editor] Failed to open scene: ",result.path);}else if(result.operation==EditorFileOperation::SaveScene){if(!m_SceneLayer->SaveSceneAs(result.path))Logger::Error("[Editor] Failed to save scene: ",result.path);else m_Project.SetLastScenePath(result.path);}else if(result.operation==EditorFileOperation::ImportAsset)m_ImportService.Import(result.path);}
void EditorLayer::NewScene(){if(!m_Context.IsEditing())return;m_SceneLayer->NewScene();m_Context.GetSelection().Clear();m_CommandStack.Clear();}
void EditorLayer::OpenSceneDialog(){if(m_Context.IsEditing())m_DialogService.RequestOpenScene(m_Window);}
void EditorLayer::SaveScene(){if(!m_SceneLayer)return;if(m_SceneLayer->HasFilePath())m_SceneLayer->SaveScene();else m_DialogService.RequestSaveScene(m_Window);}
void EditorLayer::ImportAssetDialog(){m_DialogService.RequestImportAsset(m_Window);}
