#include "Editor/EditorOperators.h"

#include "Assets/Asset.h"
#include "Assets/AssetManager.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorService.h"
#include "Game/GameViewport.h"
#include "Game/SceneRenderLayer.h"
#include "Game/SceneViewportController.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

EditorSelectionMode ToSelectionMode(EditorSelectionIntentMode mode)
{
    switch (mode) {
        case EditorSelectionIntentMode::Add: return EditorSelectionMode::Add;
        case EditorSelectionIntentMode::Toggle: return EditorSelectionMode::Toggle;
        default: return EditorSelectionMode::Replace;
    }
}

std::string ReadFileContent(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

uint64_t NextSiblingID(const Actor& actor)
{
    Scene* scene = actor.GetScene();
    if (!scene) return 0;
    const auto& siblings = actor.GetParent()
        ? actor.GetParent()->GetChildren()
        : scene->GetRootActors();
    for (size_t index = 0; index < siblings.size(); ++index) {
        if (siblings[index] == &actor) {
            const size_t next = index + 1;
            return next < siblings.size() && siblings[next] ? siblings[next]->GetID() : 0;
        }
    }
    return 0;
}

uint64_t FindNextActorID(const Scene& scene)
{
    uint64_t maxID = 0;
    scene.ForEach([&](const Actor& actor) {
        if (actor.GetID() > maxID) maxID = actor.GetID();
    });
    return maxID + 1;
}

} // namespace

bool EditorSelectionOperator::SelectActor(EditorContext& context, uint64_t actorID,
                                          EditorSelectionIntentMode mode) const
{
    Scene* scene = context.GetInspectorScene();
    Actor* actor = scene ? scene->FindByID(actorID) : nullptr;
    if (!actor) return false;
    const EditorSelectionWorldKind world =
        context.GetSceneViewMode() == EditorWorldViewMode::PlayWorldInspect
            ? EditorSelectionWorldKind::Play
            : EditorSelectionWorldKind::Editor;
    context.GetSelection().Select(EditorSelectObject::MakeActor(
        actor->GetHandle(), actor->GetID(), world), ToSelectionMode(mode));
    return true;
}

bool EditorSelectionOperator::SelectAsset(EditorContext& context, const std::string& path) const
{
    if (path.empty()) return false;
    context.GetSelection().Select(EditorSelectObject::MakeAsset(path));
    if (EditorProject* project = context.GetProject()) {
        project->GetState().selectedAssetPath = context.GetSelection().GetAssetPath();
    }
    return true;
}

void EditorSelectionOperator::Clear(EditorContext& context) const
{
    context.GetSelection().Clear();
}

EditorSelectionSnapshot EditorSelectionOperator::GetSelectionSnapshot(
    const EditorContext& context) const
{
    const EditorSelection& selection = context.GetSelection();
    EditorSelectionSnapshot snapshot;
    snapshot.actorID = selection.GetActorID();
    snapshot.assetPath = selection.GetAssetPath();
    snapshot.hasActor = selection.HasActor();
    snapshot.hasAsset = selection.HasAsset();
    return snapshot;
}

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

bool EditorDragDropOperator::ApplyActorDrop(EditorContext& context, uint64_t actorID,
                                            uint64_t afterParentID,
                                            uint64_t afterNextSiblingID) const
{
    if (EditorOperators* operators = context.GetOperators()) {
        return operators->Commands().MoveActor(
            context, actorID, afterParentID, afterNextSiblingID);
    }
    EditorCommandOperator commands;
    return commands.MoveActor(context, actorID, afterParentID, afterNextSiblingID);
}

bool EditorDragDropOperator::ApplyAssetDrop(EditorContext& context,
                                            const std::string& assetPath,
                                            const std::string& targetPath) const
{
    (void)context;
    return !assetPath.empty() && !targetPath.empty();
}

void EditorTransactionOperator::BeginSnapshot(EditorSceneTransaction& transaction,
                                              const char* label,
                                              const std::string& beforeJson,
                                              uint64_t selection) const
{
    transaction.Begin(label, beforeJson, selection);
}

bool EditorTransactionOperator::CommitIfChanged(EditorContext& context,
                                                EditorSceneTransaction& transaction) const
{
    return transaction.Commit(context);
}

bool EditorTransactionOperator::CommitSceneSnapshot(
    EditorContext& context, const char* label, const std::string& beforeJson,
    const std::string& afterJson, uint64_t beforeSelection,
    uint64_t afterSelection) const
{
    if (beforeJson.empty() || afterJson.empty() || beforeJson == afterJson) return false;
    Scene* scene = context.GetScene();
    EditorCommandStack* stack = context.GetCommandStack();
    if (!scene || !stack || !context.CanEditScene()) return false;
    SceneSerializer::LoadFromString(*scene, beforeJson);
    return stack->ExecuteCommand(EditorUndoUtil::MakeSceneSnapshotCommand(
        label ? label : "Scene Edit", beforeJson, afterJson,
        beforeSelection, afterSelection), context);
}

