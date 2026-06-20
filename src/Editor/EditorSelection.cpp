#include "Editor/EditorSelection.h"

#include "Scene/Scene.h"

void EditorSelection::SelectActorID(uint64_t actorID)
{
    m_ActorID = actorID;
    m_ActorHandle = {};
    m_AssetPath.clear();
}

void EditorSelection::SelectActorHandle(ActorHandle actor)
{
    m_ActorHandle = actor;
    m_ActorID = 0;
    m_AssetPath.clear();
}

void EditorSelection::SelectAssetPath(std::string path)
{
    m_AssetPath = std::move(path);
    m_ActorID = 0;
    m_ActorHandle = {};
}

void EditorSelection::Clear()
{
    m_ActorID = 0;
    m_ActorHandle = {};
    m_AssetPath.clear();
}

void EditorSelection::Validate(Scene& scene)
{
    if (m_ActorHandle.IsValid()) {
        if (!scene.TryGetActor(m_ActorHandle)) m_ActorHandle = {};
    } else if (m_ActorID != 0) {
        if (Actor* actor = scene.FindByID(m_ActorID)) m_ActorHandle = actor->GetHandle();
        else m_ActorID = 0;
    }
}

Actor* EditorSelection::ResolveActor(Scene& scene) const
{
    if (m_ActorHandle.IsValid()) return scene.TryGetActor(m_ActorHandle);
    return m_ActorID ? scene.FindByID(m_ActorID) : nullptr;
}

const Actor* EditorSelection::ResolveActor(const Scene& scene) const
{
    if (m_ActorHandle.IsValid()) return scene.TryGetActor(m_ActorHandle);
    return m_ActorID ? scene.FindByID(m_ActorID) : nullptr;
}
