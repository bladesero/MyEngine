#include "Editor/EditorOperatorShared.h"

namespace {

bool SceneHasSkylight(const Scene& scene) {
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (!found && actor.HasComponentType("Skylight"))
            found = true;
    });
    return found;
}

nlohmann::json ResolveInitialComponentData(const Scene& scene, const std::string& typeName,
                                           const nlohmann::json& initialData) {
    nlohmann::json resolved = initialData.is_object() ? initialData : nlohmann::json::object();
    if (typeName == "Skylight" && !resolved.contains("environmentIntensity"))
        resolved["environmentIntensity"] = scene.GetAmbientIntensity();
    return resolved;
}

} // namespace

bool EditorComponentOperator::AddComponent(EditorContext& context, uint64_t actorID, const std::string& typeName,
                                           const nlohmann::json& initialData) const {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || typeName.empty() || actor->HasComponentType(typeName) ||
        (typeName == "Skylight" && SceneHasSkylight(*scene)) || !context.GetCommandStack() || !context.CanEditScene()) {
        return false;
    }
    const nlohmann::json resolvedData = ResolveInitialComponentData(*scene, typeName, initialData);
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeAddComponentCommand(*actor, typeName, resolvedData), context);
}

bool EditorComponentOperator::AddComponents(EditorContext& context, const std::vector<uint64_t>& actorIDs,
                                            const std::string& typeName, const nlohmann::json& initialData) const {
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty() || typeName.empty() ||
        !ComponentRegistry::Get().IsRegistered(typeName)) {
        return false;
    }
    if (typeName == "Skylight" && (actorIDs.size() != 1 || SceneHasSkylight(*scene)))
        return false;
    const nlohmann::json resolvedData = ResolveInitialComponentData(*scene, typeName, initialData);

    std::vector<uint64_t> targets;
    targets.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor || actor->HasComponentType(typeName))
            return false;
        targets.push_back(actorID);
    }
    if (targets.empty())
        return false;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    bool changed = false;
    for (uint64_t actorID : targets) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor)
            continue;
        Component* component = ComponentRegistry::Get().Create(typeName, *actor);
        if (!component) {
            SceneSerializer::LoadFromString(*scene, before);
            return false;
        }
        if (!resolvedData.empty()) {
            component->Deserialize(resolvedData);
        }
        changed = true;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!changed || before == after)
        return false;

    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Add Components", before, after, beforeSelection, beforeSelection),
        context);
}

bool EditorComponentOperator::SetEnabled(EditorContext& context, uint64_t actorID, const std::string& typeName,
                                         bool enabled) const {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    Component* component = actor ? actor->GetComponentByTypeName(typeName) : nullptr;
    if (!scene || !component || component->IsEnabled() == enabled || !context.GetCommandStack() ||
        !context.CanEditScene()) {
        return false;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    component->SetEnabled(enabled);
    const std::string after = SceneSerializer::SaveToString(*scene);
    return EditorTransactionOperator{}.CommitSceneSnapshot(context, "Set Component Enabled", before, after, actorID,
                                                           actorID);
}

bool EditorComponentOperator::RemoveComponent(EditorContext& context, uint64_t actorID,
                                              const std::string& typeName) const {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    Component* component = actor ? actor->GetComponentByTypeName(typeName) : nullptr;
    return actor && component && RemoveComponent(context, *actor, *component);
}

bool EditorComponentOperator::RemoveComponent(EditorContext& context, Actor& actor, const Component& component) const {
    if (!context.GetCommandStack() || !context.CanEditScene())
        return false;
    return context.GetCommandStack()->ExecuteCommand(EditorUndoUtil::MakeRemoveComponentCommand(actor, component),
                                                     context);
}

bool EditorComponentOperator::RemoveComponents(EditorContext& context, const std::vector<uint64_t>& actorIDs,
                                               const std::string& typeName) const {
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty() || typeName.empty()) {
        return false;
    }

    std::vector<uint64_t> targets;
    targets.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor || !actor->HasComponentType(typeName))
            return false;
        targets.push_back(actorID);
    }
    if (targets.empty())
        return false;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    bool changed = false;
    for (uint64_t actorID : targets) {
        if (Actor* actor = scene->FindByID(actorID)) {
            changed = actor->RemoveComponentByTypeName(typeName) || changed;
        }
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!changed || before == after)
        return false;

    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Remove Components", before, after, beforeSelection, beforeSelection),
        context);
}

bool EditorComponentOperator::SetComponentPropertyForActors(EditorContext& context,
                                                            const std::vector<uint64_t>& actorIDs,
                                                            const std::string& typeName,
                                                            const std::string& propertyName,
                                                            const nlohmann::json& value) const {
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty() || typeName.empty() || propertyName.empty()) {
        return false;
    }

    std::vector<uint64_t> targets;
    targets.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        Component* component = actor ? actor->GetComponentByTypeName(typeName) : nullptr;
        if (!component)
            return false;

        nlohmann::json data = nlohmann::json::object();
        component->Serialize(data);
        const bool compatibleType =
            data.is_object() && data.contains(propertyName) &&
            (data[propertyName].type() == value.type() || (data[propertyName].is_number() && value.is_number()));
        if (!compatibleType) {
            return false;
        }
        targets.push_back(actorID);
    }
    if (targets.empty())
        return false;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    bool changed = false;
    for (uint64_t actorID : targets) {
        Actor* actor = scene->FindByID(actorID);
        Component* component = actor ? actor->GetComponentByTypeName(typeName) : nullptr;
        if (!component)
            return false;

        nlohmann::json data = nlohmann::json::object();
        component->Serialize(data);
        if (!data.is_object() || !data.contains(propertyName))
            return false;
        if (data[propertyName] == value)
            continue;
        data[propertyName] = value;
        component->Deserialize(data);
        changed = true;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!changed || before == after)
        return false;

    const std::string label = "Set " + typeName + "." + propertyName;
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand(label.c_str(), before, after, beforeSelection, beforeSelection),
        context);
}

bool EditorComponentOperator::SetJson(EditorContext& context, uint64_t actorID, const std::string& typeName,
                                      const nlohmann::json& beforeJson, const nlohmann::json& afterJson,
                                      const std::string& label) const {
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor)
        return false;
    return SetProperty(context, *actor, typeName, label.empty() ? typeName : label, beforeJson, afterJson);
}

bool EditorComponentOperator::SetProperty(EditorContext& context, Actor& actor, const std::string& componentType,
                                          const std::string& propertyName, const nlohmann::json& beforeJson,
                                          const nlohmann::json& afterJson) const {
    if (componentType.empty() || beforeJson == afterJson || !context.GetCommandStack() || !context.CanEditScene()) {
        return false;
    }
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeSetPropertyCommand(actor, componentType, propertyName, beforeJson, afterJson), context);
}
