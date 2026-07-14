#include "Editor/EditorOperatorShared.h"

bool EditorAssetOperator::Refresh(EditorContext& context) const {
    EditorAssetRegistry* registry = context.GetAssetRegistry();
    if (!registry)
        return false;
    registry->Refresh();
    return true;
}

bool EditorAssetOperator::WatchIfDue(EditorContext& context, float deltaSeconds, float& accumulator,
                                     float intervalSeconds) const {
    accumulator += deltaSeconds;
    if (accumulator < intervalSeconds)
        return false;
    accumulator = 0.0f;
    EditorAssetRegistry* registry = context.GetAssetRegistry();
    return registry && registry->WatchForChanges();
}

bool EditorAssetOperator::CreateFolder(EditorContext& context, const std::string& folderPath) const {
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path folder = ResolveEditorAssetPath(context, folderPath);
    if (folder.empty() || ContentOrSourceRoot(context, folder).empty() || std::filesystem::exists(folder)) {
        RecordAssetOperatorEvent(context, "Create Folder", folderPath, ElapsedOperatorMs(start), false);
        return false;
    }
    auto execute = [folder](EditorContext&) {
        std::error_code error;
        return std::filesystem::create_directories(folder, error) && !error;
    };
    auto undo = [folder](EditorContext&) {
        std::error_code error;
        return std::filesystem::remove(folder, error) && !error;
    };
    EditorCommandStack* stack = context.GetCommandStack();
    const bool ok =
        stack ? stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>("Create Folder", execute, undo), context)
              : execute(context);
    if (ok)
        Refresh(context);
    RecordAssetOperatorEvent(context, "Create Folder", folder.string(), ElapsedOperatorMs(start), ok);
    return ok;
}

bool EditorAssetOperator::RenameFolder(EditorContext& context, const std::string& folderPath,
                                       const std::string& newNameOrPath) const {
    const std::filesystem::path folder = ResolveEditorAssetPath(context, folderPath);
    const std::filesystem::path requested(newNameOrPath);
    const std::filesystem::path target = requested.is_absolute() ? requested : folder.parent_path() / requested;
    const std::filesystem::path root = ContentOrSourceRoot(context, folder);
    if (newNameOrPath.empty() || !std::filesystem::is_directory(folder) || root.empty() ||
        NormalizeAbsolute(folder) == NormalizeAbsolute(root) || ContentOrSourceRoot(context, target).empty()) {
        return false;
    }
    const bool ok = RenameAsset(context, folder.string(), target.string());
    if (ok)
        EditorSelectionOperator{}.Clear(context);
    return ok;
}

bool EditorAssetOperator::DeleteFolder(EditorContext& context, const std::string& folderPath) const {
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path folder = ResolveEditorAssetPath(context, folderPath);
    const std::filesystem::path root = ContentOrSourceRoot(context, folder);
    if (!std::filesystem::is_directory(folder) || root.empty() ||
        NormalizeAbsolute(folder) == NormalizeAbsolute(root)) {
        RecordAssetOperatorEvent(context, "Delete Folder", folderPath, ElapsedOperatorMs(start), false);
        return false;
    }

    FolderSnapshot snapshot;
    if (!CaptureFolderSnapshot(folder, snapshot)) {
        RecordAssetOperatorEvent(context, "Delete Folder", folderPath, ElapsedOperatorMs(start), false);
        return false;
    }
    std::vector<AssetRecord> databaseRecords = CaptureAssetDatabaseRecordsUnderRoot(context, folder);

    auto execute = [snapshot, databaseRecords](EditorContext& commandContext) {
        if (!RemoveAssetDatabaseRecords(commandContext, databaseRecords))
            return false;
        const bool ok = DeleteFolderSnapshot(snapshot);
        if (!ok) {
            RestoreAssetDatabaseRecords(commandContext, databaseRecords);
            return false;
        }
        RefreshAssetRegistryIfPresent(commandContext);
        return ok;
    };
    auto undo = [snapshot, databaseRecords](EditorContext& commandContext) {
        const bool ok = RestoreFolderSnapshot(snapshot);
        if (!ok)
            return false;
        if (!RestoreAssetDatabaseRecords(commandContext, databaseRecords))
            return false;
        RefreshAssetRegistryIfPresent(commandContext);
        return ok;
    };
    EditorCommandStack* stack = context.GetCommandStack();
    const bool ok =
        stack ? stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>("Delete Folder", execute, undo), context)
              : execute(context);
    if (ok)
        Refresh(context);
    RecordAssetOperatorEvent(context, "Delete Folder", folder.string(), ElapsedOperatorMs(start), ok);
    return ok;
}

