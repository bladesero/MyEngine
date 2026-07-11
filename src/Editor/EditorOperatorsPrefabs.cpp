#include "Editor/EditorOperatorShared.h"

std::vector<EditorPrefabOperator::OverrideInfo> EditorPrefabOperator::GetOverrides(
    EditorContext& context, uint64_t actorID) const
{
    std::vector<OverrideInfo> result;
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    if (!actor) return result;
    nlohmann::json overrides;
    std::string error;
    if (!PrefabSystem::BuildOverrides(*actor, overrides, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Prefab override list failed: ", error);
        return result;
    }
    if (!overrides.is_array()) return result;
    result.reserve(overrides.size());
    for (size_t index = 0; index < overrides.size(); ++index) {
        const nlohmann::json& item = overrides[index];
        OverrideInfo info;
        info.index = index;
        info.kind = item.value("kind", std::string{});
        info.localId = item.value("localId", std::string{});
        info.componentType = item.value("componentType", std::string{});
        info.path = item.value("path", std::string{});
        info.category = OverrideCategory(item);
        info.target = OverrideTargetLabel(item);
        info.property = OverridePropertyLabel(item);
        info.label = OverrideLabel(item);
        info.valuePreview = OverrideValuePreview(item);
        const bool supported = IsSupportedPrefabOverride(item);
        info.canApply = supported;
        info.canRevert = supported;
        if (!supported) {
            info.diagnostic = info.kind.empty()
                ? "Unsupported prefab override kind"
                : "Unsupported prefab override kind: " + info.kind;
        }
        result.push_back(std::move(info));
    }
    const nlohmann::json& persistedOverrides = actor->GetPrefabOverrides();
    if (persistedOverrides.is_array()) {
        for (size_t index = 0; index < persistedOverrides.size(); ++index) {
            const nlohmann::json& item = persistedOverrides[index];
            if (IsSupportedPrefabOverride(item)) continue;
            OverrideInfo info;
            info.index = (std::numeric_limits<size_t>::max)() - index;
            info.kind = item.value("kind", std::string{});
            info.localId = item.value("localId", std::string{});
            info.componentType = item.value("componentType", std::string{});
            info.path = item.value("path", std::string{});
            info.category = OverrideCategory(item);
            info.target = OverrideTargetLabel(item);
            info.property = OverridePropertyLabel(item);
            info.label = OverrideLabel(item);
            info.valuePreview = OverrideValuePreview(item);
            info.diagnostic = info.kind.empty()
                ? "Unsupported prefab override kind"
                : "Unsupported prefab override kind: " + info.kind;
            result.push_back(std::move(info));
        }
    }
    std::stable_sort(result.begin(), result.end(),
        [](const OverrideInfo& left, const OverrideInfo& right) {
            const int leftRank = OverrideCategoryRank(left.category);
            const int rightRank = OverrideCategoryRank(right.category);
            if (leftRank != rightRank) return leftRank < rightRank;
            if (left.target != right.target) return left.target < right.target;
            if (left.property != right.property) return left.property < right.property;
            return left.index < right.index;
        });
    return result;
}

bool EditorPrefabOperator::ApplyAll(EditorContext& context, uint64_t actorID) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!scene || !stack || !actor || !actor->IsPrefabRoot() || !context.CanEditScene()) {
        RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                                  ElapsedOperatorMs(start), false);
        return false;
    }

    const std::filesystem::path prefabPath =
        PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath());
    if (prefabPath.empty() || !std::filesystem::exists(prefabPath)) {
        RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }

    const std::string prefabBefore = ReadFileContent(prefabPath);
    const std::string sceneBefore = SceneSerializer::SaveToString(*scene);
    std::string error;
    if (!PrefabSystem::ApplyAll(*actor, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Apply Prefab failed: ", error);
        RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }
    const std::string prefabAfter = ReadFileContent(prefabPath);
    const std::string sceneAfter = SceneSerializer::SaveToString(*scene);
    if (!WriteFileContent(prefabPath, prefabBefore) ||
        !SceneSerializer::LoadFromString(*scene, sceneBefore)) {
        RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }

    auto applyState = [prefabPath, actorID](EditorContext& value,
                                           const std::string& prefabContent,
                                           const std::string& sceneJson) {
        Scene* targetScene = value.GetScene();
        if (!targetScene || !WriteFileContent(prefabPath, prefabContent) ||
            !SceneSerializer::LoadFromString(*targetScene, sceneJson)) {
            return false;
        }
        if (targetScene->FindByID(actorID)) value.GetSelection().SelectActorID(actorID);
        else value.GetSelection().Clear();
        if (EditorAssetRegistry* registry = value.GetAssetRegistry()) registry->Refresh();
        return true;
    };

    const bool ok = stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Apply Prefab",
        [applyState, prefabAfter, sceneAfter](EditorContext& value) {
            return applyState(value, prefabAfter, sceneAfter);
        },
        [applyState, prefabBefore, sceneBefore](EditorContext& value) {
            return applyState(value, prefabBefore, sceneBefore);
        }), context);
    RecordPrefabOperatorEvent(context, "Apply Prefab", actorID,
                              ElapsedOperatorMs(start), ok,
                              "prefab=" + prefabPath.string());
    return ok;
}

