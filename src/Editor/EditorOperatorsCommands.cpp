#include "Editor/EditorOperatorShared.h"

bool EditorCommandOperator::ExecuteAction(EditorContext& context,
                                          const std::string& actionID) const
{
    EditorActionRegistry* actions = context.GetActionRegistry();
    return actions && actions->Execute(actionID, context);
}

uint64_t EditorCommandOperator::CreateActor(EditorContext& context,
                                            const std::string& name) const
{
    if (!context.CanEditScene() || !context.GetScene() || !context.GetCommandStack()) return 0;
    ActorCreateDesc desc;
    desc.name = name.empty() ? "Actor" : name;
    const uint64_t newID = FindNextActorID(*context.GetScene());
    auto command = EditorUndoUtil::MakeCreateActorCommand(desc, newID);
    if (!command || !context.GetCommandStack()->ExecuteCommand(std::move(command), context)) return 0;
    return newID;
}

uint64_t EditorCommandOperator::CreateChildActor(EditorContext& context,
                                                 const std::string& name,
                                                 uint64_t parentActorID) const
{
    Scene* scene = context.GetScene();
    Actor* parent = scene && parentActorID ? scene->FindByID(parentActorID) : nullptr;
    if (!context.CanEditScene() || !scene || !context.GetCommandStack() || !parent) {
        return 0;
    }
    ActorCreateDesc desc;
    desc.name = name.empty() ? "Actor" : name;
    desc.parent = parent->GetHandle();
    const uint64_t newID = FindNextActorID(*scene);
    auto command = EditorUndoUtil::MakeCreateActorCommand(desc, newID);
    if (!command || !context.GetCommandStack()->ExecuteCommand(std::move(command), context)) {
        return 0;
    }
    return newID;
}

uint64_t EditorCommandOperator::CreateUIActor(EditorContext& context,
                                              const std::string& presetID,
                                              uint64_t parentActorID) const
{
    if (!context.CanEditScene() || !context.GetScene() || !context.GetCommandStack()) {
        return 0;
    }
    const std::optional<EditorUIPreset> preset = EditorUIPresetFromID(presetID);
    if (!preset) return 0;

    Scene* scene = context.GetScene();
    Actor* parent = parentActorID ? scene->FindByID(parentActorID) : nullptr;
    if (parentActorID && !parent) return 0;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t oldSelection = context.GetSelection().GetActorID();
    Actor* actor = scene->CreateActor(EditorUIPresetName(*preset), parent);
    if (!actor) return 0;
    AddUIPresetComponents(*actor, *preset);
    const uint64_t newSelection = actor->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!context.GetCommandStack()->ExecuteCommand(
            EditorUndoUtil::MakeSceneSnapshotCommand(
                "Create UI Actor", before, after, oldSelection, newSelection),
            context)) {
        return 0;
    }
    return newSelection;
}

uint64_t EditorCommandOperator::CreateEmptyParent(EditorContext& context,
                                                  uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!scene || !actor || !context.GetCommandStack() || !context.CanEditScene()) {
        return 0;
    }

    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    const std::string before = SceneSerializer::SaveToString(*scene);

    Actor* oldParent = actor->GetParent();
    const ActorHandle actorHandle = actor->GetHandle();
    const ActorHandle oldParentHandle = oldParent ? oldParent->GetHandle() : ActorHandle{};
    Actor* newParent = scene->CreateActor("Empty Parent", oldParent);
    if (!newParent) return 0;
    const uint64_t newParentID = newParent->GetID();

    scene->QueueMoveActor(newParent->GetHandle(), oldParentHandle, actorHandle);
    scene->QueueMoveActor(actorHandle, newParent->GetHandle(), ActorHandle{});
    scene->FlushCommands();

    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (before == after) return 0;

    if (!context.GetCommandStack()->ExecuteCommand(
            EditorUndoUtil::MakeSceneSnapshotCommand(
                "Create Empty Parent", before, after, beforeSelection, newParentID),
            context)) {
        return 0;
    }
    return newParentID;
}