bool EditorAssetOperator::DeleteAsset(EditorContext& context, const std::string& path) const {
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path resolved = ResolveEditorAssetPath(context, path);
    if (resolved.empty() || ContentOrSourceRoot(context, resolved).empty() ||
        !std::filesystem::is_regular_file(resolved)) {
        RecordAssetOperatorEvent(context, "Delete Asset", path, ElapsedOperatorMs(start), false);
        return false;
    }
    try {
        const std::string content = ReadFileContent(resolved);
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(std::make_unique<DeleteAssetCommand>(resolved.string(), content), context)) {
                RecordAssetOperatorEvent(context, "Delete Asset", resolved.string(), ElapsedOperatorMs(start), false);
                return false;
            }
        } else {
            std::filesystem::remove(resolved);
        }
        Refresh(context);
        EditorSelectionOperator{}.Clear(context);
        RecordAssetOperatorEvent(context, "Delete Asset", resolved.string(), ElapsedOperatorMs(start), true);
        return true;
    } catch (...) {
        RecordAssetOperatorEvent(context, "Delete Asset", resolved.string(), ElapsedOperatorMs(start), false);
        return false;
    }
}

bool EditorAssetOperator::RenameAsset(EditorContext& context, const std::string& path,
                                      const std::string& newNameOrPath) const {
    const auto start = EditorOperatorClock::now();
    if (path.empty() || newNameOrPath.empty()) {
        RecordAssetOperatorEvent(context, "Rename Asset", path, ElapsedOperatorMs(start), false);
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path src = ResolveEditorAssetPath(context, path);
    const fs::path requested(newNameOrPath);
    const fs::path dst = requested.is_absolute() ? requested : src.parent_path() / requested;
    if (NormalizeAbsolute(src) == NormalizeAbsolute(dst) || ContentOrSourceRoot(context, src).empty() ||
        ContentOrSourceRoot(context, dst).empty() || AssetRenameTargetExists(src, dst)) {
        RecordAssetOperatorEvent(context, "Rename Asset", src.string(), ElapsedOperatorMs(start), false,
                                 "target=" + dst.string());
        return false;
    }
    try {
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(std::make_unique<RenameAssetCommand>(src.string(), dst.string()), context)) {
                RecordAssetOperatorEvent(context, "Rename Asset", src.string(), ElapsedOperatorMs(start), false,
                                         "target=" + dst.string());
                return false;
            }
        } else {
            fs::rename(src, dst);
        }
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, dst.string());
        RecordAssetOperatorEvent(context, "Rename Asset", src.string(), ElapsedOperatorMs(start), true,
                                 "target=" + dst.string());
        return true;
    } catch (...) {
        RecordAssetOperatorEvent(context, "Rename Asset", src.string(), ElapsedOperatorMs(start), false,
                                 "target=" + dst.string());
        return false;
    }
}

