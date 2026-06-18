#include "Editor/EditorSelection.h"

#include "Scene/Scene.h"

void EditorSelection::SelectActorID(uint64_t actorID)
{
    m_ActorID = actorID;
    m_AssetPath.clear();
}

void EditorSelection::SelectAssetPath(std::string path)
{
    m_AssetPath = std::move(path);
    m_ActorID = 0;
}

void EditorSelection::Clear()
{
    m_ActorID = 0;
    m_AssetPath.clear();
}

void EditorSelection::Validate(Scene& scene)
{
    if (m_ActorID != 0 && !scene.FindByID(m_ActorID)) m_ActorID = 0;
}

Actor* EditorSelection::ResolveActor(Scene& scene) const
{
    return m_ActorID ? scene.FindByID(m_ActorID) : nullptr;
}

const Actor* EditorSelection::ResolveActor(const Scene& scene) const
{
    return m_ActorID ? scene.FindByID(m_ActorID) : nullptr;
}