bool EditorPrefabOperator::RevertAll(EditorContext& context, uint64_t actorID) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) {
        RecordPrefabOperatorEvent(context, "Revert Prefab", actorID,
                                  ElapsedOperatorMs(start), false);
        return false;
    }
    const bool ok = CommitPrefabSceneSnapshot(context, *actor, "Revert Prefab",
        [](Actor& value, std::string* error) {
            return PrefabSystem::RevertAll(value, error);
        });
    RecordPrefabOperatorEvent(context, "Revert Prefab", actorID,
                              ElapsedOperatorMs(start), ok);
    return ok;
}

bool EditorPrefabOperator::Unpack(EditorContext& context, uint64_t actorID) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) {
        RecordPrefabOperatorEvent(context, "Unpack Prefab", actorID,
                                  ElapsedOperatorMs(start), false);
        return false;
    }
    const bool ok = CommitPrefabSceneSnapshot(context, *actor, "Unpack Prefab",
        [](Actor& value, std::string* error) {
            return PrefabSystem::Unpack(value, error);
        });
    RecordPrefabOperatorEvent(context, "Unpack Prefab", actorID,
                              ElapsedOperatorMs(start), ok);
    return ok;
}

bool EditorPrefabOperator::CreatePrefabFromActor(EditorContext& context,
                                                 uint64_t actorID) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!scene || !stack || !actor || !context.CanEditScene()) {
        RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                                  ElapsedOperatorMs(start), false);
        return false;
    }

    const std::string sceneBefore = SceneSerializer::SaveToString(*scene);
    const ActorHandle parentHandle =
        actor->GetParent() ? actor->GetParent()->GetHandle() : ActorHandle{};
    const Transform transform = actor->GetTransform();
    const std::filesystem::path directory = context.GetContentRoot() / "Prefabs";
    std::error_code fsError;
    std::filesystem::create_directories(directory, fsError);
    const std::filesystem::path prefabPath =
        EditorImportService::MakeUniqueContentPath(
            directory, actor->GetName(), ".prefab.json");
    std::string error;
    if (!PrefabSystem::SaveSubtree(*actor, prefabPath, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Prefab creation failed: ", error);
        RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }
    const std::filesystem::path metaPath = AssetMeta::MetaPathFor(prefabPath.string());
    const std::string prefabContent = ReadFileContent(prefabPath);
    const std::string metaContent =
        std::filesystem::exists(metaPath) ? ReadFileContent(metaPath) : std::string{};
    const bool hadMeta = std::filesystem::exists(metaPath);

    scene->QueueDestroyActor(actor->GetHandle());
    if (!scene->FlushCommands()) {
        SceneSerializer::LoadFromString(*scene, sceneBefore);
        RemoveAssetFileAndMeta(prefabPath);
        RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }
    PrefabInstantiateOptions options;
    options.parent = parentHandle;
    options.rootTransform = transform;
    options.persistentRootID = actorID;
    Actor* instance = PrefabSystem::Instantiate(*scene, prefabPath, options, &error);
    if (!instance) {
        if (!error.empty()) Logger::Warn("[Editor] Prefab instance creation failed: ", error);
        SceneSerializer::LoadFromString(*scene, sceneBefore);
        RemoveAssetFileAndMeta(prefabPath);
        RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + prefabPath.string());
        return false;
    }
    const std::string sceneAfter = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, sceneBefore);
    RemoveAssetFileAndMeta(prefabPath);

    auto applyState = [prefabPath, metaPath, prefabContent, metaContent, hadMeta, actorID](
                          EditorContext& value, const std::string& sceneJson,
                          bool writePrefab) {
        Scene* targetScene = value.GetScene();
        if (!targetScene) return false;
        if (writePrefab) {
            if (!WriteFileContent(prefabPath, prefabContent)) return false;
            if (hadMeta && !WriteFileContent(metaPath, metaContent)) return false;
        }
        if (!SceneSerializer::LoadFromString(*targetScene, sceneJson)) return false;
        if (!writePrefab) {
            RemoveAssetFileAndMeta(prefabPath);
        }
        if (targetScene->FindByID(actorID)) value.GetSelection().SelectActorID(actorID);
        else value.GetSelection().Clear();
        if (EditorAssetRegistry* registry = value.GetAssetRegistry()) registry->Refresh();
        return true;
    };

    const bool ok = stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Create Prefab",
        [applyState, sceneAfter](EditorContext& value) {
            return applyState(value, sceneAfter, true);
        },
        [applyState, sceneBefore](EditorContext& value) {
            return applyState(value, sceneBefore, false);
        }), context);
    RecordPrefabOperatorEvent(context, "Create Prefab", actorID,
                              ElapsedOperatorMs(start), ok,
                              "prefab=" + prefabPath.string());
    return ok;
}

