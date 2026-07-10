#include "Editor/EditorContext.h"

#include "Game/SceneRenderLayer.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

EditorContext::EditorContext(Scene* scene)
    : m_SceneOverride(scene)
{}

EditorContext::EditorContext(SceneRenderLayer* sceneLayer, IRenderContext* renderContext,
                             IWindow* window, Engine* engine)
    : m_SceneLayer(sceneLayer), m_RenderContext(renderContext), m_Window(window), m_Engine(engine)
{}

SceneLayer* EditorContext::GetSceneLayerBase() const
{
    return m_SceneLayer;
}

SceneViewport* EditorContext::GetSceneViewport() const
{
    return m_SceneLayer ? m_SceneLayer->GetSceneViewport() : nullptr;
}

GameViewport* EditorContext::GetGameViewport() const
{
    return m_SceneLayer ? m_SceneLayer->GetGameViewport() : nullptr;
}

Scene* EditorContext::GetEditorScene() const
{
    return m_SceneOverride ? m_SceneOverride : (m_SceneLayer ? &m_SceneLayer->GetEditorScene() : nullptr);
}

Scene* EditorContext::GetPlayScene() const
{
    return m_SceneLayer ? m_SceneLayer->GetPlayScene() : nullptr;
}

Scene* EditorContext::GetSimulationScene() const
{
    return m_SceneOverride ? m_SceneOverride : (m_SceneLayer ? &m_SceneLayer->GetSimulationScene() : nullptr);
}

void EditorContext::SetSceneViewMode(EditorWorldViewMode mode)
{
    if (mode == EditorWorldViewMode::PlayWorldInspect &&
        (!m_SceneLayer || !m_SceneLayer->GetPlayScene())) {
        mode = EditorWorldViewMode::EditorWorld;
    }
    if (m_SceneOverride) mode = EditorWorldViewMode::EditorWorld;

    if (m_SceneViewMode == mode) {
        if (m_SceneLayer) {
            m_SceneLayer->SetSceneViewportUsesSimulationScene(
                mode == EditorWorldViewMode::PlayWorldInspect);
        }
        return;
    }

    m_SceneViewMode = mode;
    if (m_SceneLayer) {
        m_SceneLayer->SetSceneViewportUsesSimulationScene(
            mode == EditorWorldViewMode::PlayWorldInspect);
    }

    const EditorSelectObject selected = m_Selection.GetPrimaryObject();
    if (!selected.IsActor()) return;

    Scene* targetScene = GetInspectorScene();
    Actor* targetActor = targetScene ? targetScene->FindByID(selected.GetActorID()) : nullptr;
    if (targetActor) {
        const EditorSelectionWorldKind world =
            mode == EditorWorldViewMode::PlayWorldInspect
                ? EditorSelectionWorldKind::Play
                : EditorSelectionWorldKind::Editor;
        m_Selection.Select(EditorSelectObject::MakeActor(
            targetActor->GetHandle(), targetActor->GetID(), world));
    } else {
        m_Selection.Clear();
    }
}

void EditorContext::RefreshSceneViewMode()
{
    if (m_SceneViewMode == EditorWorldViewMode::PlayWorldInspect &&
        (!m_SceneLayer || !m_SceneLayer->GetPlayScene())) {
        SetSceneViewMode(EditorWorldViewMode::EditorWorld);
        return;
    }
    if (m_SceneLayer) {
        m_SceneLayer->SetSceneViewportUsesSimulationScene(
            m_SceneViewMode == EditorWorldViewMode::PlayWorldInspect);
    }
}

Scene* EditorContext::GetSceneViewScene() const
{
    if (m_SceneViewMode == EditorWorldViewMode::PlayWorldInspect) {
        if (Scene* playScene = GetPlayScene()) return playScene;
    }
    return GetEditorScene();
}

Scene* EditorContext::GetInspectorScene() const
{
    const EditorSelectObject& selected = m_Selection.GetPrimaryObject();
    if (selected.IsActor() &&
        selected.GetWorldKind() == EditorSelectionWorldKind::Play) {
        if (Scene* playScene = GetPlayScene()) return playScene;
    }
    return GetEditorScene();
}

bool EditorContext::CanEditScene() const
{
    return m_SceneOverride ||
        (m_SceneLayer && m_SceneLayer->IsEditing() &&
         m_SceneViewMode == EditorWorldViewMode::EditorWorld);
}

bool EditorContext::CanEditSelection() const
{
    const EditorSelectObject& selected = m_Selection.GetPrimaryObject();
    if (selected.IsActor() &&
        selected.GetWorldKind() == EditorSelectionWorldKind::Play) {
        return false;
    }
    return CanEditScene();
}

bool EditorContext::IsInspectingPlayWorld() const
{
    return m_SceneViewMode == EditorWorldViewMode::PlayWorldInspect &&
        GetPlayScene() != nullptr;
}

Scene* EditorContext::GetScene() const
{
    return GetEditorScene();
}

bool EditorContext::IsEditing() const
{
    return m_SceneOverride || (m_SceneLayer && m_SceneLayer->IsEditing());
}

void EditorContext::MarkSceneDirty() const
{
    if (m_SceneLayer) m_SceneLayer->MarkDirty();
}

void EditorContext::SetPanelFocusRequestHandler(
    std::function<void(std::string_view)> handler)
{
    m_PanelFocusRequestHandler = std::move(handler);
}

void EditorContext::RequestPanelFocus(std::string_view panelID) const
{
    if (m_PanelFocusRequestHandler && !panelID.empty()) {
        m_PanelFocusRequestHandler(panelID);
    }
}

void EditorContext::SetProjectRoot(std::filesystem::path root)
{
    m_ProjectRoot = std::move(root);
    m_ContentRoot = m_ProjectRoot / "Content";
}
