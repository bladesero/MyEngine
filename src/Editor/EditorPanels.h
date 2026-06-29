#pragma once

#include "Editor/EditorPanel.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorUndoUtil.h"
#include "Scene/ActorHandle.h"
#include "Editor/EditorViewportControllers.h"

#include <memory>
#include <string>
#include <vector>

class Actor;
class SceneViewport;
class ToolbarPanel final:public EditorPanel {
public: ToolbarPanel();
protected:void DrawContent() override;
private:int GetWindowFlags() const override;
};
class SceneHierarchyPanel final:public EditorPanel {
public:SceneHierarchyPanel();
protected:void DrawContent() override;
private:
    void DrawToolbar();
    void DrawActor(Actor* actor);
    void HandleDragDropTarget(Actor* targetParent);
    ActorHandle m_DraggedActor;
    bool m_ActorRightClicked = false;
    char m_SearchFilter[128] = {};
    char m_RenameBuffer[256] = {};
    uint64_t m_PendingRenameID = 0;
};
class SceneViewportPanel final:public EditorPanel {
public:explicit SceneViewportPanel(std::shared_ptr<EditorGizmoState> state);
protected:void DrawContent() override;
private:int GetWindowFlags() const override;void BeforeBegin() override;void AfterEnd() override;bool DrawSceneViewOverlay(EditorContext& context,SceneViewport& viewport,EditorGizmoState& state,const EditorPanelRect& rect);void DropModel(const std::string& path,float x,float y);void DropPrefab(const std::string& path,float x,float y);std::shared_ptr<EditorGizmoState> m_State;EditorPickingController m_PickingController;EditorGizmoController m_GizmoController;
};
class GameViewportPanel final:public EditorPanel {
public:GameViewportPanel();
protected:void DrawContent() override;
private:int GetWindowFlags() const override;void BeforeBegin() override;void AfterEnd() override;
};
class InspectorPanel final:public EditorPanel {
public:
    explicit InspectorPanel(std::shared_ptr<EditorGizmoState> state);
    ~InspectorPanel() override;
    void OnAttach(EditorContext& context) override;
    void OnDetach() override;
protected:
    void DrawContent() override;
private:
    void OnSelectionChanged(const EditorSelectionChangedEvent& event);
    std::shared_ptr<EditorGizmoState> m_State;
    EditorInspectorRegistry m_SectionRegistry;
    EditorSceneTransaction m_Transaction;
    EditorSelectObject m_SelectedObject;
    EditorSelection::ListenerID m_SelectionListenerID = 0;
};
class AssetBrowserPanel final:public EditorPanel {
public:AssetBrowserPanel();void OnAttach(EditorContext& context) override;void OnUpdate(float dt) override;
protected:void DrawContent() override;
private:
    void DeleteSelectedAsset();
    void DuplicateSelectedAsset();
    void RenameSelectedAsset();
    void EnsureSelectedFolder();
    std::filesystem::path CurrentContentDirectory(const char* fallback) const;
    char m_Filter[128]={};
    char m_RenameBuffer[256]={};
    std::string m_SelectedFolder = "Content";
    bool m_PendingRename = false;
    bool m_PendingDelete = false;
};
class LogPanel final:public EditorPanel {
public:LogPanel();
protected:void DrawContent() override;
};