bool EditorCommandOperator::DuplicateActorSubtree(EditorContext& context,
                                                  uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) return false;

    std::vector<PrefabNode> nodes;
    std::string error;
    if (!ActorSubtreeSerializer::Serialize(*actor, nodes, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Duplicate actor failed: ", error);
        return false;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    const uint64_t firstCloneID = FindNextActorID(*scene);
    Actor* nextSibling = nullptr;
    if (const uint64_t nextID = NextSiblingID(*actor)) nextSibling = scene->FindByID(nextID);
    Actor* clone = InstantiatePrefabNodesAsCopy(*scene, nodes, actor->GetParent(),
                                                nextSibling, &error, firstCloneID);
    if (!clone) {
        SceneSerializer::LoadFromString(*scene, before);
        if (!error.empty()) Logger::Warn("[Editor] Duplicate actor failed: ", error);
        return false;
    }

    const uint64_t cloneID = clone->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Duplicate Actor", before, after,
                                                 beforeSelection, cloneID),
        context);
}

bool IsSelectedActorDescendant(const std::vector<uint64_t>& selectedIDs,
                               const Actor& actor)
{
    for (Actor* parent = actor.GetParent(); parent; parent = parent->GetParent()) {
        if (std::find(selectedIDs.begin(), selectedIDs.end(), parent->GetID()) !=
            selectedIDs.end()) {
            return true;
        }
    }
    return false;
}

std::vector<uint64_t> OrderedSelectedActorRoots(Scene& scene,
                                                const std::vector<uint64_t>& selectedIDs)
{
    std::vector<uint64_t> roots;
    roots.reserve(selectedIDs.size());
    scene.ForEach([&](Actor& actor) {
        const uint64_t actorID = actor.GetID();
        if (std::find(selectedIDs.begin(), selectedIDs.end(), actorID) ==
            selectedIDs.end()) {
            return;
        }
        if (IsSelectedActorDescendant(selectedIDs, actor)) return;
        roots.push_back(actorID);
    });
    return roots;
}

bool EditorCommandOperator::DuplicateSelection(EditorContext& context) const
{
    const EditorSelectionSnapshot selection =
        EditorSelectionOperator{}.GetSelectionSnapshot(context);
    if (!selection.hasActor) {
        if (selection.hasAsset) {
            return EditorAssetOperator{}.DuplicateAsset(context, selection.assetPath);
        }
        return false;
    }

    const std::vector<uint64_t>& selectedIDs = context.GetSelection().GetActorIDs();
    if (selectedIDs.size() <= 1) return DuplicateActorSubtree(context, selection.actorID);

    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;

    const std::vector<uint64_t> rootIDs = OrderedSelectedActorRoots(*scene, selectedIDs);
    if (rootIDs.empty()) return false;

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    std::vector<uint64_t> cloneIDs;
    cloneIDs.reserve(rootIDs.size());

    std::string error;
    for (uint64_t actorID : rootIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;

        std::vector<PrefabNode> nodes;
        error.clear();
        if (!ActorSubtreeSerializer::Serialize(*actor, nodes, &error)) {
            SceneSerializer::LoadFromString(*scene, before);
            if (!error.empty()) Logger::Warn("[Editor] Duplicate actors failed: ", error);
            return false;
        }

        Actor* nextSibling = nullptr;
        if (const uint64_t nextID = NextSiblingID(*actor)) nextSibling = scene->FindByID(nextID);
        const uint64_t firstCloneID = FindNextActorID(*scene);
        Actor* clone = InstantiatePrefabNodesAsCopy(*scene, nodes, actor->GetParent(),
                                                    nextSibling, &error, firstCloneID);
        if (!clone) {
            SceneSerializer::LoadFromString(*scene, before);
            if (!error.empty()) Logger::Warn("[Editor] Duplicate actors failed: ", error);
            return false;
        }
        cloneIDs.push_back(clone->GetID());
    }

    if (cloneIDs.empty()) {
        SceneSerializer::LoadFromString(*scene, before);
        return false;
    }

    const std::string after = SceneSerializer::SaveToString(*scene);
    if (before == after) {
        SceneSerializer::LoadFromString(*scene, before);
        return false;
    }
    const uint64_t afterSelection = cloneIDs.back();
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Duplicate Actors", before, after,
                                                 beforeSelection, afterSelection),
        context);
}

