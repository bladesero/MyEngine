#include "Editor/EditorContext.h"

#include "Game/SceneRenderLayer.h"

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

SceneViewportController* EditorContext::GetSceneViewport() const
{
    return m_SceneLayer ? m_SceneLayer->GetSceneViewport() : nullptr;
}

SceneRenderHost* EditorContext::GetSceneRenderHost() const
{
    return m_SceneLayer ? m_SceneLayer->GetSceneRenderHost() : nullptr;
}

Scene* EditorContext::GetScene() const
{
    return m_SceneOverride ? m_SceneOverride : (m_SceneLayer ? &m_SceneLayer->GetScene() : nullptr);
}

bool EditorContext::IsEditing() const
{
    return m_SceneOverride || (m_SceneLayer && m_SceneLayer->IsEditing());
}

void EditorContext::MarkSceneDirty() const
{
    if (m_SceneLayer) m_SceneLayer->MarkDirty();
}

void EditorContext::SetProjectRoot(std::filesystem::path root)
{
    m_ProjectRoot = std::move(root);
    m_ContentRoot = m_ProjectRoot / "Content";
}
