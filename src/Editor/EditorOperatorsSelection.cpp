#include "Editor/EditorOperatorShared.h"

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


bool EditorSelectionOperator::SelectActorRange(
    EditorContext& context, uint64_t anchorActorID, uint64_t targetActorID,
    const std::vector<uint64_t>& orderedActorIDs) const
{
    Scene* scene = context.GetInspectorScene();
    if (!scene || anchorActorID == 0 || targetActorID == 0) return false;
    Actor* targetActor = scene->FindByID(targetActorID);
    if (!scene->FindByID(anchorActorID) || !targetActor) return false;

    std::vector<uint64_t> ordered = orderedActorIDs;
    if (ordered.empty()) {
        std::function<void(Actor*)> collect = [&](Actor* actor) {
            if (!actor) return;
            ordered.push_back(actor->GetID());
            for (Actor* child : actor->GetChildren()) collect(child);
        };
        for (Actor* root : scene->GetRootActors()) collect(root);
    }

    auto anchorIt = std::find(ordered.begin(), ordered.end(), anchorActorID);
    auto targetIt = std::find(ordered.begin(), ordered.end(), targetActorID);
    if (anchorIt == ordered.end() || targetIt == ordered.end()) return false;
    if (targetIt < anchorIt) std::swap(anchorIt, targetIt);

    const EditorSelectionWorldKind world =
        context.GetSceneViewMode() == EditorWorldViewMode::PlayWorldInspect
            ? EditorSelectionWorldKind::Play
            : EditorSelectionWorldKind::Editor;

    context.GetSelection().Clear();
    for (auto it = anchorIt; it <= targetIt; ++it) {
        if (Actor* actor = scene->FindByID(*it)) {
            context.GetSelection().Select(
                EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID(), world),
                EditorSelectionMode::Add);
        }
    }
    context.GetSelection().Select(
        EditorSelectObject::MakeActor(targetActor->GetHandle(), targetActor->GetID(), world),
        EditorSelectionMode::Add);
    return context.GetSelection().IsSelected(targetActorID, world);
}

bool EditorSelectionOperator::SelectActorSubtree(EditorContext& context,
                                                 uint64_t actorID,
                                                 bool includeRoot) const
{
    Scene* scene = context.GetInspectorScene();
    Actor* root = scene ? scene->FindByID(actorID) : nullptr;
    if (!root) return false;

    std::vector<Actor*> actors;
    if (includeRoot) actors.push_back(root);
    std::function<void(Actor*)> collectChildren = [&](Actor* actor) {
        if (!actor) return;
        for (Actor* child : actor->GetChildren()) {
            if (!child) continue;
            actors.push_back(child);
            collectChildren(child);
        }
    };
    collectChildren(root);
    if (actors.empty()) return false;

    const EditorSelectionWorldKind world =
        context.GetSceneViewMode() == EditorWorldViewMode::PlayWorldInspect
            ? EditorSelectionWorldKind::Play
            : EditorSelectionWorldKind::Editor;

    context.GetSelection().Clear();
    for (Actor* actor : actors) {
        context.GetSelection().Select(
            EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID(), world),
            EditorSelectionMode::Add);
    }
    return context.GetSelection().IsSelected(actors.back()->GetID(), world);
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