bool EditorAssetOperator::MoveAsset(EditorContext& context, const std::string& path,
                                    const std::string& targetFolder) const {
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path src = ResolveEditorAssetPath(context, path);
    const std::filesystem::path folder = ResolveEditorAssetPath(context, targetFolder);
    if (!std::filesystem::is_regular_file(src) || !std::filesystem::is_directory(folder) ||
        ContentOrSourceRoot(context, src).empty() || ContentOrSourceRoot(context, folder).empty()) {
        RecordAssetOperatorEvent(context, "Move Asset", path, ElapsedOperatorMs(start), false,
                                 "targetFolder=" + targetFolder);
        return false;
    }
    const bool ok = RenameAsset(context, src.string(), (folder / src.filename()).string());
    RecordAssetOperatorEvent(context, "Move Asset", src.string(), ElapsedOperatorMs(start), ok,
                             "targetFolder=" + folder.string());
    return ok;
}

bool EditorAssetOperator::MoveFolder(EditorContext& context, const std::string& folderPath,
                                     const std::string& targetFolder) const {
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path src = ResolveEditorAssetPath(context, folderPath);
    const std::filesystem::path folder = ResolveEditorAssetPath(context, targetFolder);
    const std::filesystem::path root = ContentOrSourceRoot(context, src);
    if (!std::filesystem::is_directory(src) || !std::filesystem::is_directory(folder) || root.empty() ||
        ContentOrSourceRoot(context, folder).empty() || NormalizeAbsolute(src) == NormalizeAbsolute(root)) {
        RecordAssetOperatorEvent(context, "Move Folder", folderPath, ElapsedOperatorMs(start), false,
                                 "targetFolder=" + targetFolder);
        return false;
    }
    if (IsWithinRoot(folder, src)) {
        RecordAssetOperatorEvent(context, "Move Folder", src.string(), ElapsedOperatorMs(start), false,
                                 "targetFolder=" + folder.string());
        return false;
    }
    const bool ok = RenameAsset(context, src.string(), (folder / src.filename()).string());
    if (ok)
        EditorSelectionOperator{}.Clear(context);
    RecordAssetOperatorEvent(context, "Move Folder", src.string(), ElapsedOperatorMs(start), ok,
                             "targetFolder=" + folder.string());
    return ok;
}

bool EditorAssetOperator::CopyAssetToFolder(EditorContext& context, const std::string& path,
                                            const std::string& targetFolder) const {
    const auto start = EditorOperatorClock::now();
    if (path.empty() || targetFolder.empty()) {
        RecordAssetOperatorEvent(context, "Copy Asset", path, ElapsedOperatorMs(start), false,
                                 "targetFolder=" + targetFolder);
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path src = ResolveEditorAssetPath(context, path);
    const fs::path folder = ResolveEditorAssetPath(context, targetFolder);
    if (ContentOrSourceRoot(context, src).empty() || ContentOrSourceRoot(context, folder).empty() ||
        !fs::is_regular_file(src) || !fs::is_directory(folder)) {
        RecordAssetOperatorEvent(context, "Copy Asset", src.string(), ElapsedOperatorMs(start), false,
                                 "targetFolder=" + folder.string());
        return false;
    }

    fs::path dst = folder / src.filename();
    if (NormalizeAbsolute(dst) == NormalizeAbsolute(src) || fs::exists(dst)) {
        dst = MakeUniquePath(folder, src.stem().string() + "_Copy", src.extension().string());
    }

    try {
        const std::string content = ReadFileContent(src);
        EditorCommandStack* stack = context.GetCommandStack();
        const bool ok =
            stack ? stack->ExecuteCommand(std::make_unique<CreateAssetCommand>(dst.string(), content), context)
                  : (WriteFileContent(dst, content) && EnsureAssetMeta(dst));
        if (!ok) {
            RecordAssetOperatorEvent(context, "Copy Asset", src.string(), ElapsedOperatorMs(start), false,
                                     "target=" + dst.string());
            return false;
        }
        AssetManager::Get().Load<Asset>(dst.string());
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, dst.string());
        RecordAssetOperatorEvent(context, "Copy Asset", src.string(), ElapsedOperatorMs(start), true,
                                 "target=" + dst.string());
        return true;
    } catch (...) {
        RecordAssetOperatorEvent(context, "Copy Asset", src.string(), ElapsedOperatorMs(start), false,
                                 "targetFolder=" + folder.string());
        return false;
    }
}