bool EditorCommandOperator::RenameActor(EditorContext& context, uint64_t actorID,
                                        const std::string& name) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || !context.GetCommandStack() || !context.CanEditScene()) return false;
    if (actor->GetName() == name) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeSetNameCommand(*actor, name), context);
}

bool EditorCommandOperator::DeleteActor(EditorContext& context, uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || !context.GetCommandStack() || !context.CanEditScene()) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeDestroyActorCommand(*actor), context);
}

bool EditorCommandOperator::DeleteSelection(EditorContext& context) const
{
    const EditorSelectionSnapshot selection =
        EditorSelectionOperator{}.GetSelectionSnapshot(context);
    if (selection.hasActor && context.GetSelection().GetActorIDs().size() > 1) {
        Scene* scene = context.GetScene();
        EditorCommandStack* stack = context.GetCommandStack();
        if (!scene || !stack || !context.CanEditScene()) return false;

        const std::vector<uint64_t> selectedIDs = context.GetSelection().GetActorIDs();
        std::vector<uint64_t> deleteIDs;
        deleteIDs.reserve(selectedIDs.size());
        for (uint64_t actorID : selectedIDs) {
            Actor* actor = scene->FindByID(actorID);
            if (!actor) continue;
            if (IsSelectedActorDescendant(selectedIDs, *actor)) continue;
            deleteIDs.push_back(actorID);
        }
        if (deleteIDs.empty()) return false;

        const std::string before = SceneSerializer::SaveToString(*scene);
        const uint64_t beforeSelection = context.GetSelection().GetActorID();
        for (uint64_t actorID : deleteIDs) {
            if (Actor* actor = scene->FindByID(actorID)) scene->DestroyActor(actor);
        }
        const std::string after = SceneSerializer::SaveToString(*scene);
        if (before == after) return false;
        SceneSerializer::LoadFromString(*scene, before);
        return stack->ExecuteCommand(
            EditorUndoUtil::MakeSceneSnapshotCommand(
                "Delete Actors", before, after, beforeSelection, 0),
            context);
    }
    if (selection.hasActor) return DeleteActor(context, selection.actorID);
    if (selection.hasAsset) return EditorAssetOperator{}.DeleteAsset(context, selection.assetPath);
    return false;
}

bool EditorCommandOperator::RenameSelection(EditorContext& context,
                                            const std::string& name) const
{
    const EditorSelectionSnapshot selection =
        EditorSelectionOperator{}.GetSelectionSnapshot(context);
    if (selection.hasActor) return RenameActor(context, selection.actorID, name);
    if (selection.hasAsset) return EditorAssetOperator{}.RenameAsset(context, selection.assetPath, name);
    return false;
}

bool EditorCommandOperator::CopySelection(EditorContext& context) const
{
    if (!m_Clipboard) return false;
    const EditorSelectionSnapshot selection =
        EditorSelectionOperator{}.GetSelectionSnapshot(context);
    if (selection.hasAsset) {
        return CopyAssets(context, {selection.assetPath});
    }

    Scene* scene = context.GetScene();
    if (!scene || !context.GetSelection().HasActor()) return false;

    std::vector<uint64_t> rootIDs;
    const std::vector<uint64_t>& selectedIDs = context.GetSelection().GetActorIDs();
    if (selectedIDs.size() > 1) {
        rootIDs = OrderedSelectedActorRoots(*scene, selectedIDs);
    } else {
        rootIDs.push_back(context.GetSelection().GetActorID());
    }
    if (rootIDs.empty()) return false;

    nlohmann::json roots = nlohmann::json::array();
    std::string error;
    for (uint64_t actorID : rootIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        std::vector<PrefabNode> nodes;
        error.clear();
        if (!ActorSubtreeSerializer::Serialize(*actor, nodes, &error)) {
            if (!error.empty()) Logger::Warn("[Editor] Copy actors failed: ", error);
            return false;
        }
        roots.push_back(ClipboardRootToJson(nodes));
    }
    if (roots.empty()) return false;

    nlohmann::json json;
    json["type"] = "ActorClipboard";
    json["version"] = 1;
    json["roots"] = std::move(roots);
    m_Clipboard->StoreActors(json.dump());
    return true;
}

