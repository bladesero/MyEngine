#include "Editor/EditorUndoUtil.h"

#include "Editor/EditorContext.h"
#include "Scene/Actor.h"
#include "Scene/ActorSubtreeSerializer.h"
#include "Scene/Component.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Core/Logger.h"

#include <nlohmann/json.hpp>

// ==========================================================================
// EditorSceneTransaction
// ==========================================================================

void EditorSceneTransaction::Begin(const char* name, std::string beforeJson,
                                   uint64_t selection)
{
    if (m_Active) return;
    m_Name = name ? name : "Scene Edit";
    m_BeforeJson = std::move(beforeJson);
    m_Selection = selection;
    m_Active = true;
}

bool EditorSceneTransaction::Commit(EditorContext& context)
{
    if (!m_Active || !context.GetScene() || !context.GetCommandStack()) return false;

    const std::string afterJson = SceneSerializer::SaveToString(*context.GetScene());
    if (afterJson == m_BeforeJson) {
        Cancel();
        return false;
    }

    SceneSerializer::LoadFromString(*context.GetScene(), m_BeforeJson);
    auto command = EditorUndoUtil::MakeSceneSnapshotCommand(
        m_Name.c_str(), m_BeforeJson, afterJson, m_Selection, m_Selection);
    Cancel();
    return context.GetCommandStack()->ExecuteCommand(std::move(command), context);
}

void EditorSceneTransaction::Cancel()
{
    m_Name.clear();
    m_BeforeJson.clear();
    m_Selection = 0;
    m_Active = false;
}

// ==========================================================================
// EditorUndoUtil
// ==========================================================================

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeSceneSnapshotCommand(
    const char* name, const std::string& beforeJson, const std::string& afterJson,
    uint64_t beforeSelection, uint64_t afterSelection) {
    return std::make_unique<SceneSnapshotCommand>(name ? name : "Scene Edit", beforeJson,
        afterJson, beforeSelection, afterSelection);
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeTransformCommand(
    const Actor& actor, const Transform& before, const Transform& after) {
    return std::make_unique<SetActorTransformCommand>(actor.GetID(), before, after);
}

// ---------------------------------------------------------------------------
// Subtree / component serialization helpers
// ---------------------------------------------------------------------------

namespace {

nlohmann::json SerializeActorSubtreeJson(const Actor& root) {
    nlohmann::json arr = nlohmann::json::array();
    std::function<void(const Actor&)> collect = [&](const Actor& actor) {
        nlohmann::json a;
        a["id"] = actor.GetID();
        a["name"] = actor.GetName();
        a["active"] = actor.IsActiveSelf();
        a["parentID"] = actor.GetParent() ? actor.GetParent()->GetID() : uint64_t(0);

        nlohmann::json t;
        t["position"] = nlohmann::json::array(
            {actor.GetTransform().position.x,
             actor.GetTransform().position.y,
             actor.GetTransform().position.z});
        t["rotation"] = nlohmann::json::array(
            {actor.GetTransform().rotation.x,
             actor.GetTransform().rotation.y,
             actor.GetTransform().rotation.z});
        t["scale"] = nlohmann::json::array(
            {actor.GetTransform().scale.x,
             actor.GetTransform().scale.y,
             actor.GetTransform().scale.z});
        a["transform"] = t;

        nlohmann::json comps = nlohmann::json::array();
        actor.ForEachComponent([&](Component& comp) {
            nlohmann::json c;
            c["type"] = comp.GetTypeName();
            c["enabled"] = comp.IsEnabled();
            nlohmann::json data = nlohmann::json::object();
            comp.Serialize(data);
            c["data"] = data;
            comps.push_back(c);
        });
        a["components"] = comps;
        arr.push_back(a);

        for (const auto* child : actor.GetChildren()) collect(*child);
    };
    collect(root);
    return arr;
}

} // namespace

std::string EditorUndoUtil::SerializeActorSubtree(const Actor& root) {
    return SerializeActorSubtreeJson(root).dump();
}

nlohmann::json EditorUndoUtil::SerializeComponentData(const Component& comp) {
    nlohmann::json data = nlohmann::json::object();
    comp.Serialize(data);
    return data;
}

// ---------------------------------------------------------------------------
// Command factories
// ---------------------------------------------------------------------------

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeCreateActorCommand(
    const ActorCreateDesc& desc, uint64_t newID) {
    return std::make_unique<CreateActorCommand>(desc, newID);
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeDestroyActorCommand(
    const Actor& actor) {
    const uint64_t parentID = actor.GetParent() ? actor.GetParent()->GetID() : uint64_t(0);
    return std::make_unique<DestroyActorCommand>(
        actor.GetID(), SerializeActorSubtree(actor), parentID);
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeSetParentCommand(
    const Actor& child, uint64_t beforeParentID, uint64_t afterParentID) {
    return std::make_unique<SetParentCommand>(child.GetID(), beforeParentID, afterParentID);
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeSetActiveCommand(
    const Actor& actor, bool afterActive) {
    return std::make_unique<SetActorActiveCommand>(
        actor.GetID(), actor.IsActiveSelf(), afterActive);
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeSetNameCommand(
    const Actor& actor, const std::string& afterName) {
    return std::make_unique<SetActorNameCommand>(
        actor.GetID(), actor.GetName(), afterName);
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeAddComponentCommand(
    const Actor& actor, const std::string& typeName, const nlohmann::json& initialData) {
    return std::make_unique<AddComponentCommand>(actor.GetID(), typeName, initialData);
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeRemoveComponentCommand(
    const Actor& actor, const Component& comp) {
    nlohmann::json data = nlohmann::json::object();
    comp.Serialize(data);
    return std::make_unique<RemoveComponentCommand>(
        actor.GetID(), comp.GetTypeName(), data);
}

std::unique_ptr<IEditorCommand> EditorUndoUtil::MakeSetPropertyCommand(
    const Actor& actor, const std::string& componentType,
    const std::string& propertyName,
    const nlohmann::json& beforeJson, const nlohmann::json& afterJson) {
    return std::make_unique<SetComponentPropertyCommand>(
        actor.GetID(), componentType, propertyName, beforeJson, afterJson);
}