bool EditorAssetOperator::DuplicateAsset(EditorContext& context, const std::string& path) const {
    const auto start = EditorOperatorClock::now();
    if (path.empty()) {
        RecordAssetOperatorEvent(context, "Duplicate Asset", path, ElapsedOperatorMs(start), false);
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path src = ResolveEditorAssetPath(context, path);
    if (ContentOrSourceRoot(context, src).empty() || !std::filesystem::is_regular_file(src)) {
        RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(), ElapsedOperatorMs(start), false);
        return false;
    }
    const fs::path dst = MakeUniquePath(src.parent_path(), src.stem().string() + "_Copy", src.extension().string());
    try {
        const std::string content = ReadFileContent(src);
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(std::make_unique<CreateAssetCommand>(dst.string(), content), context)) {
                RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(), ElapsedOperatorMs(start), false,
                                         "target=" + dst.string());
                return false;
            }
        } else {
            if (!WriteFileContent(dst, content) || !EnsureAssetMeta(dst)) {
                RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(), ElapsedOperatorMs(start), false,
                                         "target=" + dst.string());
                return false;
            }
        }
        AssetManager::Get().Load<Asset>(dst.string());
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, dst.string());
        RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(), ElapsedOperatorMs(start), true,
                                 "target=" + dst.string());
        return true;
    } catch (...) {
        RecordAssetOperatorEvent(context, "Duplicate Asset", src.string(), ElapsedOperatorMs(start), false);
        return false;
    }
}

bool EditorAssetOperator::CreateAssetFromTemplate(EditorContext& context, const std::string& folderPath,
                                                  const std::string& templateID) const {
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path folder = ResolveEditorAssetPath(context, folderPath);
    if (folder.empty() || ContentOrSourceRoot(context, folder).empty()) {
        RecordAssetOperatorEvent(context, "Create Asset From Template", folderPath, ElapsedOperatorMs(start), false,
                                 "template=" + templateID);
        return false;
    }
    std::error_code error;
    if (std::filesystem::exists(folder, error) && !std::filesystem::is_directory(folder, error)) {
        RecordAssetOperatorEvent(context, "Create Asset From Template", folder.string(), ElapsedOperatorMs(start),
                                 false, "template=" + templateID);
        return false;
    }
    const std::filesystem::path path =
        MakeUniquePath(folder, TemplateBaseNameFor(templateID), TemplateExtensionFor(templateID));
    if (templateID == "prefab") {
        if (!CreatePrefabTemplateAsset(context, path)) {
            RecordAssetOperatorEvent(context, "Create Asset From Template", path.string(), ElapsedOperatorMs(start),
                                     false, "template=" + templateID);
            return false;
        }
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, path.string());
        RecordAssetOperatorEvent(context, "Create Asset From Template", path.string(), ElapsedOperatorMs(start), true,
                                 "template=" + templateID);
        return true;
    }
    EditorCommandStack* stack = context.GetCommandStack();
    if (!stack || !stack->ExecuteCommand(
                      std::make_unique<CreateAssetCommand>(path.string(), TemplateContentFor(templateID)), context)) {
        RecordAssetOperatorEvent(context, "Create Asset From Template", path.string(), ElapsedOperatorMs(start), false,
                                 "template=" + templateID);
        return false;
    }
    Refresh(context);
    EditorSelectionOperator{}.SelectAsset(context, path.string());
    RecordAssetOperatorEvent(context, "Create Asset From Template", path.string(), ElapsedOperatorMs(start), true,
                             "template=" + templateID);
    return true;
}

