#include "TestHarness.h"

#include "Editor/EditorAction.h"
#include "Editor/AssetImportService.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorLayoutManager.h"
#include "Editor/EditorPanel.h"
#include "Editor/EditorOperators.h"
#include "Editor/EditorPanels.h"
#include "Editor/EditorProfiler.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorRecoveryService.h"
#include "Editor/EditorResourceOperator.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorService.h"
#include "Editor/EditorShortcutMap.h"
#include "Editor/EditorThemeManager.h"
#include "Editor/EditorUndoUtil.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"
#include "Editor/EditorUI/EditorScriptRegistry.h"
#include "Editor/EditorUIScaleManager.h"
#include "Editor/UI/EditorFontManager.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorStatusBar.h"
#include "Editor/UI/EditorTheme.h"
#include "Editor/UI/EditorUIScaleManager.h"
#include "Editor/EditorViewportControllers.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/InspectorSections.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetDatabase.h"
#include "Assets/MaterialAsset.h"
#include "Assets/PrefabAsset.h"
#include "Game/SceneRenderLayer.h"
#include "Assets/AssetMeta.h"
#include "Camera/CameraComponent.h"
#include "Renderer/SceneLighting.h"
#include "Renderer/SkylightComponent.h"
#include "Core/Sha256.h"
#include "Physics/BoxColliderComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Scene/PrefabSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Core/UIComponents.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <imnodes.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

class DockResizeTestPanel final : public EditorPanel {
public:
    DockResizeTestPanel(std::string id, std::string title, std::string area = {})
        : EditorPanel(std::move(id), std::move(title)), m_Area(std::move(area)) {}

    std::string GetDefaultDockArea() const override { return m_Area; }

protected:
    void DrawContent() override { ImGui::TextUnformatted("Dock resize test"); }

private:
    std::string m_Area;
};

bool TestEditorCommandStackAndSelection() {
    Scene scene("EditorCommands");
    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorProfiler profiler;
    context.SetCommandStack(&stack);
    context.SetProfiler(&profiler);

    int value = 0;
    auto makeAdd = [&value](const char* name, int amount) {
        return std::make_unique<LambdaEditorCommand>(
            name,
            [&value, amount](EditorContext&) {
                value += amount;
                return true;
            },
            [&value, amount](EditorContext&) {
                value -= amount;
                return true;
            });
    };

    if (!Check(stack.ExecuteCommand(makeAdd("Add One", 1), context), "editor command execute failed"))
        return false;
    if (!Check(stack.ExecuteCommand(makeAdd("Add Two", 2), context) && value == 3,
               "editor command execute order mismatch"))
        return false;
    if (!Check(stack.Undo(context) && value == 1, "editor command undo failed"))
        return false;
    if (!Check(stack.Redo(context) && value == 3, "editor command redo failed"))
        return false;

    const auto commandEvents = profiler.Snapshot();
    auto hasCommandEvent = [&](const char* operation, const char* commandName) {
        return std::any_of(commandEvents.begin(), commandEvents.end(), [&](const EditorProfilerEvent& event) {
            return event.category == "EditorCommand" && event.name == operation &&
                   event.details.find(std::string("command=") + commandName) != std::string::npos &&
                   event.details.find(";success=true") != std::string::npos &&
                   event.details.find(";dirty=scene") != std::string::npos;
        });
    };
    if (!Check(hasCommandEvent("Execute", "Add One") && hasCommandEvent("Execute", "Add Two") &&
                   hasCommandEvent("Undo", "Add Two") && hasCommandEvent("Redo", "Add Two"),
               "editor command stack did not record profiler execute/undo/redo events")) {
        return false;
    }

    if (!Check(stack.Undo(context), "editor command second undo failed"))
        return false;
    if (!Check(stack.ExecuteCommand(makeAdd("Add Four", 4), context) && value == 5, "editor command after undo failed"))
        return false;
    if (!Check(!stack.CanRedo(), "new command did not invalidate redo"))
        return false;

    if (!Check(stack.BeginTransaction("Batch Edit"), "begin transaction failed"))
        return false;
    if (!Check(stack.ExecuteCommand(makeAdd("Add Ten", 10), context), "first transaction command failed"))
        return false;
    if (!Check(stack.ExecuteCommand(makeAdd("Add Twenty", 20), context), "second transaction command failed"))
        return false;
    if (!Check(stack.CommitTransaction() && value == 35, "commit transaction failed"))
        return false;
    if (!Check(std::string(stack.GetUndoName()) == "Batch Edit", "transaction name was not preserved"))
        return false;
    if (!Check(stack.Undo(context) && value == 5, "transaction undo order mismatch"))
        return false;
    if (!Check(stack.Redo(context) && value == 35, "transaction redo order mismatch"))
        return false;

    Actor* actor = scene.CreateActor("Selected");
    const uint64_t actorID = actor->GetID();
    context.GetSelection().SelectActorID(actorID);
    Transform before = actor->GetTransform();
    Transform after = before;
    after.position = {4.0f, 5.0f, 6.0f};
    if (!Check(stack.ExecuteCommand(std::make_unique<SetActorTransformCommand>(actorID, before, after), context),
               "transform command failed"))
        return false;
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 4.0f), "transform command did not apply"))
        return false;
    if (!Check(stack.Undo(context) && NearlyEqual(actor->GetTransform().position.x, 0.0f), "transform undo failed"))
        return false;

    scene.DestroyActor(actor);
    context.GetSelection().Validate(scene);
    return Check(!context.GetSelection().HasActor() && context.GetSelection().ResolveActor(scene) == nullptr,
                 "selection did not invalidate after actor deletion");
}

bool TestEditorSelectObjectEvents() {
    Scene scene("EditorSelectionEvents");
    Actor* first = scene.CreateActor("First");
    Actor* second = scene.CreateActor("Second");

    EditorSelection selection;
    std::vector<EditorSelectionChangedEvent> events;
    const EditorSelection::ListenerID listenerID = selection.SubscribeSelectionChanged(
        [&events](const EditorSelectionChangedEvent& event) { events.push_back(event); });

    selection.Select(EditorSelectObject::MakeActor(first->GetHandle(), first->GetID()));
    if (!Check(events.size() == 1 && events.back().current.IsActor() &&
                   events.back().current.GetActorID() == first->GetID(),
               "actor selection event mismatch"))
        return false;

    selection.Select(EditorSelectObject::MakeActor(first->GetHandle(), first->GetID()));
    if (!Check(events.size() == 1, "repeated selection emitted a duplicate event"))
        return false;

    selection.Select(EditorSelectObject::MakeAsset("Content/Textures/../Materials/Test.mat"));
    if (!Check(events.size() == 2 && events.back().current.IsAsset() &&
                   events.back().current.GetAssetPath() ==
                       std::filesystem::path("Content/Materials/Test.mat").generic_string(),
               "asset selection was not normalized or notified"))
        return false;

    selection.Select(EditorSelectObject::MakeActor(first->GetHandle(), first->GetID()));
    selection.Select(EditorSelectObject::MakeActor(second->GetHandle(), second->GetID()), EditorSelectionMode::Add);
    if (!Check(events.size() == 4 && selection.IsMultiSelect() && events.back().actorIDs.size() == 2 &&
                   selection.GetPrimaryObject().GetActorID() == second->GetID(),
               "actor multi-selection event mismatch"))
        return false;

    selection.Select(EditorSelectObject::MakeActor(second->GetHandle(), second->GetID()), EditorSelectionMode::Toggle);
    if (!Check(events.size() == 5 && !selection.IsMultiSelect() &&
                   selection.GetPrimaryObject().GetActorID() == first->GetID(),
               "actor toggle did not restore the previous primary selection"))
        return false;

    selection.Clear();
    selection.Clear();
    if (!Check(events.size() == 6 && events.back().current.IsNone(), "clear selection event mismatch"))
        return false;

    selection.SelectActorID(first->GetID());
    scene.DestroyActor(first);
    selection.Validate(scene);
    if (!Check(events.size() == 8 && events.back().current.IsNone(),
               "invalid actor validation did not emit a clear event"))
        return false;

    selection.UnsubscribeSelectionChanged(listenerID);
    selection.SelectAssetPath("Content/Materials/AfterUnsubscribe.mat");
    return Check(events.size() == 8, "unsubscribed selection listener was still invoked");
}

bool TestEditorSceneSnapshotCommands() {
    Scene scene("EditorSnapshots");
    EditorContext context(&scene);
    EditorCommandStack stack;
    context.SetCommandStack(&stack);

    const std::string emptyScene = SceneSerializer::SaveToString(scene);
    Actor* parent = scene.CreateActor("SnapshotParent");
    const uint64_t parentID = parent->GetID();
    Actor* child = scene.CreateActor("SnapshotChild", parent);
    child->AddComponent<BoxColliderComponent>();
    const std::string populatedScene = SceneSerializer::SaveToString(scene);
    if (!Check(SceneSerializer::LoadFromString(scene, emptyScene), "failed to restore snapshot baseline"))
        return false;

    if (!Check(stack.ExecuteCommand(
                   std::make_unique<SceneSnapshotCommand>("Create Hierarchy", emptyScene, populatedScene, 0, parentID),
                   context),
               "scene snapshot execute failed"))
        return false;
    Actor* restoredParent = scene.FindByID(parentID);
    if (!Check(restoredParent && restoredParent->GetChildren().size() == 1, "scene snapshot did not restore hierarchy"))
        return false;
    if (!Check(restoredParent->GetChildren()[0]->GetComponent<BoxColliderComponent>() != nullptr,
               "scene snapshot did not restore component"))
        return false;
    if (!Check(context.GetSelection().GetActorID() == parentID, "scene snapshot did not restore selection"))
        return false;

    if (!Check(stack.Undo(context) && scene.ActorCount() == 0, "scene snapshot undo failed"))
        return false;
    if (!Check(stack.Redo(context), "scene snapshot redo failed"))
        return false;
    restoredParent = scene.FindByID(parentID);
    return Check(restoredParent && restoredParent->GetChildren().size() == 1 &&
                     restoredParent->GetChildren()[0]->GetComponent<BoxColliderComponent>() != nullptr,
                 "scene snapshot redo lost hierarchy or component");
}

