#pragma once

#include "Editor/EditorPanel.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorUndoUtil.h"
#include "Editor/EditorViewportControllers.h"

#include <memory>
#include <string>
#include <vector>

class Actor;
class ToolbarPanel final:public EditorPanel {
public: ToolbarPanel(); void OnImGui() override;
protected:void DrawContent() override;
};
class SceneHierarchyPanel final:public EditorPanel {
public:SceneHierarchyPanel();void OnImGui() override;
protected:void DrawContent() override;
private:void DrawActor(Actor* actor);
};
class ViewportPanel final:public EditorPanel {
public:explicit ViewportPanel(std::shared_ptr<EditorGizmoState> state);void OnImGui() override;
protected:void DrawContent() override;
private:void DropModel(const std::string& path,float x,float y);std::shared_ptr<EditorGizmoState> m_State;EditorPickingController m_PickingController;EditorGizmoController m_GizmoController;
};
class InspectorPanel final:public EditorPanel {
public:explicit InspectorPanel(std::shared_ptr<EditorGizmoState> state);~InspectorPanel() override;void OnImGui() override;
protected:void DrawContent() override;
private:std::shared_ptr<EditorGizmoState> m_State;EditorInspectorRegistry m_SectionRegistry;EditorSceneTransaction m_Transaction;
};
class AssetBrowserPanel final:public EditorPanel {
public:AssetBrowserPanel();void OnAttach(EditorContext& context) override;void OnUpdate(float dt) override;void OnImGui() override;
protected:void DrawContent() override;
private:char m_Filter[128]={};
};
class LogPanel final:public EditorPanel {
public:LogPanel();void OnImGui() override;
protected:void DrawContent() override;
};