bool EditorAssetOperator::OpenAsset(EditorContext& context, const std::string& path) const {
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path assetPath = ResolveEditorAssetPath(context, path);
    if (assetPath.empty() || !std::filesystem::exists(assetPath)) {
        RecordAssetOperatorEvent(context, "Open Asset", path, ElapsedOperatorMs(start), false);
        return false;
    }
    const EditorAssetType type = EditorAssetRegistry::Classify(assetPath);
    if (type == EditorAssetType::Scene) {
        SceneRenderLayer* layer = context.GetSceneLayer();
        if (!layer) {
            RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(), ElapsedOperatorMs(start), false,
                                     "type=Scene");
            return false;
        }
        if (layer->IsDirty()) {
            Logger::Warn("[Editor] Refusing to open scene asset with unsaved changes: ", assetPath.string());
            RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(), ElapsedOperatorMs(start), false,
                                     "type=Scene;dirtyScene=true");
            return false;
        }
        if (layer->LoadScene(assetPath.string())) {
            context.SetSceneViewMode(EditorWorldViewMode::EditorWorld);
            context.GetSelection().Clear();
            if (EditorCommandStack* stack = context.GetCommandStack())
                stack->Clear();
            if (EditorProject* project = context.GetProject())
                project->SetLastScenePath(assetPath.string());
            RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(), ElapsedOperatorMs(start), true,
                                     "type=Scene");
            return true;
        }
        RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(), ElapsedOperatorMs(start), false,
                                 "type=Scene");
        return false;
    }
    if (IsTextLikeAsset(assetPath, type) && OpenExternalFile(assetPath)) {
        RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(), ElapsedOperatorMs(start), true,
                                 "type=ExternalText");
        return true;
    }
    EditorSelectionOperator{}.SelectAsset(context, assetPath.string());
    context.RequestPanelFocus("inspector");
    Logger::Info("[Editor] Open asset: ", assetPath.string());
    RecordAssetOperatorEvent(context, "Open Asset", assetPath.string(), ElapsedOperatorMs(start), true,
                             "type=Inspector");
    return true;
}

bool EditorAssetOperator::RevealAsset(EditorContext& context, const std::string& path) const {
    const auto start = EditorOperatorClock::now();
    const std::filesystem::path assetPath = ResolveEditorAssetPath(context, path);
    if (assetPath.empty() || !std::filesystem::exists(assetPath)) {
        RecordAssetOperatorEvent(context, "Reveal Asset", path, ElapsedOperatorMs(start), false);
        return false;
    }
#if defined(_WIN32)
    const bool ok = RevealExternalPath(assetPath);
    RecordAssetOperatorEvent(context, "Reveal Asset", assetPath.string(), ElapsedOperatorMs(start), ok);
    return ok;
#else
    Logger::Info("[Editor] Reveal asset: ", assetPath.string());
    RecordAssetOperatorEvent(context, "Reveal Asset", assetPath.string(), ElapsedOperatorMs(start), true);
    return true;
#endif
}