uint64_t EditorPrefabOperator::InstantiatePrefab(
    EditorContext& context, const std::string& path, uint64_t parentActorID,
    const std::optional<Transform>& rootTransform, const char* commandName) const
{
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene() || path.empty()) {
        RecordPrefabOperatorEvent(context, "Instantiate Prefab", 0,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + path);
        return 0;
    }
    Actor* parent = parentActorID ? scene->FindByID(parentActorID) : nullptr;
    if (parentActorID && !parent) {
        RecordPrefabOperatorEvent(context, "Instantiate Prefab", 0,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + path);
        return 0;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    const uint64_t oldSelection = context.GetSelection().GetActorID();
    PrefabInstantiateOptions options;
    options.parent = parent ? parent->GetHandle() : ActorHandle{};
    options.rootTransform = rootTransform;
    std::string error;
    Actor* actor = PrefabSystem::Instantiate(*scene, path, options, &error);
    if (!actor) {
        if (!error.empty()) Logger::Warn("[Editor] Instantiate prefab failed: ", error);
        SceneSerializer::LoadFromString(*scene, before);
        RecordPrefabOperatorEvent(context, "Instantiate Prefab", 0,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + path);
        return 0;
    }
    const uint64_t newID = actor->GetID();
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (!stack->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand(
            commandName ? commandName : "Instantiate Prefab",
            before, after, oldSelection, newID), context)) {
        RecordPrefabOperatorEvent(context, "Instantiate Prefab", newID,
                                  ElapsedOperatorMs(start), false,
                                  "prefab=" + path);
        return 0;
    }
    RecordPrefabOperatorEvent(context, "Instantiate Prefab", newID,
                              ElapsedOperatorMs(start), true,
                              "prefab=" + path);
    return newID;
}

size_t EditorPrefabOperator::SelectInstances(EditorContext& context,
                                             const std::string& path) const
{
    Scene* scene = context.GetScene();
    if (!scene || path.empty()) return 0;

    const std::filesystem::path targetPath =
        PrefabSystem::ResolvePrefabPath(path).lexically_normal();
    std::vector<uint64_t> rootIDs;
    scene->ForEach([&](Actor& actor) {
        if (!actor.IsPrefabRoot() || actor.GetPrefabAssetPath().empty()) return;
        const std::filesystem::path actorPath =
            PrefabSystem::ResolvePrefabPath(actor.GetPrefabAssetPath())
                .lexically_normal();
        if (actorPath == targetPath) rootIDs.push_back(actor.GetID());
    });
    if (rootIDs.empty()) return 0;

    context.GetSelection().SelectActorID(rootIDs.front());
    for (size_t index = 1; index < rootIDs.size(); ++index) {
        context.GetSelection().AddToMultiSelect(rootIDs[index]);
    }
    context.RequestPanelFocus("sceneHierarchy");
    return rootIDs.size();
}