bool TestEditorOperatorsSelectionAndCommands() {
    Scene scene("EditorOperators");
    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    Actor* rangeFirst = scene.CreateActor("RangeFirst");
    Actor* rangeSecond = scene.CreateActor("RangeSecond");
    Actor* rangeChild = scene.CreateActor("RangeChild", rangeSecond);
    Actor* rangeThird = scene.CreateActor("RangeThird");
    if (!Check(operators.Selection().SelectActorRange(context, rangeFirst->GetID(), rangeChild->GetID()),
               "operator range selection failed"))
        return false;
    if (!Check(context.GetSelection().GetMultiCount() == 3 && context.GetSelection().IsSelected(rangeFirst->GetID()) &&
                   context.GetSelection().IsSelected(rangeSecond->GetID()) &&
                   context.GetSelection().IsSelected(rangeChild->GetID()) &&
                   context.GetSelection().GetActorID() == rangeChild->GetID(),
               "operator range selection did not follow hierarchy order"))
        return false;

    const std::vector<uint64_t> visibleOrder{rangeThird->GetID(), rangeSecond->GetID(), rangeFirst->GetID()};
    if (!Check(operators.Selection().SelectActorRange(context, rangeThird->GetID(), rangeFirst->GetID(), visibleOrder),
               "operator custom-order range selection failed"))
        return false;
    if (!Check(context.GetSelection().GetMultiCount() == 3 && context.GetSelection().IsSelected(rangeThird->GetID()) &&
                   context.GetSelection().IsSelected(rangeSecond->GetID()) &&
                   context.GetSelection().IsSelected(rangeFirst->GetID()) &&
                   !context.GetSelection().IsSelected(rangeChild->GetID()) &&
                   context.GetSelection().GetActorID() == rangeFirst->GetID(),
               "operator range selection did not honor visible order"))
        return false;
    if (!Check(!operators.Selection().SelectActorRange(context, 999999, rangeFirst->GetID(), visibleOrder),
               "operator range selection accepted invalid anchor"))
        return false;

    Actor* subtreeRoot = scene.CreateActor("SubtreeRoot");
    Actor* subtreeChildA = scene.CreateActor("SubtreeChildA", subtreeRoot);
    Actor* subtreeChildB = scene.CreateActor("SubtreeChildB", subtreeRoot);
    Actor* subtreeGrandchild = scene.CreateActor("SubtreeGrandchild", subtreeChildA);
    if (!Check(operators.Selection().SelectActorSubtree(context, subtreeRoot->GetID(), false),
               "operator select children failed"))
        return false;
    if (!Check(context.GetSelection().GetMultiCount() == 3 &&
                   !context.GetSelection().IsSelected(subtreeRoot->GetID()) &&
                   context.GetSelection().IsSelected(subtreeChildA->GetID()) &&
                   context.GetSelection().IsSelected(subtreeGrandchild->GetID()) &&
                   context.GetSelection().IsSelected(subtreeChildB->GetID()) &&
                   context.GetSelection().GetActorID() == subtreeChildB->GetID(),
               "operator select children did not select only descendants"))
        return false;
    if (!Check(operators.Selection().SelectActorSubtree(context, subtreeRoot->GetID(), true) &&
                   context.GetSelection().GetMultiCount() == 4 &&
                   context.GetSelection().IsSelected(subtreeRoot->GetID()),
               "operator select subtree did not include root"))
        return false;
    if (!Check(!operators.Selection().SelectActorSubtree(context, subtreeGrandchild->GetID(), false),
               "operator select children accepted actor without children"))
        return false;

    auto executeSceneSetting = [&](std::unique_ptr<IEditorCommand> command, const char* failure) {
        return Check(stack.ExecuteCommand(std::move(command), context), failure);
    };
    if (!executeSceneSetting(std::make_unique<LambdaEditorCommand>(
                                 "Set Scene Name",
                                 [](EditorContext& value) {
                                     Scene* scene = value.GetScene();
                                     if (!scene)
                                         return false;
                                     scene->SetName("EditedScene");
                                     return true;
                                 },
                                 [](EditorContext& value) {
                                     Scene* scene = value.GetScene();
                                     if (!scene)
                                         return false;
                                     scene->SetName("EditorOperators");
                                     return true;
                                 }),
                             "scene settings name command failed"))
        return false;
    if (!Check(scene.GetName() == "EditedScene", "scene settings name command did not apply"))
        return false;
    if (!Check(stack.Undo(context) && scene.GetName() == "EditorOperators", "scene settings name command undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.GetName() == "EditedScene", "scene settings name command redo failed"))
        return false;

    const Vec3 originalGravity = scene.GetPhysicsWorld().GetGravity();
    const Vec3 editedGravity{0.0f, -4.0f, 0.0f};
    if (!executeSceneSetting(std::make_unique<LambdaEditorCommand>(
                                 "Set Scene Gravity",
                                 [editedGravity](EditorContext& value) {
                                     Scene* scene = value.GetScene();
                                     if (!scene)
                                         return false;
                                     scene->GetPhysicsWorld().SetGravity(editedGravity);
                                     return true;
                                 },
                                 [originalGravity](EditorContext& value) {
                                     Scene* scene = value.GetScene();
                                     if (!scene)
                                         return false;
                                     scene->GetPhysicsWorld().SetGravity(originalGravity);
                                     return true;
                                 }),
                             "scene settings gravity command failed"))
        return false;
    if (!Check(scene.GetPhysicsWorld().GetGravity().y == -4.0f, "scene settings gravity command did not apply"))
        return false;
    if (!Check(stack.Undo(context) && scene.GetPhysicsWorld().GetGravity().y == originalGravity.y,
               "scene settings gravity command undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.GetPhysicsWorld().GetGravity().y == -4.0f,
               "scene settings gravity command redo failed"))
        return false;

    Actor* sceneCameraActor = scene.CreateActor("SceneHintCamera");
    sceneCameraActor->AddComponent<CameraComponent>();
    if (!Check(operators.Commands().SetSceneMainCameraHint(context, sceneCameraActor->GetID()),
               "scene settings main camera hint command failed"))
        return false;
    if (!Check(scene.GetMainCameraHintActorID() == sceneCameraActor->GetID(),
               "scene settings main camera hint did not apply"))
        return false;
    if (!Check(stack.Undo(context) && scene.GetMainCameraHintActorID() == 0,
               "scene settings main camera hint undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.GetMainCameraHintActorID() == sceneCameraActor->GetID(),
               "scene settings main camera hint redo failed"))
        return false;

    if (!Check(operators.Commands().SetSceneAmbientIntensity(context, 0.35f),
               "scene settings ambient intensity command failed"))
        return false;
    if (!Check(scene.GetAmbientIntensity() == 0.35f, "scene settings ambient intensity did not apply"))
        return false;
    if (!Check(stack.Undo(context) && scene.GetAmbientIntensity() == 1.0f,
               "scene settings ambient intensity undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.GetAmbientIntensity() == 0.35f,
               "scene settings ambient intensity redo failed"))
        return false;
    if (!Check(operators.Commands().SetSceneAmbientIntensity(context, -1.0f) && scene.GetAmbientIntensity() == 0.0f,
               "scene settings ambient intensity should clamp negative values"))
        return false;

    const uint64_t actorID = operators.Commands().CreateActor(context, "OperatorActor");
    Actor* actor = scene.FindByID(actorID);
    if (!Check(actor && actor->GetName() == "OperatorActor", "operator create actor failed"))
        return false;
    if (!Check(context.GetSelection().GetActorID() == actorID,
               "operator create actor did not preserve command selection semantics"))
        return false;

    if (!Check(operators.Commands().RenameActor(context, actorID, "Renamed"), "operator rename actor failed"))
        return false;
    if (!Check(actor->GetName() == "Renamed", "operator rename did not apply"))
        return false;
    if (!Check(stack.Undo(context) && actor->GetName() == "OperatorActor", "operator rename undo failed"))
        return false;
    if (!Check(stack.Redo(context) && actor->GetName() == "Renamed", "operator rename redo failed"))
        return false;

    if (!Check(operators.Commands().SetActorActive(context, actorID, false), "operator active toggle failed"))
        return false;
    if (!Check(!actor->IsActiveSelf(), "operator active toggle did not apply"))
        return false;
    if (!Check(stack.Undo(context) && actor->IsActiveSelf(), "operator active toggle undo failed"))
        return false;

    if (!Check(operators.Commands().SetActorTag(context, actorID, "Player"), "operator tag edit failed"))
        return false;
    if (!Check(actor->GetTag() == "Player", "operator tag edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && actor->GetTag().empty(), "operator tag edit undo failed"))
        return false;
    if (!Check(stack.Redo(context) && actor->GetTag() == "Player", "operator tag edit redo failed"))
        return false;

    if (!Check(operators.Commands().SetActorLayer(context, actorID, 7), "operator layer edit failed"))
        return false;
    if (!Check(actor->GetLayer() == 7, "operator layer edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && actor->GetLayer() == 0, "operator layer edit undo failed"))
        return false;
    if (!Check(stack.Redo(context) && actor->GetLayer() == 7, "operator layer edit redo failed"))
        return false;

    if (!Check(operators.Commands().SetActorStatic(context, actorID, true), "operator static edit failed"))
        return false;
    if (!Check(actor->IsStatic(), "operator static edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && !actor->IsStatic(), "operator static edit undo failed"))
        return false;
    if (!Check(stack.Redo(context) && actor->IsStatic(), "operator static edit redo failed"))
        return false;
    const std::string staticSceneJson = SceneSerializer::SaveToString(scene);
    Scene staticRoundTrip("RoundTrip");
    if (!Check(SceneSerializer::LoadFromString(staticRoundTrip, staticSceneJson),
               "scene static flag round-trip failed to load"))
        return false;
    Actor* staticLoaded = staticRoundTrip.FindByID(actorID);
    if (!Check(staticLoaded && staticLoaded->IsStatic(), "scene static flag round-trip did not preserve editor flags"))
        return false;

    Actor* batchA = scene.CreateActor("BatchA");
    Actor* batchB = scene.CreateActor("BatchB");
    batchA->SetActive(true);
    batchB->SetActive(false);
    batchA->SetTag("A");
    batchB->SetTag("B");
    batchA->SetLayer(1);
    batchB->SetLayer(2);
    batchA->SetStatic(false);
    batchB->SetStatic(true);
    const std::vector<uint64_t> batchIDs{batchA->GetID(), batchB->GetID()};

    if (!Check(operators.Commands().SetActorsActive(context, batchIDs, true), "operator batch active edit failed"))
        return false;
    if (!Check(batchA->IsActiveSelf() && batchB->IsActiveSelf(), "operator batch active edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && batchA->IsActiveSelf() && !batchB->IsActiveSelf(),
               "operator batch active undo failed"))
        return false;
    if (!Check(stack.Redo(context) && batchA->IsActiveSelf() && batchB->IsActiveSelf(),
               "operator batch active redo failed"))
        return false;

    if (!Check(operators.Commands().SetActorsTag(context, batchIDs, "Enemy"), "operator batch tag edit failed"))
        return false;
    if (!Check(batchA->GetTag() == "Enemy" && batchB->GetTag() == "Enemy", "operator batch tag edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && batchA->GetTag() == "A" && batchB->GetTag() == "B",
               "operator batch tag undo failed"))
        return false;
    if (!Check(stack.Redo(context) && batchA->GetTag() == "Enemy" && batchB->GetTag() == "Enemy",
               "operator batch tag redo failed"))
        return false;

    if (!Check(operators.Commands().SetActorsLayer(context, batchIDs, 4), "operator batch layer edit failed"))
        return false;
    if (!Check(batchA->GetLayer() == 4 && batchB->GetLayer() == 4, "operator batch layer edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && batchA->GetLayer() == 1 && batchB->GetLayer() == 2,
               "operator batch layer undo failed"))
        return false;
    if (!Check(stack.Redo(context) && batchA->GetLayer() == 4 && batchB->GetLayer() == 4,
               "operator batch layer redo failed"))
        return false;

    if (!Check(operators.Commands().SetActorsStatic(context, batchIDs, false), "operator batch static edit failed"))
        return false;
    if (!Check(!batchA->IsStatic() && !batchB->IsStatic(), "operator batch static edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && !batchA->IsStatic() && batchB->IsStatic(), "operator batch static undo failed"))
        return false;
    if (!Check(stack.Redo(context) && !batchA->IsStatic() && !batchB->IsStatic(), "operator batch static redo failed"))
        return false;

    batchA->GetTransform().position = {1.0f, 2.0f, 3.0f};
    batchB->GetTransform().position = {-1.0f, -2.0f, -3.0f};
    if (!Check(operators.Commands().SetActorsPosition(context, batchIDs, {9.0f, 8.0f, 7.0f}),
               "operator batch position edit failed"))
        return false;
    if (!Check(NearlyEqual(batchA->GetTransform().position.x, 9.0f) &&
                   NearlyEqual(batchB->GetTransform().position.y, 8.0f),
               "operator batch position edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && NearlyEqual(batchA->GetTransform().position.x, 1.0f) &&
                   NearlyEqual(batchB->GetTransform().position.y, -2.0f),
               "operator batch position undo failed"))
        return false;
    if (!Check(stack.Redo(context) && NearlyEqual(batchA->GetTransform().position.z, 7.0f) &&
                   NearlyEqual(batchB->GetTransform().position.x, 9.0f),
               "operator batch position redo failed"))
        return false;

    batchA->GetTransform().rotation = {10.0f, 20.0f, 30.0f};
    batchB->GetTransform().rotation = {-10.0f, -20.0f, -30.0f};
    if (!Check(operators.Commands().SetActorsRotation(context, batchIDs, {45.0f, 90.0f, 135.0f}),
               "operator batch rotation edit failed"))
        return false;
    if (!Check(NearlyEqual(batchA->GetTransform().rotation.x, 45.0f) &&
                   NearlyEqual(batchB->GetTransform().rotation.z, 135.0f),
               "operator batch rotation edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && NearlyEqual(batchA->GetTransform().rotation.y, 20.0f) &&
                   NearlyEqual(batchB->GetTransform().rotation.x, -10.0f),
               "operator batch rotation undo failed"))
        return false;
    if (!Check(stack.Redo(context) && NearlyEqual(batchA->GetTransform().rotation.z, 135.0f) &&
                   NearlyEqual(batchB->GetTransform().rotation.y, 90.0f),
               "operator batch rotation redo failed"))
        return false;

    batchA->GetTransform().scale = {1.0f, 2.0f, 3.0f};
    batchB->GetTransform().scale = {4.0f, 5.0f, 6.0f};
    if (!Check(operators.Commands().SetActorsScale(context, batchIDs, {2.0f, 2.0f, 2.0f}),
               "operator batch scale edit failed"))
        return false;
    if (!Check(NearlyEqual(batchA->GetTransform().scale.x, 2.0f) && NearlyEqual(batchB->GetTransform().scale.z, 2.0f),
               "operator batch scale edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && NearlyEqual(batchA->GetTransform().scale.y, 2.0f) &&
                   NearlyEqual(batchB->GetTransform().scale.x, 4.0f),
               "operator batch scale undo failed"))
        return false;
    if (!Check(stack.Redo(context) && NearlyEqual(batchA->GetTransform().scale.z, 2.0f) &&
                   NearlyEqual(batchB->GetTransform().scale.y, 2.0f),
               "operator batch scale redo failed"))
        return false;

    std::vector<uint64_t> idsBeforeMultiDuplicate;
    scene.ForEach([&](Actor& value) { idsBeforeMultiDuplicate.push_back(value.GetID()); });
    Actor* duplicateParent = scene.CreateActor("MultiDuplicateParent");
    Actor* duplicateChild = scene.CreateActor("MultiDuplicateChild", duplicateParent);
    Actor* duplicateSibling = scene.CreateActor("MultiDuplicateSibling");
    if (!duplicateParent || !duplicateChild || !duplicateSibling ||
        !duplicateChild->AddComponent<BoxColliderComponent>()) {
        return Check(false, "operator multi-duplicate setup failed");
    }
    idsBeforeMultiDuplicate.push_back(duplicateParent->GetID());
    idsBeforeMultiDuplicate.push_back(duplicateChild->GetID());
    idsBeforeMultiDuplicate.push_back(duplicateSibling->GetID());
    const size_t countBeforeMultiDuplicate = scene.ActorCount();
    const uint64_t duplicateParentID = duplicateParent->GetID();
    const uint64_t duplicateChildID = duplicateChild->GetID();
    const uint64_t duplicateSiblingID = duplicateSibling->GetID();
    context.GetSelection().SelectActorID(duplicateParentID);
    context.GetSelection().AddToMultiSelect(duplicateChildID);
    context.GetSelection().AddToMultiSelect(duplicateSiblingID);
    if (!Check(operators.Commands().DuplicateSelection(context), "operator multi-duplicate selection failed"))
        return false;
    if (!Check(scene.ActorCount() == countBeforeMultiDuplicate + 3,
               "operator multi-duplicate did not duplicate selected root subtrees"))
        return false;
    std::vector<uint64_t> multiDuplicateCloneIDs;
    scene.ForEach([&](Actor& value) {
        if (std::find(idsBeforeMultiDuplicate.begin(), idsBeforeMultiDuplicate.end(), value.GetID()) ==
            idsBeforeMultiDuplicate.end()) {
            multiDuplicateCloneIDs.push_back(value.GetID());
        }
    });
    if (!Check(multiDuplicateCloneIDs.size() == 3, "operator multi-duplicate produced unexpected clone count"))
        return false;
    bool foundParentClone = false;
    for (uint64_t cloneID : multiDuplicateCloneIDs) {
        Actor* clone = scene.FindByID(cloneID);
        if (clone && clone->GetChildren().size() == 1) {
            foundParentClone = clone->GetChildren().size() == 1 &&
                               clone->GetChildren()[0]->GetComponent<BoxColliderComponent>() != nullptr;
        }
    }
    if (!Check(foundParentClone, "operator multi-duplicate did not preserve selected subtree"))
        return false;
    const uint64_t selectedCloneAfterMultiDuplicate = context.GetSelection().GetActorID();
    if (!Check(std::find(multiDuplicateCloneIDs.begin(), multiDuplicateCloneIDs.end(),
                         selectedCloneAfterMultiDuplicate) != multiDuplicateCloneIDs.end(),
               "operator multi-duplicate did not select a cloned actor"))
        return false;
    if (!Check(stack.Undo(context) && scene.ActorCount() == countBeforeMultiDuplicate &&
                   !scene.FindByID(multiDuplicateCloneIDs.front()),
               "operator multi-duplicate undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.ActorCount() == countBeforeMultiDuplicate + 3 &&
                   scene.FindByID(selectedCloneAfterMultiDuplicate),
               "operator multi-duplicate redo failed"))
        return false;

    std::vector<uint64_t> idsBeforeMultiPaste;
    scene.ForEach([&](Actor& value) { idsBeforeMultiPaste.push_back(value.GetID()); });
    Actor* copyParent = scene.CreateActor("MultiCopyParent");
    Actor* copyChild = scene.CreateActor("MultiCopyChild", copyParent);
    Actor* copySibling = scene.CreateActor("MultiCopySibling");
    if (!copyParent || !copyChild || !copySibling || !copyChild->AddComponent<BoxColliderComponent>()) {
        return Check(false, "operator multi-copy setup failed");
    }
    idsBeforeMultiPaste.push_back(copyParent->GetID());
    idsBeforeMultiPaste.push_back(copyChild->GetID());
    idsBeforeMultiPaste.push_back(copySibling->GetID());
    const size_t countBeforeMultiPaste = scene.ActorCount();
    const uint64_t copyParentID = copyParent->GetID();
    const uint64_t copyChildID = copyChild->GetID();
    const uint64_t copySiblingID = copySibling->GetID();
    context.GetSelection().SelectActorID(copyParentID);
    context.GetSelection().AddToMultiSelect(copyChildID);
    context.GetSelection().AddToMultiSelect(copySiblingID);
    if (!Check(operators.Commands().CopySelection(context), "operator multi-copy selection failed"))
        return false;
    if (!Check(operators.Commands().HasActorClipboard() && !operators.Commands().HasAssetClipboard(),
               "operator actor copy did not set actor clipboard kind"))
        return false;
    context.GetSelection().SelectActorID(copySiblingID);
    if (!Check(operators.Commands().PasteSelection(context), "operator multi-paste selection failed"))
        return false;
    if (!Check(scene.ActorCount() == countBeforeMultiPaste + 3,
               "operator multi-paste did not paste copied root subtrees"))
        return false;
    std::vector<uint64_t> multiPasteCloneIDs;
    scene.ForEach([&](Actor& value) {
        if (std::find(idsBeforeMultiPaste.begin(), idsBeforeMultiPaste.end(), value.GetID()) ==
            idsBeforeMultiPaste.end()) {
            multiPasteCloneIDs.push_back(value.GetID());
        }
    });
    if (!Check(multiPasteCloneIDs.size() == 3, "operator multi-paste produced unexpected clone count"))
        return false;
    bool foundPastedParentClone = false;
    for (uint64_t cloneID : multiPasteCloneIDs) {
        Actor* pasted = scene.FindByID(cloneID);
        if (pasted && pasted->GetChildren().size() == 1) {
            foundPastedParentClone = pasted->GetChildren()[0]->GetComponent<BoxColliderComponent>() != nullptr;
        }
    }
    if (!Check(foundPastedParentClone, "operator multi-paste did not preserve copied subtree"))
        return false;
    const uint64_t selectedCloneAfterMultiPaste = context.GetSelection().GetActorID();
    if (!Check(std::find(multiPasteCloneIDs.begin(), multiPasteCloneIDs.end(), selectedCloneAfterMultiPaste) !=
                   multiPasteCloneIDs.end(),
               "operator multi-paste did not select a pasted actor"))
        return false;
    if (!Check(stack.Undo(context) && scene.ActorCount() == countBeforeMultiPaste &&
                   !scene.FindByID(multiPasteCloneIDs.front()),
               "operator multi-paste undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.ActorCount() == countBeforeMultiPaste + 3 &&
                   scene.FindByID(selectedCloneAfterMultiPaste),
               "operator multi-paste redo failed"))
        return false;

    Actor* deleteParent = scene.CreateActor("DeleteParent");
    Actor* deleteChild = scene.CreateActor("DeleteChild", deleteParent);
    Actor* deleteSibling = scene.CreateActor("DeleteSibling");
    const uint64_t deleteParentID = deleteParent->GetID();
    const uint64_t deleteChildID = deleteChild->GetID();
    const uint64_t deleteSiblingID = deleteSibling->GetID();
    context.GetSelection().SelectActorID(deleteParentID);
    context.GetSelection().AddToMultiSelect(deleteChildID);
    context.GetSelection().AddToMultiSelect(deleteSiblingID);
    if (!Check(operators.Commands().DeleteSelection(context), "operator multi-delete selection failed"))
        return false;
    if (!Check(!scene.FindByID(deleteParentID) && !scene.FindByID(deleteChildID) && !scene.FindByID(deleteSiblingID) &&
                   !context.GetSelection().HasActor(),
               "operator multi-delete did not remove selected actor set"))
        return false;
    if (!Check(stack.Undo(context) && scene.FindByID(deleteParentID) && scene.FindByID(deleteChildID) &&
                   scene.FindByID(deleteSiblingID) &&
                   scene.FindByID(deleteChildID)->GetParent() == scene.FindByID(deleteParentID) &&
                   context.GetSelection().GetActorID() == deleteSiblingID,
               "operator multi-delete undo did not restore hierarchy and selection"))
        return false;
    if (!Check(stack.Redo(context) && !scene.FindByID(deleteParentID) && !scene.FindByID(deleteChildID) &&
                   !scene.FindByID(deleteSiblingID),
               "operator multi-delete redo failed"))
        return false;

    actor = scene.FindByID(actorID);
    if (!Check(actor != nullptr, "operator multi-delete snapshot did not preserve unrelated actor"))
        return false;
    Actor* child = scene.CreateActor("Child", actor);
    if (!child || !child->AddComponent<BoxColliderComponent>()) {
        return Check(false, "operator duplicate setup failed");
    }
    if (!Check(operators.Commands().DuplicateActorSubtree(context, actorID), "operator duplicate subtree failed"))
        return false;
    const uint64_t cloneID = context.GetSelection().GetActorID();
    Actor* clone = scene.FindByID(cloneID);
    if (!Check(clone && cloneID != actorID, "operator duplicate did not select the cloned root"))
        return false;
    if (!Check(clone->GetChildren().size() == 1, "operator duplicate did not preserve subtree child"))
        return false;
    if (!Check(clone->GetChildren()[0]->GetComponent<BoxColliderComponent>() != nullptr,
               "operator duplicate did not preserve subtree component"))
        return false;
    if (!Check(stack.Undo(context) && scene.FindByID(cloneID) == nullptr, "operator duplicate undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.FindByID(cloneID) != nullptr, "operator duplicate redo failed"))
        return false;

    actor = scene.FindByID(actorID);
    if (!Check(actor != nullptr, "operator duplicate snapshot did not restore source actor"))
        return false;
    if (!Check(operators.Components().AddComponent(context, actorID, "BoxCollider"), "component operator add failed"))
        return false;
    actor = scene.FindByID(actorID);
    if (!Check(actor && actor->GetComponent<BoxColliderComponent>() != nullptr, "component operator add did not apply"))
        return false;
    if (!Check(stack.Undo(context) && actor->GetComponent<BoxColliderComponent>() == nullptr,
               "component operator add undo failed"))
        return false;
    if (!Check(stack.Redo(context) && actor->GetComponent<BoxColliderComponent>() != nullptr,
               "component operator add redo failed"))
        return false;
    if (!Check(operators.Components().RemoveComponent(context, actorID, "BoxCollider"),
               "component operator remove failed"))
        return false;
    if (!Check(actor->GetComponent<BoxColliderComponent>() == nullptr, "component operator remove did not apply"))
        return false;
    if (!Check(stack.Undo(context) && actor->GetComponent<BoxColliderComponent>() != nullptr,
               "component operator remove undo failed"))
        return false;

    Actor* batchComponentA = scene.CreateActor("BatchComponentA");
    Actor* batchComponentB = scene.CreateActor("BatchComponentB");
    Actor* batchComponentMissing = scene.CreateActor("BatchComponentMissing");
    if (!batchComponentA || !batchComponentB || !batchComponentMissing ||
        !batchComponentA->AddComponent<BoxColliderComponent>() ||
        !batchComponentB->AddComponent<BoxColliderComponent>()) {
        return Check(false, "component operator batch remove setup failed");
    }
    const uint64_t batchComponentAID = batchComponentA->GetID();
    const uint64_t batchComponentBID = batchComponentB->GetID();
    const uint64_t batchComponentMissingID = batchComponentMissing->GetID();
    if (!Check(!operators.Components().RemoveComponents(context, {batchComponentAID, batchComponentMissingID},
                                                        "BoxCollider"),
               "component operator batch remove allowed missing component"))
        return false;
    if (!Check(operators.Components().RemoveComponents(context, {batchComponentAID, batchComponentBID}, "BoxCollider"),
               "component operator batch remove failed"))
        return false;
    batchComponentA = scene.FindByID(batchComponentAID);
    batchComponentB = scene.FindByID(batchComponentBID);
    if (!Check(batchComponentA && batchComponentB && !batchComponentA->HasComponentType("BoxCollider") &&
                   !batchComponentB->HasComponentType("BoxCollider"),
               "component operator batch remove did not apply"))
        return false;
    if (!Check(stack.Undo(context) && scene.FindByID(batchComponentAID)->HasComponentType("BoxCollider") &&
                   scene.FindByID(batchComponentBID)->HasComponentType("BoxCollider"),
               "component operator batch remove undo failed"))
        return false;
    if (!Check(stack.Redo(context) && !scene.FindByID(batchComponentAID)->HasComponentType("BoxCollider") &&
                   !scene.FindByID(batchComponentBID)->HasComponentType("BoxCollider"),
               "component operator batch remove redo failed"))
        return false;

    Actor* batchAddA = scene.CreateActor("BatchAddA");
    Actor* batchAddB = scene.CreateActor("BatchAddB");
    Actor* batchAddExisting = scene.CreateActor("BatchAddExisting");
    if (!Check(batchAddA && batchAddB && batchAddExisting && batchAddExisting->AddComponent<BoxColliderComponent>(),
               "component operator batch add setup failed"))
        return false;
    const std::vector<uint64_t> batchAddIDs{batchAddA->GetID(), batchAddB->GetID()};
    if (!Check(!operators.Components().AddComponents(context, {batchAddA->GetID(), batchAddExisting->GetID()},
                                                     "BoxCollider"),
               "component operator batch add allowed existing component"))
        return false;
    if (!Check(operators.Components().AddComponents(context, batchAddIDs, "BoxCollider"),
               "component operator batch add failed"))
        return false;
    if (!Check(scene.FindByID(batchAddIDs[0])->HasComponentType("BoxCollider") &&
                   scene.FindByID(batchAddIDs[1])->HasComponentType("BoxCollider"),
               "component operator batch add did not apply"))
        return false;
    if (!Check(stack.Undo(context) && !scene.FindByID(batchAddIDs[0])->HasComponentType("BoxCollider") &&
                   !scene.FindByID(batchAddIDs[1])->HasComponentType("BoxCollider"),
               "component operator batch add undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.FindByID(batchAddIDs[0])->HasComponentType("BoxCollider") &&
                   scene.FindByID(batchAddIDs[1])->HasComponentType("BoxCollider"),
               "component operator batch add redo failed"))
        return false;

    Actor* batchPropertyA = scene.CreateActor("BatchPropertyA");
    Actor* batchPropertyB = scene.CreateActor("BatchPropertyB");
    BoxColliderComponent* batchBoxA = batchPropertyA ? batchPropertyA->AddComponent<BoxColliderComponent>() : nullptr;
    BoxColliderComponent* batchBoxB = batchPropertyB ? batchPropertyB->AddComponent<BoxColliderComponent>() : nullptr;
    if (!Check(batchPropertyA && batchPropertyB && batchBoxA && batchBoxB,
               "component operator batch property setup failed"))
        return false;
    batchBoxA->SetHalfExtents({1.0f, 1.0f, 1.0f});
    batchBoxB->SetHalfExtents({2.0f, 2.0f, 2.0f});
    const std::vector<uint64_t> batchPropertyIDs{batchPropertyA->GetID(), batchPropertyB->GetID()};
    if (!Check(operators.Components().SetComponentPropertyForActors(
                   context, batchPropertyIDs, "BoxCollider", "halfExtents", nlohmann::json::array({3.0f, 4.0f, 5.0f})),
               "component operator batch property edit failed"))
        return false;
    batchBoxA = scene.FindByID(batchPropertyIDs[0])->GetComponent<BoxColliderComponent>();
    batchBoxB = scene.FindByID(batchPropertyIDs[1])->GetComponent<BoxColliderComponent>();
    if (!Check(batchBoxA && batchBoxB && NearlyEqual(batchBoxA->GetHalfExtents().x, 3.0f) &&
                   NearlyEqual(batchBoxB->GetHalfExtents().z, 5.0f),
               "component operator batch property edit did not apply"))
        return false;
    if (!Check(stack.Undo(context), "component operator batch property undo command failed"))
        return false;
    batchBoxA = scene.FindByID(batchPropertyIDs[0])->GetComponent<BoxColliderComponent>();
    batchBoxB = scene.FindByID(batchPropertyIDs[1])->GetComponent<BoxColliderComponent>();
    if (!Check(batchBoxA && batchBoxB && NearlyEqual(batchBoxA->GetHalfExtents().x, 1.0f) &&
                   NearlyEqual(batchBoxB->GetHalfExtents().y, 2.0f),
               "component operator batch property undo failed"))
        return false;
    if (!Check(stack.Redo(context), "component operator batch property redo command failed"))
        return false;
    batchBoxA = scene.FindByID(batchPropertyIDs[0])->GetComponent<BoxColliderComponent>();
    batchBoxB = scene.FindByID(batchPropertyIDs[1])->GetComponent<BoxColliderComponent>();
    if (!Check(batchBoxA && batchBoxB && NearlyEqual(batchBoxA->GetHalfExtents().y, 4.0f) &&
                   NearlyEqual(batchBoxB->GetHalfExtents().z, 5.0f),
               "component operator batch property redo failed"))
        return false;

    actor = scene.FindByID(actorID);
    if (!Check(actor != nullptr, "component operator batch remove snapshot did not restore source actor")) {
        return false;
    }
    BoxColliderComponent* box = actor->GetComponent<BoxColliderComponent>();
    if (!Check(box != nullptr, "component operator property setup missing collider"))
        return false;
    nlohmann::json beforeCollider;
    box->Serialize(beforeCollider);
    box->SetHalfExtents({2.0f, 3.0f, 4.0f});
    box->SetLayer(8);
    nlohmann::json afterCollider;
    box->Serialize(afterCollider);
    box->Deserialize(beforeCollider);
    if (!Check(operators.Components().SetProperty(context, *actor, "BoxCollider", "halfExtents", beforeCollider,
                                                  afterCollider),
               "component operator property edit failed"))
        return false;
    if (!Check(box->GetHalfExtents().x == 2.0f && box->GetLayer() == 8,
               "component operator property edit did not apply"))
        return false;
    if (!Check(stack.Undo(context) && box->GetHalfExtents().x == 0.5f && box->GetLayer() == 1,
               "component operator property edit undo failed"))
        return false;
    if (!Check(stack.Redo(context) && box->GetHalfExtents().x == 2.0f && box->GetLayer() == 8,
               "component operator property edit redo failed"))
        return false;

    auto verifyPropertyEdit = [&](Component& component, const char* componentType, const char* propertyName,
                                  auto&& edit, auto&& isApplied, auto&& isRestored, const char* failurePrefix) {
        nlohmann::json before;
        component.Serialize(before);
        edit();
        nlohmann::json after;
        component.Serialize(after);
        component.Deserialize(before);
        if (!Check(operators.Components().SetProperty(context, *actor, componentType, propertyName, before, after),
                   std::string(failurePrefix) + " set property failed"))
            return false;
        if (!Check(isApplied(), std::string(failurePrefix) + " property did not apply"))
            return false;
        if (!Check(stack.Undo(context) && isRestored(), std::string(failurePrefix) + " property undo failed"))
            return false;
        if (!Check(stack.Redo(context) && isApplied(), std::string(failurePrefix) + " property redo failed"))
            return false;
        return true;
    };

    auto* light = actor->AddComponent<LightComponent>();
    auto* camera = actor->AddComponent<CameraComponent>();
    auto* post = actor->AddComponent<PostProcessComponent>();
    if (!Check(light && camera && post, "render component property setup failed"))
        return false;
    if (!verifyPropertyEdit(
            *light, "Light", "intensity", [&] { light->SetIntensity(7.0f); },
            [&] { return light->GetIntensity() == 7.0f; }, [&] { return light->GetIntensity() == 3.0f; }, "light"))
        return false;
    if (!verifyPropertyEdit(
            *camera, "Camera", "fovYDegrees", [&] { camera->SetFovYDegrees(75.0f); },
            [&] { return camera->GetFovYDegrees() == 75.0f; }, [&] { return camera->GetFovYDegrees() == 60.0f; },
            "camera"))
        return false;
    if (!verifyPropertyEdit(
            *post, "PostProcess", "exposure", [&] { post->SetExposure(2.5f); },
            [&] { return post->GetExposure() == 2.5f; }, [&] { return post->GetExposure() == 1.0f; }, "post process"))
        return false;
    if (!verifyPropertyEdit(
            *post, "PostProcess", "ssgiStepCount", [&] { post->SetSSGIStepCount(64); },
            [&] { return post->GetSSGIStepCount() == 64; }, [&] { return post->GetSSGIStepCount() == 32; },
            "post process SSGI"))
        return false;
    if (!verifyPropertyEdit(
            *post, "PostProcess", "ssrHistoryWeight", [&] { post->SetSSRHistoryWeight(0.72f); },
            [&] { return NearlyEqual(post->GetSSRHistoryWeight(), 0.72f); },
            [&] { return NearlyEqual(post->GetSSRHistoryWeight(), 0.9f); }, "post process SSR"))
        return false;
    if (!verifyPropertyEdit(
            *post, "PostProcess", "taaHistoryClipExpansion", [&] { post->SetTAAHistoryClipExpansion(1.5f); },
            [&] { return NearlyEqual(post->GetTAAHistoryClipExpansion(), 1.5f); },
            [&] { return NearlyEqual(post->GetTAAHistoryClipExpansion(), 0.0f); }, "post process TAA"))
        return false;

    auto* canvas = actor->AddComponent<UICanvasComponent>();
    auto* rect = actor->AddComponent<UIRectTransformComponent>();
    if (!Check(canvas && rect, "ui component property setup failed"))
        return false;
    if (!verifyPropertyEdit(
            *canvas, "UICanvas", "sortOrder", [&] { canvas->SetSortOrder(12); },
            [&] { return canvas->GetSortOrder() == 12; }, [&] { return canvas->GetSortOrder() == 0; }, "ui canvas"))
        return false;
    if (!verifyPropertyEdit(
            *rect, "UIRectTransform", "anchorMin", [&] { rect->GetRect().anchorMin = {0.25f, 0.5f}; },
            [&] { return rect->GetRect().anchorMin.x == 0.25f && rect->GetRect().anchorMin.y == 0.5f; },
            [&] { return rect->GetRect().anchorMin.x == 0.0f && rect->GetRect().anchorMin.y == 0.0f; },
            "ui rect transform"))
        return false;

    if (!Check(operators.Selection().SelectAsset(context, "Content/Models/test.gltf"),
               "operator asset selection failed"))
        return false;
    const EditorSelectionSnapshot snapshot = operators.Selection().GetSelectionSnapshot(context);
    if (!Check(snapshot.hasAsset &&
                   snapshot.assetPath == std::filesystem::path("Content/Models/test.gltf").generic_string(),
               "operator selection snapshot mismatch"))
        return false;
    operators.Selection().Clear(context);
    if (!Check(!context.GetSelection().HasActor() && !context.GetSelection().HasAsset(),
               "operator clear selection failed"))
        return false;

    operators.Selection().SelectActor(context, actorID);
    if (!Check(operators.Commands().DeleteActor(context, actorID), "operator delete actor failed"))
        return false;
    if (!Check(scene.FindByID(actorID) == nullptr, "operator delete actor did not apply"))
        return false;
    if (!Check(stack.Undo(context) && scene.FindByID(actorID) != nullptr, "operator delete actor undo failed"))
        return false;
    return true;
}

bool TestEditorCommandOperatorCreateUIActor() {
    Scene scene("EditorUIActorOperator");
    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    if (!Check(operators.Commands().CreateUIActor(context, "missing") == 0,
               "operator accepted invalid UI actor preset"))
        return false;

    const uint64_t canvasID = operators.Commands().CreateUIActor(context, "canvas");
    Actor* canvas = scene.FindByID(canvasID);
    if (!Check(canvas && canvas->GetName() == "UI Canvas" && canvas->GetComponent<UICanvasComponent>() &&
                   canvas->GetComponent<UIRectTransformComponent>(),
               "operator create UI canvas failed"))
        return false;
    if (!Check(context.GetSelection().GetActorID() == canvasID, "operator create UI canvas did not select new actor"))
        return false;
    if (!Check(stack.Undo(context) && scene.FindByID(canvasID) == nullptr, "operator create UI canvas undo failed"))
        return false;
    if (!Check(stack.Redo(context) && scene.FindByID(canvasID) != nullptr, "operator create UI canvas redo failed"))
        return false;

    canvas = scene.FindByID(canvasID);
    if (!Check(canvas != nullptr, "operator create UI canvas redo did not restore actor")) {
        return false;
    }
    const uint64_t buttonID = operators.Commands().CreateUIActor(context, "button", canvasID);
    Actor* button = scene.FindByID(buttonID);
    if (!Check(button != nullptr, "operator create UI button child missing"))
        return false;
    canvas = scene.FindByID(canvasID);
    if (!Check(canvas != nullptr, "operator create UI button lost canvas parent"))
        return false;
    if (!Check(button->GetParent() == canvas, "operator create UI button parent mismatch"))
        return false;
    if (!Check(button->GetComponent<UIRectTransformComponent>() != nullptr,
               "operator create UI button missing rect transform"))
        return false;
    if (!Check(button->GetComponent<UIButtonComponent>() != nullptr,
               "operator create UI button missing button component"))
        return false;
    if (!Check(stack.Undo(context) && scene.FindByID(buttonID) == nullptr && scene.FindByID(canvasID) != nullptr,
               "operator create UI button undo failed"))
        return false;
    if (!Check(stack.Redo(context), "operator create UI button redo failed"))
        return false;
    button = scene.FindByID(buttonID);
    canvas = scene.FindByID(canvasID);
    if (!Check(button && canvas && button->GetParent() == canvas && button->GetComponent<UIButtonComponent>(),
               "operator create UI button redo did not restore child"))
        return false;

    return true;
}

bool TestEditorCommandOperatorCreateEmptyParent() {
    Scene scene("EditorEmptyParentOperator");
    Actor* first = scene.CreateActor("First");
    Actor* selected = scene.CreateActor("Selected");
    Actor* third = scene.CreateActor("Third");
    const uint64_t firstID = first->GetID();
    const uint64_t selectedID = selected->GetID();
    const uint64_t thirdID = third->GetID();

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.GetSelection().SelectActorID(selectedID);

    const uint64_t parentID = operators.Commands().CreateEmptyParent(context, selectedID);
    Actor* parent = scene.FindByID(parentID);
    Actor* child = scene.FindByID(selectedID);
    std::vector<Actor*> roots = scene.GetRootActors();
    if (!Check(parent && parent->GetName() == "Empty Parent" && child && child->GetParent() == parent &&
                   context.GetSelection().GetActorID() == parentID,
               "operator create empty parent failed"))
        return false;
    if (!Check(roots.size() >= 3 && roots[0]->GetID() == firstID && roots[1]->GetID() == parentID &&
                   roots[2]->GetID() == thirdID,
               "operator create empty parent did not preserve sibling slot"))
        return false;

    if (!Check(stack.Undo(context), "operator create empty parent undo failed"))
        return false;
    child = scene.FindByID(selectedID);
    roots = scene.GetRootActors();
    if (!Check(child && child->GetParent() == nullptr && scene.FindByID(parentID) == nullptr && roots.size() >= 3 &&
                   roots[0]->GetID() == firstID && roots[1]->GetID() == selectedID && roots[2]->GetID() == thirdID,
               "operator create empty parent undo did not restore hierarchy"))
        return false;

    if (!Check(stack.Redo(context), "operator create empty parent redo failed"))
        return false;
    parent = scene.FindByID(parentID);
    child = scene.FindByID(selectedID);
    roots = scene.GetRootActors();
    if (!Check(parent && child && child->GetParent() == parent && roots.size() >= 3 && roots[1]->GetID() == parentID,
               "operator create empty parent redo did not restore hierarchy"))
        return false;

    return true;
}

bool TestEditorCommandOperatorCreateChildActor() {
    Scene scene("EditorChildActorOperator");
    Actor* parent = scene.CreateActor("Parent");
    const uint64_t parentID = parent->GetID();

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    if (!Check(operators.Commands().CreateChildActor(context, "Orphan", 999999) == 0,
               "operator accepted invalid parent for child actor"))
        return false;

    const uint64_t childID = operators.Commands().CreateChildActor(context, "Child", parentID);
    Actor* child = scene.FindByID(childID);
    parent = scene.FindByID(parentID);
    if (!Check(child && parent && child->GetName() == "Child" && child->GetParent() == parent &&
                   context.GetSelection().GetActorID() == childID,
               "operator create child actor failed"))
        return false;
    if (!Check(parent->GetChildren().size() == 1 && parent->GetChildren().front() == child,
               "operator create child actor did not attach to parent"))
        return false;

    if (!Check(stack.Undo(context) && scene.FindByID(childID) == nullptr && scene.FindByID(parentID) != nullptr,
               "operator create child actor undo failed"))
        return false;
    if (!Check(stack.Redo(context), "operator create child actor redo failed"))
        return false;
    child = scene.FindByID(childID);
    parent = scene.FindByID(parentID);
    if (!Check(child && parent && child->GetParent() == parent,
               "operator create child actor redo did not restore parent"))
        return false;

    return true;
}

bool TestEditorCommandOperatorHierarchyOrganization() {
    Scene scene("EditorHierarchyOperator");
    Actor* first = scene.CreateActor("First");
    Actor* second = scene.CreateActor("Second");
    Actor* third = scene.CreateActor("Third");
    const uint64_t firstID = first->GetID();
    const uint64_t secondID = second->GetID();
    const uint64_t thirdID = third->GetID();

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    if (!Check(operators.Commands().MoveActorUp(context, secondID), "operator move actor up failed"))
        return false;
    std::vector<Actor*> roots = scene.GetRootActors();
    if (!Check(roots.size() >= 3 && roots[0]->GetID() == secondID && roots[1]->GetID() == firstID &&
                   roots[2]->GetID() == thirdID,
               "operator move actor up did not reorder roots"))
        return false;
    if (!Check(stack.Undo(context), "operator move actor up undo failed"))
        return false;
    roots = scene.GetRootActors();
    if (!Check(roots.size() >= 3 && roots[0]->GetID() == firstID && roots[1]->GetID() == secondID &&
                   roots[2]->GetID() == thirdID,
               "operator move actor up undo did not restore roots"))
        return false;

    if (!Check(operators.Commands().MoveActorDown(context, secondID), "operator move actor down failed"))
        return false;
    roots = scene.GetRootActors();
    if (!Check(roots.size() >= 3 && roots[0]->GetID() == firstID && roots[1]->GetID() == thirdID &&
                   roots[2]->GetID() == secondID,
               "operator move actor down did not reorder roots"))
        return false;
    if (!Check(stack.Undo(context), "operator move actor down undo failed"))
        return false;

    Actor* parent = scene.CreateActor("Parent");
    Actor* child = scene.CreateActor("Child", parent);
    const uint64_t parentID = parent->GetID();
    const uint64_t childID = child->GetID();
    if (!Check(operators.Commands().UnparentActor(context, childID), "operator unparent actor failed"))
        return false;
    Actor* reloadedChild = scene.FindByID(childID);
    roots = scene.GetRootActors();
    if (!Check(reloadedChild && reloadedChild->GetParent() == nullptr && roots.size() >= 5 &&
                   roots[3]->GetID() == parentID && roots[4]->GetID() == childID,
               "operator unparent actor did not move child after parent"))
        return false;
    if (!Check(stack.Undo(context), "operator unparent actor undo failed"))
        return false;
    reloadedChild = scene.FindByID(childID);
    Actor* reloadedParent = scene.FindByID(parentID);
    if (!Check(reloadedChild && reloadedParent && reloadedChild->GetParent() == reloadedParent,
               "operator unparent actor undo did not restore parent"))
        return false;

    return true;
}

bool TestEditorMoveActorCommandUndoRedo() {
    Scene scene("EditorMoveActor");
    EditorContext context(&scene);
    EditorCommandStack stack;
    context.SetCommandStack(&stack);

    Actor* parent = scene.CreateActor("Parent");
    Actor* childA = scene.CreateActor("ChildA", parent);
    Actor* childB = scene.CreateActor("ChildB", parent);
    Actor* childC = scene.CreateActor("ChildC", parent);
    Actor* otherParent = scene.CreateActor("OtherParent");
    Actor* otherA = scene.CreateActor("OtherA", otherParent);
    Actor* otherB = scene.CreateActor("OtherB", otherParent);

    const uint64_t beforeParentID = parent->GetID();
    const uint64_t beforeNextID = childC->GetID();
    const uint64_t afterParentID = otherParent->GetID();
    const uint64_t afterNextID = otherB->GetID();
    if (!Check(stack.ExecuteCommand(EditorUndoUtil::MakeMoveActorCommand(*childB, beforeParentID, beforeNextID,
                                                                         afterParentID, afterNextID),
                                    context),
               "move actor command execute failed"))
        return false;
    if (!Check(childB->GetParent() == otherParent && otherParent->GetChildren().size() == 3 &&
                   otherParent->GetChildren()[0] == otherA && otherParent->GetChildren()[1] == childB &&
                   otherParent->GetChildren()[2] == otherB,
               "move actor command did not apply target order"))
        return false;
    if (!Check(context.GetSelection().GetActorID() == childB->GetID(), "move actor command did not select moved actor"))
        return false;

    if (!Check(stack.Undo(context), "move actor command undo failed"))
        return false;
    if (!Check(childB->GetParent() == parent && parent->GetChildren().size() == 3 &&
                   parent->GetChildren()[0] == childA && parent->GetChildren()[1] == childB &&
                   parent->GetChildren()[2] == childC,
               "move actor command undo did not restore original order"))
        return false;

    if (!Check(stack.Redo(context), "move actor command redo failed"))
        return false;
    return Check(childB->GetParent() == otherParent && otherParent->GetChildren()[1] == childB,
                 "move actor command redo did not restore target order");
}

bool TestEditorContextWorldRouting() {
    SceneRenderLayer layer(nullptr, 320, 180);
    Actor* editorActor = layer.GetEditorScene().CreateActor("EditorOnly");
    EditorContext context(&layer, nullptr, nullptr, nullptr);
    context.GetSelection().Select(EditorSelectObject::MakeActor(editorActor->GetHandle(), editorActor->GetID()));

    if (!Check(context.GetScene() == &layer.GetEditorScene(), "EditorContext::GetScene should return EditorWorld"))
        return false;
    if (!Check(context.GetEditorScene() == &layer.GetEditorScene(), "EditorContext::GetEditorScene mismatch"))
        return false;
    if (!Check(context.GetSimulationScene() == &layer.GetEditorScene(),
               "Edit mode simulation scene should be EditorWorld"))
        return false;
    if (!Check(context.GetPlayScene() == nullptr, "Edit mode should not expose a PlayWorld"))
        return false;
    context.SetSceneViewMode(EditorWorldViewMode::PlayWorldInspect);
    if (!Check(context.GetSceneViewMode() == EditorWorldViewMode::EditorWorld &&
                   context.GetSceneViewScene() == &layer.GetEditorScene(),
               "Edit mode should reject PlayWorldInspect"))
        return false;

    layer.MarkDirty();
    if (!Check(layer.BeginPlay(), "BeginPlay failed for editor context routing"))
        return false;
    if (!Check(context.GetScene() == &layer.GetEditorScene() && context.GetEditorScene() == &layer.GetEditorScene(),
               "EditorContext should keep editing routed to EditorWorld in Play"))
        return false;
    if (!Check(context.GetPlayScene() == layer.GetPlayScene() && context.GetSimulationScene() == layer.GetPlayScene(),
               "Play mode simulation scene should be PlayWorld"))
        return false;
    Actor* playActor = layer.GetPlayScene()->FindByID(editorActor->GetID());
    if (!Check(playActor && playActor != editorActor, "PlayWorld should contain cloned actor instances"))
        return false;
    playActor->GetTransform().position.x = 42.0f;

    if (!Check(context.GetSceneViewScene() == &layer.GetEditorScene() && !layer.GetSceneViewportUsesSimulationScene(),
               "SceneView should stay on EditorWorld after BeginPlay"))
        return false;
    context.SetSceneViewMode(EditorWorldViewMode::PlayWorldInspect);
    if (!Check(context.GetSceneViewScene() == layer.GetPlayScene() && layer.GetSceneViewportUsesSimulationScene(),
               "PlayWorldInspect did not route SceneView to PlayWorld"))
        return false;
    if (!Check(context.GetInspectorScene() == layer.GetPlayScene() &&
                   context.GetSelection().GetPrimaryObject().GetWorldKind() == EditorSelectionWorldKind::Play,
               "PlayWorldInspect did not map actor selection to PlayWorld"))
        return false;
    if (!Check(context.GetSelection().ResolveActor(*layer.GetPlayScene()) == playActor &&
                   context.GetSelection().ResolveActor(*context.GetScene()) == editorActor,
               "selection did not preserve persistent id across worlds"))
        return false;
    if (!Check(!context.CanEditScene() && !context.CanEditSelection(), "PlayWorldInspect should be read-only"))
        return false;

    layer.PausePlay();
    if (!Check(context.GetScene() == &layer.GetEditorScene() && context.GetSimulationScene() == layer.GetPlayScene(),
               "Pause mode should preserve EditorWorld edits and PlayWorld simulation"))
        return false;

    layer.StopPlay();
    context.RefreshSceneViewMode();
    return Check(context.GetPlayScene() == nullptr && context.GetSimulationScene() == &layer.GetEditorScene() &&
                     context.GetSceneViewScene() == &layer.GetEditorScene() &&
                     context.GetInspectorScene() == &layer.GetEditorScene() &&
                     context.GetScene() == &layer.GetEditorScene() &&
                     context.GetSelection().GetPrimaryObject().GetWorldKind() == EditorSelectionWorldKind::Editor,
                 "StopPlay did not return editor context to EditorWorld-only routing");
}

bool TestEditorViewportOperatorFrameSelected() {
    SceneRenderLayer layer(nullptr, 320, 180);
    EditorContext context(&layer, nullptr, nullptr, nullptr);
    EditorOperators operators;
    context.SetOperators(&operators);

    if (!Check(!operators.Viewport().FrameSelected(context), "frame selected should fail without actor selection"))
        return false;

    Actor* actor = layer.GetEditorScene().CreateActor("FrameMe");
    actor->GetTransform().position = {3.0f, 4.0f, -5.0f};
    context.GetSelection().Select(EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID()));
    SceneViewport* viewport = context.GetSceneViewport();
    if (!Check(viewport != nullptr, "scene viewport missing for frame selected test"))
        return false;

    const Vec3 before = viewport->GetCamera().GetPosition();
    if (!Check(operators.Viewport().FrameSelected(context), "frame selected operator failed with actor selection"))
        return false;
    const Vec3 target = actor->GetWorldMatrix().TransformPoint(Vec3::Zero());
    if (!Check((viewport->GetCamera().GetTarget() - target).Length() < 1e-3f &&
                   (viewport->GetCamera().GetPosition() - before).Length() > 0.1f,
               "frame selected did not move camera to selected actor"))
        return false;

    viewport->ToggleProjectionMode();
    if (!Check(operators.Viewport().FrameSelected(context) && viewport->IsOrthographic() &&
                   viewport->GetCamera().GetOrthoWidth() >= 2.0f,
               "frame selected did not preserve orthographic projection"))
        return false;
    return true;
}

bool TestEditorGizmoRowVectorLocalConversion() {
    Transform parentTransform;
    parentTransform.position = {3.0f, 4.0f, 5.0f};
    parentTransform.rotation = {10.0f, 25.0f, -5.0f};
    Transform localTransform;
    localTransform.position = {1.0f, 2.0f, -3.0f};
    localTransform.rotation = {-8.0f, 15.0f, 12.0f};
    localTransform.scale = {1.2f, 0.8f, 1.5f};

    const Mat4 parentWorld = parentTransform.GetLocalMatrix();
    const Mat4 expectedLocal = localTransform.GetLocalMatrix();
    const Mat4 world = expectedLocal * parentWorld;
    Mat4 actualLocal;
    if (!Check(EditorGizmoController::ComputeLocalMatrix(world, &parentWorld, actualLocal),
               "gizmo local matrix conversion failed"))
        return false;

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (!Check(NearlyEqual(actualLocal.m[row][column], expectedLocal.m[row][column], 1e-3f),
                       "gizmo local matrix violated row-vector order"))
                return false;
        }
    }
    return true;
}

bool TestEditorServiceActionAndInspectorRegistries() {
    Scene scene("EditorRegistries");
    EditorContext context(&scene);
    std::vector<std::string> events;

    class RecordingService final : public EditorService {
    public:
        RecordingService(std::string name, std::vector<std::string>& events)
            : m_Name(std::move(name)), m_Events(events) {}
        void OnAttach(EditorContext& context) override {
            EditorService::OnAttach(context);
            m_Events.push_back("attach:" + m_Name);
        }
        void OnUpdate(float) override { m_Events.push_back("update:" + m_Name); }
        void OnDetach() override {
            m_Events.push_back("detach:" + m_Name);
            EditorService::OnDetach();
        }

    private:
        std::string m_Name;
        std::vector<std::string>& m_Events;
    };

    RecordingService first("first", events);
    RecordingService second("second", events);
    EditorServiceCollection services;
    services.Add(first);
    services.Add(second);
    services.AttachAll(context);
    if (!Check(context.GetService<RecordingService>() == &second,
               "typed service lookup did not return registered service"))
        return false;
    services.UpdateAll(0.016f);
    services.DetachAll(context);
    const std::vector<std::string> expected{"attach:first",  "attach:second", "update:first",
                                            "update:second", "detach:second", "detach:first"};
    if (!Check(events == expected, "service lifecycle order mismatch"))
        return false;

    EditorActionRegistry actions;
    bool actionEnabled = false;
    int actionExecutions = 0;
    if (!Check(actions.Register(std::make_unique<LambdaEditorAction>(
                   "test.action", "Test Action", [&actionExecutions](EditorContext&) { ++actionExecutions; },
                   [&actionEnabled](EditorContext&) { return actionEnabled; })),
               "action registration failed"))
        return false;
    if (!Check(!actions.Execute("test.action", context) && actionExecutions == 0, "disabled action executed"))
        return false;
    actionEnabled = true;
    if (!Check(actions.Execute("test.action", context) && actionExecutions == 1, "enabled action did not execute"))
        return false;

    class TestSection final : public EditorInspectorSection {
    public:
        TestSection(const char* id, int order) : m_ID(id), m_Order(order) {}
        const char* GetID() const override { return m_ID.c_str(); }
        int GetOrder() const override { return m_Order; }
        bool CanDraw(const EditorSelectObject& object, const EditorContext&) const override { return object.IsActor(); }
        void Draw(EditorContext&) override {}

    private:
        std::string m_ID;
        int m_Order;
    };

    EditorInspectorRegistry sections;
    sections.Register(std::make_unique<TestSection>("late", 20));
    sections.Register(std::make_unique<TestSection>("early", 10));
    if (!Check(std::string(sections.GetSections()[0]->GetID()) == "early", "inspector section order mismatch"))
        return false;
    EditorSelection selection;
    if (!Check(!sections.GetSections()[0]->CanDraw(selection.GetPrimaryObject(), context),
               "inspector section filtering mismatch"))
        return false;
    Actor* selected = scene.CreateActor("SectionSelection");
    selection.SelectActorID(selected->GetID());
    return Check(sections.GetSections()[0]->CanDraw(selection.GetPrimaryObject(), context),
                 "inspector section did not accept actor selection");
}

bool TestEditorShortcutMapAndWorkspacePersistence() {
    EditorShortcutChord chord;
    std::string error;
    if (!Check(EditorShortcutMap::ParseChord("Ctrl+Shift+Z", chord, &error), "shortcut parse failed: " + error))
        return false;
    if (!Check(chord.ctrl && chord.shift && !chord.alt && chord.IsValid(), "shortcut modifiers parsed incorrectly"))
        return false;
    if (!Check(EditorShortcutMap::FormatChord(chord) == "Ctrl+Shift+Z", "shortcut format mismatch"))
        return false;
    if (!Check(!EditorShortcutMap::ParseChord("Ctrl+NoSuchKey", chord, &error), "invalid shortcut key was accepted"))
        return false;

    EditorShortcutMap shortcuts = EditorShortcutMap::CreateDefault();
    if (!Check(shortcuts.FindShortcut("scene.save") &&
                   EditorShortcutMap::FormatChord(*shortcuts.FindShortcut("scene.save")) == "Ctrl+S",
               "default save shortcut missing"))
        return false;

    EditorShortcutChord saveChord;
    if (!Check(EditorShortcutMap::ParseChord("Ctrl+S", saveChord, &error), "save shortcut parse failed"))
        return false;
    shortcuts.SetShortcut("conflict.action", saveChord);
    if (!Check(shortcuts.FindConflict("scene.save", saveChord) == "conflict.action",
               "shortcut conflict was not detected"))
        return false;

    Scene scene("ShortcutDispatch");
    EditorContext context(&scene);
    EditorActionRegistry actions;
    bool enabled = false;
    int disabledCount = 0;
    int enabledCount = 0;
    if (!Check(actions.Register(std::make_unique<LambdaEditorAction>(
                   "disabled.action", "Disabled", [&disabledCount](EditorContext&) { ++disabledCount; },
                   [&enabled](EditorContext&) { return enabled; })),
               "disabled action registration failed"))
        return false;
    if (!Check(actions.Register(std::make_unique<LambdaEditorAction>(
                   "enabled.action", "Enabled", [&enabledCount](EditorContext&) { ++enabledCount; })),
               "enabled action registration failed"))
        return false;
    EditorShortcutMap dispatchMap;
    dispatchMap.SetShortcut("disabled.action", saveChord);
    if (!Check(!dispatchMap.DispatchChord(saveChord, actions, context) && disabledCount == 0,
               "disabled shortcut action executed"))
        return false;
    dispatchMap.ClearShortcut("disabled.action");
    dispatchMap.SetShortcut("enabled.action", saveChord);
    if (!Check(dispatchMap.DispatchChord(saveChord, actions, context) && enabledCount == 1,
               "enabled shortcut action did not execute"))
        return false;

    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() /
        ("myengine_shortcut_workspace_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    EditorWorkspace workspace(root / "workspace.json");
    workspace.GetShortcuts().SetShortcut("scene.save", saveChord);
    workspace.GetShortcuts().ClearShortcut("edit.undo");
    if (!Check(workspace.Save(&error), "workspace shortcut save failed: " + error))
        return false;

    EditorWorkspace loaded(root / "workspace.json");
    if (!Check(loaded.Load(&error), "workspace shortcut load failed: " + error))
        return false;
    const auto* loadedSave = loaded.GetShortcuts().FindShortcut("scene.save");
    const auto* loadedUndo = loaded.GetShortcuts().FindShortcut("edit.undo");
    const bool persisted = loadedSave && *loadedSave == saveChord && loadedUndo && !loadedUndo->IsValid();
    std::error_code ec;
    fs::remove_all(root, ec);
    return Check(persisted, "workspace shortcut persistence mismatch");
}

bool TestEditorAppearancePreferencesAndScale() {
    if (!Check(EditorUIScaleManager::ClampUserScale(0.1f) == EditorUIScaleSettings::kMinUserScale,
               "ui scale lower clamp mismatch"))
        return false;
    if (!Check(Editor::UI::EditorUIScaleManager::ClampUserScale(0.1f) ==
                   Editor::UI::EditorUIScaleSettings::kMinUserScale,
               "namespaced ui scale lower clamp mismatch"))
        return false;
    if (!Check(EditorUIScaleManager::ClampUserScale(10.0f) == EditorUIScaleSettings::kMaxUserScale,
               "ui scale upper clamp mismatch"))
        return false;
    if (!Check(NearlyEqual(EditorUIScaleManager::ComputeEffectiveScale(1.5f, 1.25f), 1.875f),
               "effective ui scale calculation mismatch"))
        return false;

    const auto& fontConfig = Editor::UI::EditorFontManager::GetDefaultConfig();
    if (!Check(fontConfig.uiRegularPath.filename() == "Inter-Regular.ttf" &&
                   fontConfig.uiSemiBoldPath.filename() == "Inter-SemiBold.ttf" &&
                   fontConfig.logRegularPath.filename() == "JetBrainsMono-Regular.ttf" &&
                   fontConfig.iconSolidPath.filename() == "FontAwesome-Free-Solid-900.ttf",
               "editor font config filenames mismatch"))
        return false;
    if (!Check(std::string(Editor::UI::EditorIcons::PlayIcon()).size() > 0 &&
                   std::string(Editor::UI::EditorIcons::StopIcon()).size() > 0 &&
                   std::string(Editor::UI::EditorIcons::PauseIcon()).size() > 0 &&
                   std::string(Editor::UI::EditorIcons::StepIcon()).size() > 0,
               "editor icon fallback tokens are empty"))
        return false;

    EditorUIScaleManager scale;
    scale.Initialize(nullptr, 1.0f);
    scale.SetPlatformScaleForTesting(1.25f);
    const float first = scale.GetEffectiveScale();
    if (!Check(NearlyEqual(first, 1.25f), "testing dpi scale did not affect effective scale"))
        return false;
    if (!Check(scale.SetUserScale(1.5f) && NearlyEqual(scale.GetEffectiveScale(), 1.875f),
               "user ui scale did not affect effective scale"))
        return false;
    if (!Check(!scale.SetUserScale(1.5f), "unchanged user ui scale reported a change"))
        return false;

    EditorThemeManager theme;
    theme.Initialize("unknown");
    if (!Check(theme.GetThemeID() == "dark", "unknown theme did not fall back to dark"))
        return false;
    if (!Check(NearlyEqual(EditorThemeManager::ScaleValue(8.0f, 1.5f), 12.0f), "theme scaled spacing mismatch"))
        return false;
    if (!Check(NearlyEqual(Editor::UI::ScaleToken(Editor::UI::EditorStyleTokens{}.toolbarHeight, 1.25f),
                           Editor::UI::EditorStyleTokens{}.toolbarHeight * 1.25f),
               "toolbar style token scale mismatch"))
        return false;
    if (!Check(NearlyEqual(Editor::UI::ScaleToken(Editor::UI::EditorStyleTokens{}.statusBarHeight, 1.5f),
                           Editor::UI::EditorStyleTokens{}.statusBarHeight * 1.5f),
               "status bar style token scale mismatch"))
        return false;

    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_appearance_workspace_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    std::string error;
    EditorWorkspace workspace(root / "workspace.json");
    workspace.SetUserUiScale(1.35f);
    workspace.SetEditorThemeId("dark");
    workspace.SetPanelStateValue("assetBrowser", "filter", "missing");
    workspace.SetPanelStateValue("assetBrowser", "selectedFolder", "Content/Materials");
    workspace.SetPanelStateValue("assetBrowser", "diagnosticsOnly", "true");
    if (!Check(workspace.Save(&error), "appearance workspace save failed: " + error))
        return false;

    EditorWorkspace loaded(root / "workspace.json");
    if (!Check(loaded.Load(&error), "appearance workspace load failed: " + error))
        return false;
    const bool persisted =
        NearlyEqual(loaded.GetUserUiScale(), 1.35f) && loaded.GetEditorThemeId() == "dark" &&
        loaded.GetPanelStateValue("assetBrowser", "filter").value_or("") == "missing" &&
        loaded.GetPanelStateValue("assetBrowser", "selectedFolder").value_or("") == "Content/Materials" &&
        loaded.GetPanelStateValue("assetBrowser", "diagnosticsOnly").value_or("") == "true";
    std::error_code ec;
    fs::remove_all(root, ec);
    return Check(persisted, "appearance workspace persistence mismatch");
}

bool TestEditorStatusBarTextAndActionRouting() {
    Scene scene("StatusBar");
    EditorContext context(&scene);
    if (!Check(Editor::UI::EditorStatusBar::FormatSelectedText(context) == "None",
               "status bar empty selection mismatch"))
        return false;

    Actor* actor = scene.CreateActor("monkey");
    context.GetSelection().SelectActorID(actor->GetID());
    if (!Check(Editor::UI::EditorStatusBar::FormatSelectedText(context) == "monkey",
               "status bar actor selection mismatch"))
        return false;

    context.GetSelection().SelectAssetPath("Content/Models/monkey.gltf");
    if (!Check(Editor::UI::EditorStatusBar::FormatSelectedText(context) == "monkey.gltf",
               "status bar asset selection mismatch"))
        return false;
    if (!Check(Editor::UI::EditorStatusBar::FormatEditorModeText(context) == "Edit" &&
                   Editor::UI::EditorStatusBar::FormatSceneText(context) == "StatusBar",
               "status bar editor state mismatch"))
        return false;

    SceneRenderLayer sceneLayer(nullptr, 320, 180);
    EditorContext layerContext(&sceneLayer, nullptr, nullptr, nullptr);
    sceneLayer.MarkDirty();
    if (!Check(Editor::UI::EditorStatusBar::FormatSceneText(layerContext) == "Untitled *" &&
                   Editor::UI::EditorStatusBar::FormatEditorModeText(layerContext) == "Edit",
               "status bar dirty scene state mismatch"))
        return false;
    if (!Check(sceneLayer.BeginPlay() && Editor::UI::EditorStatusBar::FormatEditorModeText(layerContext) == "Play",
               "status bar play state mismatch"))
        return false;
    sceneLayer.PausePlay();
    if (!Check(Editor::UI::EditorStatusBar::FormatEditorModeText(layerContext) == "Paused",
               "status bar paused state mismatch"))
        return false;
    sceneLayer.StopPlay();

    EditorActionRegistry actions;
    int executions = 0;
    bool enabled = false;
    actions.Register(std::make_unique<LambdaEditorAction>(
        "menu.test", "Menu Test", [&executions](EditorContext&) { ++executions; },
        [&enabled](EditorContext&) { return enabled; }));
    if (!Check(!actions.Execute("menu.test", context) && executions == 0, "disabled menu action executed"))
        return false;
    enabled = true;
    return Check(actions.Execute("menu.test", context) && executions == 1, "enabled menu action did not execute");
}

bool TestEditorPanelSelectAllActionRouting() {
    Scene scene("PanelSelectAll");
    Actor* first = scene.CreateActor("First");
    Actor* second = scene.CreateActor("Second");
    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    SceneHierarchyPanel hierarchy;
    hierarchy.OnAttach(context);
    context.GetSelection().SelectActorID(first->GetID());
    if (!Check(hierarchy.CanHandleEditorAction(context, "edit.rename") &&
                   hierarchy.HandleEditorAction(context, "edit.rename"),
               "scene hierarchy did not expose or handle actor rename intent"))
        return false;
    if (!Check(hierarchy.CanHandleEditorAction(context, "edit.selectAll"),
               "scene hierarchy capability did not expose select all"))
        return false;
    if (!Check(hierarchy.HandleEditorAction(context, "edit.selectAll"), "scene hierarchy did not handle select all"))
        return false;
    if (!Check(context.GetSelection().HasActor() && context.GetSelection().GetActorID() == second->GetID() &&
                   context.GetSelection().GetMultiCount() == 2 && context.GetSelection().IsSelected(first->GetID()) &&
                   context.GetSelection().IsSelected(second->GetID()),
               "scene hierarchy select all did not select every actor"))
        return false;
    if (!Check(hierarchy.HandleEditorAction(context, "hierarchy.expandAll") &&
                   hierarchy.HandleEditorAction(context, "hierarchy.collapseAll"),
               "scene hierarchy did not handle expand/collapse all"))
        return false;

    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() /
        ("myengine_panel_select_all_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto content = root / "Content";
    std::error_code ec;
    fs::create_directories(content / "Materials", ec);
    fs::create_directories(content / "Tools", ec);
    const fs::path assetPath = content / "Materials" / "Selection.mat";
    std::ofstream(assetPath) << "{}";
    const fs::path moveSourcePath = content / "Tools" / "MoveMe.as";
    std::ofstream(moveSourcePath) << "class MoveMe {}";

    EditorAssetRegistry registry;
    registry.SetRoot(content);
    registry.Refresh();
    context.SetProjectRoot(root);
    context.SetAssetRegistry(&registry);
    context.GetSelection().Clear();

    AssetBrowserPanel browser;
    browser.OnAttach(context);
    if (!Check(browser.CanHandleEditorAction(context, "edit.selectAll") &&
                   !browser.CanHandleEditorAction(context, "edit.delete"),
               "asset browser capability did not reflect selectable asset/root folder state")) {
        fs::remove_all(root, ec);
        return false;
    }
    const fs::path createdFolder = content / "New Folder";
    if (!Check(browser.CanHandleEditorAction(context, "asset.createFolder") &&
                   browser.HandleEditorAction(context, "asset.createFolder") && fs::is_directory(createdFolder),
               "asset browser did not expose or handle asset.createFolder")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(createdFolder), "asset browser create folder action did not undo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::is_directory(createdFolder),
               "asset browser create folder action did not redo")) {
        fs::remove_all(root, ec);
        return false;
    }
    registry.Refresh();

    const fs::path materialTemplatePath = content / "Materials" / "NewMaterial.mat";
    if (!Check(browser.CanHandleEditorAction(context, "asset.createMaterial") &&
                   browser.HandleEditorAction(context, "asset.createMaterial") &&
                   fs::is_regular_file(materialTemplatePath) &&
                   fs::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       materialTemplatePath.lexically_normal(),
               "asset browser did not expose or handle material template action")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(materialTemplatePath),
               "asset browser material template action did not undo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::is_regular_file(materialTemplatePath),
               "asset browser material template action did not redo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(materialTemplatePath),
               "asset browser material template action cleanup failed")) {
        fs::remove_all(root, ec);
        return false;
    }

    const fs::path textureTemplatePath = content / "Textures" / "NewTexture.tex";
    if (!Check(browser.CanHandleEditorAction(context, "asset.createTexture") &&
                   browser.HandleEditorAction(context, "asset.createTexture") &&
                   fs::is_regular_file(textureTemplatePath) &&
                   fs::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       textureTemplatePath.lexically_normal(),
               "asset browser did not expose or handle texture template action")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(textureTemplatePath),
               "asset browser texture template action did not undo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::is_regular_file(textureTemplatePath),
               "asset browser texture template action did not redo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(textureTemplatePath),
               "asset browser texture template action cleanup failed")) {
        fs::remove_all(root, ec);
        return false;
    }

    const fs::path prefabTemplatePath = content / "Prefabs" / "NewPrefab.prefab.json";
    if (!Check(browser.CanHandleEditorAction(context, "asset.createPrefab") &&
                   browser.HandleEditorAction(context, "asset.createPrefab") &&
                   fs::is_regular_file(prefabTemplatePath) &&
                   fs::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       prefabTemplatePath.lexically_normal(),
               "asset browser did not expose or handle prefab template action")) {
        fs::remove_all(root, ec);
        return false;
    }
    PrefabAsset prefabTemplate;
    std::string prefabError;
    const auto prefabMeta = AssetMeta::Load(prefabTemplatePath.string(), &prefabError);
    if (!Check(prefabMeta.has_value() && PrefabAsset::Load(prefabTemplatePath, prefabTemplate, &prefabError) &&
                   prefabTemplate.uuid == prefabMeta->uuid && prefabTemplate.rootLocalId == "root" &&
                   prefabTemplate.nodes.size() == 1 && prefabTemplate.nodes.front().name == "NewPrefab",
               "asset browser prefab template did not create valid prefab metadata")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(prefabTemplatePath) &&
                   !fs::exists(AssetMeta::MetaPathFor(prefabTemplatePath.string())),
               "asset browser prefab template action did not undo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::is_regular_file(prefabTemplatePath) &&
                   fs::is_regular_file(AssetMeta::MetaPathFor(prefabTemplatePath.string())),
               "asset browser prefab template action did not redo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(prefabTemplatePath),
               "asset browser prefab template action cleanup failed")) {
        fs::remove_all(root, ec);
        return false;
    }

    const fs::path scriptTemplatePath = content / "NewScript.as";
    if (!Check(browser.CanHandleEditorAction(context, "asset.createAngelScript") &&
                   browser.HandleEditorAction(context, "asset.createAngelScript") &&
                   fs::is_regular_file(scriptTemplatePath) &&
                   fs::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       scriptTemplatePath.lexically_normal(),
               "asset browser did not expose or handle AngelScript template action")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(scriptTemplatePath),
               "asset browser AngelScript template action did not undo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::is_regular_file(scriptTemplatePath),
               "asset browser AngelScript template action did not redo")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(scriptTemplatePath),
               "asset browser AngelScript template action cleanup failed")) {
        fs::remove_all(root, ec);
        return false;
    }
    registry.Refresh();

    const bool handled = browser.HandleEditorAction(context, "edit.selectAll");
    const std::string selected = context.GetSelection().GetAssetPath();
    if (!Check(handled && context.GetSelection().HasAsset() &&
                   fs::path(selected).lexically_normal() == assetPath.lexically_normal(),
               "asset browser select all did not select the current folder asset")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(browser.HandleEditorAction(context, "edit.duplicate"),
               "asset browser multi-select duplicate action failed")) {
        fs::remove_all(root, ec);
        return false;
    }
    const fs::path multiDuplicateMaterial = content / "Materials" / "Selection_Copy.mat";
    const fs::path multiDuplicateScript = content / "Tools" / "MoveMe_Copy.as";
    if (!Check(fs::exists(multiDuplicateMaterial) && fs::exists(multiDuplicateScript),
               "asset browser multi-select duplicate did not duplicate visible assets")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(stack.Undo(context) && stack.Undo(context) && !fs::exists(multiDuplicateMaterial) &&
                   !fs::exists(multiDuplicateScript),
               "asset browser multi-select duplicate undo failed")) {
        fs::remove_all(root, ec);
        return false;
    }
    registry.Refresh();

    context.GetSelection().SelectAssetPath(moveSourcePath.string());
    if (!Check(browser.CanHandleEditorAction(context, "asset.move") &&
                   browser.HandleEditorAction(context, "asset.move"),
               "asset browser did not expose or handle asset.move")) {
        fs::remove_all(root, ec);
        return false;
    }
    const fs::path movedPath = content / "MoveMe.as";
    if (!Check(!fs::exists(moveSourcePath) && fs::exists(movedPath) &&
                   fs::path(context.GetSelection().GetAssetPath()).lexically_normal() == movedPath.lexically_normal(),
               "asset browser asset.move did not move asset into selected folder")) {
        fs::remove_all(root, ec);
        return false;
    }
    registry.Refresh();
    const auto movedFolderAssets = registry.GetAssetsInFolder("Content", false);
    if (!Check(std::any_of(movedFolderAssets.begin(), movedFolderAssets.end(),
                           [&](const EditorAssetInfo& info) {
                               return info.absolutePath.lexically_normal() == movedPath.lexically_normal();
                           }),
               "asset browser asset.move did not refresh registry folder contents")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(!browser.CanHandleEditorAction(context, "asset.move") &&
                   !browser.HandleEditorAction(context, "asset.move"),
               "asset browser asset.move stayed enabled for asset already in selected folder")) {
        fs::remove_all(root, ec);
        return false;
    }

    context.GetSelection().Clear();
    if (!Check(!browser.CanHandleEditorAction(context, "asset.open"),
               "asset browser exposed asset.open without asset selection")) {
        fs::remove_all(root, ec);
        return false;
    }
    context.GetSelection().SelectAssetPath(assetPath.string());
    if (!Check(browser.CanHandleEditorAction(context, "asset.open") &&
                   browser.HandleEditorAction(context, "asset.open") &&
                   fs::path(context.GetSelection().GetAssetPath()).lexically_normal() == assetPath.lexically_normal(),
               "asset browser asset.open did not keep inspectable asset selected")) {
        fs::remove_all(root, ec);
        return false;
    }

    context.GetSelection().SelectAssetPath(assetPath.string());
    if (!Check(browser.HandleEditorAction(context, "edit.duplicate"),
               "asset browser did not handle duplicate action")) {
        fs::remove_all(root, ec);
        return false;
    }
    const fs::path duplicatePath = content / "Materials" / "Selection_Copy.mat";
    if (!Check(fs::exists(duplicatePath), "asset browser duplicate action did not create copy")) {
        fs::remove_all(root, ec);
        return false;
    }
    context.GetSelection().SelectAssetPath(duplicatePath.string());
    if (!Check(browser.HandleEditorAction(context, "edit.delete") && fs::exists(duplicatePath),
               "asset browser delete action bypassed delete confirmation")) {
        fs::remove_all(root, ec);
        return false;
    }

    context.GetSelection().SelectAssetPath(assetPath.string());
    if (!Check(browser.CanHandleEditorAction(context, "edit.copy") &&
                   !browser.CanHandleEditorAction(context, "edit.paste"),
               "asset browser capability did not reflect copy/paste before asset clipboard")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(browser.HandleEditorAction(context, "edit.copy"), "asset browser did not handle copy action")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(browser.CanHandleEditorAction(context, "edit.paste"),
               "asset browser capability did not expose paste after asset copy")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(!hierarchy.HandleEditorAction(context, "edit.paste"),
               "scene hierarchy accepted asset clipboard paste")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(browser.HandleEditorAction(context, "edit.paste"), "asset browser did not handle copy/paste actions")) {
        fs::remove_all(root, ec);
        return false;
    }
    const fs::path pastedPath = content / "Selection.mat";
    if (!Check(fs::exists(pastedPath) && fs::exists(AssetMeta::MetaPathFor(pastedPath.string())),
               "asset browser paste action did not copy asset into selected folder")) {
        fs::remove_all(root, ec);
        return false;
    }
    context.GetSelection().SelectAssetPath(assetPath.string());
    if (!Check(browser.CanHandleEditorAction(context, "edit.rename") &&
                   browser.HandleEditorAction(context, "edit.rename") &&
                   browser.CanHandleEditorAction(context, "asset.rename") &&
                   browser.HandleEditorAction(context, "asset.rename"),
               "asset browser did not expose or handle asset rename intent")) {
        fs::remove_all(root, ec);
        return false;
    }

    fs::remove_all(root, ec);
    return true;
}

bool TestSceneHierarchyOrganizationActions() {
    Scene scene("HierarchyOrganization");
    Actor* first = scene.CreateActor("First");
    Actor* second = scene.CreateActor("Second");
    Actor* third = scene.CreateActor("Third");
    const uint64_t firstID = first->GetID();
    const uint64_t secondID = second->GetID();
    const uint64_t thirdID = third->GetID();

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    SceneHierarchyPanel hierarchy;
    hierarchy.OnAttach(context);

    context.GetSelection().SelectActorID(secondID);
    if (!Check(hierarchy.CanHandleEditorAction(context, "edit.copy") &&
                   hierarchy.CanHandleEditorAction(context, "edit.duplicate") &&
                   hierarchy.CanHandleEditorAction(context, "edit.delete") &&
                   !hierarchy.CanHandleEditorAction(context, "edit.paste"),
               "scene hierarchy capability did not reflect selected actor edit state"))
        return false;
    if (!Check(hierarchy.HandleEditorAction(context, "edit.copy"), "scene hierarchy did not handle actor copy action"))
        return false;
    if (!Check(operators.Commands().HasActorClipboard(), "scene hierarchy copy did not populate actor clipboard"))
        return false;
    if (!Check(hierarchy.CanHandleEditorAction(context, "edit.paste"),
               "scene hierarchy capability did not expose actor paste after copy"))
        return false;
    if (!Check(hierarchy.HandleEditorAction(context, "edit.duplicate"),
               "scene hierarchy did not handle actor duplicate action"))
        return false;
    const uint64_t duplicateID = context.GetSelection().GetActorID();
    if (!Check(duplicateID != secondID && scene.FindByID(duplicateID),
               "scene hierarchy duplicate action did not create selected clone"))
        return false;
    stack.Undo(context);
    if (!Check(!scene.FindByID(duplicateID) && scene.FindByID(secondID),
               "scene hierarchy duplicate undo did not remove clone"))
        return false;
    if (!Check(hierarchy.HandleEditorAction(context, "edit.paste"),
               "scene hierarchy did not handle actor paste action"))
        return false;
    const uint64_t pastedID = context.GetSelection().GetActorID();
    if (!Check(pastedID != secondID && scene.FindByID(pastedID),
               "scene hierarchy paste action did not create selected actor copy"))
        return false;
    stack.Undo(context);
    if (!Check(!scene.FindByID(pastedID) && scene.FindByID(secondID),
               "scene hierarchy paste undo did not remove pasted actor"))
        return false;

    context.GetSelection().SelectActorID(thirdID);
    if (!Check(hierarchy.HandleEditorAction(context, "edit.delete"),
               "scene hierarchy did not handle actor delete action"))
        return false;
    if (!Check(!scene.FindByID(thirdID), "scene hierarchy delete action did not remove selected actor"))
        return false;
    stack.Undo(context);
    if (!Check(scene.FindByID(thirdID), "scene hierarchy delete undo did not restore actor"))
        return false;

    context.GetSelection().SelectActorID(secondID);
    if (!Check(hierarchy.HandleEditorAction(context, "hierarchy.moveUp"),
               "scene hierarchy did not move selected actor up"))
        return false;
    auto roots = scene.GetRootActors();
    if (!Check(roots.size() >= 3 && roots[0]->GetID() == secondID && roots[1]->GetID() == firstID &&
                   roots[2]->GetID() == thirdID,
               "move up did not reorder root actors"))
        return false;
    stack.Undo(context);
    roots = scene.GetRootActors();
    if (!Check(roots.size() >= 3 && roots[0]->GetID() == firstID && roots[1]->GetID() == secondID &&
                   roots[2]->GetID() == thirdID,
               "move up undo did not restore root order"))
        return false;
    stack.Redo(context);
    roots = scene.GetRootActors();
    if (!Check(roots.size() >= 3 && roots[0]->GetID() == secondID && roots[1]->GetID() == firstID &&
                   roots[2]->GetID() == thirdID,
               "move up redo did not restore moved order"))
        return false;
    stack.Undo(context);

    context.GetSelection().SelectActorID(secondID);
    if (!Check(hierarchy.HandleEditorAction(context, "hierarchy.moveDown"),
               "scene hierarchy did not move selected actor down"))
        return false;
    roots = scene.GetRootActors();
    if (!Check(roots.size() >= 3 && roots[0]->GetID() == firstID && roots[1]->GetID() == thirdID &&
                   roots[2]->GetID() == secondID,
               "move down did not reorder root actors"))
        return false;
    stack.Undo(context);

    context.GetSelection().SelectActorID(secondID);
    if (!Check(hierarchy.HandleEditorAction(context, "hierarchy.createEmptyParent"),
               "scene hierarchy did not create empty parent"))
        return false;
    Actor* newParent = scene.FindByID(context.GetSelection().GetActorID());
    Actor* reloadedSecond = scene.FindByID(secondID);
    if (!Check(newParent && newParent->GetName() == "Empty Parent" && reloadedSecond &&
                   reloadedSecond->GetParent() == newParent,
               "create empty parent did not wrap selected actor"))
        return false;
    stack.Undo(context);
    if (!Check(scene.FindByID(secondID) && !scene.FindByID(secondID)->GetParent() &&
                   scene.FindByName("Empty Parent") == nullptr,
               "create empty parent undo did not restore hierarchy"))
        return false;
    stack.Redo(context);
    newParent = scene.FindByID(context.GetSelection().GetActorID());
    if (!Check(newParent && scene.FindByID(secondID) && scene.FindByID(secondID)->GetParent() == newParent,
               "create empty parent redo did not restore hierarchy"))
        return false;
    stack.Undo(context);

    Actor* parent = scene.CreateActor("Parent");
    Actor* child = scene.CreateActor("Child", parent);
    const uint64_t parentID = parent->GetID();
    const uint64_t childID = child->GetID();
    context.GetSelection().SelectActorID(childID);
    if (!Check(hierarchy.HandleEditorAction(context, "hierarchy.unparent"),
               "scene hierarchy did not unparent selected actor"))
        return false;
    if (!Check(scene.FindByID(childID) && scene.FindByID(childID)->GetParent() == nullptr,
               "unparent did not move actor to parent scope"))
        return false;
    stack.Undo(context);
    if (!Check(scene.FindByID(childID) && scene.FindByID(parentID) &&
                   scene.FindByID(childID)->GetParent() == scene.FindByID(parentID),
               "unparent undo did not restore parent"))
        return false;

    Actor* grandchild = scene.CreateActor("Grandchild", child);
    Actor* sibling = scene.CreateActor("Sibling", parent);
    const uint64_t grandchildID = grandchild->GetID();
    const uint64_t siblingID = sibling->GetID();
    context.GetSelection().SelectActorID(parentID);
    if (!Check(hierarchy.CanHandleEditorAction(context, "hierarchy.selectSubtree") &&
                   hierarchy.HandleEditorAction(context, "hierarchy.selectSubtree"),
               "scene hierarchy did not expose or handle select subtree"))
        return false;
    if (!Check(context.GetSelection().GetMultiCount() == 4 && context.GetSelection().IsSelected(parentID) &&
                   context.GetSelection().IsSelected(childID) && context.GetSelection().IsSelected(grandchildID) &&
                   context.GetSelection().IsSelected(siblingID),
               "select subtree did not select root and descendants"))
        return false;
    context.GetSelection().SelectActorID(parentID);
    if (!Check(hierarchy.HandleEditorAction(context, "hierarchy.selectChildren") &&
                   context.GetSelection().GetMultiCount() == 3 && !context.GetSelection().IsSelected(parentID) &&
                   context.GetSelection().IsSelected(childID) && context.GetSelection().IsSelected(grandchildID) &&
                   context.GetSelection().IsSelected(siblingID),
               "select children did not select only descendants"))
        return false;

    context.GetSelection().SelectActorID(childID);
    const std::string undoNameBeforeSelectionTools = stack.GetUndoName();
    if (!Check(hierarchy.CanHandleEditorAction(context, "hierarchy.selectParent") &&
                   hierarchy.HandleEditorAction(context, "hierarchy.selectParent") &&
                   context.GetSelection().GetActorID() == parentID,
               "scene hierarchy did not select parent"))
        return false;
    if (!Check(std::string(stack.GetUndoName()) == undoNameBeforeSelectionTools,
               "select parent should not enter the command stack"))
        return false;

    context.GetSelection().SelectActorID(childID);
    if (!Check(hierarchy.CanHandleEditorAction(context, "hierarchy.selectNextSibling") &&
                   hierarchy.HandleEditorAction(context, "hierarchy.selectNextSibling") &&
                   context.GetSelection().GetActorID() == siblingID,
               "scene hierarchy did not select next sibling"))
        return false;
    if (!Check(std::string(stack.GetUndoName()) == undoNameBeforeSelectionTools,
               "select next sibling should not enter the command stack"))
        return false;

    context.GetSelection().SelectActorID(siblingID);
    if (!Check(hierarchy.CanHandleEditorAction(context, "hierarchy.selectPreviousSibling") &&
                   hierarchy.HandleEditorAction(context, "hierarchy.selectPreviousSibling") &&
                   context.GetSelection().GetActorID() == childID,
               "scene hierarchy did not select previous sibling"))
        return false;
    if (!Check(std::string(stack.GetUndoName()) == undoNameBeforeSelectionTools,
               "select previous sibling should not enter the command stack"))
        return false;

    return true;
}

bool TestEditorInspectorSelectionRouting() {
    const auto root = std::filesystem::temp_directory_path() / "myengine_inspector_selection_routing";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    const auto materialPath = root / "Selected.mat";
    const auto texturePath = root / "Selected.png";
    const auto audioPath = root / "Selected.wav";
    const auto modelPath = root / "Selected.obj";
    const auto prefabPath = root / "Selected.prefab.json";
    const auto jsonPath = root / "Selected.json";
    std::ofstream(materialPath) << "{}";
    std::ofstream(texturePath) << "png";
    std::ofstream(audioPath) << "wav";
    std::ofstream(modelPath) << "obj";
    std::ofstream(prefabPath) << R"({"version":1,"uuid":"test-prefab","rootLocalId":"root","nodes":[]})";
    std::ofstream(jsonPath) << R"({"name":"Selected"})";

    EditorAssetRegistry registry;
    registry.SetRoot(root);
    registry.Refresh();
    Scene scene("InspectorRouting");
    EditorContext context(&scene);
    context.SetAssetRegistry(&registry);
    auto sections = CreateDefaultInspectorSections();

    const auto accepts = [&](const char* id, const EditorSelectObject& object) {
        const auto found = std::find_if(sections.begin(), sections.end(),
                                        [id](const auto& section) { return std::string(section->GetID()) == id; });
        return found != sections.end() && (*found)->CanDraw(object, context);
    };

    if (!Check(accepts("sceneSettings", {}) && !accepts("transform", {}),
               "empty selection did not route to scene settings"))
        return false;

    Actor* actor = scene.CreateActor("SelectedActor");
    const EditorSelectObject actorObject = EditorSelectObject::MakeActor(actor->GetHandle(), actor->GetID());
    if (!Check(accepts("transform", actorObject) && !accepts("materialAsset", actorObject),
               "actor selection routed to the wrong inspector sections"))
        return false;

    const EditorSelectObject materialObject = EditorSelectObject::MakeAsset(materialPath.string());
    if (!Check(accepts("materialAsset", materialObject) && !accepts("textureAsset", materialObject) &&
                   !accepts("genericAsset", materialObject),
               "material selection routing mismatch"))
        return false;

    const EditorSelectObject textureObject = EditorSelectObject::MakeAsset(texturePath.string());
    if (!Check(accepts("textureAsset", textureObject) && !accepts("materialAsset", textureObject) &&
                   !accepts("genericAsset", textureObject),
               "texture selection routing mismatch"))
        return false;

    const EditorSelectObject modelObject = EditorSelectObject::MakeAsset(modelPath.string());
    const bool modelRouted = accepts("modelAsset", modelObject) && !accepts("genericAsset", modelObject) &&
                             !accepts("materialAsset", modelObject) && !accepts("textureAsset", modelObject);
    const EditorSelectObject jsonObject = EditorSelectObject::MakeAsset(jsonPath.string());
    const bool jsonRouted = accepts("genericAsset", jsonObject) && !accepts("modelAsset", jsonObject) &&
                            !accepts("prefabAsset", jsonObject) && !accepts("audioAsset", jsonObject) &&
                            !accepts("materialAsset", jsonObject) && !accepts("textureAsset", jsonObject);
    const EditorSelectObject prefabObject = EditorSelectObject::MakeAsset(prefabPath.string());
    const bool prefabRouted = accepts("prefabAsset", prefabObject) && !accepts("genericAsset", prefabObject) &&
                              !accepts("modelAsset", prefabObject) && !accepts("audioAsset", prefabObject) &&
                              !accepts("materialAsset", prefabObject) && !accepts("textureAsset", prefabObject);
    const EditorSelectObject audioObject = EditorSelectObject::MakeAsset(audioPath.string());
    const bool audioRouted = accepts("audioAsset", audioObject) && !accepts("genericAsset", audioObject) &&
                             !accepts("prefabAsset", audioObject) && !accepts("modelAsset", audioObject) &&
                             !accepts("materialAsset", audioObject) && !accepts("textureAsset", audioObject);
    std::filesystem::remove_all(root, error);
    return Check(modelRouted && prefabRouted && jsonRouted && audioRouted,
                 "asset inspector selection routing mismatch");
}

bool TestInspectorPanelSnapshotsAreInputGated() {
    const char* candidates[] = {
        "src/Editor/Panels/InspectorPanel.cpp",
        "../../../src/Editor/Panels/InspectorPanel.cpp",
        "../../../../src/Editor/Panels/InspectorPanel.cpp",
        "../../../../../src/Editor/Panels/InspectorPanel.cpp",
    };
    std::string source;
    for (const char* path : candidates) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        break;
    }
    if (!Check(!source.empty(), "InspectorPanel source was not found"))
        return false;
    if (!Check(source.find("ShouldCaptureInspectorEditSnapshot") != std::string::npos,
               "InspectorPanel does not gate edit snapshots behind input state"))
        return false;
    if (!Check(source.find("const std::string before = actor ? SceneSerializer::SaveToString(*scene)") ==
                   std::string::npos,
               "InspectorPanel still snapshots the whole scene for idle actor selection"))
        return false;
    if (!Check(source.find("captureInspectorSnapshot") != std::string::npos &&
                   source.find("SceneSerializer::SaveToString(*scene) : std::string{}") != std::string::npos,
               "InspectorPanel snapshot capture is not guarded by an explicit gate"))
        return false;
    return Check(source.find("m_Transaction.Commit(*context)") != std::string::npos,
                 "InspectorPanel no longer commits inspector edit transactions");
}

bool TestEditorProfilerBufferAndSourceContracts() {
    EditorProfiler profiler(2);
    profiler.RecordEvent("Editor", "First", 1.0, "a", 1);
    profiler.RecordEvent("Editor", "Second", 2.0, "b", 2);
    profiler.RecordEvent("Runtime", "Third", 3.0, "c", 3);
    auto events = profiler.Snapshot();
    if (!Check(events.size() == 2 && events[0].name == "Second" && events[1].name == "Third",
               "EditorProfiler did not keep the newest events"))
        return false;
    profiler.SetEnabled(false);
    profiler.RecordEvent("Editor", "Paused", 4.0);
    if (!Check(profiler.Snapshot().size() == 2, "EditorProfiler recorded events while disabled"))
        return false;
    profiler.Clear();
    if (!Check(profiler.Snapshot().empty(), "EditorProfiler clear did not remove events"))
        return false;

    const auto readSource = [](const char* relativePath) {
        const char* prefixes[] = {"", "../../../", "../../../../", "../../../../../"};
        for (const char* prefix : prefixes) {
            std::ifstream file(std::string(prefix) + relativePath, std::ios::binary);
            if (!file)
                continue;
            std::ostringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }
        return std::string{};
    };
    const auto readSources = [&](std::initializer_list<const char*> relativePaths) {
        std::string result;
        for (const char* relativePath : relativePaths) {
            result += readSource(relativePath);
            result.push_back('\n');
        }
        return result;
    };
    const std::string editorLayer = readSource("src/Editor/EditorLayer.cpp");
    const std::string editorCommand = readSource("src/Editor/EditorCommand.cpp");
    const std::string editorOperators = readSources({
        "src/Editor/EditorOperators.cpp",
        "src/Editor/EditorOperatorShared.h",
        "src/Editor/EditorOperatorsAssets.cpp",
        "src/Editor/EditorOperatorsCommands.cpp",
        "src/Editor/EditorOperatorsComponents.cpp",
        "src/Editor/EditorOperatorsDragDrop.cpp",
        "src/Editor/EditorOperatorsPrefabs.cpp",
        "src/Editor/EditorOperatorsSelection.cpp",
        "src/Editor/EditorOperatorsTransactions.cpp",
        "src/Editor/EditorOperatorsViewport.cpp",
    });
    const std::string editorImportService = readSource("src/Editor/EditorImportService.cpp");
    const std::string assetRegistry = readSource("src/Editor/EditorAssetRegistry.cpp");
    const std::string statusBar = readSource("src/Editor/UI/EditorStatusBar.cpp");
    const std::string profilerPanel = readSource("src/Editor/Panels/ProfilerPanel.cpp");
    const std::string sceneSerializer = readSource("src/Runtime/Scene/SceneSerializer.cpp");
    const std::string editingWorkflows = readSource("docs/editor-editing-workflows.md");
    if (!Check(!editorLayer.empty() && !editorCommand.empty() && !editorOperators.empty() &&
                   !editorImportService.empty() && !assetRegistry.empty() && !statusBar.empty() &&
                   !profilerPanel.empty() && !sceneSerializer.empty() && !editingWorkflows.empty(),
               "profiler source contract files were not found"))
        return false;
    if (!Check(statusBar.find("Mode: %s | Scene: %s | Selected: %s | Project: %s") != std::string::npos &&
                   statusBar.find("FormatEditorModeText") != std::string::npos &&
                   statusBar.find("FormatSceneText") != std::string::npos &&
                   statusBar.find("FrameStats") == std::string::npos &&
                   statusBar.find("RendererFrameStats") == std::string::npos &&
                   statusBar.find("renderContext") == std::string::npos && statusBar.find(" FPS") == std::string::npos,
               "status bar still exposes performance or renderer data"))
        return false;
    if (!Check(profilerPanel.find("stats.frameNumber") != std::string::npos &&
                   profilerPanel.find("stats.frameMs") != std::string::npos &&
                   profilerPanel.find("stats.smoothedFrameMs") != std::string::npos &&
                   profilerPanel.find("renderer.shadowGpuMs") != std::string::npos &&
                   profilerPanel.find("renderer.mainGpuMs") != std::string::npos &&
                   profilerPanel.find("renderer.ssaoGpuMs") != std::string::npos &&
                   profilerPanel.find("renderer.compositeGpuMs") != std::string::npos &&
                   profilerPanel.find("renderer.subMeshCount") != std::string::npos &&
                   profilerPanel.find("renderer.textureUploadBytes") != std::string::npos,
               "profiler panel is missing frame or renderer performance data"))
        return false;
    if (!Check(editorLayer.find("ProfilerPanel") != std::string::npos &&
                   editorLayer.find("RecordEvent(\"Editor\", \"OpenProject\"") != std::string::npos &&
                   editorLayer.find("RecordEvent(\"Editor\", \"Scene Load\"") != std::string::npos,
               "EditorLayer does not register profiler panel and editor load events"))
        return false;
    if (!Check(assetRegistry.find("RecordEvent(\"Editor\", \"AssetRegistry Refresh\"") != std::string::npos &&
                   assetRegistry.find("Logger::Info(\"[EditorAssetRegistry] Refresh") == std::string::npos,
               "EditorAssetRegistry refresh timing still logs or is not profiled"))
        return false;
    if (!Check(assetRegistry.find("database.ValidateAgainstProject(m_Root.parent_path(), validationReport)") !=
                       std::string::npos &&
                   assetRegistry.find("AttachValidationDiagnostics(m_Assets, validationReport)") != std::string::npos &&
                   assetRegistry.find("[Validation]") != std::string::npos,
               "EditorAssetRegistry does not surface AssetDatabase validation issues as asset diagnostics"))
        return false;
    if (!Check(editorCommand.find("RecordCommandEvent") != std::string::npos &&
                   editorCommand.find("RecordEvent(\"EditorCommand\"") != std::string::npos &&
                   editorCommand.find("std::chrono::steady_clock") != std::string::npos,
               "EditorCommandStack does not profile editor command execute/undo/redo events"))
        return false;
    if (!Check(editorOperators.find("RecordAssetOperatorEvent") != std::string::npos &&
                   editorOperators.find("RecordEvent(\"EditorAsset\"") != std::string::npos &&
                   editorOperators.find("EditorAssetOperator::ReimportWithSettings") != std::string::npos &&
                   editorOperators.find("EditorAssetOperator::OpenAsset") != std::string::npos,
               "EditorAssetOperator does not profile asset editing/open/reimport events"))
        return false;
    if (!Check(editorOperators.find("RecordPrefabOperatorEvent") != std::string::npos &&
                   editorOperators.find("RecordEvent(\"EditorPrefab\"") != std::string::npos &&
                   editorOperators.find("EditorPrefabOperator::ApplyOverride") != std::string::npos &&
                   editorOperators.find("EditorPrefabOperator::CreatePrefabFromActor") != std::string::npos,
               "EditorPrefabOperator does not profile prefab apply/revert/create events"))
        return false;
    if (!Check(editorImportService.find("RecordImportEvent") != std::string::npos &&
                   editorImportService.find("RecordEvent(\"EditorImport\"") != std::string::npos &&
                   editorImportService.find("EditorImportService::Import") != std::string::npos &&
                   editorImportService.find("EditorImportService::ReimportWithSettings") != std::string::npos,
               "EditorImportService does not profile import/reimport events"))
        return false;
    if (!Check(editorImportService.find("EditorImportService::RefreshValidation") != std::string::npos &&
                   editorImportService.find("\"Validate\"") != std::string::npos &&
                   editorImportService.find("issues=\" + std::to_string(issueCount)") != std::string::npos,
               "EditorImportService does not profile asset validation events"))
        return false;
    if (!Check(editorImportService.find("EditorImportService::GetValidationSummaryText") != std::string::npos &&
                   editorImportService.find("asset validation unavailable") != std::string::npos &&
                   editorImportService.find("return report->Summary()") != std::string::npos,
               "EditorImportService does not centralize asset validation summary text"))
        return false;
    if (!Check(editorImportService.find("EditorImportService::HasValidationIssues") != std::string::npos &&
                   editorImportService.find("return report && !report->Passed()") != std::string::npos,
               "EditorImportService does not expose validation summary severity"))
        return false;
    if (!Check(editingWorkflows.find("EditorCommand") != std::string::npos &&
                   editingWorkflows.find("EditorAsset") != std::string::npos &&
                   editingWorkflows.find("EditorPrefab") != std::string::npos &&
                   editingWorkflows.find("EditorImport") != std::string::npos &&
                   editingWorkflows.find("AssetRegistry Refresh") != std::string::npos,
               "editor editing workflow docs do not document profiler categories"))
        return false;
    return Check(sceneSerializer.find("EditorProfiler") == std::string::npos &&
                     sceneSerializer.find("loadMs=") == std::string::npos,
                 "SceneSerializer should not depend on editor profiler or log load timing");
}

bool TestEditorLayoutConfigAndStatePersistence() {
    namespace fs = std::filesystem;
    EditorLayoutConfig config = EditorLayoutConfig::CreateDefault();
    std::string error;
    if (!Check(config.Validate(&error), "default editor layout invalid: " + error))
        return false;

    std::unordered_set<std::string> ids;
    for (const auto& panel : config.panels)
        ids.insert(panel.panelID);
    for (const char* required :
         {"toolbar", "sceneHierarchy", "viewport", "gameViewport", "inspector", "assetBrowser", "log", "profiler"}) {
        if (!Check(ids.count(required) == 1, std::string("default layout missing panel: ") + required))
            return false;
    }

    const auto root =
        fs::temp_directory_path() /
        ("myengine_editor_layout_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const fs::path layoutPath = root / "Config" / "EditorLayout.default.json";
    if (!Check(EditorLayoutConfig::SaveToFile(layoutPath, config, &error), "default layout save failed: " + error))
        return false;
    EditorLayoutConfig loaded;
    if (!Check(EditorLayoutConfig::LoadFromFile(layoutPath, loaded, &error), "default layout load failed: " + error))
        return false;
    if (!Check(loaded.panels.size() == config.panels.size(), "default layout round trip changed panel count"))
        return false;

    const fs::path invalidPath = root / "Config" / "InvalidLayout.json";
    std::ofstream(invalidPath) << R"({"version":1,"panels":[{"id":"toolbar","area":"top"}]})";
    EditorLayoutConfig invalid;
    if (!Check(!EditorLayoutConfig::LoadFromFile(invalidPath, invalid, &error) &&
                   error.find("missing panel id") != std::string::npos,
               "invalid layout was accepted"))
        return false;

    nlohmann::json legacyState = {{"lastScenePath", "Content/Scenes/Legacy.scene.json"},
                                  {"selectedAssetPath", "Content/Models/Legacy.gltf"},
                                  {"lastOpenDirectory", "Content"},
                                  {"panels",
                                   {{"toolbar", true},
                                    {"sceneHierarchy", false},
                                    {"viewport", true},
                                    {"inspector", true},
                                    {"assetBrowser", false},
                                    {"log", true},
                                    {"profiler", true}}}};
    std::ofstream(root / ".myengine_editor_state.json") << legacyState.dump(2);

    EditorProject project;
    if (!Check(project.Open(root), "layout state project open failed"))
        return false;
    if (!Check(
            !project.GetState().IsPanelVisible("sceneHierarchy") && project.GetState().IsPanelVisible("gameViewport") &&
                !project.GetState().IsPanelVisible("assetBrowser") && project.GetState().IsPanelVisible("profiler") &&
                project.GetLastScenePath() == "Content/Scenes/Legacy.scene.json" &&
                project.GetState().selectedAssetPath == "Content/Models/Legacy.gltf",
            "legacy panel visibility migration failed"))
        return false;

    project.GetState().imguiLayoutIni = "[Window][Scene View###viewport]\nPos=10,10\n";
    project.GetState().SetPanelVisible("log", false);
    if (!Check(project.SaveState(), "layout state save failed"))
        return false;

    EditorProject reloaded;
    if (!Check(reloaded.Open(root), "layout state reload failed"))
        return false;
    const bool stateMatches =
        reloaded.GetLastScenePath() == "Content/Scenes/Legacy.scene.json" &&
        reloaded.GetState().selectedAssetPath == "Content/Models/Legacy.gltf" &&
        !reloaded.GetState().IsPanelVisible("sceneHierarchy") && reloaded.GetState().IsPanelVisible("gameViewport") &&
        !reloaded.GetState().IsPanelVisible("assetBrowser") && !reloaded.GetState().IsPanelVisible("log") &&
        reloaded.GetState().IsPanelVisible("profiler") &&
        reloaded.GetState().imguiLayoutIni.find("Scene View###viewport") != std::string::npos;
    std::error_code ec;
    fs::remove_all(root, ec);
    return Check(stateMatches, "layout state persistence mismatch");
}

bool TestEditorDockSpaceAdoptsPersistedRootAndResizesTree() {
    namespace fs = std::filesystem;
    constexpr ImGuiID persistedRootID = 0xA0B0C0D0;
    constexpr ImGuiID leftNodeID = 0xA0B0C0D1;
    constexpr ImGuiID rightNodeID = 0xA0B0C0D2;

    const auto root =
        fs::temp_directory_path() /
        ("myengine_editor_dock_resize_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);

    EditorProjectState state;
    state.imguiLayoutIni = "[Window][###dockResizeLeft]\n"
                           "Pos=0,20\n"
                           "Size=300,560\n"
                           "Collapsed=0\n"
                           "DockId=0xA0B0C0D1,0\n\n"
                           "[Window][###dockResizeRight]\n"
                           "Pos=302,20\n"
                           "Size=498,560\n"
                           "Collapsed=0\n"
                           "DockId=0xA0B0C0D2,0\n\n"
                           "[Docking][Data]\n"
                           "DockSpace ID=0xA0B0C0D0 Window=0x01020304 Pos=0,20 Size=800,560 Split=X\n"
                           "  DockNode ID=0xA0B0C0D1 Parent=0xA0B0C0D0 SizeRef=300,560\n"
                           "  DockNode ID=0xA0B0C0D2 Parent=0xA0B0C0D0 SizeRef=498,560 CentralNode=1\n\n";

    std::vector<std::unique_ptr<EditorPanel>> panels;
    panels.push_back(std::make_unique<DockResizeTestPanel>("dockResizeLeft", "Left"));
    panels.push_back(std::make_unique<DockResizeTestPanel>("dockResizeRight", "Right"));

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    EditorLayoutManager manager;
    manager.OpenProject(root, state, panels);
    const auto drawFrame = [&](float width, float height) {
        io.DisplaySize = {width, height};
        ImGui::NewFrame();
        manager.BeginDockSpace(panels, 20.0f, 20.0f);
        for (const auto& panel : panels)
            panel->OnImGui();
        ImGui::Render();
    };

    bool passed = true;
    drawFrame(800.0f, 600.0f);
    ImGuiDockNode* persistedRoot = ImGui::DockBuilderGetNode(persistedRootID);
    passed &= Check(persistedRoot && persistedRoot->ChildNodes[0] && persistedRoot->ChildNodes[1],
                    "persisted dock root was not adopted by the current host");

    drawFrame(1280.0f, 900.0f);
    persistedRoot = ImGui::DockBuilderGetNode(persistedRootID);
    passed &= Check(persistedRoot && NearlyEqual(persistedRoot->Size.x, 1280.0f) &&
                        NearlyEqual(persistedRoot->Size.y, 860.0f),
                    "persisted dock root did not follow the resized main viewport");
    if (persistedRoot && persistedRoot->ChildNodes[0] && persistedRoot->ChildNodes[1]) {
        ImGuiDockNode* left = ImGui::DockBuilderGetNode(leftNodeID);
        ImGuiDockNode* right = ImGui::DockBuilderGetNode(rightNodeID);
        passed &= Check(left && right && NearlyEqual(left->Size.y, persistedRoot->Size.y) &&
                            NearlyEqual(right->Size.y, persistedRoot->Size.y) &&
                            NearlyEqual(right->Pos.x + right->Size.x, persistedRoot->Pos.x + persistedRoot->Size.x),
                        "persisted dock child tree remained at its pre-resize bounds");
    }

    manager.CloseProject();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previousContext);
    std::error_code ec;
    fs::remove_all(root, ec);
    return passed;
}

bool TestEditorDockSpaceRejectsOrphanedEmptyRoot() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() /
        ("myengine_editor_dock_orphan_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);

    EditorProjectState state;
    state.imguiLayoutIni = "[Window][###toolbar]\n"
                           "Pos=0,0\n"
                           "Size=800,40\n"
                           "Collapsed=0\n"
                           "\n"
                           "[Docking][Data]\n"
                           "DockSpace ID=0xBAD0F00D Window=0x01020304 Pos=0,0 Size=800,600 CentralNode=1\n\n";

    std::vector<std::unique_ptr<EditorPanel>> panels;
    panels.push_back(std::make_unique<DockResizeTestPanel>("toolbar", "Toolbar", "top"));

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = {1024.0f, 768.0f};
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    EditorLayoutManager manager;
    manager.OpenProject(root, state, panels);
    bool passed = Check(manager.GetLastWarning().find("orphaned editor dock layout") != std::string::npos,
                        "orphaned empty dock root was not rejected");

    ImGui::NewFrame();
    manager.BeginDockSpace(panels);
    for (const auto& panel : panels)
        panel->OnImGui();
    ImGui::Render();

    const ImGuiID stableRootID = ImHashStr("EditorDockSpace", 0, 0);
    ImGuiDockNode* stableRoot = ImGui::DockBuilderGetNode(stableRootID);
    passed &= Check(stableRoot && (stableRoot->ChildNodes[0] || stableRoot->ChildNodes[1]),
                    "default dock tree was not rebuilt after rejecting orphaned state");
    passed &= Check(ImGui::DockBuilderGetNode(0xBAD0F00D) == nullptr,
                    "orphaned empty dock root remained active after fallback");

    manager.CloseProject();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previousContext);
    std::error_code ec;
    fs::remove_all(root, ec);
    return passed;
}

bool TestEditorProjectAndAssetRegistry() {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("myengine_editor_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto content = root / "Content";
    std::filesystem::create_directories(content / "Models");
    std::filesystem::create_directories(content / "Textures");
    std::filesystem::create_directories(content / "Materials");
    std::filesystem::create_directories(content / "Audio");
    std::filesystem::create_directories(content / "Empty");
    std::filesystem::create_directories(root / "SourceAssets" / "Raw");
    std::ofstream(content / "Models" / "test.gltf") << "{}";
    std::filesystem::create_directories(content / "Models" / "Nested");
    std::ofstream(content / "Models" / "Nested" / "child.gltf") << "{}";
    std::ofstream(content / "Textures" / "test.png") << "png";
    std::ofstream(content / "Materials" / "test.mat") << "{}";
    std::ofstream(content / "Audio" / "test.wav") << "wav";
    std::ofstream(content / "Models" / "test.gltf.meta") << "{}";
    std::ofstream(root / "SourceAssets" / "Raw" / "raw.dat") << "raw";

    EditorAssetRegistry registry;
    EditorProfiler registryProfiler;
    registry.SetProfiler(&registryProfiler);
    registry.SetRoot(content);
    registry.Refresh();
    if (!Check(registry.GetAssets(EditorAssetType::Model).size() == 2, "asset registry model classification failed"))
        return false;
    if (!Check(registry.GetAssets(EditorAssetType::Texture).size() == 1,
               "asset registry texture classification failed"))
        return false;
    if (!Check(registry.GetAssets(EditorAssetType::Material).size() == 1,
               "asset registry material classification failed"))
        return false;
    if (!Check(registry.GetAssets(EditorAssetType::Audio).size() == 1, "asset registry audio classification failed"))
        return false;
    if (!Check(registry.GetAssets().size() == 6, "asset registry exposed metadata files"))
        return false;
    const auto folders = registry.GetFolders();
    const auto findFolder = [&](const std::string& path) {
        return std::find_if(folders.begin(), folders.end(),
                            [&path](const EditorAssetFolderInfo& info) { return info.relativePath == path; });
    };
    if (!Check(findFolder("Content") != folders.end() && findFolder("Content/Models") != folders.end() &&
                   findFolder("Content/Models/Nested") != folders.end() &&
                   findFolder("Content/Empty") != folders.end() && findFolder("SourceAssets") != folders.end() &&
                   findFolder("SourceAssets/Raw") != folders.end(),
               "asset registry folder index missed Content or SourceAssets folders"))
        return false;
    if (!Check(findFolder("Content/Empty")->assetCount == 0 && findFolder("Content/Models")->assetCount == 2 &&
                   findFolder("Content/Models")->directAssetCount == 1 &&
                   findFolder("Content/Models/Nested")->assetCount == 1 &&
                   findFolder("Content/Models/Nested")->directAssetCount == 1 &&
                   findFolder("Content")->assetCount == 5 && findFolder("SourceAssets")->assetCount == 1,
               "asset registry folder counts are incorrect"))
        return false;
    const auto modelFolderAssets = registry.GetAssetsInFolder("Content/Models", true);
    const bool modelFolderContainsRootAsset =
        std::any_of(modelFolderAssets.begin(), modelFolderAssets.end(),
                    [](const EditorAssetInfo& info) { return info.relativePath == "Models/test.gltf"; });
    if (!Check(modelFolderAssets.size() == 2 && modelFolderContainsRootAsset,
               "asset registry folder query returned the wrong Content assets"))
        return false;
    if (!Check(registry.GetAssetsInFolder("SourceAssets", true).size() == 1,
               "asset registry folder query returned the wrong SourceAssets assets"))
        return false;
    registryProfiler.Clear();
    if (!Check(!registry.WatchForChanges(),
               "asset registry reported a change when the directory snapshot was unchanged"))
        return false;
    if (!Check(registryProfiler.Snapshot().empty(), "asset registry refreshed during an unchanged watch pass"))
        return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(content / "Models" / "test.gltf", std::ios::app) << " ";
    if (!Check(registry.WatchForChanges(), "asset registry missed mtime change"))
        return false;

    const auto importSource = root / "source.obj";
    std::ofstream(importSource) << "v 0 0 0\n"
                                << "v 1 0 0\n"
                                << "v 0 1 0\n"
                                << "f 1 2 3\n";
    Scene importScene("ImportContext");
    EditorContext importContext(&importScene);
    EditorProfiler importProfiler;
    importContext.SetProjectRoot(root);
    importContext.SetAssetRegistry(&registry);
    importContext.SetProfiler(&importProfiler);
    EditorImportService importService;
    importService.OnAttach(importContext);
    if (!Check(importService.Import(importSource.string()), "editor import service failed first copy"))
        return false;
    if (!Check(importService.Import(importSource.string()), "editor import service failed unique copy"))
        return false;
    importService.OnDetach();
    if (!Check(std::filesystem::exists(root / "SourceAssets" / "source.obj") &&
                   std::filesystem::exists(root / "SourceAssets" / "source_1.obj"),
               "editor import service overwrote existing asset"))
        return false;
    const auto importEvents = importProfiler.Snapshot();
    const size_t importSuccessCount = static_cast<size_t>(
        std::count_if(importEvents.begin(), importEvents.end(), [](const EditorProfilerEvent& event) {
            return event.category == "EditorImport" && event.name == "Import" &&
                   event.details.find(";success=true") != std::string::npos &&
                   event.details.find("artifact=") != std::string::npos &&
                   event.details.find("uuid=") != std::string::npos;
        }));
    if (!Check(importSuccessCount == 2, "editor import service did not record profiler events for both imports"))
        return false;

    EditorProject project;
    if (!Check(project.Open(root), "editor project open failed"))
        return false;
    project.SetLastScenePath("Content/Scenes/test.json");
    project.GetState().selectedAssetPath = "Models/test.gltf";
    project.GetState().showLog = false;
    project.GetState().SetPanelVisible("log", false);
    project.GetState().imguiLayoutIni = "[Window][Log Output###log]\nCollapsed=1\n";
    if (!Check(project.SaveState(), "editor project state save failed"))
        return false;

    EditorProject loaded;
    if (!Check(loaded.Open(root), "editor project state load failed"))
        return false;
    const bool stateMatches = loaded.GetLastScenePath() == "Content/Scenes/test.json" &&
                              loaded.GetState().selectedAssetPath == "Models/test.gltf" && !loaded.GetState().showLog &&
                              !loaded.GetState().IsPanelVisible("log") &&
                              loaded.GetState().imguiLayoutIni.find("Log Output###log") != std::string::npos;
    // Import() populated the process-wide AssetManager with artifacts owned by
    // this temporary project. Clear those entries before deleting the project;
    // otherwise later Editor tests can dereference stale cached assets.
    AssetManager::Get().Clear();
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return Check(stateMatches, "editor project state persistence mismatch");
}

bool TestEditorAssetOperatorCommandsAndWatch() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_asset_operator_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto content = root / "Content";
    std::error_code error;
    fs::create_directories(content / "Materials", error);
    const fs::path materialPath = content / "Materials" / "test.mat";
    std::ofstream(materialPath) << "{}";

    EditorAssetRegistry registry;
    registry.SetRoot(content);
    registry.Refresh();
    Scene scene("AssetOperatorContext");
    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorProfiler profiler;
    context.SetProjectRoot(root);
    context.SetAssetRegistry(&registry);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetProfiler(&profiler);

    fs::create_directories(content / "Textures", error);
    const fs::path sceneTexturePath = content / "Textures" / "SceneReferenced.tex";
    std::ofstream(sceneTexturePath) << "{}";
    Actor* imageActor = scene.CreateActor("Scene Image");
    auto* imageComponent = imageActor ? imageActor->AddComponent<UIImageComponent>() : nullptr;
    if (!Check(imageActor && imageComponent, "asset scene reference setup failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const uint64_t imageActorID = imageActor->GetID();
    imageComponent->source = "Content/Textures/SceneReferenced.tex";
    const auto sceneReferences = operators.Assets().FindSceneReferences(context, sceneTexturePath.string());
    if (!Check(sceneReferences.size() == 1 && sceneReferences.front().actorID == imageActor->GetID() &&
                   sceneReferences.front().actorName == "Scene Image" &&
                   sceneReferences.front().componentType == "UIImage" && sceneReferences.front().jsonPath == "/source",
               "asset operator did not find scene component asset reference")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Assets().FindSceneReferences(context, (content / "Textures" / "Unused.tex").string()).empty(),
               "asset operator reported stale scene references for unused asset")) {
        fs::remove_all(root, error);
        return false;
    }

    fs::create_directories(content / "Scenes", error);
    const fs::path projectScenePath = content / "Scenes" / "Referencer.scene.json";
    std::ofstream(projectScenePath)
        << nlohmann::json{{"name", "Referencer"},
                          {"actors",
                           nlohmann::json::array(
                               {{{"id", uint64_t{42}},
                                 {"name", "Project Scene Image"},
                                 {"components",
                                  nlohmann::json::array(
                                      {{{"type", "UIImage"},
                                        {"data", {{"source", "Content/Textures/SceneReferenced.tex"}}}}})}}})}}
               .dump(2);
    const auto projectSceneReferences =
        operators.Assets().FindProjectSceneReferences(context, sceneTexturePath.string());
    if (!Check(projectSceneReferences.size() == 1 &&
                   projectSceneReferences.front().scenePath == "Content/Scenes/Referencer.scene.json" &&
                   projectSceneReferences.front().actorID == 42 &&
                   projectSceneReferences.front().actorName == "Project Scene Image" &&
                   projectSceneReferences.front().componentType == "UIImage" &&
                   projectSceneReferences.front().jsonPath == "/source",
               "asset operator did not find project scene asset reference")) {
        fs::remove_all(root, error);
        return false;
    }

    const fs::path retargetTexturePath = content / "Textures" / "SceneRetargeted.tex";
    std::ofstream(retargetTexturePath) << "{}";
    if (!Check(operators.Assets().RetargetProjectSceneReferences(context, sceneTexturePath.string(),
                                                                 retargetTexturePath.string()) == 1,
               "asset operator project scene retarget failed")) {
        fs::remove_all(root, error);
        return false;
    }
    {
        std::ifstream in(projectScenePath, std::ios::binary);
        const std::string contentText{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (!Check(contentText.find("Content/Textures/SceneRetargeted.tex") != std::string::npos,
                   "asset operator project scene retarget did not update scene file")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    if (!Check(stack.Undo(context), "asset operator project scene retarget undo command failed")) {
        fs::remove_all(root, error);
        return false;
    }
    {
        std::ifstream in(projectScenePath, std::ios::binary);
        const std::string contentText{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (!Check(contentText.find("Content/Textures/SceneReferenced.tex") != std::string::npos,
                   "asset operator project scene retarget undo did not restore scene file")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    if (!Check(operators.Assets().RetargetSceneReferences(context, sceneTexturePath.string(),
                                                          retargetTexturePath.string()) == 1,
               "asset operator retarget scene references failed")) {
        fs::remove_all(root, error);
        return false;
    }
    imageActor = scene.FindByID(imageActorID);
    imageComponent = imageActor ? imageActor->GetComponent<UIImageComponent>() : nullptr;
    if (!Check(imageComponent && imageComponent->source == "Content/Textures/SceneRetargeted.tex",
               "asset operator retarget did not update component asset path")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context), "asset operator retarget scene references undo command failed")) {
        fs::remove_all(root, error);
        return false;
    }
    imageActor = scene.FindByID(imageActorID);
    imageComponent = imageActor ? imageActor->GetComponent<UIImageComponent>() : nullptr;
    if (!Check(imageComponent && imageComponent->source == "Content/Textures/SceneReferenced.tex",
               "asset operator retarget undo did not restore component asset path")) {
        fs::remove_all(root, error);
        return false;
    }

    float accumulator = 0.0f;
    if (!Check(!operators.Assets().WatchIfDue(context, 0.25f, accumulator),
               "asset operator watched before throttle interval")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(accumulator > 0.0f, "asset operator did not accumulate watch delta")) {
        fs::remove_all(root, error);
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(materialPath, std::ios::app) << " ";
    if (!Check(operators.Assets().WatchIfDue(context, 1.0f, accumulator),
               "asset operator missed throttled registry change")) {
        fs::remove_all(root, error);
        return false;
    }

    if (!Check(operators.Assets().DuplicateAsset(context, materialPath.string()), "asset operator duplicate failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path copyPath = content / "Materials" / "test_Copy.mat";
    const auto firstCopyMeta = AssetMeta::Load(copyPath.string());
    if (!Check(fs::exists(copyPath) && fs::exists(AssetMeta::MetaPathFor(copyPath.string())) &&
                   firstCopyMeta.has_value() &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       copyPath.lexically_normal(),
               "asset operator duplicate did not create copy, metadata, and selection")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(copyPath) && !fs::exists(AssetMeta::MetaPathFor(copyPath.string())) &&
                   !context.GetSelection().HasAsset(),
               "asset operator duplicate undo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::exists(copyPath) && fs::exists(AssetMeta::MetaPathFor(copyPath.string())) &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       copyPath.lexically_normal(),
               "asset operator duplicate redo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Assets().DuplicateAsset(context, materialPath.string()),
               "asset operator second duplicate failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path secondCopyPath = content / "Materials" / "test_Copy_1.mat";
    const auto secondCopyMeta = AssetMeta::Load(secondCopyPath.string());
    if (!Check(fs::exists(secondCopyPath) && fs::exists(AssetMeta::MetaPathFor(secondCopyPath.string())) &&
                   secondCopyMeta.has_value() && firstCopyMeta->uuid != secondCopyMeta->uuid &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       secondCopyPath.lexically_normal(),
               "asset operator duplicate did not create unique copy metadata")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(secondCopyPath) &&
                   !fs::exists(AssetMeta::MetaPathFor(secondCopyPath.string())) && !context.GetSelection().HasAsset(),
               "asset operator second duplicate undo failed")) {
        fs::remove_all(root, error);
        return false;
    }

    {
        const fs::path materialEditPath = content / "Materials" / "operator_material.mat";
        auto material = MaterialAsset::CreateDefaultAtPath(materialEditPath.string(), "OperatorMaterial");
        material->SetTwoSided(false);
        material->SetParam("Metallic", MaterialParam::FromFloat(0.0f));
        if (!Check(SaveMaterialAssetToFile(*material, materialEditPath.string()),
                   "material modifier setup save failed")) {
            fs::remove_all(root, error);
            return false;
        }
        AssetManager::Get().Unload(materialEditPath.string());
        auto handle = AssetManager::Get().Load<MaterialAsset>(materialEditPath.string());
        if (!Check(handle.IsValid(), "material modifier setup load failed")) {
            fs::remove_all(root, error);
            return false;
        }
        handle->SetTwoSided(true);
        handle->SetParam("Metallic", MaterialParam::FromFloat(0.75f));
        MaterialModifier modifier(materialEditPath.string(), "Modify Material Test",
                                  [source = handle.Get()](MaterialAsset& target) {
                                      target.ReloadFrom(*source);
                                      return true;
                                  });
        if (!Check(modifier.Modify(context), "material modifier command failed")) {
            fs::remove_all(root, error);
            return false;
        }
        auto readMaterial = [&]() { return LoadMaterialAssetFromFile(materialEditPath.string()); };
        auto saved = readMaterial();
        if (!Check(saved && saved->IsTwoSided() && NearlyEqual(saved->GetFloat("Metallic"), 0.75f),
                   "material modifier did not save edited material")) {
            fs::remove_all(root, error);
            return false;
        }
        if (!Check(stack.Undo(context), "material modifier undo command failed")) {
            fs::remove_all(root, error);
            return false;
        }
        saved = readMaterial();
        handle = AssetManager::Get().GetByPath<MaterialAsset>(materialEditPath.string());
        if (!Check(saved && !saved->IsTwoSided() && NearlyEqual(saved->GetFloat("Metallic"), 0.0f) &&
                       handle.IsValid() && !handle->IsTwoSided(),
                   "material modifier undo did not restore disk and loaded asset")) {
            fs::remove_all(root, error);
            return false;
        }
        if (!Check(stack.Redo(context), "material modifier redo command failed")) {
            fs::remove_all(root, error);
            return false;
        }
        saved = readMaterial();
        handle = AssetManager::Get().GetByPath<MaterialAsset>(materialEditPath.string());
        if (!Check(saved && saved->IsTwoSided() && NearlyEqual(saved->GetFloat("Metallic"), 0.75f) &&
                       handle.IsValid() && handle->IsTwoSided(),
                   "material modifier redo did not restore disk and loaded asset")) {
            fs::remove_all(root, error);
            return false;
        }
    }

    if (!Check(operators.Assets().DeleteAsset(context, copyPath.string()), "asset operator delete failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(!fs::exists(copyPath), "asset operator delete did not remove file")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::exists(copyPath), "asset operator delete undo failed")) {
        fs::remove_all(root, error);
        return false;
    }

    const fs::path toolsFolder = content / "Tools";
    if (!Check(operators.Assets().CreateFolder(context, toolsFolder.string()) && fs::is_directory(toolsFolder),
               "asset operator create folder failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(toolsFolder), "asset operator create folder undo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::is_directory(toolsFolder), "asset operator create folder redo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(!operators.Assets().MoveFolder(context, content.string(), (content / "Materials").string()) &&
                   !operators.Assets().DeleteFolder(context, content.string()),
               "asset operator allowed root folder mutation")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Assets().MoveFolder(context, toolsFolder.string(), (content / "Materials").string()),
               "asset operator move folder failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path movedToolsFolder = content / "Materials" / "Tools";
    if (!Check(fs::is_directory(movedToolsFolder), "asset operator move folder did not move directory")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Assets().MoveFolder(context, movedToolsFolder.string(), content.string()),
               "asset operator move folder back to root failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(fs::is_directory(toolsFolder) && !fs::exists(movedToolsFolder),
               "asset operator move folder back to root did not move directory")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::is_directory(movedToolsFolder) && !fs::exists(toolsFolder),
               "asset operator move folder back to root undo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::is_directory(toolsFolder) && !fs::exists(movedToolsFolder),
               "asset operator move folder back to root redo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    fs::create_directories(movedToolsFolder, error);
    if (!Check(!operators.Assets().MoveFolder(context, toolsFolder.string(), (content / "Materials").string()) &&
                   fs::is_directory(toolsFolder) && fs::is_directory(movedToolsFolder),
               "asset operator move folder overwrote existing target folder")) {
        fs::remove_all(root, error);
        return false;
    }
    fs::remove_all(movedToolsFolder, error);
    const fs::path existingToolsName = content / "ExistingTools";
    fs::create_directories(existingToolsName, error);
    if (!Check(!operators.Assets().RenameFolder(context, toolsFolder.string(), "ExistingTools") &&
                   fs::is_directory(toolsFolder) && fs::is_directory(existingToolsName),
               "asset operator rename folder overwrote existing target folder")) {
        fs::remove_all(root, error);
        return false;
    }
    fs::remove_all(existingToolsName, error);
    if (!Check(operators.Assets().RenameFolder(context, toolsFolder.string(), "RenamedTools"),
               "asset operator rename folder failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path renamedToolsFolder = content / "RenamedTools";
    if (!Check(fs::is_directory(renamedToolsFolder), "asset operator rename folder did not move directory")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::is_directory(toolsFolder), "asset operator rename folder undo failed")) {
        fs::remove_all(root, error);
        return false;
    }

    const fs::path databasePath = root / ".myengine" / "AssetDatabase.json";
    fs::create_directories(databasePath.parent_path(), error);
    auto databaseHasRecord = [&](const std::string& uuid) {
        AssetDatabase database;
        database.Open(databasePath);
        return database.FindByUuid(uuid) != nullptr;
    };
    const fs::path dbFolder = content / "DatabaseTracked";
    fs::create_directories(dbFolder, error);
    const fs::path dbFolderAsset = dbFolder / "Tracked.as";
    std::ofstream(dbFolderAsset) << "class Tracked {}";
    AssetMeta dbFolderAssetMeta = AssetMeta::Create(dbFolderAsset.string());
    if (!Check(AssetMeta::Save(dbFolderAssetMeta), "asset operator database folder fixture meta save failed")) {
        fs::remove_all(root, error);
        return false;
    }
    {
        AssetDatabase database;
        AssetRecord record;
        record.uuid = dbFolderAssetMeta.uuid;
        record.sourcePath = dbFolderAsset.string();
        record.type = "script";
        if (!Check(database.Open(databasePath) && database.Upsert(record) && database.Save(),
                   "asset operator database folder fixture save failed")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    if (!Check(operators.Assets().MoveFolder(context, dbFolder.string(), (content / "Materials").string()),
               "asset operator move folder with database record failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path movedDbFolderAsset = content / "Materials" / "DatabaseTracked" / "Tracked.as";
    {
        AssetDatabase database;
        database.Open(databasePath);
        const AssetRecord* record = database.FindByUuid(dbFolderAssetMeta.uuid);
        if (!Check(record && fs::path(record->sourcePath).lexically_normal() == movedDbFolderAsset.lexically_normal(),
                   "asset operator folder move did not update asset database source path")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    if (!Check(stack.Undo(context) && fs::exists(dbFolderAsset),
               "asset operator move folder with database record undo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    {
        AssetDatabase database;
        database.Open(databasePath);
        const AssetRecord* record = database.FindByUuid(dbFolderAssetMeta.uuid);
        if (!Check(record && fs::path(record->sourcePath).lexically_normal() == dbFolderAsset.lexically_normal(),
                   "asset operator folder move undo did not restore database source path")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    const fs::path dbDeleteAsset = content / "Materials" / "DeleteTracked.as";
    std::ofstream(dbDeleteAsset) << "class DeleteTracked {}";
    AssetMeta dbDeleteAssetMeta = AssetMeta::Create(dbDeleteAsset.string());
    if (!Check(AssetMeta::Save(dbDeleteAssetMeta), "asset operator database delete fixture meta save failed")) {
        fs::remove_all(root, error);
        return false;
    }
    {
        AssetDatabase database;
        AssetRecord record;
        record.uuid = dbDeleteAssetMeta.uuid;
        record.sourcePath = dbDeleteAsset.string();
        record.type = "script";
        if (!Check(database.Open(databasePath) && database.Upsert(record) && database.Save(),
                   "asset operator database delete fixture save failed")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    if (!Check(operators.Assets().DeleteAsset(context, dbDeleteAsset.string()) && !fs::exists(dbDeleteAsset) &&
                   !fs::exists(AssetMeta::MetaPathFor(dbDeleteAsset.string())) &&
                   !databaseHasRecord(dbDeleteAssetMeta.uuid),
               "asset operator delete asset did not remove database record")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::exists(dbDeleteAsset) &&
                   fs::exists(AssetMeta::MetaPathFor(dbDeleteAsset.string())) &&
                   databaseHasRecord(dbDeleteAssetMeta.uuid),
               "asset operator delete asset undo did not restore database record")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && !fs::exists(dbDeleteAsset) &&
                   !fs::exists(AssetMeta::MetaPathFor(dbDeleteAsset.string())) &&
                   !databaseHasRecord(dbDeleteAssetMeta.uuid),
               "asset operator delete asset redo did not remove database record")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::exists(dbDeleteAsset) && databaseHasRecord(dbDeleteAssetMeta.uuid),
               "asset operator delete asset final undo did not restore database record")) {
        fs::remove_all(root, error);
        return false;
    }

    if (!Check(operators.Assets().CreateAssetFromTemplate(context, toolsFolder.string(), "as"),
               "asset operator create script template failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path scriptPath = toolsFolder / "NewScript.as";
    if (!Check(fs::exists(scriptPath), "asset operator script template did not create file")) {
        fs::remove_all(root, error);
        return false;
    }
    const auto scriptMeta = AssetMeta::Load(scriptPath.string());
    if (!Check(scriptMeta.has_value(), "asset operator script template did not create readable meta")) {
        fs::remove_all(root, error);
        return false;
    }
    {
        AssetDatabase database;
        AssetRecord record;
        record.uuid = scriptMeta->uuid;
        record.sourcePath = scriptPath.string();
        record.type = "script";
        if (!Check(database.Open(databasePath) && database.Upsert(record) && database.Save(),
                   "asset operator database move fixture save failed")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    const fs::path conflictingScriptPath = toolsFolder / "ExistingScript.as";
    std::ofstream(conflictingScriptPath) << "class ExistingScript {}";
    if (!Check(!operators.Assets().RenameAsset(context, scriptPath.string(), conflictingScriptPath.string()) &&
                   fs::exists(scriptPath) && fs::exists(conflictingScriptPath),
               "asset operator rename asset overwrote existing file")) {
        fs::remove_all(root, error);
        return false;
    }
    fs::remove(conflictingScriptPath, error);
    fs::create_directories(content / "Conflicts", error);
    const fs::path metaOnlyConflict = content / "Conflicts" / "NewScript.as";
    AssetMeta metaOnly = AssetMeta::Create(metaOnlyConflict.string());
    if (!Check(AssetMeta::Save(metaOnly) &&
                   !operators.Assets().MoveAsset(context, scriptPath.string(), (content / "Conflicts").string()) &&
                   fs::exists(scriptPath) && !fs::exists(metaOnlyConflict) &&
                   fs::exists(AssetMeta::MetaPathFor(metaOnlyConflict.string())),
               "asset operator move asset ignored existing target metadata")) {
        fs::remove_all(root, error);
        return false;
    }
    fs::remove(AssetMeta::MetaPathFor(metaOnlyConflict.string()), error);
    if (!Check(operators.Assets().CreateAssetFromTemplate(context, toolsFolder.string(), "material"),
               "asset operator create material template failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path templatedMaterialPath = toolsFolder / "NewMaterial.mat";
    std::ifstream materialInput(templatedMaterialPath);
    std::string materialContent((std::istreambuf_iterator<char>(materialInput)), std::istreambuf_iterator<char>());
    materialInput.close();
    if (!Check(fs::exists(templatedMaterialPath) &&
                   fs::exists(AssetMeta::MetaPathFor(templatedMaterialPath.string())) &&
                   materialContent.find("\"type\": \"Material\"") != std::string::npos,
               "asset operator material template did not create material file and meta")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Assets().CreateAssetFromTemplate(context, toolsFolder.string(), "texture"),
               "asset operator create texture template failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path templatedTexturePath = toolsFolder / "NewTexture.tex";
    if (!Check(fs::exists(templatedTexturePath) && fs::exists(AssetMeta::MetaPathFor(templatedTexturePath.string())),
               "asset operator texture template did not create file and meta")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(templatedTexturePath) &&
                   !fs::exists(AssetMeta::MetaPathFor(templatedTexturePath.string())),
               "asset operator texture template undo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::exists(templatedTexturePath) &&
                   fs::exists(AssetMeta::MetaPathFor(templatedTexturePath.string())),
               "asset operator texture template redo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Assets().DeleteFolder(context, toolsFolder.string()) && !fs::exists(toolsFolder) &&
                   !fs::exists(scriptPath) && !fs::exists(AssetMeta::MetaPathFor(scriptPath.string())) &&
                   !fs::exists(templatedTexturePath) &&
                   !fs::exists(AssetMeta::MetaPathFor(templatedTexturePath.string())),
               "asset operator delete folder did not remove non-empty folder contents")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(!databaseHasRecord(scriptMeta->uuid),
               "asset operator delete folder did not remove nested asset database record")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::is_directory(toolsFolder) && fs::exists(scriptPath) &&
                   fs::exists(AssetMeta::MetaPathFor(scriptPath.string())) && fs::exists(templatedTexturePath) &&
                   fs::exists(AssetMeta::MetaPathFor(templatedTexturePath.string())),
               "asset operator delete folder undo did not restore contents and metadata")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(databaseHasRecord(scriptMeta->uuid),
               "asset operator delete folder undo did not restore nested asset database record")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && !fs::exists(toolsFolder) && !fs::exists(scriptPath) &&
                   !fs::exists(AssetMeta::MetaPathFor(scriptPath.string())),
               "asset operator delete folder redo did not delete restored contents")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(!databaseHasRecord(scriptMeta->uuid),
               "asset operator delete folder redo did not remove nested asset database record")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::is_directory(toolsFolder) && fs::exists(scriptPath) &&
                   fs::exists(AssetMeta::MetaPathFor(scriptPath.string())),
               "asset operator delete folder final undo did not restore tools folder")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(databaseHasRecord(scriptMeta->uuid),
               "asset operator delete folder final undo did not restore nested asset database record")) {
        fs::remove_all(root, error);
        return false;
    }

    auto registryContains = [&](const std::string& folder, const fs::path& path) {
        registry.Refresh();
        const auto assets = registry.GetAssetsInFolder(folder, true);
        const fs::path normalizedPath = path.lexically_normal();
        return std::any_of(assets.begin(), assets.end(), [&](const EditorAssetInfo& info) {
            return info.absolutePath.lexically_normal() == normalizedPath;
        });
    };

    if (!Check(operators.Assets().MoveAsset(context, scriptPath.string(), (content / "Materials").string()),
               "asset operator move asset failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path movedScript = content / "Materials" / "NewScript.as";
    if (!Check(fs::exists(movedScript) && fs::exists(AssetMeta::MetaPathFor(movedScript.string())),
               "asset operator move did not move file and meta")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       movedScript.lexically_normal() &&
                   registryContains("Content/Materials", movedScript),
               "asset operator move did not select moved asset and refresh registry")) {
        fs::remove_all(root, error);
        return false;
    }
    {
        AssetDatabase database;
        database.Open(databasePath);
        const AssetRecord* record = database.FindByUuid(scriptMeta->uuid);
        if (!Check(record && fs::path(record->sourcePath).lexically_normal() == movedScript.lexically_normal(),
                   "asset operator move did not update asset database source path")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    if (!Check(stack.Undo(context) && fs::exists(scriptPath) &&
                   fs::exists(AssetMeta::MetaPathFor(scriptPath.string())) &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       scriptPath.lexically_normal() &&
                   registryContains("Content/Tools", scriptPath),
               "asset operator move undo did not restore file, meta, selection, and registry")) {
        fs::remove_all(root, error);
        return false;
    }
    {
        AssetDatabase database;
        database.Open(databasePath);
        const AssetRecord* record = database.FindByUuid(scriptMeta->uuid);
        if (!Check(record && fs::path(record->sourcePath).lexically_normal() == scriptPath.lexically_normal(),
                   "asset operator move undo did not restore asset database source path")) {
            fs::remove_all(root, error);
            return false;
        }
    }
    if (!Check(stack.Redo(context) && fs::exists(movedScript) &&
                   fs::exists(AssetMeta::MetaPathFor(movedScript.string())) &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       movedScript.lexically_normal() &&
                   registryContains("Content/Materials", movedScript),
               "asset operator move redo did not restore file, meta, selection, and registry")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::exists(scriptPath) &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       scriptPath.lexically_normal(),
               "asset operator move final undo did not restore script for later tests")) {
        fs::remove_all(root, error);
        return false;
    }

    context.GetSelection().SelectAssetPath(scriptPath.string());
    if (!Check(operators.Commands().CopySelection(context), "asset operator command copy asset selection failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Commands().HasAssetClipboard() && !operators.Commands().HasActorClipboard(),
               "asset operator command copy did not set asset clipboard kind")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Commands().PasteAssetToFolder(context, (content / "Materials").string()),
               "asset operator command paste asset to folder failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path pastedScript = content / "Materials" / "NewScript.as";
    auto pastedMeta = AssetMeta::Load(pastedScript.string());
    if (!Check(fs::exists(pastedScript) && fs::exists(AssetMeta::MetaPathFor(pastedScript.string())) &&
                   pastedMeta.has_value() &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       pastedScript.lexically_normal() &&
                   registryContains("Content/Materials", pastedScript),
               "asset operator paste did not create file, meta, selection, and registry")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(pastedScript) &&
                   !fs::exists(AssetMeta::MetaPathFor(pastedScript.string())),
               "asset operator paste undo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::exists(pastedScript) &&
                   fs::exists(AssetMeta::MetaPathFor(pastedScript.string())) &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       pastedScript.lexically_normal(),
               "asset operator paste redo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    pastedMeta = AssetMeta::Load(pastedScript.string());
    if (!Check(operators.Commands().PasteAssetToFolder(context, (content / "Materials").string()),
               "asset operator command second paste asset to folder failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path pastedScriptCopy = content / "Materials" / "NewScript_Copy.as";
    const auto pastedCopyMeta = AssetMeta::Load(pastedScriptCopy.string());
    if (!Check(fs::exists(pastedScriptCopy) && fs::exists(AssetMeta::MetaPathFor(pastedScriptCopy.string())) &&
                   pastedMeta.has_value() && pastedCopyMeta.has_value() && pastedMeta->uuid != pastedCopyMeta->uuid &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       pastedScriptCopy.lexically_normal() &&
                   registryContains("Content/Materials", pastedScriptCopy),
               "asset operator second paste did not create unique file, meta, selection, and registry")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(pastedScriptCopy) &&
                   !fs::exists(AssetMeta::MetaPathFor(pastedScriptCopy.string())) && !context.GetSelection().HasAsset(),
               "asset operator second paste undo failed")) {
        fs::remove_all(root, error);
        return false;
    }

    const fs::path extraScript = content / "Tools" / "ExtraScript.as";
    std::ofstream(extraScript) << "class ExtraScript {}";
    const fs::path batchTarget = content / "BatchPaste";
    fs::create_directories(batchTarget, error);
    if (!Check(operators.Commands().CopyAssets(context, {scriptPath.string(), extraScript.string()}),
               "asset operator command copy multiple assets failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(operators.Commands().HasAssetClipboard() &&
                   operators.Commands().PasteAssetToFolder(context, batchTarget.string()),
               "asset operator command paste multiple assets failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path batchScript = batchTarget / "NewScript.as";
    const fs::path batchExtra = batchTarget / "ExtraScript.as";
    if (!Check(fs::exists(batchScript) && fs::exists(AssetMeta::MetaPathFor(batchScript.string())) &&
                   fs::exists(batchExtra) && fs::exists(AssetMeta::MetaPathFor(batchExtra.string())) &&
                   std::filesystem::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       batchExtra.lexically_normal(),
               "asset operator multi-paste did not copy every asset and select last")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && stack.Undo(context) && !fs::exists(batchScript) && !fs::exists(batchExtra),
               "asset operator multi-paste undo failed")) {
        fs::remove_all(root, error);
        return false;
    }

    const fs::path textureSource = root / "tile.bmp";
    {
        std::ofstream output(textureSource, std::ios::binary);
        const unsigned char bmp[] = {0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00,
                                     0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
                                     0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
                                     0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
        output.write(reinterpret_cast<const char*>(bmp), sizeof(bmp));
    }
    EditorImportService importService;
    importService.OnAttach(context);
    context.RegisterService(importService);
    if (!Check(importService.Import(textureSource.string()), "asset operator import settings setup import failed")) {
        importService.OnDetach();
        context.ClearServices();
        fs::remove_all(root, error);
        return false;
    }
    registry.Refresh();
    const auto importedTextures = registry.GetAssets(EditorAssetType::Texture);
    auto imported = std::find_if(importedTextures.begin(), importedTextures.end(),
                                 [](const EditorAssetInfo& info) { return info.imported && !info.uuid.empty(); });
    if (!Check(imported != importedTextures.end(),
               "asset operator import settings setup did not find imported texture")) {
        importService.OnDetach();
        context.ClearServices();
        fs::remove_all(root, error);
        return false;
    }
    const std::string textureUuid = imported->uuid;
    const nlohmann::json nearestSettings = {
        {"textureSampler", {{"filter", "nearest"}, {"wrapU", "clamp"}, {"wrapV", "clamp"}}}};
    if (!Check(operators.Assets().ReimportWithSettings(context, textureUuid, nearestSettings.dump()),
               "asset operator import settings command failed")) {
        importService.OnDetach();
        context.ClearServices();
        fs::remove_all(root, error);
        return false;
    }
    auto readSettings = [&]() {
        AssetDatabase database;
        database.Open(root / ".myengine" / "AssetDatabase.json");
        const AssetRecord* record = database.FindByUuid(textureUuid);
        return record ? record->settingsJson : std::string{};
    };
    if (!Check(readSettings().find("\"nearest\"") != std::string::npos,
               "asset operator import settings command did not persist settings")) {
        importService.OnDetach();
        context.ClearServices();
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && readSettings() == "{}",
               "asset operator import settings undo did not restore previous settings")) {
        importService.OnDetach();
        context.ClearServices();
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && readSettings().find("\"nearest\"") != std::string::npos,
               "asset operator import settings redo did not restore edited settings")) {
        importService.OnDetach();
        context.ClearServices();
        fs::remove_all(root, error);
        return false;
    }

    const auto assetEvents = profiler.Snapshot();
    auto hasAssetEvent = [&](const char* operation, const char* detailNeedle = nullptr) {
        return std::any_of(assetEvents.begin(), assetEvents.end(), [&](const EditorProfilerEvent& event) {
            return event.category == "EditorAsset" && event.name == operation &&
                   event.details.find(";success=true") != std::string::npos &&
                   (!detailNeedle || event.details.find(detailNeedle) != std::string::npos);
        });
    };
    if (!Check(hasAssetEvent("Duplicate Asset", "target=") && hasAssetEvent("Delete Asset") &&
                   hasAssetEvent("Create Folder") && hasAssetEvent("Move Folder", "targetFolder=") &&
                   hasAssetEvent("Create Asset From Template", "template=material") &&
                   hasAssetEvent("Copy Asset", "target=") && hasAssetEvent("Move Asset", "targetFolder=") &&
                   hasAssetEvent("Update Import Settings"),
               "asset operator did not record expected profiler events")) {
        importService.OnDetach();
        context.ClearServices();
        fs::remove_all(root, error);
        return false;
    }
    importService.OnDetach();
    context.ClearServices();

    fs::remove_all(root, error);
    return true;
}

bool TestEditorAssetOperatorOpenSceneAsset() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_open_scene_asset_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto sceneFolder = root / "Content" / "Scenes";
    const auto prefabFolder = root / "Content" / "Prefabs";
    std::error_code error;
    fs::create_directories(sceneFolder, error);
    fs::create_directories(prefabFolder, error);

    const fs::path scenePath = sceneFolder / "OpenMe.scene.json";
    const fs::path prefabPath = prefabFolder / "OpenMe.prefab.json";
    std::ofstream(prefabPath) << "{}";
    Scene savedScene("OpenedFromAsset");
    savedScene.CreateActor("LoadedActor");
    if (!Check(SceneSerializer::SaveToFile(savedScene, scenePath.string()),
               "open scene asset setup failed to save scene")) {
        fs::remove_all(root, error);
        return false;
    }

    SceneRenderLayer layer(nullptr, 320, 180);
    EditorContext context(&layer, nullptr, nullptr, nullptr);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorProject project;
    if (!Check(project.Open(root), "open scene asset project setup failed")) {
        fs::remove_all(root, error);
        return false;
    }
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetProject(&project);
    context.SetProjectRoot(root);
    std::vector<std::string> focusRequests;
    context.SetPanelFocusRequestHandler([&](std::string_view panelID) { focusRequests.emplace_back(panelID); });

    const uint64_t scratchID = operators.Commands().CreateActor(context, "Scratch");
    context.GetSelection().SelectActorID(scratchID);
    if (!Check(stack.CanUndo() && context.GetSelection().HasActor(),
               "open scene asset setup did not create undoable selected actor")) {
        fs::remove_all(root, error);
        return false;
    }

    if (!Check(layer.IsDirty() && !operators.Assets().OpenAsset(context, scenePath.string()) &&
                   layer.GetEditorScene().FindByName("Scratch") != nullptr && context.GetSelection().HasActor() &&
                   stack.CanUndo() && project.GetLastScenePath() != scenePath.string(),
               "asset operator opened scene asset despite unsaved changes")) {
        fs::remove_all(root, error);
        return false;
    }

    const fs::path scratchScenePath = sceneFolder / "Scratch.scene.json";
    if (!Check(layer.SaveSceneAs(scratchScenePath.string()) && !layer.IsDirty(),
               "open scene asset setup failed to save dirty scene before open")) {
        fs::remove_all(root, error);
        return false;
    }

    if (!Check(operators.Assets().OpenAsset(context, scenePath.string()), "asset operator did not open scene asset")) {
        fs::remove_all(root, error);
        return false;
    }

    const bool loadedScene = layer.GetEditorScene().GetName() == "OpenedFromAsset" &&
                             layer.GetEditorScene().FindByName("LoadedActor") != nullptr;
    const bool clearedEditorState = !context.GetSelection().HasActor() && !context.GetSelection().HasAsset() &&
                                    !stack.CanUndo() && !stack.CanRedo() &&
                                    context.GetSceneViewMode() == EditorWorldViewMode::EditorWorld;
    const bool projectUpdated = project.GetLastScenePath() == scenePath.string();
    if (!Check(operators.Assets().OpenAsset(context, prefabPath.string()) && context.GetSelection().HasAsset() &&
                   fs::path(context.GetSelection().GetAssetPath()).lexically_normal() ==
                       prefabPath.lexically_normal() &&
                   !focusRequests.empty() && focusRequests.back() == "inspector",
               "opening inspectable prefab asset did not select it and focus inspector")) {
        fs::remove_all(root, error);
        return false;
    }

    fs::remove_all(root, error);
    return Check(loadedScene && clearedEditorState && projectUpdated,
                 "opening scene asset did not load scene and reset editor state");
}

bool TestEditorPrefabOverrideOperatorSingleApplyRevert() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_prefab_override_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content" / "Prefabs", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    Scene source("PrefabSource");
    Actor* sourceRoot = source.CreateActor("Vehicle");
    Actor* sourceChild = source.CreateActor("Wheel", sourceRoot);
    const fs::path prefabPath = root / "Content" / "Prefabs" / "Vehicle.prefab.json";
    std::string error;
    if (!Check(PrefabSystem::SaveSubtree(*sourceRoot, prefabPath, &error), "prefab save failed: " + error)) {
        cleanup();
        return false;
    }

    Scene scene("PrefabEditor");
    Actor* instance = PrefabSystem::Instantiate(scene, prefabPath, {}, &error);
    if (!Check(instance && !instance->GetChildren().empty(), "prefab instantiate failed: " + error)) {
        cleanup();
        return false;
    }
    const uint64_t rootID = instance->GetID();
    instance->GetChildren().front()->SetName("EditedWheel");

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    EditorProfiler profiler;
    registry.SetRoot(root / "Content");
    registry.Refresh();
    context.SetProjectRoot(root);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.SetProfiler(&profiler);
    context.GetSelection().SelectActorID(rootID);

    auto overrides = operators.Prefabs().GetOverrides(context, rootID);
    if (!Check(overrides.size() == 1 && overrides.front().kind == "SetProperty",
               "prefab override list did not expose edited child name")) {
        cleanup();
        return false;
    }
    if (!Check(overrides.front().category == "Actor" && overrides.front().target.find("Actor ") == 0 &&
                   overrides.front().property == "name" && overrides.front().valuePreview == "EditedWheel" &&
                   overrides.front().diagnostic.empty(),
               "prefab override row did not expose readable actor property metadata")) {
        cleanup();
        return false;
    }
    if (!Check(operators.Prefabs().RevertOverride(context, rootID, overrides.front().index),
               "prefab single revert failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && !instance->GetChildren().empty() && instance->GetChildren().front()->GetName() == "Wheel",
               "prefab single revert did not restore source value")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab single revert undo failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && !instance->GetChildren().empty() &&
                   instance->GetChildren().front()->GetName() == "EditedWheel",
               "prefab single revert undo did not restore edited override")) {
        cleanup();
        return false;
    }

    overrides = operators.Prefabs().GetOverrides(context, rootID);
    if (!Check(!overrides.empty() && operators.Prefabs().ApplyOverride(context, rootID, overrides.front().index),
               "prefab single apply failed")) {
        cleanup();
        return false;
    }
    PrefabAsset appliedAsset;
    if (!Check(PrefabAsset::Load(prefabPath, appliedAsset, &error) &&
                   std::any_of(appliedAsset.nodes.begin(), appliedAsset.nodes.end(),
                               [](const PrefabNode& node) { return node.name == "EditedWheel"; }),
               "prefab single apply did not write source asset")) {
        cleanup();
        return false;
    }
    if (!Check(operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab single apply left applied override on instance")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab single apply undo failed")) {
        cleanup();
        return false;
    }
    PrefabAsset restoredAsset;
    if (!Check(PrefabAsset::Load(prefabPath, restoredAsset, &error) &&
                   std::any_of(restoredAsset.nodes.begin(), restoredAsset.nodes.end(),
                               [](const PrefabNode& node) { return node.name == "Wheel"; }),
               "prefab single apply undo did not restore source asset")) {
        cleanup();
        return false;
    }

    const auto prefabEvents = profiler.Snapshot();
    auto hasPrefabEvent = [&](const char* operation, const char* detailNeedle = nullptr) {
        return std::any_of(prefabEvents.begin(), prefabEvents.end(), [&](const EditorProfilerEvent& event) {
            return event.category == "EditorPrefab" && event.name == operation &&
                   event.details.find(";success=true") != std::string::npos &&
                   (!detailNeedle || event.details.find(detailNeedle) != std::string::npos);
        });
    };
    if (!Check(hasPrefabEvent("Revert Prefab Override", "overrideIndex=") &&
                   hasPrefabEvent("Apply Prefab Override", "overrideIndex="),
               "prefab single override operations did not record profiler events")) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

bool TestEditorPrefabOverrideDisplayModelSorting() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_prefab_override_display_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content" / "Prefabs", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    Scene source("PrefabDisplaySource");
    Actor* sourceRoot = source.CreateActor("Vehicle");
    Actor* sourceChild = source.CreateActor("Wheel", sourceRoot);
    const fs::path prefabPath = root / "Content" / "Prefabs" / "Vehicle.prefab.json";
    std::string error;
    if (!Check(sourceRoot && sourceChild && PrefabSystem::SaveSubtree(*sourceRoot, prefabPath, &error),
               "prefab display fixture save failed: " + error)) {
        cleanup();
        return false;
    }

    Scene scene("PrefabDisplayEditor");
    Actor* instance = PrefabSystem::Instantiate(scene, prefabPath, {}, &error);
    if (!Check(instance && !instance->GetChildren().empty(), "prefab display instantiate failed: " + error)) {
        cleanup();
        return false;
    }
    Actor* child = instance->GetChildren().front();
    child->SetName("EditedWheel");
    child->AddComponent<BoxColliderComponent>();

    EditorContext context(&scene);
    EditorOperators operators;
    context.SetProjectRoot(root);
    context.SetOperators(&operators);

    const auto overrides = operators.Prefabs().GetOverrides(context, instance->GetID());
    if (!Check(overrides.size() >= 2, "prefab display model did not expose multiple override categories")) {
        cleanup();
        return false;
    }
    if (!Check(overrides.front().category == "Actor" && std::any_of(overrides.begin(), overrides.end(),
                                                                    [](const EditorPrefabOperator::OverrideInfo& item) {
                                                                        return item.category == "Component" &&
                                                                               item.property == "Added component" &&
                                                                               item.valuePreview.find("halfExtents") !=
                                                                                   std::string::npos;
                                                                    }),
               "prefab display model did not expose readable sorted override rows")) {
        cleanup();
        return false;
    }
    if (!Check(std::is_sorted(
                   overrides.begin(), overrides.end(),
                   [](const EditorPrefabOperator::OverrideInfo& left, const EditorPrefabOperator::OverrideInfo& right) {
                       const auto rank = [](const std::string& category) {
                           if (category == "Actor")
                               return 0;
                           if (category == "Component")
                               return 1;
                           if (category == "Hierarchy")
                               return 2;
                           return 3;
                       };
                       if (rank(left.category) != rank(right.category)) {
                           return rank(left.category) < rank(right.category);
                       }
                       if (left.target != right.target)
                           return left.target < right.target;
                       if (left.property != right.property)
                           return left.property < right.property;
                       return left.index < right.index;
                   }),
               "prefab display model rows were not sorted for inspector scanning")) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

bool TestEditorPrefabOverrideUnsupportedPersistedKindIsBlocked() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_prefab_override_unsupported_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content" / "Prefabs", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    Scene source("PrefabUnsupportedSource");
    Actor* sourceRoot = source.CreateActor("Vehicle");
    const fs::path prefabPath = root / "Content" / "Prefabs" / "Vehicle.prefab.json";
    std::string error;
    if (!Check(sourceRoot && PrefabSystem::SaveSubtree(*sourceRoot, prefabPath, &error),
               "prefab unsupported fixture save failed: " + error)) {
        cleanup();
        return false;
    }

    Scene scene("PrefabUnsupportedEditor");
    Actor* instance = PrefabSystem::Instantiate(scene, prefabPath, {}, &error);
    if (!Check(instance, "prefab unsupported instantiate failed: " + error)) {
        cleanup();
        return false;
    }
    const uint64_t instanceID = instance->GetID();

    nlohmann::json unsupported = nlohmann::json::array({{{"kind", "FutureOverrideKind"},
                                                         {"localId", instance->GetPrefabLocalId()},
                                                         {"path", "/future"},
                                                         {"value", 42}}});
    if (!Check(PrefabSystem::SetInstanceOverrides(*instance, unsupported, &error),
               "prefab unsupported override setup failed: " + error)) {
        cleanup();
        return false;
    }

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetProjectRoot(root);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    const auto overrides = operators.Prefabs().GetOverrides(context, instanceID);
    if (!Check(overrides.size() == 1 && overrides.front().kind == "FutureOverrideKind" &&
                   overrides.front().category == "Unsupported" && !overrides.front().canApply &&
                   !overrides.front().canRevert &&
                   overrides.front().diagnostic.find("FutureOverrideKind") != std::string::npos,
               "unsupported persisted prefab override was not surfaced as blocked")) {
        cleanup();
        return false;
    }
    if (!Check(!operators.Prefabs().ApplyOverride(context, instanceID, overrides.front().index) &&
                   !operators.Prefabs().RevertOverride(context, instanceID, overrides.front().index) &&
                   !stack.CanUndo(),
               "unsupported prefab override should not apply, revert, or enter undo")) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

bool TestEditorNestedPrefabApplyRevertUndoRedo() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_nested_prefab_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content" / "Prefabs", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    std::string error;
    const fs::path partPath = root / "Content" / "Prefabs" / "Part.prefab.json";
    Scene partSource("NestedPartSource");
    Actor* partRoot = partSource.CreateActor("Part");
    Actor* partChild = partSource.CreateActor("Socket", partRoot);
    if (!Check(partRoot && partChild && PrefabSystem::SaveSubtree(*partRoot, partPath, &error),
               "nested editor part save failed: " + error)) {
        cleanup();
        return false;
    }

    const fs::path outerPath = root / "Content" / "Prefabs" / "Outer.prefab.json";
    Scene outerSource("NestedOuterSource");
    Actor* outerRoot = outerSource.CreateActor("Outer");
    Actor* nestedPart = PrefabSystem::Instantiate(outerSource, partPath, {}, &error);
    if (nestedPart)
        nestedPart->SetParent(outerRoot);
    if (!Check(outerRoot && nestedPart && PrefabSystem::SaveSubtree(*outerRoot, outerPath, &error),
               "nested editor outer save failed: " + error)) {
        cleanup();
        return false;
    }

    Scene scene("NestedPrefabEditor");
    Actor* outer = PrefabSystem::Instantiate(scene, outerPath, {}, &error);
    if (!Check(outer && outer->GetChildren().size() == 1 && outer->GetChildren().front()->IsPrefabRoot(),
               "nested editor instance did not retain nested prefab root: " + error)) {
        cleanup();
        return false;
    }
    auto resolveNested = [&]() -> Actor* {
        Actor* currentOuter = scene.FindByName("Outer");
        return currentOuter && currentOuter->GetChildren().size() == 1 ? currentOuter->GetChildren().front() : nullptr;
    };
    auto nestedChildName = [&]() {
        Actor* current = resolveNested();
        return current && current->GetChildren().size() == 1 ? current->GetChildren().front()->GetName()
                                                             : std::string{};
    };
    Actor* nested = resolveNested();
    const uint64_t nestedID = nested->GetID();
    nested->GetChildren().front()->SetName("LocalSocket");

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    registry.SetRoot(root / "Content");
    registry.Refresh();
    context.SetProjectRoot(root);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.GetSelection().SelectActorID(nestedID);

    if (!Check(operators.Prefabs().RevertAll(context, nestedID), "nested prefab revert command failed") ||
        !Check(nestedChildName() == "Socket", "nested prefab revert did not restore source state") ||
        !Check(stack.Undo(context) && nestedChildName() == "LocalSocket",
               "nested prefab revert undo did not restore local state") ||
        !Check(stack.Redo(context) && nestedChildName() == "Socket",
               "nested prefab revert redo did not restore source state") ||
        !Check(stack.Undo(context) && nestedChildName() == "LocalSocket",
               "nested prefab second revert undo did not restore local state")) {
        cleanup();
        return false;
    }

    nested = resolveNested();
    if (!Check(nested && operators.Prefabs().ApplyAll(context, nested->GetID()),
               "nested prefab apply command failed")) {
        cleanup();
        return false;
    }
    PrefabAsset applied;
    const auto childName = [](const PrefabAsset& asset) {
        auto child = std::find_if(asset.nodes.begin(), asset.nodes.end(),
                                  [&](const PrefabNode& node) { return node.localId != asset.rootLocalId; });
        return child == asset.nodes.end() ? std::string{} : child->name;
    };
    if (!Check(PrefabAsset::Load(partPath, applied, &error) && childName(applied) == "LocalSocket" &&
                   stack.Undo(context),
               "nested prefab apply or undo failed: " + error)) {
        cleanup();
        return false;
    }
    PrefabAsset restored;
    if (!Check(PrefabAsset::Load(partPath, restored, &error) && childName(restored) == "Socket" && resolveNested() &&
                   resolveNested()->GetChildren().front()->GetName() == "LocalSocket" && stack.Redo(context),
               "nested prefab apply undo did not restore source and local override")) {
        cleanup();
        return false;
    }
    PrefabAsset redone;
    const bool ok = PrefabAsset::Load(partPath, redone, &error) && childName(redone) == "LocalSocket" &&
                    resolveNested() && resolveNested()->GetChildren().front()->GetName() == "LocalSocket";
    cleanup();
    return Check(ok, "nested prefab apply redo did not restore applied source: " + error);
}

bool TestEditorPrefabOperatorCreateAndInstantiateCommands() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_prefab_create_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    Scene scene("PrefabCreateOperator");
    Actor* actor = scene.CreateActor("Pickup");
    Actor* child = scene.CreateActor("Visual", actor);
    actor->GetTransform().position = {2.0f, 3.0f, 4.0f};
    child->GetTransform().scale = {0.5f, 0.5f, 0.5f};
    const uint64_t actorID = actor->GetID();

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    EditorProfiler profiler;
    registry.SetRoot(root / "Content");
    context.SetProjectRoot(root);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.SetProfiler(&profiler);
    context.GetSelection().SelectActorID(actorID);

    if (!Check(operators.Prefabs().CreatePrefabFromActor(context, actorID), "prefab create operator command failed")) {
        cleanup();
        return false;
    }
    actor = scene.FindByID(actorID);
    if (!Check(actor && actor->IsPrefabRoot() && !actor->GetChildren().empty(),
               "prefab create did not replace actor with prefab instance")) {
        cleanup();
        return false;
    }
    fs::path prefabPath;
    for (const auto& entry : fs::directory_iterator(root / "Content" / "Prefabs")) {
        if (entry.path().extension() == ".json" &&
            entry.path().filename().string().find(".prefab") != std::string::npos) {
            prefabPath = entry.path();
            break;
        }
    }
    if (!Check(!prefabPath.empty() && fs::exists(prefabPath), "prefab create command did not write prefab asset")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab create undo failed")) {
        cleanup();
        return false;
    }
    actor = scene.FindByID(actorID);
    if (!Check(actor && !actor->IsPrefabInstance() && !fs::exists(prefabPath),
               "prefab create undo did not restore scene and remove asset")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Redo(context), "prefab create redo failed")) {
        cleanup();
        return false;
    }
    actor = scene.FindByID(actorID);
    if (!Check(actor && actor->IsPrefabRoot() && fs::exists(prefabPath),
               "prefab create redo did not restore prefab instance and asset")) {
        cleanup();
        return false;
    }

    const size_t rootCountBeforeInstantiate = scene.GetRootActors().size();
    Transform placement;
    placement.position = {9.0f, 0.0f, 1.0f};
    const uint64_t instantiatedID =
        operators.Prefabs().InstantiatePrefab(context, prefabPath.string(), 0, placement, "Instantiate Prefab Test");
    Actor* instantiated = scene.FindByID(instantiatedID);
    if (!Check(instantiated && instantiated->IsPrefabRoot() &&
                   scene.GetRootActors().size() == rootCountBeforeInstantiate + 1 &&
                   NearlyEqual(instantiated->GetTransform().position.x, 9.0f),
               "prefab instantiate operator did not create placed instance")) {
        cleanup();
        return false;
    }
    if (!Check(operators.Prefabs().SelectInstances(context, prefabPath.string()) == 2 &&
                   context.GetSelection().GetMultiCount() == 2 && context.GetSelection().IsSelected(actorID) &&
                   context.GetSelection().IsSelected(instantiatedID),
               "prefab select instances did not select all scene prefab roots")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context) && scene.GetRootActors().size() == rootCountBeforeInstantiate,
               "prefab instantiate undo did not remove instance")) {
        cleanup();
        return false;
    }

    const auto prefabEvents = profiler.Snapshot();
    auto hasPrefabEvent = [&](const char* operation, const char* detailNeedle = nullptr) {
        return std::any_of(prefabEvents.begin(), prefabEvents.end(), [&](const EditorProfilerEvent& event) {
            return event.category == "EditorPrefab" && event.name == operation &&
                   event.details.find(";success=true") != std::string::npos &&
                   (!detailNeedle || event.details.find(detailNeedle) != std::string::npos);
        });
    };
    if (!Check(hasPrefabEvent("Create Prefab", "prefab=") && hasPrefabEvent("Instantiate Prefab", "prefab="),
               "prefab create/instantiate operations did not record profiler events")) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

bool TestEditorPrefabOperatorRemovedActorOverrideApplyRevert() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_prefab_remove_actor_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content" / "Prefabs", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    Scene source("PrefabRemovedActorSource");
    Actor* sourceRoot = source.CreateActor("Crate");
    Actor* sourceChild = source.CreateActor("Lid", sourceRoot);
    const fs::path prefabPath = root / "Content" / "Prefabs" / "Crate.prefab.json";
    std::string error;
    if (!Check(sourceRoot && sourceChild && PrefabSystem::SaveSubtree(*sourceRoot, prefabPath, &error),
               "prefab removed actor source save failed: " + error)) {
        cleanup();
        return false;
    }

    Scene scene("PrefabRemovedActorEditor");
    Actor* instance = PrefabSystem::Instantiate(scene, prefabPath, {}, &error);
    if (!Check(instance && !instance->GetChildren().empty(), "prefab removed actor instantiate failed: " + error)) {
        cleanup();
        return false;
    }
    const uint64_t rootID = instance->GetID();
    Actor* child = instance->GetChildren().front();
    const std::string removedLocalID = child->GetPrefabLocalId();
    scene.QueueDestroyActor(child->GetHandle());
    if (!Check(scene.FlushCommands() && instance->GetChildren().empty(),
               "prefab removed actor setup did not delete child")) {
        cleanup();
        return false;
    }

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    registry.SetRoot(root / "Content");
    registry.Refresh();
    context.SetProjectRoot(root);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.GetSelection().SelectActorID(rootID);

    auto overrides = operators.Prefabs().GetOverrides(context, rootID);
    auto removed =
        std::find_if(overrides.begin(), overrides.end(), [&](const EditorPrefabOperator::OverrideInfo& item) {
            return item.kind == "RemoveActorSubtree" && item.localId == removedLocalID && item.canApply &&
                   item.canRevert;
        });
    if (!Check(removed != overrides.end(), "prefab removed actor override was not exposed as apply/revert row")) {
        cleanup();
        return false;
    }

    if (!Check(operators.Prefabs().RevertOverride(context, rootID, removed->index),
               "prefab removed actor single revert failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && !instance->GetChildren().empty() &&
                   instance->GetChildren().front()->GetPrefabLocalId() == removedLocalID,
               "prefab removed actor revert did not restore deleted child")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab removed actor revert undo failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && instance->GetChildren().empty(),
               "prefab removed actor revert undo did not restore deletion override")) {
        cleanup();
        return false;
    }

    overrides = operators.Prefabs().GetOverrides(context, rootID);
    removed = std::find_if(overrides.begin(), overrides.end(), [&](const EditorPrefabOperator::OverrideInfo& item) {
        return item.kind == "RemoveActorSubtree" && item.localId == removedLocalID;
    });
    if (!Check(removed != overrides.end() && operators.Prefabs().ApplyOverride(context, rootID, removed->index),
               "prefab removed actor single apply failed")) {
        cleanup();
        return false;
    }
    PrefabAsset appliedAsset;
    if (!Check(PrefabAsset::Load(prefabPath, appliedAsset, &error) &&
                   std::none_of(appliedAsset.nodes.begin(), appliedAsset.nodes.end(),
                                [&](const PrefabNode& node) { return node.localId == removedLocalID; }),
               "prefab removed actor apply did not remove child from source asset")) {
        cleanup();
        return false;
    }
    if (!Check(operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab removed actor apply left stale deletion override")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab removed actor apply undo failed")) {
        cleanup();
        return false;
    }
    PrefabAsset restoredAsset;
    if (!Check(PrefabAsset::Load(prefabPath, restoredAsset, &error) &&
                   std::any_of(restoredAsset.nodes.begin(), restoredAsset.nodes.end(),
                               [&](const PrefabNode& node) { return node.localId == removedLocalID; }) &&
                   !operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab removed actor apply undo did not restore asset and deletion override")) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

bool TestEditorPrefabOperatorAddedActorOverrideApplyRevert() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_prefab_add_actor_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content" / "Prefabs", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    Scene source("PrefabAddedActorSource");
    Actor* sourceRoot = source.CreateActor("Robot");
    const fs::path prefabPath = root / "Content" / "Prefabs" / "Robot.prefab.json";
    std::string error;
    if (!Check(sourceRoot && PrefabSystem::SaveSubtree(*sourceRoot, prefabPath, &error),
               "prefab added actor source save failed: " + error)) {
        cleanup();
        return false;
    }

    Scene scene("PrefabAddedActorEditor");
    Actor* instance = PrefabSystem::Instantiate(scene, prefabPath, {}, &error);
    if (!Check(instance, "prefab added actor instantiate failed: " + error)) {
        cleanup();
        return false;
    }
    const uint64_t rootID = instance->GetID();
    Actor* added = scene.CreateActor("Antenna", instance);
    Actor* tip = scene.CreateActor("Tip", added);
    if (!Check(added && tip && instance->GetChildren().size() == 1 && added->GetChildren().size() == 1,
               "prefab added actor setup did not create subtree")) {
        cleanup();
        return false;
    }

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    registry.SetRoot(root / "Content");
    registry.Refresh();
    context.SetProjectRoot(root);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.GetSelection().SelectActorID(rootID);

    auto overrides = operators.Prefabs().GetOverrides(context, rootID);
    auto addedOverride =
        std::find_if(overrides.begin(), overrides.end(), [](const EditorPrefabOperator::OverrideInfo& item) {
            return item.kind == "AddActorSubtree" && item.canApply && item.canRevert;
        });
    if (!Check(addedOverride != overrides.end(), "prefab added actor override was not exposed as apply/revert row")) {
        cleanup();
        return false;
    }

    if (!Check(operators.Prefabs().RevertOverride(context, rootID, addedOverride->index),
               "prefab added actor single revert failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && instance->GetChildren().empty(), "prefab added actor revert did not remove added subtree")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab added actor revert undo failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && instance->GetChildren().size() == 1 &&
                   instance->GetChildren().front()->GetName() == "Antenna" &&
                   instance->GetChildren().front()->GetChildren().size() == 1,
               "prefab added actor revert undo did not restore added subtree")) {
        cleanup();
        return false;
    }

    overrides = operators.Prefabs().GetOverrides(context, rootID);
    addedOverride =
        std::find_if(overrides.begin(), overrides.end(),
                     [](const EditorPrefabOperator::OverrideInfo& item) { return item.kind == "AddActorSubtree"; });
    if (!Check(addedOverride != overrides.end() &&
                   operators.Prefabs().ApplyOverride(context, rootID, addedOverride->index),
               "prefab added actor single apply failed")) {
        cleanup();
        return false;
    }
    PrefabAsset appliedAsset;
    if (!Check(PrefabAsset::Load(prefabPath, appliedAsset, &error) &&
                   std::any_of(appliedAsset.nodes.begin(), appliedAsset.nodes.end(),
                               [](const PrefabNode& node) { return node.name == "Antenna"; }) &&
                   std::any_of(appliedAsset.nodes.begin(), appliedAsset.nodes.end(),
                               [](const PrefabNode& node) { return node.name == "Tip"; }),
               "prefab added actor apply did not write subtree to source asset")) {
        cleanup();
        return false;
    }
    if (!Check(operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab added actor apply left stale subtree override")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab added actor apply undo failed")) {
        cleanup();
        return false;
    }
    PrefabAsset restoredAsset;
    if (!Check(PrefabAsset::Load(prefabPath, restoredAsset, &error) &&
                   std::none_of(restoredAsset.nodes.begin(), restoredAsset.nodes.end(),
                                [](const PrefabNode& node) { return node.name == "Antenna" || node.name == "Tip"; }) &&
                   !operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab added actor apply undo did not restore asset and override")) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

bool TestEditorPrefabOperatorAddedComponentOverrideApplyRevert() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_prefab_add_component_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content" / "Prefabs", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    Scene source("PrefabAddedComponentSource");
    Actor* sourceRoot = source.CreateActor("Pickup");
    const fs::path prefabPath = root / "Content" / "Prefabs" / "Pickup.prefab.json";
    std::string error;
    if (!Check(sourceRoot && PrefabSystem::SaveSubtree(*sourceRoot, prefabPath, &error),
               "prefab added component source save failed: " + error)) {
        cleanup();
        return false;
    }

    Scene scene("PrefabAddedComponentEditor");
    Actor* instance = PrefabSystem::Instantiate(scene, prefabPath, {}, &error);
    if (!Check(instance, "prefab added component instantiate failed: " + error)) {
        cleanup();
        return false;
    }
    const uint64_t rootID = instance->GetID();
    BoxColliderComponent* collider = instance->AddComponent<BoxColliderComponent>();
    if (!Check(collider != nullptr, "prefab added component setup could not add collider")) {
        cleanup();
        return false;
    }
    collider->SetHalfExtents({2.0f, 3.0f, 4.0f});

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    registry.SetRoot(root / "Content");
    registry.Refresh();
    context.SetProjectRoot(root);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.GetSelection().SelectActorID(rootID);

    auto overrides = operators.Prefabs().GetOverrides(context, rootID);
    auto added = std::find_if(overrides.begin(), overrides.end(), [](const EditorPrefabOperator::OverrideInfo& item) {
        return item.kind == "AddComponent" && item.componentType == "BoxCollider" && item.canApply && item.canRevert;
    });
    if (!Check(added != overrides.end(), "prefab added component override was not exposed as apply/revert row")) {
        cleanup();
        return false;
    }

    if (!Check(operators.Prefabs().RevertOverride(context, rootID, added->index),
               "prefab added component single revert failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && instance->GetComponent<BoxColliderComponent>() == nullptr,
               "prefab added component revert did not remove instance component")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab added component revert undo failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && instance->GetComponent<BoxColliderComponent>() != nullptr,
               "prefab added component revert undo did not restore component override")) {
        cleanup();
        return false;
    }

    overrides = operators.Prefabs().GetOverrides(context, rootID);
    added = std::find_if(overrides.begin(), overrides.end(), [](const EditorPrefabOperator::OverrideInfo& item) {
        return item.kind == "AddComponent" && item.componentType == "BoxCollider";
    });
    if (!Check(added != overrides.end() && operators.Prefabs().ApplyOverride(context, rootID, added->index),
               "prefab added component single apply failed")) {
        cleanup();
        return false;
    }
    PrefabAsset appliedAsset;
    if (!Check(PrefabAsset::Load(prefabPath, appliedAsset, &error) &&
                   std::any_of(appliedAsset.nodes.begin(), appliedAsset.nodes.end(),
                               [](const PrefabNode& node) {
                                   return std::any_of(node.components.begin(), node.components.end(),
                                                      [](const ComponentCreateDesc& component) {
                                                          return component.type == "BoxCollider";
                                                      });
                               }),
               "prefab added component apply did not write component to source asset")) {
        cleanup();
        return false;
    }
    if (!Check(operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab added component apply left stale component override")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab added component apply undo failed")) {
        cleanup();
        return false;
    }
    PrefabAsset restoredAsset;
    if (!Check(PrefabAsset::Load(prefabPath, restoredAsset, &error) &&
                   std::none_of(restoredAsset.nodes.begin(), restoredAsset.nodes.end(),
                                [](const PrefabNode& node) {
                                    return std::any_of(node.components.begin(), node.components.end(),
                                                       [](const ComponentCreateDesc& component) {
                                                           return component.type == "BoxCollider";
                                                       });
                                }) &&
                   !operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab added component apply undo did not restore asset and override")) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

bool TestEditorPrefabOperatorRemovedComponentOverrideApplyRevert() {
    namespace fs = std::filesystem;
    const auto root =
        fs::temp_directory_path() / ("myengine_editor_prefab_remove_component_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code errorCode;
    fs::create_directories(root / "Content" / "Prefabs", errorCode);
    const fs::path previousProjectRoot = AssetManager::Get().GetProjectRoot();
    auto cleanup = [&]() {
        AssetManager::Get().SetProjectRoot(previousProjectRoot);
        fs::remove_all(root, errorCode);
    };
    AssetManager::Get().SetProjectRoot(root);

    Scene source("PrefabRemovedComponentSource");
    Actor* sourceRoot = source.CreateActor("Trigger");
    BoxColliderComponent* sourceCollider = sourceRoot ? sourceRoot->AddComponent<BoxColliderComponent>() : nullptr;
    if (sourceCollider)
        sourceCollider->SetHalfExtents({5.0f, 6.0f, 7.0f});
    const fs::path prefabPath = root / "Content" / "Prefabs" / "Trigger.prefab.json";
    std::string error;
    if (!Check(sourceRoot && sourceCollider && PrefabSystem::SaveSubtree(*sourceRoot, prefabPath, &error),
               "prefab removed component source save failed: " + error)) {
        cleanup();
        return false;
    }

    Scene scene("PrefabRemovedComponentEditor");
    Actor* instance = PrefabSystem::Instantiate(scene, prefabPath, {}, &error);
    if (!Check(instance && instance->GetComponent<BoxColliderComponent>() != nullptr,
               "prefab removed component instantiate failed: " + error)) {
        cleanup();
        return false;
    }
    const uint64_t rootID = instance->GetID();
    if (!Check(instance->RemoveComponent<BoxColliderComponent>() &&
                   instance->GetComponent<BoxColliderComponent>() == nullptr,
               "prefab removed component setup did not remove collider")) {
        cleanup();
        return false;
    }

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    registry.SetRoot(root / "Content");
    registry.Refresh();
    context.SetProjectRoot(root);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.GetSelection().SelectActorID(rootID);

    auto overrides = operators.Prefabs().GetOverrides(context, rootID);
    auto removed = std::find_if(overrides.begin(), overrides.end(), [](const EditorPrefabOperator::OverrideInfo& item) {
        return item.kind == "RemoveComponent" && item.componentType == "BoxCollider" && item.canApply && item.canRevert;
    });
    if (!Check(removed != overrides.end(), "prefab removed component override was not exposed as apply/revert row")) {
        cleanup();
        return false;
    }

    if (!Check(operators.Prefabs().RevertOverride(context, rootID, removed->index),
               "prefab removed component single revert failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && instance->GetComponent<BoxColliderComponent>() != nullptr,
               "prefab removed component revert did not restore source component")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab removed component revert undo failed")) {
        cleanup();
        return false;
    }
    instance = scene.FindByID(rootID);
    if (!Check(instance && instance->GetComponent<BoxColliderComponent>() == nullptr,
               "prefab removed component revert undo did not restore removal override")) {
        cleanup();
        return false;
    }

    overrides = operators.Prefabs().GetOverrides(context, rootID);
    removed = std::find_if(overrides.begin(), overrides.end(), [](const EditorPrefabOperator::OverrideInfo& item) {
        return item.kind == "RemoveComponent" && item.componentType == "BoxCollider";
    });
    if (!Check(removed != overrides.end() && operators.Prefabs().ApplyOverride(context, rootID, removed->index),
               "prefab removed component single apply failed")) {
        cleanup();
        return false;
    }
    PrefabAsset appliedAsset;
    if (!Check(PrefabAsset::Load(prefabPath, appliedAsset, &error) &&
                   std::none_of(appliedAsset.nodes.begin(), appliedAsset.nodes.end(),
                                [](const PrefabNode& node) {
                                    return std::any_of(node.components.begin(), node.components.end(),
                                                       [](const ComponentCreateDesc& component) {
                                                           return component.type == "BoxCollider";
                                                       });
                                }),
               "prefab removed component apply did not remove component from source asset")) {
        cleanup();
        return false;
    }
    if (!Check(operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab removed component apply left stale component override")) {
        cleanup();
        return false;
    }
    if (!Check(stack.Undo(context), "prefab removed component apply undo failed")) {
        cleanup();
        return false;
    }
    PrefabAsset restoredAsset;
    if (!Check(PrefabAsset::Load(prefabPath, restoredAsset, &error) &&
                   std::any_of(restoredAsset.nodes.begin(), restoredAsset.nodes.end(),
                               [](const PrefabNode& node) {
                                   return std::any_of(node.components.begin(), node.components.end(),
                                                      [](const ComponentCreateDesc& component) {
                                                          return component.type == "BoxCollider";
                                                      });
                               }) &&
                   !operators.Prefabs().GetOverrides(context, rootID).empty(),
               "prefab removed component apply undo did not restore asset and override")) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

bool TestEditorPerformanceSourceContracts() {
    const auto readSource = [](const char* relativePath) {
        const char* prefixes[] = {"", "../../../", "../../../../", "../../../../../"};
        for (const char* prefix : prefixes) {
            std::ifstream file(std::string(prefix) + relativePath, std::ios::binary);
            if (!file)
                continue;
            std::ostringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }
        return std::string{};
    };

    const std::string panelHeader = readSource("src/Editor/EditorPanel.h");
    const std::string editorLayer = readSource("src/Editor/EditorLayer.cpp");
    const std::string assetRegistry = readSource("src/Editor/EditorAssetRegistry.cpp");
    const std::string assetBrowser = readSource("src/Editor/Panels/AssetBrowserPanel.cpp");
    const std::string sceneLayerHeader = readSource("src/Runtime/Game/SceneRenderLayer.h");
    const std::string sceneLayer = readSource("src/Runtime/Game/SceneRenderLayer.cpp");
    const std::string viewportPanel = readSource("src/Editor/Panels/ViewportPanel.cpp");
    const std::string shaderGraphPanel = readSource("src/Editor/Panels/ShaderGraphPanel.cpp");
    const std::string imguiBackend = readSource("src/Editor/EditorImGuiBackend.cpp");
    const std::string hierarchyPanel = readSource("src/Editor/Panels/SceneHierarchyPanel.cpp");
    const std::string shaderWatcher = readSource("src/Editor/EditorShaderWatchService.cpp");
    const std::string d3d12Header = readSource("src/Runtime/Renderer/D3D12Context.h");

    if (!Check(!panelHeader.empty() && !editorLayer.empty() && !assetRegistry.empty() && !assetBrowser.empty() &&
                   !sceneLayerHeader.empty() && !sceneLayer.empty() && !viewportPanel.empty() &&
                   !shaderGraphPanel.empty() && !imguiBackend.empty() && !hierarchyPanel.empty() &&
                   !shaderWatcher.empty() && !d3d12Header.empty(),
               "performance source contract files were not found"))
        return false;

    if (!Check(panelHeader.find("ShouldUpdateWhenHidden") != std::string::npos &&
                   editorLayer.find("panel->IsVisible() || panel->ShouldUpdateWhenHidden()") != std::string::npos,
               "hidden panels are not gated during update"))
        return false;
    const size_t editorFrameBegin = editorLayer.find("m_RenderContext->BeginFrame");
    const size_t imguiFrameBegin = editorLayer.find("m_ImGuiBackend->BeginFrame");
    const size_t editorFrameEnd = editorLayer.find("m_RenderContext->EndFrame");
    if (!Check(editorFrameBegin != std::string::npos && imguiFrameBegin != std::string::npos &&
                   editorFrameEnd != std::string::npos && editorFrameBegin < imguiFrameBegin &&
                   imguiFrameBegin < editorFrameEnd,
               "ImGui swapchain frame still depends on a viewport renderer"))
        return false;
    if (!Check(assetRegistry.find("BuildDirectorySnapshot") != std::string::npos &&
                   assetRegistry.find("std::vector<EditorAssetInfo> before") == std::string::npos &&
                   assetRegistry.find("AccumulateFolderCounts") != std::string::npos,
               "asset registry watch still uses full-list copies or old folder counting"))
        return false;
    if (!Check(assetBrowser.find("operators->Assets().WatchIfDue") != std::string::npos,
               "asset browser watch is not routed through the throttled asset operator"))
        return false;
    if (!Check(sceneLayerHeader.find("SetSceneViewportActive") != std::string::npos &&
                   sceneLayerHeader.find("SetGameViewportActive") != std::string::npos &&
                   sceneLayerHeader.find("BeginViewportActivityFrame") != std::string::npos &&
                   sceneLayerHeader.find("CommitViewportActivityFrame") != std::string::npos &&
                   sceneLayer.find("if (m_SceneViewportActive)") != std::string::npos &&
                   sceneLayer.find("if (m_GameViewportActive)") != std::string::npos &&
                   editorLayer.find("BeginViewportActivityFrame()") != std::string::npos &&
                   editorLayer.find("CommitViewportActivityFrame()") != std::string::npos &&
                   viewportPanel.find("SetSceneViewportActive(true)") != std::string::npos &&
                   viewportPanel.find("SetGameViewportActive(true)") != std::string::npos,
               "editor viewport rendering is not controlled by active panel state"))
        return false;
    if (!Check(sceneLayerHeader.find("m_SceneViewportActive = false") != std::string::npos &&
                   sceneLayerHeader.find("m_GameViewportActive = false") != std::string::npos &&
                   sceneLayer.find("m_MaterialPreviewDirty || m_MaterialPreviewRealtime") != std::string::npos &&
                   shaderGraphPanel.find("SetMaterialPreviewRealtime(m_PreviewRealtime)") != std::string::npos &&
                   shaderGraphPanel.find("IsVisible() && !m_Path.empty()") == std::string::npos,
               "preview visibility or dirty-driven scheduling contract regressed"))
        return false;
    if (!Check(d3d12Header.find("kDsvDescriptorCount = 128") != std::string::npos,
               "D3D12 DSV safety capacity regressed"))
        return false;
    if (!Check(imguiBackend.find("SDL_WINDOW_OCCLUDED") != std::string::npos &&
                   imguiBackend.find("RenderRenderablePlatformWindows") != std::string::npos,
               "occluded ImGui platform viewports can still throttle the editor render loop"))
        return false;
    if (!Check(hierarchyPanel.find("m_SearchMatches") != std::string::npos &&
                   hierarchyPanel.find("RebuildSearchCache") != std::string::npos &&
                   hierarchyPanel.find("ActorMatchesFilter") == std::string::npos,
               "scene hierarchy search still recursively rematches per drawn actor"))
        return false;
    return Check(shaderWatcher.find("m_Paths.empty()") != std::string::npos,
                 "shader watcher does not skip empty project watch sets");
}

bool TestProductionAssetDatabaseAndImportPipeline() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "myengine_production_asset_pipeline";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const auto source = root / "external.png";
    std::ofstream(source, std::ios::binary) << "stable texture source";

    AssetImportService imports;
    std::string error;
    if (!Check(imports.OpenProject(root / "Project", &error), "asset import project open failed: " + error))
        return false;
    const AssetImportReport first = imports.Import(source, "{\"srgb\":true}", &error);
    if (!Check(first.succeeded && !first.cacheHit && !first.record.uuid.empty(),
               "initial asset import failed: " + error))
        return false;
    if (!Check(fs::is_regular_file(first.record.sourcePath + ".meta") && fs::is_regular_file(first.record.artifactPath),
               "import did not create metadata and artifact"))
        return false;

    const AssetImportReport cached = imports.Reimport(first.record.uuid, &error);
    if (!Check(cached.succeeded && cached.cacheHit && cached.record.uuid == first.record.uuid,
               "reimport did not preserve uuid or hit DDC"))
        return false;

    AssetRecord material;
    material.uuid = "material-uuid";
    material.sourcePath = (root / "Project/Content/Test.mat").generic_string();
    material.type = "material";
    material.dependencies = {first.record.uuid};
    fs::create_directories(root / "Project/Content");
    std::ofstream(material.sourcePath) << "{}";
    if (!Check(imports.GetDatabase().Upsert(material, &error) && imports.GetDatabase().Save(&error),
               "asset database dependency update failed: " + error))
        return false;
    if (!Check(imports.GetDatabase().GetReferencers(first.record.uuid).size() == 1,
               "asset database reverse dependency lookup failed"))
        return false;

    const std::array importFaults = {
        AssetImportFault::AfterArtifactValidation,
        AssetImportFault::AfterArtifactPromote,
        AssetImportFault::BeforeDatabaseSave,
    };
    for (size_t index = 0; index < importFaults.size(); ++index) {
        imports.SetInjectedFaultForTesting(importFaults[index]);
        error.clear();
        const AssetImportReport failed = imports.ReimportWithSettings(
            first.record.uuid, "{\"srgb\":false,\"fault\":" + std::to_string(index) + "}", &error);
        const AssetRecord* retained = imports.GetDatabase().FindByUuid(first.record.uuid);
        if (!Check(!failed.succeeded && retained && retained->artifactPath == first.record.artifactPath &&
                       retained->artifactHash == first.record.artifactHash &&
                       fs::is_regular_file(first.record.artifactPath),
                   "import transaction fault did not retain ready record/artifact: " + error))
            return false;
    }
    imports.SetInjectedFaultForTesting(AssetImportFault::None);
    for (const auto& entry : fs::recursive_directory_iterator(root / "Project/Library")) {
        const std::string filename = entry.path().filename().string();
        if (!Check(filename.find(".import-staging") == std::string::npos &&
                       filename.find(".import-backup") == std::string::npos,
                   "import transaction left staging or backup debris"))
            return false;
    }

    AssetDatabase reopened;
    if (!Check(reopened.Open(root / "Project/.myengine/AssetDatabase.json", &error),
               "asset database reload failed: " + error))
        return false;
    const AssetRecord* restored = reopened.FindByUuid(first.record.uuid);
    const bool valid = restored && restored->artifactHash == first.record.artifactHash &&
                       reopened.GetReferencers(first.record.uuid).size() == 1;
    if (!Check(valid, "asset database round trip lost identity or dependencies"))
        return false;

    AssetDatabaseValidationReport validation;
    if (!Check(reopened.ValidateAgainstProject(root / "Project", validation),
               "valid asset database failed project validation: " + validation.Summary()))
        return false;

    std::ofstream(first.record.artifactPath, std::ios::binary | std::ios::trunc) << "tampered";
    reopened.ValidateAgainstProject(root / "Project", validation);
    const bool detectedHashMismatch =
        std::any_of(validation.issues.begin(), validation.issues.end(), [](const AssetDatabaseValidationIssue& issue) {
            return issue.code == AssetDatabaseValidationIssueCode::ArtifactHashMismatch;
        });
    if (!Check(detectedHashMismatch, "asset database validation missed an artifact hash mismatch"))
        return false;

    fs::remove(first.record.sourcePath);
    const AssetImportReport missing = imports.Reimport(first.record.uuid, &error);
    if (!Check(!missing.succeeded && missing.record.state == AssetImportState::MissingSource &&
                   missing.record.artifactPath == first.record.artifactPath,
               "failed reimport did not retain previous artifact record"))
        return false;

    AssetRecord cycleA;
    cycleA.uuid = "cycle-a";
    cycleA.sourcePath = (root / "Project/Content/CycleA.asset").generic_string();
    cycleA.dependencies = {"cycle-b"};
    std::ofstream(cycleA.sourcePath) << "a";
    AssetRecord cycleB;
    cycleB.uuid = "cycle-b";
    cycleB.sourcePath = (root / "Project/Content/CycleB.asset").generic_string();
    cycleB.dependencies = {"cycle-a"};
    std::ofstream(cycleB.sourcePath) << "b";
    if (!Check(imports.GetDatabase().Upsert(cycleA, &error) && imports.GetDatabase().Upsert(cycleB, &error),
               "failed to add cycle validation records: " + error))
        return false;
    imports.GetDatabase().ValidateAgainstProject(root / "Project", validation);
    const bool detectedCycle =
        std::any_of(validation.issues.begin(), validation.issues.end(), [](const AssetDatabaseValidationIssue& issue) {
            return issue.code == AssetDatabaseValidationIssueCode::DependencyCycle;
        });
    if (!Check(detectedCycle, "asset database validation missed a dependency cycle"))
        return false;

    EditorAssetRegistry registry;
    registry.SetRoot(root / "Project/Content");
    registry.Refresh();
    const auto assets = registry.GetAssets();
    const auto sourceFolderAssets = registry.GetAssetsInFolder("SourceAssets", true);
    const bool registrySawImport = std::any_of(assets.begin(), assets.end(), [&first](const EditorAssetInfo& info) {
        return info.uuid == first.record.uuid && info.imported && !info.artifactPath.empty() &&
               !info.diagnostics.empty();
    });
    const bool registrySawValidation = std::any_of(assets.begin(), assets.end(), [&first](const EditorAssetInfo& info) {
        return info.uuid == first.record.uuid &&
               std::any_of(info.diagnostics.begin(), info.diagnostics.end(), [](const AssetDiagnostic& diagnostic) {
                   return diagnostic.severity == "error" &&
                          diagnostic.message.find("[Validation]") != std::string::npos;
               });
    });
    const bool sourceFolderSawImport =
        std::any_of(sourceFolderAssets.begin(), sourceFolderAssets.end(), [&first](const EditorAssetInfo& info) {
            return info.uuid == first.record.uuid && info.imported && !info.diagnostics.empty();
        });
    if (!Check(registrySawImport, "asset registry did not surface database import state or diagnostics"))
        return false;
    if (!Check(registrySawValidation, "asset registry did not surface AssetDatabase validation diagnostics"))
        return false;
    if (!Check(sourceFolderSawImport, "asset registry folder query did not surface SourceAssets diagnostics"))
        return false;

    fs::remove_all(root, ec);
    return true;
}

bool TestEditorAngelScriptDomainRegistryOverrideAndRollback() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "MyEngineEditorScriptDomainTest";
    const fs::path engineScripts = root / "Engine";
    const fs::path projectScripts = root / "Project";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(engineScripts, ec);
    fs::create_directories(projectScripts, ec);

    {
        std::ofstream script(engineScripts / "Default.as");
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.Panel("engineTool", "Engine Tool", Top, "DrawEngineTool");
    registry.Panel("log", "Illegal Core Log", BottomCenter, "DrawLog");
}
void DrawEngineTool() {}
void DrawLog() {}
)AS";
    }
    {
        std::ofstream script(projectScripts / "Override.as");
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.Panel("engineTool", "Project Tool Override", Top, "DrawProjectTool");
    registry.Panel("toolbar", "Illegal Core Toolbar", Top, "DrawProjectToolbar");
    registry.Panel("custom", "Custom Panel", Right, "DrawCustom");
}
void DrawProjectTool() {}
void DrawProjectToolbar() {}
void DrawCustom() {}
)AS";
    }

    EditorAngelScriptDomain domain;
    std::string error;
    if (!Check(domain.Load(engineScripts, projectScripts, &error),
               "editor script domain failed to load valid scripts: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    const auto& panels = domain.GetRegistry().GetPanels();
    auto findPanel = [&](const std::string& id) -> const EditorScriptPanelSpec* {
        for (const auto& panel : panels) {
            if (panel.id == id)
                return &panel;
        }
        return nullptr;
    };
    const EditorScriptPanelSpec* engineTool = findPanel("engineTool");
    if (!Check(engineTool && engineTool->title == "Project Tool Override" && engineTool->callback == "DrawProjectTool",
               "project script did not override non-core tool panel by id")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(findPanel("toolbar") == nullptr && findPanel("log") == nullptr,
               "core panel ids were accepted as scripted tool panels")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(findPanel("custom") != nullptr, "project script did not append custom panel")) {
        fs::remove_all(root, ec);
        return false;
    }

    {
        std::ofstream script(projectScripts / "Broken.as");
        script << "void RegisterEditor(EditorRegistry@ registry) { broken script";
    }
    if (!Check(!domain.ReloadIfChanged(&error), "invalid editor script reload unexpectedly succeeded")) {
        fs::remove_all(root, ec);
        return false;
    }
    engineTool = findPanel("engineTool");
    const bool registryPreserved =
        engineTool && engineTool->title == "Project Tool Override" && findPanel("custom") != nullptr;
    fs::remove_all(root, ec);
    return Check(registryPreserved, "failed editor script reload did not preserve last valid registry");
}

bool TestEditorPanelBodyRegistrationAndProjectCoreOverridePolicy() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "MyEngineEditorPanelBodyTest";
    const fs::path engineScripts = root / "Engine";
    const fs::path projectScripts = root / "Project";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(engineScripts, ec);
    fs::create_directories(projectScripts, ec);

    {
        std::ofstream script(engineScripts / "Default.as");
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.PanelBody("toolbar", "DrawEngineToolbar");
}
void DrawEngineToolbar() {}
)AS";
    }
    {
        std::ofstream script(projectScripts / "Project.as");
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.PanelBody("toolbar", "DrawProjectToolbar");
    registry.Panel("customPanel", "Custom Panel", Right, "DrawCustom");
}
void DrawProjectToolbar() {}
void DrawCustom() {}
)AS";
    }

    EditorAngelScriptDomain domain;
    EditorScriptConfig config;
    config.enabledCorePanels = {"toolbar"};
    config.allowProjectOverrideCore = false;
    domain.SetConfig(config);

    std::string error;
    if (!Check(domain.Load(engineScripts, projectScripts, &error),
               "editor script domain failed to load panel body scripts: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    const std::string* toolbarBody = domain.GetRegistry().FindPanelBodyCallback("toolbar");
    if (!Check(toolbarBody && *toolbarBody == "DrawEngineToolbar",
               "project script overrode core toolbar PanelBody despite policy")) {
        fs::remove_all(root, ec);
        return false;
    }

    const bool hasProjectAppend = std::any_of(
        domain.GetRegistry().GetPanels().begin(), domain.GetRegistry().GetPanels().end(),
        [](const EditorScriptPanelSpec& panel) { return panel.id == "customPanel" && panel.callback == "DrawCustom"; });
    if (!Check(hasProjectAppend, "project script append panel registration was lost")) {
        fs::remove_all(root, ec);
        return false;
    }

    Scene scene("EditorPanelBody");
    EditorContext context(&scene);
    if (!Check(domain.ExecutePanelBody("toolbar", context, &error),
               "registered toolbar PanelBody did not execute: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    config.corePanelMode = EditorScriptCorePanelMode::CppOnly;
    domain.SetConfig(config);
    if (!Check(!domain.ExecutePanelBody("toolbar", context, &error),
               "cppOnly mode still executed a scripted panel body")) {
        fs::remove_all(root, ec);
        return false;
    }

    fs::remove_all(root, ec);
    return true;
}

bool TestEditorScriptBindingsAndPanelStatePersistence() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "MyEngineEditorBindingFacadeTest";
    const fs::path engineScripts = root / "Engine";
    const fs::path projectScripts = root / "Project";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(engineScripts, ec);
    fs::create_directories(projectScripts, ec);

    const fs::path scriptPath = engineScripts / "Default.as";
    {
        std::ofstream script(scriptPath);
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.PanelBody("toolbar", "DrawToolbar");
}
void DrawToolbar()
{
    PanelState::SetString("flag", "kept");
    string text = PanelState::GetString("flag", "");
}
)AS";
    }

    EditorAngelScriptDomain domain;
    EditorScriptConfig config;
    config.enabledCorePanels = {"toolbar"};
    domain.SetConfig(config);
    std::string error;
    if (!Check(domain.Load(engineScripts, projectScripts, &error),
               "editor binding facade script failed to compile: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    Scene scene("EditorBindingFacade");
    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    EditorProfiler profiler;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.SetProfiler(&profiler);
    if (!Check(domain.ExecutePanelBody("toolbar", context, &error),
               "editor binding facade script failed to execute: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    {
        std::ofstream script(scriptPath, std::ios::trunc);
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.PanelBody("toolbar", "DrawToolbar");
}
void DrawToolbar()
{
    if (PanelState::GetString("flag", "") == "kept")
    {
        Selection::SelectAsset("Content/StateKept.asset");
    }
}
)AS";
    }

    if (!Check(domain.ReloadIfChanged(&error), "editor binding facade reload failed: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(domain.ExecutePanelBody("toolbar", context, &error),
               "editor binding facade reloaded script failed: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    const bool statePersisted =
        context.GetSelection().GetAssetPath() == std::filesystem::path("Content/StateKept.asset").generic_string();
    fs::remove_all(root, ec);
    return Check(statePersisted, "PanelState did not persist across editor script reload");
}

bool TestEditorScriptExtensionPointRegistryAndExecution() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "MyEngineEditorExtensionPointTest";
    const fs::path engineScripts = root / "Engine";
    const fs::path projectScripts = root / "Project";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(engineScripts, ec);
    fs::create_directories(projectScripts, ec);

    {
        std::ofstream script(engineScripts / "Default.as");
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.ToolPanel("engineTool", "Engine Tool", Right, "DrawEngineTool");
}
void DrawEngineTool() {}
)AS";
    }
    {
        std::ofstream script(projectScripts / "Project.as");
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.ToolPanel("projectTool", "Project Tool", Left, "DrawProjectTool");
    registry.ToolPanel("assetBrowser", "Illegal Core", Left, "DrawIllegal");
    registry.MenuItem("Tools/Project Tool", "OpenProjectTool");
    registry.ToolbarItem("project.run", 10, "DrawToolbarRun");
    registry.InspectorSection("Actor", 50, "DrawActorSection");
    registry.AssetContextMenu("*", "DrawAssetMenu");
    registry.ActorContextMenu("DrawActorMenu");
}
void DrawProjectTool()
{
    PanelState::SetString("opened", "yes");
    Selection::SelectAsset("Content/ProjectTool.asset");
}
void DrawIllegal() {}
void OpenProjectTool() {}
void DrawToolbarRun() {}
void DrawActorSection() {}
void DrawAssetMenu() {}
void DrawActorMenu() {}
)AS";
    }

    EditorAngelScriptDomain domain;
    EditorScriptConfig config;
    config.allowProjectAppend = true;
    config.allowProjectOverrideCore = false;
    domain.SetConfig(config);
    std::string error;
    if (!Check(domain.Load(engineScripts, projectScripts, &error),
               "editor extension point script failed to compile: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    const auto& registry = domain.GetRegistry();
    const bool hasProjectTool =
        std::any_of(registry.GetPanels().begin(), registry.GetPanels().end(), [](const EditorScriptPanelSpec& panel) {
            return panel.id == "projectTool" && panel.callback == "DrawProjectTool";
        });
    const bool rejectedCoreTool =
        std::none_of(registry.GetPanels().begin(), registry.GetPanels().end(),
                     [](const EditorScriptPanelSpec& panel) { return panel.id == "assetBrowser"; });
    if (!Check(hasProjectTool && rejectedCoreTool,
               "project ToolPanel registration did not preserve core panel boundary")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(!registry.GetDiagnostics().empty(), "rejected core extension registration did not emit diagnostics")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(registry.GetMenus().size() == 1 && registry.GetToolbarItems().size() == 1 &&
                   registry.GetInspectors().size() == 1 && registry.GetAssetContextMenus().size() == 1 &&
                   registry.GetActorContextMenus().size() == 1,
               "extension point registry did not capture every append registration")) {
        fs::remove_all(root, ec);
        return false;
    }

    Scene scene("EditorExtensionPoint");
    EditorContext context(&scene);
    EditorOperators operators;
    context.SetOperators(&operators);
    if (!Check(domain.ExecuteExtension("DrawProjectTool", "tool:projectTool", context, &error),
               "scripted tool panel callback failed: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    const bool executed =
        context.GetSelection().GetAssetPath() == std::filesystem::path("Content/ProjectTool.asset").generic_string();
    fs::remove_all(root, ec);
    return Check(executed, "scripted extension callback did not run through facade");
}

bool TestEditorScriptValidationAndComponentMetadataFacade() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "MyEngineEditorFacadeMetadataTest";
    const fs::path engineScripts = root / "Engine";
    const fs::path projectScripts = root / "Project";
    const fs::path contentRoot = root / "ProjectRoot" / "Content";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(engineScripts, ec);
    fs::create_directories(projectScripts, ec);
    fs::create_directories(contentRoot / "Materials", ec);
    {
        std::ofstream material(contentRoot / "Materials" / "Test.mat");
        material << R"({"name":"Test"})";
    }

    {
        std::ofstream script(projectScripts / "Project.as");
        script << R"AS(
void RegisterEditor(EditorRegistry@ registry)
{
    registry.ToolPanel("facade.metadata", "Facade Metadata", Right, "RunFacadeMetadata");
}
void RunFacadeMetadata()
{
    uint64 actorId = Selection::GetActorId();
    PanelState::SetString("metadata", Components::GetMetadata("BoxCollider"));
    PanelState::SetString("actors", Scene::FindActorsWithComponent("BoxCollider"));
    PanelState::SetString("materials", Assets::ListByType("Material"));
    PanelState::SetString("contentRoot", Project::GetContentRoot());
    Validation::ReportInfo("metadata facade test");
    Components::SetFieldJson(actorId, "BoxCollider", "halfExtents", "[2.0,2.0,2.0]");
}
)AS";
    }

    EditorAngelScriptDomain domain;
    EditorScriptConfig config;
    domain.SetConfig(config);
    std::string error;
    if (!Check(domain.Load(engineScripts, projectScripts, &error),
               "editor metadata facade script failed to compile: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    Scene scene("EditorFacadeMetadata");
    Actor* actor = scene.CreateActor("ColliderActor");
    auto* collider = actor ? actor->AddComponent<BoxColliderComponent>() : nullptr;
    if (!Check(actor && collider, "failed to create collider actor")) {
        fs::remove_all(root, ec);
        return false;
    }
    collider->SetHalfExtents({1.0f, 1.0f, 1.0f});

    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    EditorAssetRegistry registry;
    registry.SetRoot(contentRoot);
    registry.Refresh();
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);
    context.SetAssetRegistry(&registry);
    context.SetProjectRoot(root / "ProjectRoot");
    context.GetSelection().SelectActorID(actor->GetID());

    if (!Check(domain.ExecuteExtension("RunFacadeMetadata", "tool:facade.metadata", context, &error),
               "metadata facade callback failed: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    nlohmann::json colliderJson;
    collider->Serialize(colliderJson);
    const bool edited = colliderJson["halfExtents"][0].get<float>() == 2.0f;
    const bool canUndo = stack.CanUndo();
    if (canUndo)
        stack.Undo(context);
    collider->Serialize(colliderJson);
    const bool undone = colliderJson["halfExtents"][0].get<float>() == 1.0f;

    const bool metadataOk = domain.ExecuteExtension("RunFacadeMetadata", "tool:facade.metadata", context, &error);
    (void)metadataOk;
    const bool hasActor = context.GetSelection().GetActorID() == actor->GetID();
    fs::remove_all(root, ec);
    return Check(edited && canUndo && undone && hasActor,
                 "metadata facade or SetFieldJson command did not behave correctly");
}

bool TestDefaultEditorScriptCompilesAndRegistersToolbarBody() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "EngineContent/Editor/Scripts/DefaultEditor.as")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path())
                break;
            current = current.parent_path();
        }
        return std::filesystem::path{};
    };

    const std::filesystem::path root = findRepositoryRoot();
    EditorAngelScriptDomain domain;
    EditorScriptConfig config;
    config.enabledCorePanels = {"toolbar"};
    domain.SetConfig(config);
    std::string error;
    if (!Check(domain.Load(root / "EngineContent/Editor/Scripts", root / "Content/Editor/Scripts", &error),
               "default editor AngelScript failed to compile: " + error)) {
        return false;
    }
    const std::string* toolbarBody = domain.GetRegistry().FindPanelBodyCallback("toolbar");
    return Check(toolbarBody && *toolbarBody == "DrawToolbar",
                 "default editor script did not register toolbar PanelBody");
}

bool TestEditorRecoveryServiceLifecycle() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_editor_recovery_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Content" / "Scenes", ec);
    std::string error;
    EditorRecoveryService first;
    if (!Check(first.OpenProject(root, &error) && first.PreviousShutdownWasClean(),
               "recovery service did not start from a clean project: " + error))
        return false;
    const std::string scene = R"({"version":1,"name":"Recovered","actors":[]})";
    if (!Check(first.WriteSnapshot("Content/Scenes/Main.scene.json", 7, scene, &error),
               "recovery snapshot write failed: " + error))
        return false;
    const auto snapshots = first.ListSnapshots(&error);
    if (!Check(snapshots.size() == 1 && snapshots[0].revision == 7, "recovery snapshot listing failed: " + error))
        return false;
    std::string recovered;
    if (!Check(first.ReadSnapshot(snapshots[0], recovered, &error) && recovered.find("Recovered") != std::string::npos,
               "recovery snapshot read failed: " + error))
        return false;

    EditorRecoveryService afterCrash;
    if (!Check(afterCrash.OpenProject(root, &error) && !afterCrash.PreviousShutdownWasClean(),
               "unclean shutdown was not detected: " + error))
        return false;
    if (!Check(afterCrash.RemoveMatchingRevision("Content/Scenes/Main.scene.json", 7, &error) &&
                   afterCrash.ListSnapshots(&error).empty(),
               "matching saved revision was not removed: " + error))
        return false;
    if (!Check(afterCrash.MarkCleanShutdown(&error), "clean shutdown marker failed: " + error))
        return false;

    EditorRecoveryService cleanRestart;
    const bool clean = cleanRestart.OpenProject(root, &error) && cleanRestart.PreviousShutdownWasClean();
    cleanRestart.MarkCleanShutdown(nullptr);
    fs::remove_all(root, ec);
    return Check(clean, "clean shutdown was not persisted: " + error);
}

bool TestEditorSkylightUniquenessAndLegacyInitialization() {
    Scene scene("EditorSkylight");
    scene.SetAmbientIntensity(2.75f);
    EditorContext context(&scene);
    EditorCommandStack stack;
    EditorOperators operators;
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    Actor* firstActor = scene.CreateActor("FirstSkylightActor");
    Actor* secondActor = scene.CreateActor("SecondSkylightActor");
    const uint64_t firstID = firstActor->GetID();
    const uint64_t secondID = secondActor->GetID();
    if (!Check(!operators.Components().AddComponents(context, {firstID, secondID}, "Skylight"),
               "batch component add created multiple scene Skylights")) {
        return false;
    }
    if (!Check(operators.Components().AddComponents(context, {firstID}, "Skylight"),
               "single-target Skylight add failed")) {
        return false;
    }

    firstActor = scene.FindByID(firstID);
    auto* skylight = firstActor ? firstActor->GetComponent<SkylightComponent>() : nullptr;
    if (!Check(skylight && std::abs(skylight->GetEnvironmentIntensity() - 2.75f) < 1e-5f,
               "new Skylight did not inherit legacy ambient intensity")) {
        return false;
    }
    if (!Check(!operators.Components().AddComponent(context, secondID, "Skylight"),
               "component operator allowed a second scene Skylight")) {
        return false;
    }

    if (!Check(stack.Undo(context) && !CollectSceneEnvironmentData(scene).HasSkylight() &&
                   std::abs(CollectSceneEnvironmentData(scene).environmentIntensity - 2.75f) < 1e-5f,
               "Skylight add undo did not restore legacy ambient fallback")) {
        return false;
    }
    if (!Check(stack.Redo(context), "Skylight add redo failed"))
        return false;
    const SceneEnvironmentData restored = CollectSceneEnvironmentData(scene);
    if (!Check(restored.sourceActorID == firstID && std::abs(restored.environmentIntensity - 2.75f) < 1e-5f,
               "Skylight add redo did not restore the resolved environment")) {
        return false;
    }

    if (!Check(operators.Components().SetEnabled(context, firstID, "Skylight", false) &&
                   !CollectSceneEnvironmentData(scene).HasSkylight(),
               "disabling Skylight did not restore the legacy fallback")) {
        return false;
    }
    if (!Check(stack.Undo(context) && CollectSceneEnvironmentData(scene).HasSkylight(),
               "Skylight enabled-state undo failed")) {
        return false;
    }
    if (!Check(stack.Redo(context) && !CollectSceneEnvironmentData(scene).HasSkylight(),
               "Skylight enabled-state redo failed")) {
        return false;
    }
    if (!Check(stack.Undo(context) && CollectSceneEnvironmentData(scene).HasSkylight(),
               "Skylight enabled-state restore failed")) {
        return false;
    }

    firstActor = scene.FindByID(firstID);
    skylight = firstActor ? firstActor->GetComponent<SkylightComponent>() : nullptr;
    nlohmann::json before = nlohmann::json::object();
    if (skylight)
        skylight->Serialize(before);
    nlohmann::json after = before;
    after["environmentIntensity"] = 4.25f;
    if (!Check(skylight &&
                   operators.Components().SetProperty(context, *firstActor, "Skylight", "environmentIntensity", before,
                                                      after) &&
                   std::abs(CollectSceneEnvironmentData(scene).environmentIntensity - 4.25f) < 1e-5f,
               "Skylight parameter edit did not apply through the component operation API")) {
        return false;
    }
    if (!Check(stack.Undo(context) &&
                   std::abs(CollectSceneEnvironmentData(scene).environmentIntensity - 2.75f) < 1e-5f &&
                   stack.Redo(context) &&
                   std::abs(CollectSceneEnvironmentData(scene).environmentIntensity - 4.25f) < 1e-5f,
               "Skylight parameter undo or redo failed")) {
        return false;
    }

    if (!Check(operators.Components().RemoveComponent(context, firstID, "Skylight") &&
                   !CollectSceneEnvironmentData(scene).HasSkylight(),
               "removing Skylight did not restore legacy fallback")) {
        return false;
    }
    return Check(stack.Undo(context) && CollectSceneEnvironmentData(scene).HasSkylight(),
                 "Skylight remove undo did not restore the component");
}

bool TestImNodesMatchesImGuiAbiAndDestroysCleanly() {
    IMGUI_CHECKVERSION();
    ImGuiContext* imguiContext = ImGui::CreateContext();
    if (!Check(imguiContext != nullptr, "failed to create ImGui context for imnodes ABI test"))
        return false;

    ImNodes::SetImGuiContext(imguiContext);
    ImNodes::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    bool emittedDrawData = false;
    for (int frame = 0; frame < 8; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_Always);
        ImGui::Begin("ImNodes ABI Test");
        ImNodes::BeginNodeEditor();
        ImNodes::BeginNode(1);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("Surface Output");
        ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(2);
        ImGui::TextUnformatted("Base Color");
        ImNodes::EndInputAttribute();
        ImNodes::EndNode();
        ImNodes::MiniMap(0.2f, ImNodesMiniMapLocation_BottomRight);
        ImNodes::EndNodeEditor();
        ImGui::End();
        ImGui::Render();
        emittedDrawData |= ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 0;
    }

    // The original regression was detected by the CRT here: imnodes compiled
    // against ImGui 1.91 wrote beyond ImGui 1.92 draw-channel allocations.
    ImNodes::DestroyContext();
    ImGui::DestroyContext(imguiContext);
    return Check(emittedDrawData, "imnodes ABI test did not exercise draw-channel generation");
}

MYENGINE_REGISTER_TEST("Editor", "TestEditorRecoveryServiceLifecycle", TestEditorRecoveryServiceLifecycle);
MYENGINE_REGISTER_TEST("Editor", "TestEditorSkylightUniquenessAndLegacyInitialization",
                       TestEditorSkylightUniquenessAndLegacyInitialization);
MYENGINE_REGISTER_TEST("Editor", "TestImNodesMatchesImGuiAbiAndDestroysCleanly",
                       TestImNodesMatchesImGuiAbiAndDestroysCleanly);
MYENGINE_REGISTER_TEST("Editor", "TestEditorCommandStackAndSelection", TestEditorCommandStackAndSelection);
MYENGINE_REGISTER_TEST("Editor", "TestEditorOperatorsSelectionAndCommands", TestEditorOperatorsSelectionAndCommands);
MYENGINE_REGISTER_TEST("Editor", "TestEditorCommandOperatorCreateUIActor", TestEditorCommandOperatorCreateUIActor);
MYENGINE_REGISTER_TEST("Editor", "TestEditorCommandOperatorCreateEmptyParent",
                       TestEditorCommandOperatorCreateEmptyParent);
MYENGINE_REGISTER_TEST("Editor", "TestEditorCommandOperatorCreateChildActor",
                       TestEditorCommandOperatorCreateChildActor);
MYENGINE_REGISTER_TEST("Editor", "TestEditorCommandOperatorHierarchyOrganization",
                       TestEditorCommandOperatorHierarchyOrganization);
MYENGINE_REGISTER_TEST("Editor", "TestEditorSelectObjectEvents", TestEditorSelectObjectEvents);
MYENGINE_REGISTER_TEST("Editor", "TestEditorSceneSnapshotCommands", TestEditorSceneSnapshotCommands);
MYENGINE_REGISTER_TEST("Editor", "TestEditorMoveActorCommandUndoRedo", TestEditorMoveActorCommandUndoRedo);
MYENGINE_REGISTER_TEST("Editor", "TestEditorContextWorldRouting", TestEditorContextWorldRouting);
MYENGINE_REGISTER_TEST("Editor", "TestEditorViewportOperatorFrameSelected", TestEditorViewportOperatorFrameSelected);
MYENGINE_REGISTER_TEST("Editor", "TestEditorGizmoRowVectorLocalConversion", TestEditorGizmoRowVectorLocalConversion);
MYENGINE_REGISTER_TEST("Editor", "TestEditorServiceActionAndInspectorRegistries",
                       TestEditorServiceActionAndInspectorRegistries);
MYENGINE_REGISTER_TEST("Editor", "TestEditorShortcutMapAndWorkspacePersistence",
                       TestEditorShortcutMapAndWorkspacePersistence);
MYENGINE_REGISTER_TEST("Editor", "TestEditorAppearancePreferencesAndScale", TestEditorAppearancePreferencesAndScale);
MYENGINE_REGISTER_TEST("Editor", "TestEditorStatusBarTextAndActionRouting", TestEditorStatusBarTextAndActionRouting);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPanelSelectAllActionRouting", TestEditorPanelSelectAllActionRouting);
MYENGINE_REGISTER_TEST("Editor", "TestSceneHierarchyOrganizationActions", TestSceneHierarchyOrganizationActions);
MYENGINE_REGISTER_TEST("Editor", "TestEditorInspectorSelectionRouting", TestEditorInspectorSelectionRouting);
MYENGINE_REGISTER_TEST("Editor", "TestInspectorPanelSnapshotsAreInputGated", TestInspectorPanelSnapshotsAreInputGated);
MYENGINE_REGISTER_TEST("Editor", "TestEditorProfilerBufferAndSourceContracts",
                       TestEditorProfilerBufferAndSourceContracts);
MYENGINE_REGISTER_TEST("Editor", "TestEditorLayoutConfigAndStatePersistence",
                       TestEditorLayoutConfigAndStatePersistence);
MYENGINE_REGISTER_TEST("Editor", "TestEditorDockSpaceAdoptsPersistedRootAndResizesTree",
                       TestEditorDockSpaceAdoptsPersistedRootAndResizesTree);
MYENGINE_REGISTER_TEST("Editor", "TestEditorDockSpaceRejectsOrphanedEmptyRoot",
                       TestEditorDockSpaceRejectsOrphanedEmptyRoot);
MYENGINE_REGISTER_TEST("Editor", "TestEditorProjectAndAssetRegistry", TestEditorProjectAndAssetRegistry);
MYENGINE_REGISTER_TEST("Editor", "TestEditorAssetOperatorCommandsAndWatch", TestEditorAssetOperatorCommandsAndWatch);
MYENGINE_REGISTER_TEST("Editor", "TestEditorAssetOperatorOpenSceneAsset", TestEditorAssetOperatorOpenSceneAsset);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPrefabOverrideOperatorSingleApplyRevert",
                       TestEditorPrefabOverrideOperatorSingleApplyRevert);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPrefabOverrideDisplayModelSorting",
                       TestEditorPrefabOverrideDisplayModelSorting);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPrefabOverrideUnsupportedPersistedKindIsBlocked",
                       TestEditorPrefabOverrideUnsupportedPersistedKindIsBlocked);
MYENGINE_REGISTER_TEST("Editor", "TestEditorNestedPrefabApplyRevertUndoRedo",
                       TestEditorNestedPrefabApplyRevertUndoRedo);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPrefabOperatorCreateAndInstantiateCommands",
                       TestEditorPrefabOperatorCreateAndInstantiateCommands);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPrefabOperatorRemovedActorOverrideApplyRevert",
                       TestEditorPrefabOperatorRemovedActorOverrideApplyRevert);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPrefabOperatorAddedActorOverrideApplyRevert",
                       TestEditorPrefabOperatorAddedActorOverrideApplyRevert);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPrefabOperatorAddedComponentOverrideApplyRevert",
                       TestEditorPrefabOperatorAddedComponentOverrideApplyRevert);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPrefabOperatorRemovedComponentOverrideApplyRevert",
                       TestEditorPrefabOperatorRemovedComponentOverrideApplyRevert);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPerformanceSourceContracts", TestEditorPerformanceSourceContracts);
MYENGINE_REGISTER_TEST("Editor", "TestProductionAssetDatabaseAndImportPipeline",
                       TestProductionAssetDatabaseAndImportPipeline);
MYENGINE_REGISTER_TEST("Editor", "TestEditorAngelScriptDomainRegistryOverrideAndRollback",
                       TestEditorAngelScriptDomainRegistryOverrideAndRollback);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPanelBodyRegistrationAndProjectCoreOverridePolicy",
                       TestEditorPanelBodyRegistrationAndProjectCoreOverridePolicy);
MYENGINE_REGISTER_TEST("Editor", "TestEditorScriptBindingsAndPanelStatePersistence",
                       TestEditorScriptBindingsAndPanelStatePersistence);
MYENGINE_REGISTER_TEST("Editor", "TestEditorScriptExtensionPointRegistryAndExecution",
                       TestEditorScriptExtensionPointRegistryAndExecution);
MYENGINE_REGISTER_TEST("Editor", "TestEditorScriptValidationAndComponentMetadataFacade",
                       TestEditorScriptValidationAndComponentMetadataFacade);
MYENGINE_REGISTER_TEST("Editor", "TestDefaultEditorScriptCompilesAndRegistersToolbarBody",
                       TestDefaultEditorScriptCompilesAndRegistersToolbarBody);

} // namespace