std::vector<EditorAssetOperator::SceneReferenceInfo>
EditorAssetOperator::FindSceneReferences(EditorContext& context, const std::string& path) const {
    const auto start = EditorOperatorClock::now();
    std::vector<SceneReferenceInfo> result;
    Scene* scene = context.GetInspectorScene();
    const std::filesystem::path assetPath = ResolveEditorAssetPath(context, path);
    if (!scene || assetPath.empty() || ContentOrSourceRoot(context, assetPath).empty()) {
        RecordAssetOperatorEvent(context, "Find Scene References", path, ElapsedOperatorMs(start), false);
        return result;
    }

    const std::filesystem::path normalizedAsset = NormalizeAbsolute(assetPath);
    std::string scenePath;
    if (const SceneRenderLayer* layer = context.GetSceneLayer()) {
        if (layer->HasFilePath()) {
            scenePath = ProjectRelativeReferencePath(context, std::filesystem::path(layer->GetSceneFilePath()));
        }
    }
    scene->ForEach([&](Actor& actor) {
        if (!actor.GetPrefabAssetPath().empty() &&
            AssetReferenceStringMatches(context, normalizedAsset, actor.GetPrefabAssetPath())) {
            SceneReferenceInfo info;
            info.scenePath = scenePath;
            info.actorID = actor.GetID();
            info.actorName = actor.GetName();
            info.componentType = "Prefab";
            info.jsonPath = "/prefab";
            info.valuePreview = actor.GetPrefabAssetPath();
            result.push_back(std::move(info));
        }

        actor.ForEachComponent([&](Component& component) {
            nlohmann::json data = nlohmann::json::object();
            component.Serialize(data);
            FindAssetReferencesInJson(context, normalizedAsset, data, "", result, scenePath, actor.GetID(),
                                      actor.GetName(), component.GetTypeName());
        });
    });

    std::stable_sort(result.begin(), result.end(), [](const SceneReferenceInfo& left, const SceneReferenceInfo& right) {
        if (left.actorName != right.actorName)
            return left.actorName < right.actorName;
        if (left.actorID != right.actorID)
            return left.actorID < right.actorID;
        if (left.componentType != right.componentType) {
            return left.componentType < right.componentType;
        }
        return left.jsonPath < right.jsonPath;
    });

    RecordAssetOperatorEvent(context, "Find Scene References", normalizedAsset.string(), ElapsedOperatorMs(start), true,
                             "count=" + std::to_string(result.size()));
    return result;
}

std::vector<EditorAssetOperator::SceneReferenceInfo>
EditorAssetOperator::FindProjectSceneReferences(EditorContext& context, const std::string& path) const {
    const auto start = EditorOperatorClock::now();
    std::vector<SceneReferenceInfo> result;
    const std::filesystem::path assetPath = ResolveEditorAssetPath(context, path);
    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (assetPath.empty() || contentRoot.empty() || ContentOrSourceRoot(context, assetPath).empty() ||
        !std::filesystem::exists(contentRoot)) {
        RecordAssetOperatorEvent(context, "Find Project Scene References", path, ElapsedOperatorMs(start), false);
        return result;
    }

    const std::filesystem::path normalizedAsset = NormalizeAbsolute(assetPath);
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(
             contentRoot, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (error || !it->is_regular_file(error))
            continue;
        const std::filesystem::path scenePath = it->path();
        if (EditorAssetRegistry::Classify(scenePath) != EditorAssetType::Scene) {
            continue;
        }

        std::ifstream input(scenePath, std::ios::binary);
        if (!input)
            continue;
        nlohmann::json sceneJson;
        try {
            input >> sceneJson;
        } catch (...) {
            continue;
        }

        FindAssetReferencesInSceneJson(context, normalizedAsset, sceneJson,
                                       ProjectRelativeReferencePath(context, scenePath), result);
    }

    std::stable_sort(result.begin(), result.end(), [](const SceneReferenceInfo& left, const SceneReferenceInfo& right) {
        if (left.scenePath != right.scenePath)
            return left.scenePath < right.scenePath;
        if (left.actorName != right.actorName)
            return left.actorName < right.actorName;
        if (left.actorID != right.actorID)
            return left.actorID < right.actorID;
        if (left.componentType != right.componentType) {
            return left.componentType < right.componentType;
        }
        return left.jsonPath < right.jsonPath;
    });

    RecordAssetOperatorEvent(context, "Find Project Scene References", normalizedAsset.string(),
                             ElapsedOperatorMs(start), true, "count=" + std::to_string(result.size()));
    return result;
}