void EditorTransactionOperator::Cancel(EditorSceneTransaction& transaction) const
{
    transaction.Cancel();
}

bool EditorAssetOperator::Refresh(EditorContext& context) const
{
    EditorAssetRegistry* registry = context.GetAssetRegistry();
    if (!registry) return false;
    registry->Refresh();
    return true;
}

bool EditorAssetOperator::WatchIfDue(EditorContext& context, float deltaSeconds,
                                     float& accumulator, float intervalSeconds) const
{
    accumulator += deltaSeconds;
    if (accumulator < intervalSeconds) return false;
    accumulator = 0.0f;
    EditorAssetRegistry* registry = context.GetAssetRegistry();
    return registry && registry->WatchForChanges();
}

bool EditorAssetOperator::DeleteAsset(EditorContext& context, const std::string& path) const
{
    if (path.empty()) return false;
    try {
        const std::string content = ReadFileContent(path);
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(std::make_unique<DeleteAssetCommand>(path, content), context)) {
                return false;
            }
        } else {
            std::filesystem::remove(path);
        }
        Refresh(context);
        EditorSelectionOperator{}.Clear(context);
        return true;
    } catch (...) {
        return false;
    }
}

bool EditorAssetOperator::RenameAsset(EditorContext& context, const std::string& path,
                                      const std::string& newNameOrPath) const
{
    if (path.empty() || newNameOrPath.empty()) return false;
    namespace fs = std::filesystem;
    const fs::path src(path);
    const fs::path requested(newNameOrPath);
    const fs::path dst = requested.is_absolute()
        ? requested
        : src.parent_path() / requested;
    if (src == dst) return false;
    try {
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(
                    std::make_unique<RenameAssetCommand>(src.string(), dst.string()), context)) {
                return false;
            }
        } else {
            fs::rename(src, dst);
        }
        Refresh(context);
        EditorSelectionOperator{}.SelectAsset(context, dst.string());
        return true;
    } catch (...) {
        return false;
    }
}

bool EditorAssetOperator::DuplicateAsset(EditorContext& context, const std::string& path) const
{
    if (path.empty()) return false;
    namespace fs = std::filesystem;
    const fs::path src(path);
    const fs::path dst =
        src.parent_path() / (src.stem().string() + "_Copy" + src.extension().string());
    try {
        const std::string content = ReadFileContent(src);
        if (EditorCommandStack* stack = context.GetCommandStack()) {
            if (!stack->ExecuteCommand(
                    std::make_unique<CreateAssetCommand>(dst.string(), content), context)) {
                return false;
            }
        } else {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        }
        AssetManager::Get().Load<Asset>(dst.string());
        Refresh(context);
        return true;
    } catch (...) {
        return false;
    }
}

bool EditorAssetOperator::Reimport(EditorContext& context, const std::string& uuid) const
{
    if (uuid.empty()) return false;
    EditorImportService* importer = context.GetService<EditorImportService>();
    if (!importer) return false;
    const bool result = importer->Reimport(uuid);
    Refresh(context);
    return result;
}

bool EditorViewportOperator::SetSceneViewportRect(EditorContext& context,
                                                  const EditorRect& rect,
                                                  bool hovered) const
{
    SceneViewport* viewport = context.GetSceneViewport();
    if (!viewport || rect.width <= 1.0f || rect.height <= 1.0f) return false;
    viewport->SetViewportRect(static_cast<int>(rect.x), static_cast<int>(rect.y),
                              static_cast<int>(rect.width), static_cast<int>(rect.height));
    viewport->SetInputEnabled(hovered);
    if (SceneRenderLayer* layer = context.GetSceneLayer()) layer->SetSceneViewportActive(true);
    return true;
}

bool EditorViewportOperator::SetGameViewportRect(EditorContext& context,
                                                 const EditorRect& rect) const
{
    GameViewport* viewport = context.GetGameViewport();
    if (!viewport || rect.width <= 1.0f || rect.height <= 1.0f) return false;
    viewport->SetViewportRect(static_cast<int>(rect.x), static_cast<int>(rect.y),
                              static_cast<int>(rect.width), static_cast<int>(rect.height));
    if (SceneRenderLayer* layer = context.GetSceneLayer()) layer->SetGameViewportActive(true);
    return true;
}