bool EditorPrefabOperator::ApplyOverride(EditorContext& context, uint64_t actorID,
                                         size_t overrideIndex) const
{
    const auto start = EditorOperatorClock::now();
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    EditorCommandStack* stack = context.GetCommandStack();
    if (!actor || !stack || !context.CanEditScene()) {
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }
    const std::filesystem::path prefabPath =
        PrefabSystem::ResolvePrefabPath(actor->GetPrefabAssetPath());
    if (prefabPath.empty() || !std::filesystem::exists(prefabPath)) {
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }
    Scene* scene = actor->GetScene();
    if (!scene) {
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }

    const std::string prefabBefore = ReadFileContent(prefabPath);
    const std::string sceneBefore = SceneSerializer::SaveToString(*scene);
    std::string error;
    if (!ApplySinglePrefabOverrideNow(context, actorID, overrideIndex, &error)) {
        if (!error.empty()) Logger::Warn("[Editor] Apply Prefab Override failed: ", error);
        RestorePrefabEditorState(context, prefabPath, prefabBefore, sceneBefore, actorID);
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }
    const std::string prefabAfter = ReadFileContent(prefabPath);
    const std::string sceneAfter = SceneSerializer::SaveToString(*scene);
    if (!RestorePrefabEditorState(context, prefabPath, prefabBefore, sceneBefore, actorID)) {
        RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }

    const bool ok = stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>(
        "Apply Prefab Override",
        [prefabPath, prefabAfter, sceneAfter, actorID](EditorContext& value) {
            return RestorePrefabEditorState(value, prefabPath, prefabAfter, sceneAfter, actorID);
        },
        [prefabPath, prefabBefore, sceneBefore, actorID](EditorContext& value) {
            return RestorePrefabEditorState(value, prefabPath, prefabBefore, sceneBefore, actorID);
        }), context);
    RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                              ElapsedOperatorMs(start), ok,
                              "overrideIndex=" + std::to_string(overrideIndex));
    return ok;
}

bool EditorPrefabOperator::RevertOverride(EditorContext& context, uint64_t actorID,
                                          size_t overrideIndex) const
{
    const auto start = EditorOperatorClock::now();
    Actor* actor = ResolveEditablePrefabActor(context, actorID);
    if (!actor) {
        RecordPrefabOperatorEvent(context, "Revert Prefab Override", actorID,
                                  ElapsedOperatorMs(start), false,
                                  "overrideIndex=" + std::to_string(overrideIndex));
        return false;
    }
    const bool ok = CommitPrefabSceneSnapshot(context, *actor, "Revert Prefab Override",
        [actorID, overrideIndex, &context](Actor&, std::string* error) {
            return RevertSinglePrefabOverrideNow(context, actorID, overrideIndex, error);
        });
    RecordPrefabOperatorEvent(context, "Revert Prefab Override", actorID,
                              ElapsedOperatorMs(start), ok,
                              "overrideIndex=" + std::to_string(overrideIndex));
    return ok;
}

bool EditorPrefabOperator::ApplyOverride(EditorContext& context, uint64_t actorID,
                                         const std::string& overridePath) const
{
    const auto start = EditorOperatorClock::now();
    const auto overrides = GetOverrides(context, actorID);
    for (const auto& item : overrides) {
        if (item.path == overridePath || item.label == overridePath) {
            return ApplyOverride(context, actorID, item.index);
        }
    }
    RecordPrefabOperatorEvent(context, "Apply Prefab Override", actorID,
                              ElapsedOperatorMs(start), false,
                              "overridePath=" + overridePath);
    return false;
}

bool EditorPrefabOperator::RevertOverride(EditorContext& context, uint64_t actorID,
                                          const std::string& overridePath) const
{
    const auto start = EditorOperatorClock::now();
    const auto overrides = GetOverrides(context, actorID);
    for (const auto& item : overrides) {
        if (item.path == overridePath || item.label == overridePath) {
            return RevertOverride(context, actorID, item.index);
        }
    }
    RecordPrefabOperatorEvent(context, "Revert Prefab Override", actorID,
                              ElapsedOperatorMs(start), false,
                              "overridePath=" + overridePath);
    return false;
}