size_t EditorAssetOperator::RetargetSceneReferences(EditorContext& context, const std::string& oldPath,
                                                    const std::string& newPath) const {
    const auto start = EditorOperatorClock::now();
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    const std::filesystem::path oldAssetPath = ResolveEditorAssetPath(context, oldPath);
    const std::filesystem::path newAssetPath = ResolveEditorAssetPath(context, newPath);
    if (!scene || !stack || !context.CanEditScene() || oldAssetPath.empty() || newAssetPath.empty() ||
        ContentOrSourceRoot(context, oldAssetPath).empty() || ContentOrSourceRoot(context, newAssetPath).empty()) {
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath);
        return 0;
    }

    const std::string before = SceneSerializer::SaveToString(*scene);
    nlohmann::json sceneJson;
    try {
        sceneJson = nlohmann::json::parse(before);
    } catch (...) {
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath);
        return 0;
    }

    const size_t changedCount = RetargetAssetReferencesInJson(context, NormalizeAbsolute(oldAssetPath),
                                                              NormalizeAbsolute(newAssetPath), sceneJson);
    if (changedCount == 0) {
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath + ";unchanged=true");
        return 0;
    }

    const uint64_t selection = context.GetSelection().GetActorID();
    const std::string candidate = sceneJson.dump(2);
    if (!SceneSerializer::LoadFromString(*scene, candidate)) {
        SceneSerializer::LoadFromString(*scene, before);
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath);
        return 0;
    }
    const std::string after = SceneSerializer::SaveToString(*scene);
    SceneSerializer::LoadFromString(*scene, before);
    if (before == after) {
        RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath + ";unchanged=true");
        return 0;
    }

    const bool ok = stack->ExecuteCommand(
        EditorUndoUtil::MakeSceneSnapshotCommand("Retarget Asset References", before, after, selection, selection),
        context);
    RecordAssetOperatorEvent(context, "Retarget Scene References", oldPath, ElapsedOperatorMs(start), ok,
                             "target=" + newPath + ";count=" + std::to_string(changedCount));
    return ok ? changedCount : 0;
}