bool EditorCommandOperator::CopyAssets(
    EditorContext& context, const std::vector<std::string>& paths) const
{
    if (!m_Clipboard) return false;
    std::vector<std::string> validPaths;
    validPaths.reserve(paths.size());
    for (const std::string& path : paths) {
        const std::filesystem::path assetPath =
            ResolveEditorAssetPath(context, path);
        if (assetPath.empty() || !std::filesystem::is_regular_file(assetPath) ||
            ContentOrSourceRoot(context, assetPath).empty()) {
            continue;
        }
        const std::string normalized = assetPath.lexically_normal().string();
        if (std::find(validPaths.begin(), validPaths.end(), normalized) ==
            validPaths.end()) {
            validPaths.push_back(normalized);
        }
    }
    if (validPaths.empty()) return false;
    m_Clipboard->StoreAssets(std::move(validPaths));
    return true;
}

bool EditorCommandOperator::HasActorClipboard() const
{
    return m_Clipboard && m_Clipboard->HasActors();
}

bool EditorCommandOperator::HasAssetClipboard() const
{
    return m_Clipboard && m_Clipboard->HasAssets();
}

bool EditorCommandOperator::PasteSelection(EditorContext& context) const
{
    if (!m_Clipboard) return false;
    if (m_Clipboard->GetKind() == EditorClipboardService::Kind::Asset) {
        const EditorSelectionSnapshot selection =
            EditorSelectionOperator{}.GetSelectionSnapshot(context);
        const std::filesystem::path targetFolder = selection.hasAsset
            ? ResolveEditorAssetPath(context, selection.assetPath).parent_path()
            : context.GetContentRoot();
        return PasteAssetToFolder(context, targetFolder.string());
    }
    if (m_Clipboard->GetKind() != EditorClipboardService::Kind::Actors) return false;

    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    std::vector<std::vector<PrefabNode>> roots;
    if (!LoadClipboardRoots(m_Clipboard->GetActorJson(), roots)) return false;

    Actor* selected = context.GetSelection().ResolveActor(*scene);
    Actor* parent = selected ? selected->GetParent() : nullptr;
    Actor* nextSibling = selected ? scene->FindByID(NextSiblingID(*selected)) : nullptr;
    const uint64_t beforeSelection = context.GetSelection().GetActorID();
    const std::string before = SceneSerializer::SaveToString(*scene);
    std::string error;
    std::vector<uint64_t> pastedIDs;
    pastedIDs.reserve(roots.size());
    for (const std::vector<PrefabNode>& nodes : roots) {
        const uint64_t firstCloneID = FindNextActorID(*scene);
        Actor* pasted = InstantiatePrefabNodesAsCopy(*scene, nodes, parent, nextSibling,
                                                     &error, firstCloneID);
        if (!pasted) {
            SceneSerializer::LoadFromString(*scene, before);
            if (!error.empty()) Logger::Warn("[Editor] Paste actors failed: ", error);
            return false;
        }
        pastedIDs.push_back(pasted->GetID());
    }
    if (pastedIDs.empty()) {
        SceneSerializer::LoadFromString(*scene, before);
        return false;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    return stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand(
            pastedIDs.size() > 1 ? "Paste Actors" : "Paste Actor", before, after,
            beforeSelection, pastedIDs.back()),
        context);
}

bool EditorCommandOperator::PasteAssetToFolder(EditorContext& context,
                                               const std::string& targetFolder) const
{
    if (!m_Clipboard ||
        m_Clipboard->GetKind() != EditorClipboardService::Kind::Asset ||
        m_Clipboard->GetAssetPaths().empty()) {
        return false;
    }
    bool pastedAny = false;
    for (const std::string& path : m_Clipboard->GetAssetPaths()) {
        pastedAny = EditorAssetOperator{}.CopyAssetToFolder(
            context, path, targetFolder) || pastedAny;
    }
    return pastedAny;
}

bool EditorCommandOperator::SetActorActive(EditorContext& context, uint64_t actorID,
                                           bool active) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || !context.GetCommandStack() || !context.CanEditScene()) return false;
    if (actor->IsActiveSelf() == active) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeSetActiveCommand(*actor, active), context);
}

