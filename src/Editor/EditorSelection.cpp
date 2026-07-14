#include "Editor/EditorSelection.h"

#include "Editor/EditorContext.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <filesystem>

EditorSelectObject EditorSelectObject::MakeActor(ActorHandle handle, uint64_t persistentID,
                                                 EditorSelectionWorldKind world) {
    EditorSelectObject object;
    if (!handle.IsValid() && persistentID == 0)
        return object;
    object.m_Type = EditorSelectObjectType::Actor;
    object.m_ActorHandle = handle;
    object.m_ActorID = persistentID;
    object.m_WorldKind = world;
    return object;
}

EditorSelectObject EditorSelectObject::MakeActor(uint64_t persistentID, EditorSelectionWorldKind world) {
    return MakeActor({}, persistentID, world);
}

EditorSelectObject EditorSelectObject::MakeAsset(std::string path) {
    EditorSelectObject object;
    if (path.empty())
        return object;
    object.m_Type = EditorSelectObjectType::Asset;
    object.m_AssetPath = std::filesystem::path(std::move(path)).lexically_normal().generic_string();
    return object;
}

bool operator==(const EditorSelectObject& left, const EditorSelectObject& right) {
    if (left.m_Type != right.m_Type)
        return false;
    if (left.IsNone())
        return true;
    if (left.IsAsset())
        return left.m_AssetPath == right.m_AssetPath;
    if (left.m_WorldKind != right.m_WorldKind)
        return false;
    if (left.m_ActorID != 0 && right.m_ActorID != 0) {
        return left.m_ActorID == right.m_ActorID;
    }
    if (left.m_ActorHandle.IsValid() && right.m_ActorHandle.IsValid()) {
        return left.m_ActorHandle == right.m_ActorHandle;
    }
    return false;
}

void EditorSelection::Select(EditorSelectObject object, EditorSelectionMode mode) {
    const EditorSelectObject previous = m_PrimaryObject;
    const std::vector<uint64_t> previousActorIDs = m_MultiActorIDs;

    if (!object.IsActor() || mode == EditorSelectionMode::Replace) {
        m_PrimaryObject = std::move(object);
        m_MultiActorIDs.clear();
        if (m_PrimaryObject.IsActor() && m_PrimaryObject.GetActorID() != 0) {
            m_MultiActorIDs.push_back(m_PrimaryObject.GetActorID());
        }
        PublishIfChanged(previous, previousActorIDs);
        return;
    }

    const uint64_t actorID = object.GetActorID();
    if (actorID == 0) {
        m_PrimaryObject = std::move(object);
        PublishIfChanged(previous, previousActorIDs);
        return;
    }

    const auto found = std::find(m_MultiActorIDs.begin(), m_MultiActorIDs.end(), actorID);
    if (mode == EditorSelectionMode::Toggle && found != m_MultiActorIDs.end()) {
        m_MultiActorIDs.erase(found);
        if (m_MultiActorIDs.empty()) {
            m_PrimaryObject = {};
        } else {
            m_PrimaryObject = EditorSelectObject::MakeActor(m_MultiActorIDs.back());
        }
    } else {
        if (found == m_MultiActorIDs.end())
            m_MultiActorIDs.push_back(actorID);
        m_PrimaryObject = std::move(object);
    }
    PublishIfChanged(previous, previousActorIDs);
}

EditorSelection::ListenerID EditorSelection::SubscribeSelectionChanged(SelectionChangedCallback callback) {
    if (!callback)
        return 0;
    const ListenerID listenerID = m_NextListenerID++;
    m_Listeners.emplace(listenerID, std::move(callback));
    return listenerID;
}

void EditorSelection::UnsubscribeSelectionChanged(ListenerID listenerID) {
    if (listenerID != 0)
        m_Listeners.erase(listenerID);
}

void EditorSelection::SelectActorID(uint64_t actorID, EditorSelectionWorldKind world) {
    Select(EditorSelectObject::MakeActor(actorID, world));
}

void EditorSelection::SelectActorHandle(ActorHandle actor, EditorSelectionWorldKind world) {
    Select(EditorSelectObject::MakeActor(actor, 0, world));
}

void EditorSelection::SelectAssetPath(std::string path) {
    Select(EditorSelectObject::MakeAsset(std::move(path)));
}

void EditorSelection::Clear() {
    Select({});
}

void EditorSelection::Validate(Scene& scene) {
    Validate(scene, m_PrimaryObject.GetWorldKind());
}