size_t EditorAssetOperator::RetargetProjectSceneReferences(EditorContext& context, const std::string& oldPath,
                                                           const std::string& newPath) const {
    const auto start = EditorOperatorClock::now();
    EditorCommandStack* stack = context.GetCommandStack();
    const std::filesystem::path oldAssetPath = ResolveEditorAssetPath(context, oldPath);
    const std::filesystem::path newAssetPath = ResolveEditorAssetPath(context, newPath);
    const std::filesystem::path contentRoot = context.GetContentRoot();
    if (!stack || oldAssetPath.empty() || newAssetPath.empty() || contentRoot.empty() ||
        !std::filesystem::exists(contentRoot) || ContentOrSourceRoot(context, oldAssetPath).empty() ||
        ContentOrSourceRoot(context, newAssetPath).empty()) {
        RecordAssetOperatorEvent(context, "Retarget Project Scene References", oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath);
        return 0;
    }

    std::vector<ModifyAssetsCommand::Entry> entries;
    size_t changedCount = 0;
    std::filesystem::path currentSceneFile;
    if (const SceneRenderLayer* layer = context.GetSceneLayer()) {
        if (layer->HasFilePath()) {
            currentSceneFile = NormalizeAbsolute(layer->GetSceneFilePath());
        }
    }
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(
             contentRoot, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (error || !it->is_regular_file(error))
            continue;
        const std::filesystem::path scenePath = it->path();
        if (EditorAssetRegistry::Classify(scenePath) != EditorAssetType::Scene) {
            continue;
        }
        if (!currentSceneFile.empty() && NormalizeAbsolute(scenePath) == currentSceneFile) {
            continue;
        }

        std::ifstream input(scenePath, std::ios::binary);
        if (!input)
            continue;
        const std::string before{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        nlohmann::json sceneJson;
        try {
            sceneJson = nlohmann::json::parse(before);
        } catch (...) {
            continue;
        }

        const size_t fileChanges = RetargetAssetReferencesInJson(context, NormalizeAbsolute(oldAssetPath),
                                                                 NormalizeAbsolute(newAssetPath), sceneJson);
        if (fileChanges == 0)
            continue;

        const std::string after = sceneJson.dump(2);
        if (before == after)
            continue;
        changedCount += fileChanges;
        entries.push_back({scenePath.string(), before, after});
    }

    if (entries.empty()) {
        RecordAssetOperatorEvent(context, "Retarget Project Scene References", oldPath, ElapsedOperatorMs(start), false,
                                 "target=" + newPath + ";unchanged=true");
        return 0;
    }

    const bool ok = stack->ExecuteCommand(std::make_unique<ModifyAssetsCommand>(std::move(entries)), context);
    RecordAssetOperatorEvent(context, "Retarget Project Scene References", oldPath, ElapsedOperatorMs(start), ok,
                             "target=" + newPath + ";count=" + std::to_string(changedCount));
    return ok ? changedCount : 0;
}

bool EditorAssetOperator::Reimport(EditorContext& context, const std::string& uuid) const {
    const auto start = EditorOperatorClock::now();
    if (uuid.empty()) {
        RecordAssetOperatorEvent(context, "Reimport Asset", uuid, ElapsedOperatorMs(start), false);
        return false;
    }
    EditorImportService* importer = context.GetService<EditorImportService>();
    if (!importer) {
        RecordAssetOperatorEvent(context, "Reimport Asset", uuid, ElapsedOperatorMs(start), false);
        return false;
    }
    const bool result = importer->Reimport(uuid);
    Refresh(context);
    RecordAssetOperatorEvent(context, "Reimport Asset", uuid, ElapsedOperatorMs(start), result);
    return result;
}

bool EditorAssetOperator::ReimportAll(EditorContext& context, std::vector<std::string>* failures) const {
    const auto start = EditorOperatorClock::now();
    EditorImportService* importer = context.GetService<EditorImportService>();
    if (!importer) {
        RecordAssetOperatorEvent(context, "Reimport All", "*", ElapsedOperatorMs(start), false);
        return false;
    }

    std::vector<std::string> localFailures;
    std::vector<std::string>* outputFailures = failures ? failures : &localFailures;
    importer->ReimportAll(outputFailures);
    Refresh(context);
    const bool ok = outputFailures->empty();
    RecordAssetOperatorEvent(context, "Reimport All", "*", ElapsedOperatorMs(start), ok,
                             "failures=" + std::to_string(outputFailures->size()));
    return ok;
}

bool EditorAssetOperator::ReimportWithSettings(EditorContext& context, const std::string& uuid,
                                               const std::string& settingsJson) const {
    const auto start = EditorOperatorClock::now();
    if (uuid.empty() || settingsJson.empty()) {
        RecordAssetOperatorEvent(context, "Update Import Settings", uuid, ElapsedOperatorMs(start), false);
        return false;
    }

    std::string beforeSettings;
    if (!ReadImportSettings(context, uuid, beforeSettings)) {
        RecordAssetOperatorEvent(context, "Update Import Settings", uuid, ElapsedOperatorMs(start), false);
        return false;
    }
    if (beforeSettings == settingsJson) {
        RecordAssetOperatorEvent(context, "Update Import Settings", uuid, ElapsedOperatorMs(start), false,
                                 "unchanged=true");
        return false;
    }

    auto applySettings = [uuid](const std::string& settings) {
        return [uuid, settings](EditorContext& commandContext) {
            return ApplyImportSettings(commandContext, uuid, settings);
        };
    };

    if (EditorCommandStack* stack = context.GetCommandStack()) {
        const bool ok = stack->ExecuteCommand(std::make_unique<LambdaEditorCommand>("Update Import Settings",
                                                                                    applySettings(settingsJson),
                                                                                    applySettings(beforeSettings)),
                                              context);
        RecordAssetOperatorEvent(context, "Update Import Settings", uuid, ElapsedOperatorMs(start), ok);
        return ok;
    }

    const bool ok = ApplyImportSettings(context, uuid, settingsJson);
    RecordAssetOperatorEvent(context, "Update Import Settings", uuid, ElapsedOperatorMs(start), ok);
    return ok;
}