bool EditorCommandOperator::SetActorTag(EditorContext& context, uint64_t actorID,
                                        const std::string& tag) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) return false;
    const std::string before = actor->GetTag();
    if (before == tag) return false;
    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actor Tag",
        [actorID, tag](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetTag(tag);
            return true;
        },
        [actorID, before](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetTag(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetActorLayer(EditorContext& context, uint64_t actorID,
                                          uint32_t layer) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) return false;
    const uint32_t before = actor->GetLayer();
    if (before == layer) return false;
    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actor Layer",
        [actorID, layer](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetLayer(layer);
            return true;
        },
        [actorID, before](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetLayer(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetActorEditorFlags(EditorContext& context,
                                                uint64_t actorID,
                                                uint32_t flags) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) return false;
    const uint32_t before = actor->GetEditorFlags();
    if (before == flags) return false;
    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actor Flags",
        [actorID, flags](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetEditorFlags(flags);
            return true;
        },
        [actorID, before](EditorContext& value) {
            Scene* scene = value.GetScene();
            Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
            if (!actor) return false;
            actor->SetEditorFlags(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetActorStatic(EditorContext& context,
                                           uint64_t actorID,
                                           bool isStatic) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    uint32_t flags = actor->GetEditorFlags();
    if (isStatic) flags |= 1u;
    else flags &= ~1u;
    return SetActorEditorFlags(context, actorID, flags);
}

bool EditorCommandOperator::SetSceneName(EditorContext& context, const std::string& name) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    const std::string before = scene->GetName();
    if (before == name) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Scene Name",
        [name](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetName(name);
            return true;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetName(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetSceneGravity(EditorContext& context, const Vec3& gravity) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    const Vec3 before = scene->GetPhysicsWorld().GetGravity();
    if (before.x == gravity.x && before.y == gravity.y && before.z == gravity.z) {
        return false;
    }

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Scene Gravity",
        [gravity](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->GetPhysicsWorld().SetGravity(gravity);
            return true;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->GetPhysicsWorld().SetGravity(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetSceneMainCameraHint(EditorContext& context,
                                                   uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    if (actorID != 0) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor || !actor->GetComponent<CameraComponent>()) return false;
    }
    const uint64_t before = scene->GetMainCameraHintActorID();
    if (before == actorID) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Scene Main Camera",
        [actorID](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetMainCameraHintActorID(actorID);
            return true;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetMainCameraHintActorID(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetSceneAmbientIntensity(EditorContext& context,
                                                     float intensity) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    if (intensity < 0.0f) intensity = 0.0f;
    const float before = scene->GetAmbientIntensity();
    if (before == intensity) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Scene Ambient Intensity",
        [intensity](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetAmbientIntensity(intensity);
            return true;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            scene->SetAmbientIntensity(before);
            return true;
        }), context);
}

bool EditorCommandOperator::SetActorsActive(
    EditorContext& context, const std::vector<uint64_t>& actorIDs, bool active) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, bool>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (actor && actor->IsActiveSelf() != active) {
            before.emplace_back(actorID, actor->IsActiveSelf());
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Active",
        [before, active](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetActive(active);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, activeSelf] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetActive(activeSelf);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsTag(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const std::string& tag) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, std::string>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (actor && actor->GetTag() != tag) before.emplace_back(actorID, actor->GetTag());
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Tag",
        [before, tag](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetTag(tag);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, tagBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetTag(tagBefore);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsLayer(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    uint32_t layer) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, uint32_t>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (actor && actor->GetLayer() != layer) before.emplace_back(actorID, actor->GetLayer());
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Layer",
        [before, layer](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetLayer(layer);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, layerBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetLayer(layerBefore);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsEditorFlags(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    uint32_t flags) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, uint32_t>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (actor && actor->GetEditorFlags() != flags) {
            before.emplace_back(actorID, actor->GetEditorFlags());
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Flags",
        [before, flags](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetEditorFlags(flags);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, flagsBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetEditorFlags(flagsBefore);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsStatic(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    bool isStatic) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, uint32_t>> before;
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor || actor->IsStatic() == isStatic) continue;
        before.emplace_back(actorID, actor->GetEditorFlags());
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Static",
        [before, isStatic](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetStatic(isStatic);
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, flagsBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->SetEditorFlags(flagsBefore);
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsPosition(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const Vec3& position) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, Vec3>> before;
    before.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        const Vec3 current = actor->GetTransform().position;
        if (current.x != position.x || current.y != position.y || current.z != position.z) {
            before.emplace_back(actorID, current);
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Position",
        [before, position](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().position = position;
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, positionBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().position = positionBefore;
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsRotation(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const Vec3& rotation) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, Vec3>> before;
    before.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        const Vec3 current = actor->GetTransform().rotation;
        if (current.x != rotation.x || current.y != rotation.y ||
            current.z != rotation.z) {
            before.emplace_back(actorID, current);
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Rotation",
        [before, rotation](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().rotation = rotation;
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, rotationBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().rotation = rotationBefore;
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::SetActorsScale(
    EditorContext& context, const std::vector<uint64_t>& actorIDs,
    const Vec3& scale) const
{
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || actorIDs.empty()) return false;

    std::vector<std::pair<uint64_t, Vec3>> before;
    before.reserve(actorIDs.size());
    for (uint64_t actorID : actorIDs) {
        Actor* actor = scene->FindByID(actorID);
        if (!actor) continue;
        const Vec3 current = actor->GetTransform().scale;
        if (current.x != scale.x || current.y != scale.y || current.z != scale.z) {
            before.emplace_back(actorID, current);
        }
    }
    if (before.empty()) return false;

    return stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Set Actors Scale",
        [before, scale](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, ignored] : before) {
                (void)ignored;
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().scale = scale;
                    changed = true;
                }
            }
            return changed;
        },
        [before](EditorContext& value) {
            Scene* scene = value.GetScene();
            if (!scene) return false;
            bool changed = false;
            for (const auto& [actorID, scaleBefore] : before) {
                if (Actor* actor = scene->FindByID(actorID)) {
                    actor->GetTransform().scale = scaleBefore;
                    changed = true;
                }
            }
            return changed;
        }), context);
}

bool EditorCommandOperator::MoveActor(EditorContext& context, uint64_t actorID,
                                      uint64_t afterParentID,
                                      uint64_t afterNextSiblingID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor || !context.GetCommandStack() || !context.CanEditScene()) return false;
    const uint64_t beforeParentID = actor->GetParent() ? actor->GetParent()->GetID() : 0;
    const uint64_t beforeNextSiblingID = NextSiblingID(*actor);
    if (beforeParentID == afterParentID && beforeNextSiblingID == afterNextSiblingID) return false;
    return context.GetCommandStack()->ExecuteCommand(
        EditorUndoUtil::MakeMoveActorCommand(
            *actor, beforeParentID, beforeNextSiblingID,
            afterParentID, afterNextSiblingID),
        context);
}

bool EditorCommandOperator::UnparentActor(EditorContext& context, uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    Actor* parent = actor ? actor->GetParent() : nullptr;
    if (!scene || !actor || !parent) return false;
    Actor* grandParent = parent->GetParent();
    const uint64_t grandParentID = grandParent ? grandParent->GetID() : 0;
    const uint64_t nextAfterParentID = NextSiblingID(*parent);
    return MoveActor(context, actorID, grandParentID, nextAfterParentID);
}

bool EditorCommandOperator::MoveActorUp(EditorContext& context, uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    const uint64_t previousID = PreviousSiblingID(*actor);
    if (!previousID) return false;
    const uint64_t parentID = actor->GetParent() ? actor->GetParent()->GetID() : 0;
    return MoveActor(context, actorID, parentID, previousID);
}

bool EditorCommandOperator::MoveActorDown(EditorContext& context, uint64_t actorID) const
{
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    const uint64_t nextID = NextSiblingID(*actor);
    if (!nextID) return false;
    Actor* next = scene ? scene->FindByID(nextID) : nullptr;
    if (!next) return false;
    const uint64_t parentID = actor->GetParent() ? actor->GetParent()->GetID() : 0;
    return MoveActor(context, actorID, parentID, NextSiblingID(*next));
}