void EditorSelection::Validate(Scene& scene, EditorSelectionWorldKind world) {
    const EditorSelectObject previous = m_PrimaryObject;
    const std::vector<uint64_t> previousActorIDs = m_MultiActorIDs;

    m_MultiActorIDs.erase(std::remove_if(m_MultiActorIDs.begin(), m_MultiActorIDs.end(),
                                         [&](uint64_t id) { return scene.FindByID(id) == nullptr; }),
                          m_MultiActorIDs.end());

    if (m_PrimaryObject.IsActor()) {
        Actor* actor = nullptr;
        if (m_PrimaryObject.GetActorHandle().IsValid()) {
            actor = scene.TryGetActor(m_PrimaryObject.GetActorHandle());
        }
        if (!actor && m_PrimaryObject.GetActorID() != 0) {
            actor = scene.FindByID(m_PrimaryObject.GetActorID());
        }
        if (actor) {
            m_PrimaryObject = EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID(), world);
        } else if (!m_MultiActorIDs.empty()) {
            Actor* fallback = scene.FindByID(m_MultiActorIDs.back());
            m_PrimaryObject = fallback ? EditorSelectObject::MakeActor(fallback->GetHandle(), fallback->GetID(), world)
                                       : EditorSelectObject{};
        } else {
            m_PrimaryObject = {};
        }
    }

    PublishIfChanged(previous, previousActorIDs);
}

Actor* EditorSelection::ResolveActor(Scene& scene) const {
    if (!m_PrimaryObject.IsActor())
        return nullptr;
    if (m_PrimaryObject.GetActorHandle().IsValid()) {
        if (Actor* actor = scene.TryGetActor(m_PrimaryObject.GetActorHandle()))
            return actor;
    }
    return m_PrimaryObject.GetActorID() ? scene.FindByID(m_PrimaryObject.GetActorID()) : nullptr;
}

const Actor* EditorSelection::ResolveActor(const Scene& scene) const {
    if (!m_PrimaryObject.IsActor())
        return nullptr;
    if (m_PrimaryObject.GetActorHandle().IsValid()) {
        if (const Actor* actor = scene.TryGetActor(m_PrimaryObject.GetActorHandle()))
            return actor;
    }
    return m_PrimaryObject.GetActorID() ? scene.FindByID(m_PrimaryObject.GetActorID()) : nullptr;
}

Actor* EditorSelection::ResolveActor(EditorContext& context) const {
    Scene* scene = context.GetInspectorScene();
    return scene ? ResolveActor(*scene) : nullptr;
}

const Actor* EditorSelection::ResolveActor(const EditorContext& context) const {
    const Scene* scene = context.GetInspectorScene();
    return scene ? ResolveActor(*scene) : nullptr;
}

void EditorSelection::ToggleActorID(uint64_t actorID, EditorSelectionWorldKind world) {
    Select(EditorSelectObject::MakeActor(actorID, world), EditorSelectionMode::Toggle);
}

void EditorSelection::AddToMultiSelect(uint64_t actorID, EditorSelectionWorldKind world) {
    Select(EditorSelectObject::MakeActor(actorID, world), EditorSelectionMode::Add);
}

void EditorSelection::RemoveFromMultiSelect(uint64_t actorID) {
    if (!IsSelected(actorID))
        return;
    Select(EditorSelectObject::MakeActor(actorID), EditorSelectionMode::Toggle);
}

bool EditorSelection::IsSelected(uint64_t actorID, EditorSelectionWorldKind world) const {
    if (m_PrimaryObject.IsActor() && m_PrimaryObject.GetWorldKind() != world)
        return false;
    return std::find(m_MultiActorIDs.begin(), m_MultiActorIDs.end(), actorID) != m_MultiActorIDs.end();
}

void EditorSelection::ClearMultiSelect() {
    Clear();
}

void EditorSelection::PublishIfChanged(const EditorSelectObject& previous,
                                       const std::vector<uint64_t>& previousActorIDs) {
    if (previous == m_PrimaryObject && previousActorIDs == m_MultiActorIDs)
        return;

    EditorSelectionChangedEvent event;
    event.previous = previous;
    event.current = m_PrimaryObject;
    event.actorIDs = m_MultiActorIDs;

    std::vector<SelectionChangedCallback> callbacks;
    callbacks.reserve(m_Listeners.size());
    for (const auto& [listenerID, callback] : m_Listeners) {
        (void)listenerID;
        callbacks.push_back(callback);
    }
    for (const auto& callback : callbacks)
        callback(event);
}
