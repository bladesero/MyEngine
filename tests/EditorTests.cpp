#include "TestHarness.h"

#include "Assets/AssetManager.h"
#include "Assets/MeshSdfVoxel.h"
#include "Assets/ModelAsset.h"
#include "Editor/EditorAction.h"
#include "Editor/AssetImportService.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorLayoutManager.h"
#include "Editor/EditorOperators.h"
#include "Editor/EditorProfiler.h"
#include "Editor/EditorProject.h"
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
#include "Game/SceneRenderLayer.h"
#include "Core/Sha256.h"
#include "Physics/BoxColliderComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

bool TestEditorCommandStackAndSelection() {
    Scene scene("EditorCommands");
    EditorContext context(&scene);
    EditorCommandStack stack;
    context.SetCommandStack(&stack);

    int value = 0;
    auto makeAdd = [&value](const char* name, int amount) {
        return std::make_unique<LambdaEditorCommand>(
            name,
            [&value, amount](EditorContext&) { value += amount; return true; },
            [&value, amount](EditorContext&) { value -= amount; return true; });
    };

    if (!Check(stack.ExecuteCommand(makeAdd("Add One", 1), context),
               "editor command execute failed")) return false;
    if (!Check(stack.ExecuteCommand(makeAdd("Add Two", 2), context) && value == 3,
               "editor command execute order mismatch")) return false;
    if (!Check(stack.Undo(context) && value == 1, "editor command undo failed")) return false;
    if (!Check(stack.Redo(context) && value == 3, "editor command redo failed")) return false;
    if (!Check(stack.Undo(context), "editor command second undo failed")) return false;
    if (!Check(stack.ExecuteCommand(makeAdd("Add Four", 4), context) && value == 5,
               "editor command after undo failed")) return false;
    if (!Check(!stack.CanRedo(), "new command did not invalidate redo")) return false;

    if (!Check(stack.BeginTransaction("Batch Edit"), "begin transaction failed")) return false;
    if (!Check(stack.ExecuteCommand(makeAdd("Add Ten", 10), context),
               "first transaction command failed")) return false;
    if (!Check(stack.ExecuteCommand(makeAdd("Add Twenty", 20), context),
               "second transaction command failed")) return false;
    if (!Check(stack.CommitTransaction() && value == 35,
               "commit transaction failed")) return false;
    if (!Check(std::string(stack.GetUndoName()) == "Batch Edit",
               "transaction name was not preserved")) return false;
    if (!Check(stack.Undo(context) && value == 5,
               "transaction undo order mismatch")) return false;
    if (!Check(stack.Redo(context) && value == 35,
               "transaction redo order mismatch")) return false;

    Actor* actor = scene.CreateActor("Selected");
    const uint64_t actorID = actor->GetID();
    context.GetSelection().SelectActorID(actorID);
    Transform before = actor->GetTransform();
    Transform after = before;
    after.position = {4.0f, 5.0f, 6.0f};
    if (!Check(stack.ExecuteCommand(
            std::make_unique<SetActorTransformCommand>(actorID, before, after), context),
            "transform command failed")) return false;
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 4.0f),
               "transform command did not apply")) return false;
    if (!Check(stack.Undo(context) && NearlyEqual(actor->GetTransform().position.x, 0.0f),
               "transform undo failed")) return false;

    scene.DestroyActor(actor);
    context.GetSelection().Validate(scene);
    return Check(!context.GetSelection().HasActor() &&
                 context.GetSelection().ResolveActor(scene) == nullptr,
                 "selection did not invalidate after actor deletion");
}

bool TestEditorSelectObjectEvents() {
    Scene scene("EditorSelectionEvents");
    Actor* first = scene.CreateActor("First");
    Actor* second = scene.CreateActor("Second");

    EditorSelection selection;
    std::vector<EditorSelectionChangedEvent> events;
    const EditorSelection::ListenerID listenerID =
        selection.SubscribeSelectionChanged(
            [&events](const EditorSelectionChangedEvent& event) {
                events.push_back(event);
            });

    selection.Select(EditorSelectObject::MakeActor(
        first->GetHandle(), first->GetID()));
    if (!Check(events.size() == 1 && events.back().current.IsActor()
            && events.back().current.GetActorID() == first->GetID(),
            "actor selection event mismatch")) return false;

    selection.Select(EditorSelectObject::MakeActor(
        first->GetHandle(), first->GetID()));
    if (!Check(events.size() == 1,
               "repeated selection emitted a duplicate event")) return false;

    selection.Select(EditorSelectObject::MakeAsset("Content/Textures/../Materials/Test.mat"));
    if (!Check(events.size() == 2 && events.back().current.IsAsset()
            && events.back().current.GetAssetPath()
                == std::filesystem::path("Content/Materials/Test.mat").generic_string(),
            "asset selection was not normalized or notified")) return false;

    selection.Select(EditorSelectObject::MakeActor(
        first->GetHandle(), first->GetID()));
    selection.Select(EditorSelectObject::MakeActor(
        second->GetHandle(), second->GetID()), EditorSelectionMode::Add);
    if (!Check(events.size() == 4 && selection.IsMultiSelect()
            && events.back().actorIDs.size() == 2
            && selection.GetPrimaryObject().GetActorID() == second->GetID(),
            "actor multi-selection event mismatch")) return false;

    selection.Select(EditorSelectObject::MakeActor(
        second->GetHandle(), second->GetID()), EditorSelectionMode::Toggle);
    if (!Check(events.size() == 5 && !selection.IsMultiSelect()
            && selection.GetPrimaryObject().GetActorID() == first->GetID(),
            "actor toggle did not restore the previous primary selection")) return false;

    selection.Clear();
    selection.Clear();
    if (!Check(events.size() == 6 && events.back().current.IsNone(),
               "clear selection event mismatch")) return false;

    selection.SelectActorID(first->GetID());
    scene.DestroyActor(first);
    selection.Validate(scene);
    if (!Check(events.size() == 8 && events.back().current.IsNone(),
               "invalid actor validation did not emit a clear event")) return false;

    selection.UnsubscribeSelectionChanged(listenerID);
    selection.SelectAssetPath("Content/Materials/AfterUnsubscribe.mat");
    return Check(events.size() == 8,
                 "unsubscribed selection listener was still invoked");
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
    if (!Check(SceneSerializer::LoadFromString(scene, emptyScene),
               "failed to restore snapshot baseline")) return false;

    if (!Check(stack.ExecuteCommand(std::make_unique<SceneSnapshotCommand>(
            "Create Hierarchy", emptyScene, populatedScene, 0, parentID), context),
            "scene snapshot execute failed")) return false;
    Actor* restoredParent = scene.FindByID(parentID);
    if (!Check(restoredParent && restoredParent->GetChildren().size() == 1,
               "scene snapshot did not restore hierarchy")) return false;
    if (!Check(restoredParent->GetChildren()[0]->GetComponent<BoxColliderComponent>() != nullptr,
               "scene snapshot did not restore component")) return false;
    if (!Check(context.GetSelection().GetActorID() == parentID,
               "scene snapshot did not restore selection")) return false;

    if (!Check(stack.Undo(context) && scene.ActorCount() == 0,
               "scene snapshot undo failed")) return false;
    if (!Check(stack.Redo(context), "scene snapshot redo failed")) return false;
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

    const uint64_t actorID = operators.Commands().CreateActor(context, "OperatorActor");
    Actor* actor = scene.FindByID(actorID);
    if (!Check(actor && actor->GetName() == "OperatorActor",
               "operator create actor failed")) return false;
    if (!Check(context.GetSelection().GetActorID() == actorID,
               "operator create actor did not preserve command selection semantics")) return false;

    if (!Check(operators.Commands().RenameActor(context, actorID, "Renamed"),
               "operator rename actor failed")) return false;
    if (!Check(actor->GetName() == "Renamed",
               "operator rename did not apply")) return false;
    if (!Check(stack.Undo(context) && actor->GetName() == "OperatorActor",
               "operator rename undo failed")) return false;
    if (!Check(stack.Redo(context) && actor->GetName() == "Renamed",
               "operator rename redo failed")) return false;

    if (!Check(operators.Commands().SetActorActive(context, actorID, false),
               "operator active toggle failed")) return false;
    if (!Check(!actor->IsActiveSelf(),
               "operator active toggle did not apply")) return false;
    if (!Check(stack.Undo(context) && actor->IsActiveSelf(),
               "operator active toggle undo failed")) return false;

    if (!Check(operators.Selection().SelectAsset(context, "Content/Models/test.gltf"),
               "operator asset selection failed")) return false;
    const EditorSelectionSnapshot snapshot =
        operators.Selection().GetSelectionSnapshot(context);
    if (!Check(snapshot.hasAsset &&
               snapshot.assetPath == std::filesystem::path("Content/Models/test.gltf").generic_string(),
               "operator selection snapshot mismatch")) return false;
    operators.Selection().Clear(context);
    if (!Check(!context.GetSelection().HasActor() && !context.GetSelection().HasAsset(),
               "operator clear selection failed")) return false;

    operators.Selection().SelectActor(context, actorID);
    if (!Check(operators.Commands().DeleteActor(context, actorID),
               "operator delete actor failed")) return false;
    if (!Check(scene.FindByID(actorID) == nullptr,
               "operator delete actor did not apply")) return false;
    if (!Check(stack.Undo(context) && scene.FindByID(actorID) != nullptr,
               "operator delete actor undo failed")) return false;
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
    if (!Check(stack.ExecuteCommand(
            EditorUndoUtil::MakeMoveActorCommand(
                *childB, beforeParentID, beforeNextID, afterParentID, afterNextID),
            context),
            "move actor command execute failed")) return false;
    if (!Check(childB->GetParent() == otherParent &&
               otherParent->GetChildren().size() == 3 &&
               otherParent->GetChildren()[0] == otherA &&
               otherParent->GetChildren()[1] == childB &&
               otherParent->GetChildren()[2] == otherB,
               "move actor command did not apply target order")) return false;
    if (!Check(context.GetSelection().GetActorID() == childB->GetID(),
               "move actor command did not select moved actor")) return false;

    if (!Check(stack.Undo(context), "move actor command undo failed")) return false;
    if (!Check(childB->GetParent() == parent &&
               parent->GetChildren().size() == 3 &&
               parent->GetChildren()[0] == childA &&
               parent->GetChildren()[1] == childB &&
               parent->GetChildren()[2] == childC,
               "move actor command undo did not restore original order")) return false;

    if (!Check(stack.Redo(context), "move actor command redo failed")) return false;
    return Check(childB->GetParent() == otherParent &&
                 otherParent->GetChildren()[1] == childB,
                 "move actor command redo did not restore target order");
}

bool TestEditorContextWorldRouting() {
    SceneRenderLayer layer(nullptr, 320, 180);
    Actor* editorActor = layer.GetEditorScene().CreateActor("EditorOnly");
    EditorContext context(&layer, nullptr, nullptr, nullptr);
    context.GetSelection().Select(EditorSelectObject::MakeActor(
        editorActor->GetHandle(), editorActor->GetID()));

    if (!Check(context.GetScene() == &layer.GetEditorScene(),
               "EditorContext::GetScene should return EditorWorld")) return false;
    if (!Check(context.GetEditorScene() == &layer.GetEditorScene(),
               "EditorContext::GetEditorScene mismatch")) return false;
    if (!Check(context.GetSimulationScene() == &layer.GetEditorScene(),
               "Edit mode simulation scene should be EditorWorld")) return false;
    if (!Check(context.GetPlayScene() == nullptr,
               "Edit mode should not expose a PlayWorld")) return false;
    context.SetSceneViewMode(EditorWorldViewMode::PlayWorldInspect);
    if (!Check(context.GetSceneViewMode() == EditorWorldViewMode::EditorWorld &&
               context.GetSceneViewScene() == &layer.GetEditorScene(),
               "Edit mode should reject PlayWorldInspect")) return false;

    layer.MarkDirty();
    if (!Check(layer.BeginPlay(), "BeginPlay failed for editor context routing")) return false;
    if (!Check(context.GetScene() == &layer.GetEditorScene() &&
               context.GetEditorScene() == &layer.GetEditorScene(),
               "EditorContext should keep editing routed to EditorWorld in Play")) return false;
    if (!Check(context.GetPlayScene() == layer.GetPlayScene() &&
               context.GetSimulationScene() == layer.GetPlayScene(),
               "Play mode simulation scene should be PlayWorld")) return false;
    Actor* playActor = layer.GetPlayScene()->FindByID(editorActor->GetID());
    if (!Check(playActor && playActor != editorActor,
               "PlayWorld should contain cloned actor instances")) return false;
    playActor->GetTransform().position.x = 42.0f;

    if (!Check(context.GetSceneViewScene() == &layer.GetEditorScene() &&
               !layer.GetSceneViewportUsesSimulationScene(),
               "SceneView should stay on EditorWorld after BeginPlay")) return false;
    context.SetSceneViewMode(EditorWorldViewMode::PlayWorldInspect);
    if (!Check(context.GetSceneViewScene() == layer.GetPlayScene() &&
               layer.GetSceneViewportUsesSimulationScene(),
               "PlayWorldInspect did not route SceneView to PlayWorld")) return false;
    if (!Check(context.GetInspectorScene() == layer.GetPlayScene() &&
               context.GetSelection().GetPrimaryObject().GetWorldKind() == EditorSelectionWorldKind::Play,
               "PlayWorldInspect did not map actor selection to PlayWorld")) return false;
    if (!Check(context.GetSelection().ResolveActor(*layer.GetPlayScene()) == playActor &&
               context.GetSelection().ResolveActor(*context.GetScene()) == editorActor,
               "selection did not preserve persistent id across worlds")) return false;
    if (!Check(!context.CanEditScene() && !context.CanEditSelection(),
               "PlayWorldInspect should be read-only")) return false;

    layer.PausePlay();
    if (!Check(context.GetScene() == &layer.GetEditorScene() &&
               context.GetSimulationScene() == layer.GetPlayScene(),
               "Pause mode should preserve EditorWorld edits and PlayWorld simulation")) return false;

    layer.StopPlay();
    context.RefreshSceneViewMode();
    return Check(context.GetPlayScene() == nullptr &&
                 context.GetSimulationScene() == &layer.GetEditorScene() &&
                 context.GetSceneViewScene() == &layer.GetEditorScene() &&
                 context.GetInspectorScene() == &layer.GetEditorScene() &&
                 context.GetScene() == &layer.GetEditorScene() &&
                 context.GetSelection().GetPrimaryObject().GetWorldKind() == EditorSelectionWorldKind::Editor,
                 "StopPlay did not return editor context to EditorWorld-only routing");
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
    if (!Check(EditorGizmoController::ComputeLocalMatrix(
            world, &parentWorld, actualLocal),
            "gizmo local matrix conversion failed")) return false;

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (!Check(NearlyEqual(actualLocal.m[row][column],
                                   expectedLocal.m[row][column], 1e-3f),
                       "gizmo local matrix violated row-vector order")) return false;
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
               "typed service lookup did not return registered service")) return false;
    services.UpdateAll(0.016f);
    services.DetachAll(context);
    const std::vector<std::string> expected {
        "attach:first", "attach:second", "update:first", "update:second",
        "detach:second", "detach:first"
    };
    if (!Check(events == expected, "service lifecycle order mismatch")) return false;

    EditorActionRegistry actions;
    bool actionEnabled = false;
    int actionExecutions = 0;
    if (!Check(actions.Register(std::make_unique<LambdaEditorAction>(
            "test.action", "Test Action",
            [&actionExecutions](EditorContext&) { ++actionExecutions; },
            [&actionEnabled](EditorContext&) { return actionEnabled; })),
            "action registration failed")) return false;
    if (!Check(!actions.Execute("test.action", context) && actionExecutions == 0,
               "disabled action executed")) return false;
    actionEnabled = true;
    if (!Check(actions.Execute("test.action", context) && actionExecutions == 1,
               "enabled action did not execute")) return false;

    class TestSection final : public EditorInspectorSection {
    public:
        TestSection(const char* id, int order) : m_ID(id), m_Order(order) {}
        const char* GetID() const override { return m_ID.c_str(); }
        int GetOrder() const override { return m_Order; }
        bool CanDraw(const EditorSelectObject& object,
                     const EditorContext&) const override {
            return object.IsActor();
        }
        void Draw(EditorContext&) override {}
    private:
        std::string m_ID;
        int m_Order;
    };

    EditorInspectorRegistry sections;
    sections.Register(std::make_unique<TestSection>("late", 20));
    sections.Register(std::make_unique<TestSection>("early", 10));
    if (!Check(std::string(sections.GetSections()[0]->GetID()) == "early",
               "inspector section order mismatch")) return false;
    EditorSelection selection;
    if (!Check(!sections.GetSections()[0]->CanDraw(
                   selection.GetPrimaryObject(), context),
               "inspector section filtering mismatch")) return false;
    Actor* selected = scene.CreateActor("SectionSelection");
    selection.SelectActorID(selected->GetID());
    return Check(sections.GetSections()[0]->CanDraw(
                     selection.GetPrimaryObject(), context),
                 "inspector section did not accept actor selection");
}

bool TestEditorShortcutMapAndWorkspacePersistence() {
    EditorShortcutChord chord;
    std::string error;
    if (!Check(EditorShortcutMap::ParseChord("Ctrl+Shift+Z", chord, &error),
               "shortcut parse failed: " + error)) return false;
    if (!Check(chord.ctrl && chord.shift && !chord.alt && chord.IsValid(),
               "shortcut modifiers parsed incorrectly")) return false;
    if (!Check(EditorShortcutMap::FormatChord(chord) == "Ctrl+Shift+Z",
               "shortcut format mismatch")) return false;
    if (!Check(!EditorShortcutMap::ParseChord("Ctrl+NoSuchKey", chord, &error),
               "invalid shortcut key was accepted")) return false;

    EditorShortcutMap shortcuts = EditorShortcutMap::CreateDefault();
    if (!Check(shortcuts.FindShortcut("scene.save") &&
               EditorShortcutMap::FormatChord(*shortcuts.FindShortcut("scene.save")) == "Ctrl+S",
               "default save shortcut missing")) return false;

    EditorShortcutChord saveChord;
    if (!Check(EditorShortcutMap::ParseChord("Ctrl+S", saveChord, &error),
               "save shortcut parse failed")) return false;
    shortcuts.SetShortcut("conflict.action", saveChord);
    if (!Check(shortcuts.FindConflict("scene.save", saveChord) == "conflict.action",
               "shortcut conflict was not detected")) return false;

    Scene scene("ShortcutDispatch");
    EditorContext context(&scene);
    EditorActionRegistry actions;
    bool enabled = false;
    int disabledCount = 0;
    int enabledCount = 0;
    if (!Check(actions.Register(std::make_unique<LambdaEditorAction>(
            "disabled.action", "Disabled",
            [&disabledCount](EditorContext&) { ++disabledCount; },
            [&enabled](EditorContext&) { return enabled; })),
            "disabled action registration failed")) return false;
    if (!Check(actions.Register(std::make_unique<LambdaEditorAction>(
            "enabled.action", "Enabled",
            [&enabledCount](EditorContext&) { ++enabledCount; })),
            "enabled action registration failed")) return false;
    EditorShortcutMap dispatchMap;
    dispatchMap.SetShortcut("disabled.action", saveChord);
    if (!Check(!dispatchMap.DispatchChord(saveChord, actions, context) && disabledCount == 0,
               "disabled shortcut action executed")) return false;
    dispatchMap.ClearShortcut("disabled.action");
    dispatchMap.SetShortcut("enabled.action", saveChord);
    if (!Check(dispatchMap.DispatchChord(saveChord, actions, context) && enabledCount == 1,
               "enabled shortcut action did not execute")) return false;

    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("myengine_shortcut_workspace_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    EditorWorkspace workspace(root / "workspace.json");
    workspace.GetShortcuts().SetShortcut("scene.save", saveChord);
    workspace.GetShortcuts().ClearShortcut("edit.undo");
    if (!Check(workspace.Save(&error), "workspace shortcut save failed: " + error)) return false;

    EditorWorkspace loaded(root / "workspace.json");
    if (!Check(loaded.Load(&error), "workspace shortcut load failed: " + error)) return false;
    const auto* loadedSave = loaded.GetShortcuts().FindShortcut("scene.save");
    const auto* loadedUndo = loaded.GetShortcuts().FindShortcut("edit.undo");
    const bool persisted = loadedSave && *loadedSave == saveChord &&
        loadedUndo && !loadedUndo->IsValid();
    std::error_code ec;
    fs::remove_all(root, ec);
    return Check(persisted, "workspace shortcut persistence mismatch");
}

bool TestEditorAppearancePreferencesAndScale() {
    if (!Check(EditorUIScaleManager::ClampUserScale(0.1f) ==
               EditorUIScaleSettings::kMinUserScale,
               "ui scale lower clamp mismatch")) return false;
    if (!Check(Editor::UI::EditorUIScaleManager::ClampUserScale(0.1f) ==
               Editor::UI::EditorUIScaleSettings::kMinUserScale,
               "namespaced ui scale lower clamp mismatch")) return false;
    if (!Check(EditorUIScaleManager::ClampUserScale(10.0f) ==
               EditorUIScaleSettings::kMaxUserScale,
               "ui scale upper clamp mismatch")) return false;
    if (!Check(NearlyEqual(EditorUIScaleManager::ComputeEffectiveScale(1.5f, 1.25f), 1.875f),
               "effective ui scale calculation mismatch")) return false;

    const auto& fontConfig = Editor::UI::EditorFontManager::GetDefaultConfig();
    if (!Check(fontConfig.uiRegularPath.filename() == "Inter-Regular.ttf" &&
               fontConfig.uiSemiBoldPath.filename() == "Inter-SemiBold.ttf" &&
               fontConfig.logRegularPath.filename() == "JetBrainsMono-Regular.ttf" &&
               fontConfig.iconSolidPath.filename() == "FontAwesome-Free-Solid-900.ttf",
               "editor font config filenames mismatch")) return false;
    if (!Check(std::string(Editor::UI::EditorIcons::PlayIcon()).size() > 0 &&
               std::string(Editor::UI::EditorIcons::StopIcon()).size() > 0 &&
               std::string(Editor::UI::EditorIcons::PauseIcon()).size() > 0 &&
               std::string(Editor::UI::EditorIcons::StepIcon()).size() > 0,
               "editor icon fallback tokens are empty")) return false;

    EditorUIScaleManager scale;
    scale.Initialize(nullptr, 1.0f);
    scale.SetPlatformScaleForTesting(1.25f);
    const float first = scale.GetEffectiveScale();
    if (!Check(NearlyEqual(first, 1.25f),
               "testing dpi scale did not affect effective scale")) return false;
    if (!Check(scale.SetUserScale(1.5f) &&
               NearlyEqual(scale.GetEffectiveScale(), 1.875f),
               "user ui scale did not affect effective scale")) return false;
    if (!Check(!scale.SetUserScale(1.5f),
               "unchanged user ui scale reported a change")) return false;

    EditorThemeManager theme;
    theme.Initialize("unknown");
    if (!Check(theme.GetThemeID() == "dark",
               "unknown theme did not fall back to dark")) return false;
    if (!Check(NearlyEqual(EditorThemeManager::ScaleValue(8.0f, 1.5f), 12.0f),
               "theme scaled spacing mismatch")) return false;
    if (!Check(NearlyEqual(Editor::UI::ScaleToken(
            Editor::UI::EditorStyleTokens{}.toolbarHeight, 1.25f),
            Editor::UI::EditorStyleTokens{}.toolbarHeight * 1.25f),
            "toolbar style token scale mismatch")) return false;
    if (!Check(NearlyEqual(Editor::UI::ScaleToken(
            Editor::UI::EditorStyleTokens{}.statusBarHeight, 1.5f),
            Editor::UI::EditorStyleTokens{}.statusBarHeight * 1.5f),
            "status bar style token scale mismatch")) return false;

    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("myengine_appearance_workspace_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    std::string error;
    EditorWorkspace workspace(root / "workspace.json");
    workspace.SetUserUiScale(1.35f);
    workspace.SetEditorThemeId("dark");
    if (!Check(workspace.Save(&error), "appearance workspace save failed: " + error)) return false;

    EditorWorkspace loaded(root / "workspace.json");
    if (!Check(loaded.Load(&error), "appearance workspace load failed: " + error)) return false;
    const bool persisted = NearlyEqual(loaded.GetUserUiScale(), 1.35f) &&
        loaded.GetEditorThemeId() == "dark";
    std::error_code ec;
    fs::remove_all(root, ec);
    return Check(persisted, "appearance workspace persistence mismatch");
}

bool TestEditorStatusBarTextAndActionRouting() {
    Scene scene("StatusBar");
    EditorContext context(&scene);
    if (!Check(Editor::UI::EditorStatusBar::FormatSelectedText(context) == "None",
               "status bar empty selection mismatch")) return false;

    Actor* actor = scene.CreateActor("monkey");
    context.GetSelection().SelectActorID(actor->GetID());
    if (!Check(Editor::UI::EditorStatusBar::FormatSelectedText(context) == "monkey",
               "status bar actor selection mismatch")) return false;

    context.GetSelection().SelectAssetPath("Content/Models/monkey.gltf");
    if (!Check(Editor::UI::EditorStatusBar::FormatSelectedText(context) == "monkey.gltf",
               "status bar asset selection mismatch")) return false;
    if (!Check(Editor::UI::EditorStatusBar::FormatBackendText(RHIBackend::D3D12) == "D3D12" &&
               Editor::UI::EditorStatusBar::FormatBackendText(RHIBackend::D3D11) == "D3D11" &&
               Editor::UI::EditorStatusBar::FormatBackendText(RHIBackend::Vulkan) == "Vulkan" &&
               Editor::UI::EditorStatusBar::FormatBackendText(RHIBackend::Unknown) == "Unknown",
               "status bar backend text mismatch")) return false;

    EditorActionRegistry actions;
    int executions = 0;
    bool enabled = false;
    actions.Register(std::make_unique<LambdaEditorAction>(
        "menu.test", "Menu Test",
        [&executions](EditorContext&) { ++executions; },
        [&enabled](EditorContext&) { return enabled; }));
    if (!Check(!actions.Execute("menu.test", context) && executions == 0,
               "disabled menu action executed")) return false;
    enabled = true;
    return Check(actions.Execute("menu.test", context) && executions == 1,
                 "enabled menu action did not execute");
}

bool TestEditorInspectorSelectionRouting() {
    const auto root = std::filesystem::temp_directory_path()
        / "myengine_inspector_selection_routing";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    const auto materialPath = root / "Selected.mat";
    const auto texturePath = root / "Selected.png";
    const auto audioPath = root / "Selected.wav";
    const auto modelPath = root / "Selected.obj";
    const auto jsonPath = root / "Selected.json";
    std::ofstream(materialPath) << "{}";
    std::ofstream(texturePath) << "png";
    std::ofstream(audioPath) << "wav";
    std::ofstream(modelPath) << "obj";
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
            [id](const auto& section) {
                return std::string(section->GetID()) == id;
            });
        return found != sections.end() && (*found)->CanDraw(object, context);
    };

    if (!Check(accepts("sceneSettings", {})
            && !accepts("transform", {}),
            "empty selection did not route to scene settings")) return false;

    Actor* actor = scene.CreateActor("SelectedActor");
    const EditorSelectObject actorObject = EditorSelectObject::MakeActor(
        actor->GetHandle(), actor->GetID());
    if (!Check(accepts("transform", actorObject)
            && !accepts("materialAsset", actorObject),
            "actor selection routed to the wrong inspector sections")) return false;

    const EditorSelectObject materialObject =
        EditorSelectObject::MakeAsset(materialPath.string());
    if (!Check(accepts("materialAsset", materialObject)
            && !accepts("textureAsset", materialObject)
            && !accepts("genericAsset", materialObject),
            "material selection routing mismatch")) return false;

    const EditorSelectObject textureObject =
        EditorSelectObject::MakeAsset(texturePath.string());
    if (!Check(accepts("textureAsset", textureObject)
            && !accepts("materialAsset", textureObject)
            && !accepts("genericAsset", textureObject),
            "texture selection routing mismatch")) return false;

    const EditorSelectObject modelObject =
        EditorSelectObject::MakeAsset(modelPath.string());
    const bool modelRouted = accepts("genericAsset", modelObject)
        && !accepts("materialAsset", modelObject)
        && !accepts("textureAsset", modelObject);
    const EditorSelectObject jsonObject =
        EditorSelectObject::MakeAsset(jsonPath.string());
    const bool jsonRouted = accepts("genericAsset", jsonObject)
        && !accepts("materialAsset", jsonObject)
        && !accepts("textureAsset", jsonObject);
    std::filesystem::remove_all(root, error);
    const EditorSelectObject audioObject =
        EditorSelectObject::MakeAsset(audioPath.string());
    const bool audioRouted = accepts("genericAsset", audioObject)
        && !accepts("materialAsset", audioObject)
        && !accepts("textureAsset", audioObject);
    return Check(modelRouted && jsonRouted && audioRouted,
                 "generic asset selection routing mismatch");
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
        if (!file) continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        break;
    }
    if (!Check(!source.empty(), "InspectorPanel source was not found")) return false;
    if (!Check(source.find("ShouldCaptureInspectorEditSnapshot") != std::string::npos,
               "InspectorPanel does not gate edit snapshots behind input state")) return false;
    if (!Check(source.find("const std::string before = actor ? SceneSerializer::SaveToString(*scene)") ==
                   std::string::npos,
               "InspectorPanel still snapshots the whole scene for idle actor selection")) return false;
    if (!Check(source.find("captureInspectorSnapshot") != std::string::npos &&
               source.find("SceneSerializer::SaveToString(*scene) : std::string{}") != std::string::npos,
               "InspectorPanel snapshot capture is not guarded by an explicit gate")) return false;
    return Check(source.find("m_Transaction.Commit(*context)") != std::string::npos,
                 "InspectorPanel no longer commits inspector edit transactions");
}

bool TestEditorProfilerBufferAndSourceContracts() {
    EditorProfiler profiler(2);
    profiler.RecordEvent("Editor", "First", 1.0, "a", 1);
    profiler.RecordEvent("Editor", "Second", 2.0, "b", 2);
    profiler.RecordEvent("Runtime", "Third", 3.0, "c", 3);
    auto events = profiler.Snapshot();
    if (!Check(events.size() == 2 &&
               events[0].name == "Second" &&
               events[1].name == "Third",
               "EditorProfiler did not keep the newest events")) return false;
    profiler.SetEnabled(false);
    profiler.RecordEvent("Editor", "Paused", 4.0);
    if (!Check(profiler.Snapshot().size() == 2,
               "EditorProfiler recorded events while disabled")) return false;
    profiler.Clear();
    if (!Check(profiler.Snapshot().empty(),
               "EditorProfiler clear did not remove events")) return false;

    const auto readSource = [](const char* relativePath) {
        const char* prefixes[] = {"", "../../../", "../../../../", "../../../../../"};
        for (const char* prefix : prefixes) {
            std::ifstream file(std::string(prefix) + relativePath, std::ios::binary);
            if (!file) continue;
            std::ostringstream contents;
            contents << file.rdbuf();
            return contents.str();
        }
        return std::string{};
    };
    const std::string editorLayer = readSource("src/Editor/EditorLayer.cpp");
    const std::string assetRegistry = readSource("src/Editor/EditorAssetRegistry.cpp");
    const std::string sceneSerializer = readSource("src/Runtime/Scene/SceneSerializer.cpp");
    if (!Check(!editorLayer.empty() && !assetRegistry.empty() && !sceneSerializer.empty(),
               "profiler source contract files were not found")) return false;
    if (!Check(editorLayer.find("ProfilerPanel") != std::string::npos &&
               editorLayer.find("RecordEvent(\"Editor\", \"OpenProject\"") != std::string::npos &&
               editorLayer.find("RecordEvent(\"Editor\", \"Scene Load\"") != std::string::npos,
               "EditorLayer does not register profiler panel and editor load events")) return false;
    if (!Check(assetRegistry.find("RecordEvent(\"Editor\", \"AssetRegistry Refresh\"") != std::string::npos &&
               assetRegistry.find("Logger::Info(\"[EditorAssetRegistry] Refresh") == std::string::npos,
               "EditorAssetRegistry refresh timing still logs or is not profiled")) return false;
    return Check(sceneSerializer.find("EditorProfiler") == std::string::npos &&
                 sceneSerializer.find("loadMs=") == std::string::npos,
                 "SceneSerializer should not depend on editor profiler or log load timing");
}

bool TestEditorLayoutConfigAndStatePersistence() {
    namespace fs = std::filesystem;
    EditorLayoutConfig config = EditorLayoutConfig::CreateDefault();
    std::string error;
    if (!Check(config.Validate(&error), "default editor layout invalid: " + error)) return false;

    std::unordered_set<std::string> ids;
    for (const auto& panel : config.panels) ids.insert(panel.panelID);
    for (const char* required : {"toolbar", "sceneHierarchy", "viewport", "gameViewport",
                                 "inspector", "assetBrowser", "log", "profiler"}) {
        if (!Check(ids.count(required) == 1,
                   std::string("default layout missing panel: ") + required)) return false;
    }

    const auto root = fs::temp_directory_path() /
        ("myengine_editor_layout_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const fs::path layoutPath = root / "Config" / "EditorLayout.default.json";
    if (!Check(EditorLayoutConfig::SaveToFile(layoutPath, config, &error),
               "default layout save failed: " + error)) return false;
    EditorLayoutConfig loaded;
    if (!Check(EditorLayoutConfig::LoadFromFile(layoutPath, loaded, &error),
               "default layout load failed: " + error)) return false;
    if (!Check(loaded.panels.size() == config.panels.size(),
               "default layout round trip changed panel count")) return false;

    const fs::path invalidPath = root / "Config" / "InvalidLayout.json";
    std::ofstream(invalidPath) << R"({"version":1,"panels":[{"id":"toolbar","area":"top"}]})";
    EditorLayoutConfig invalid;
    if (!Check(!EditorLayoutConfig::LoadFromFile(invalidPath, invalid, &error) &&
               error.find("missing panel id") != std::string::npos,
               "invalid layout was accepted")) return false;

    nlohmann::json legacyState = {
        {"lastScenePath", "Content/Scenes/Legacy.scene.json"},
        {"selectedAssetPath", "Content/Models/Legacy.gltf"},
        {"lastOpenDirectory", "Content"},
        {"panels", {
            {"toolbar", true},
            {"sceneHierarchy", false},
            {"viewport", true},
            {"inspector", true},
            {"assetBrowser", false},
            {"log", true},
            {"profiler", true}
        }}
    };
    std::ofstream(root / ".myengine_editor_state.json") << legacyState.dump(2);

    EditorProject project;
    if (!Check(project.Open(root), "layout state project open failed")) return false;
    if (!Check(!project.GetState().IsPanelVisible("sceneHierarchy") &&
               project.GetState().IsPanelVisible("gameViewport") &&
               !project.GetState().IsPanelVisible("assetBrowser") &&
               project.GetState().IsPanelVisible("profiler") &&
               project.GetLastScenePath() == "Content/Scenes/Legacy.scene.json" &&
               project.GetState().selectedAssetPath == "Content/Models/Legacy.gltf",
               "legacy panel visibility migration failed")) return false;

    project.GetState().imguiLayoutIni = "[Window][Scene View###viewport]\nPos=10,10\n";
    project.GetState().SetPanelVisible("log", false);
    if (!Check(project.SaveState(), "layout state save failed")) return false;

    EditorProject reloaded;
    if (!Check(reloaded.Open(root), "layout state reload failed")) return false;
    const bool stateMatches =
        reloaded.GetLastScenePath() == "Content/Scenes/Legacy.scene.json" &&
        reloaded.GetState().selectedAssetPath == "Content/Models/Legacy.gltf" &&
        !reloaded.GetState().IsPanelVisible("sceneHierarchy") &&
        reloaded.GetState().IsPanelVisible("gameViewport") &&
        !reloaded.GetState().IsPanelVisible("assetBrowser") &&
        !reloaded.GetState().IsPanelVisible("log") &&
        reloaded.GetState().IsPanelVisible("profiler") &&
        reloaded.GetState().imguiLayoutIni.find("Scene View###viewport") != std::string::npos;
    std::error_code ec;
    fs::remove_all(root, ec);
    return Check(stateMatches, "layout state persistence mismatch");
}

bool TestEditorProjectAndAssetRegistry() {
    const auto root = std::filesystem::temp_directory_path() /
        ("myengine_editor_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
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
    if (!Check(registry.GetAssets(EditorAssetType::Model).size() == 2,
               "asset registry model classification failed")) return false;
    if (!Check(registry.GetAssets(EditorAssetType::Texture).size() == 1,
               "asset registry texture classification failed")) return false;
    if (!Check(registry.GetAssets(EditorAssetType::Material).size() == 1,
               "asset registry material classification failed")) return false;
    if (!Check(registry.GetAssets(EditorAssetType::Audio).size() == 1,
               "asset registry audio classification failed")) return false;
    if (!Check(registry.GetAssets().size() == 6,
               "asset registry exposed metadata files")) return false;
    const auto folders = registry.GetFolders();
    const auto findFolder = [&](const std::string& path) {
        return std::find_if(folders.begin(), folders.end(),
            [&path](const EditorAssetFolderInfo& info) {
                return info.relativePath == path;
            });
    };
    if (!Check(findFolder("Content") != folders.end() &&
               findFolder("Content/Models") != folders.end() &&
               findFolder("Content/Models/Nested") != folders.end() &&
               findFolder("Content/Empty") != folders.end() &&
               findFolder("SourceAssets") != folders.end() &&
               findFolder("SourceAssets/Raw") != folders.end(),
               "asset registry folder index missed Content or SourceAssets folders")) return false;
    if (!Check(findFolder("Content/Empty")->assetCount == 0 &&
               findFolder("Content/Models")->assetCount == 2 &&
               findFolder("Content/Models")->directAssetCount == 1 &&
               findFolder("Content/Models/Nested")->assetCount == 1 &&
               findFolder("Content/Models/Nested")->directAssetCount == 1 &&
               findFolder("Content")->assetCount == 5 &&
               findFolder("SourceAssets")->assetCount == 1,
               "asset registry folder counts are incorrect")) return false;
    const auto modelFolderAssets = registry.GetAssetsInFolder("Content/Models", true);
    const bool modelFolderContainsRootAsset = std::any_of(
        modelFolderAssets.begin(), modelFolderAssets.end(),
        [](const EditorAssetInfo& info) {
            return info.relativePath == "Models/test.gltf";
        });
    if (!Check(modelFolderAssets.size() == 2 && modelFolderContainsRootAsset,
               "asset registry folder query returned the wrong Content assets")) return false;
    if (!Check(registry.GetAssetsInFolder("SourceAssets", true).size() == 1,
               "asset registry folder query returned the wrong SourceAssets assets")) return false;
    registryProfiler.Clear();
    if (!Check(!registry.WatchForChanges(),
               "asset registry reported a change when the directory snapshot was unchanged")) return false;
    if (!Check(registryProfiler.Snapshot().empty(),
               "asset registry refreshed during an unchanged watch pass")) return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(content / "Models" / "test.gltf", std::ios::app) << " ";
    if (!Check(registry.WatchForChanges(), "asset registry missed mtime change")) return false;

    const auto importSource = root / "source.obj";
    std::ofstream(importSource)
        << "v 0 0 0\n"
        << "v 1 0 0\n"
        << "v 0 1 0\n"
        << "f 1 2 3\n";
    Scene importScene("ImportContext");
    EditorContext importContext(&importScene);
    importContext.SetProjectRoot(root);
    importContext.SetAssetRegistry(&registry);
    EditorImportService importService;
    importService.OnAttach(importContext);
    if (!Check(importService.Import(importSource.string()),
               "editor import service failed first copy")) return false;
    if (!Check(importService.Import(importSource.string()),
               "editor import service failed unique copy")) return false;
    importService.OnDetach();
    if (!Check(std::filesystem::exists(root / "SourceAssets" / "source.obj") &&
               std::filesystem::exists(root / "SourceAssets" / "source_1.obj"),
               "editor import service overwrote existing asset")) return false;

    EditorProject project;
    if (!Check(project.Open(root), "editor project open failed")) return false;
    project.SetLastScenePath("Content/Scenes/test.json");
    project.GetState().selectedAssetPath = "Models/test.gltf";
    project.GetState().showLog = false;
    project.GetState().SetPanelVisible("log", false);
    project.GetState().imguiLayoutIni = "[Window][Log Output###log]\nCollapsed=1\n";
    if (!Check(project.SaveState(), "editor project state save failed")) return false;

    EditorProject loaded;
    if (!Check(loaded.Open(root), "editor project state load failed")) return false;
    const bool stateMatches =
        loaded.GetLastScenePath() == "Content/Scenes/test.json" &&
        loaded.GetState().selectedAssetPath == "Models/test.gltf" &&
        !loaded.GetState().showLog &&
        !loaded.GetState().IsPanelVisible("log") &&
        loaded.GetState().imguiLayoutIni.find("Log Output###log") != std::string::npos;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return Check(stateMatches, "editor project state persistence mismatch");
}

bool TestEditorAssetOperatorCommandsAndWatch() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("myengine_editor_asset_operator_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
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
    context.SetProjectRoot(root);
    context.SetAssetRegistry(&registry);
    context.SetCommandStack(&stack);
    context.SetOperators(&operators);

    float accumulator = 0.0f;
    if (!Check(!operators.Assets().WatchIfDue(context, 0.25f, accumulator),
               "asset operator watched before throttle interval")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(accumulator > 0.0f,
               "asset operator did not accumulate watch delta")) {
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

    if (!Check(operators.Assets().DuplicateAsset(context, materialPath.string()),
               "asset operator duplicate failed")) {
        fs::remove_all(root, error);
        return false;
    }
    const fs::path copyPath = content / "Materials" / "test_Copy.mat";
    if (!Check(fs::exists(copyPath),
               "asset operator duplicate did not create copy")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && !fs::exists(copyPath),
               "asset operator duplicate undo failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Redo(context) && fs::exists(copyPath),
               "asset operator duplicate redo failed")) {
        fs::remove_all(root, error);
        return false;
    }

    if (!Check(operators.Assets().DeleteAsset(context, copyPath.string()),
               "asset operator delete failed")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(!fs::exists(copyPath),
               "asset operator delete did not remove file")) {
        fs::remove_all(root, error);
        return false;
    }
    if (!Check(stack.Undo(context) && fs::exists(copyPath),
               "asset operator delete undo failed")) {
        fs::remove_all(root, error);
        return false;
    }

    fs::remove_all(root, error);
    return true;
}

bool TestEditorPerformanceSourceContracts() {
    const auto readSource = [](const char* relativePath) {
        const char* prefixes[] = {"", "../../../", "../../../../", "../../../../../"};
        for (const char* prefix : prefixes) {
            std::ifstream file(std::string(prefix) + relativePath, std::ios::binary);
            if (!file) continue;
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
    const std::string hierarchyPanel = readSource("src/Editor/Panels/SceneHierarchyPanel.cpp");
    const std::string shaderWatcher = readSource("src/Editor/EditorShaderWatchService.cpp");

    if (!Check(!panelHeader.empty() && !editorLayer.empty() && !assetRegistry.empty() &&
               !assetBrowser.empty() && !sceneLayerHeader.empty() && !sceneLayer.empty() &&
               !viewportPanel.empty() && !hierarchyPanel.empty() && !shaderWatcher.empty(),
               "performance source contract files were not found")) return false;

    if (!Check(panelHeader.find("ShouldUpdateWhenHidden") != std::string::npos &&
               editorLayer.find("panel->IsVisible() || panel->ShouldUpdateWhenHidden()") != std::string::npos,
               "hidden panels are not gated during update")) return false;
    if (!Check(assetRegistry.find("BuildDirectorySnapshot") != std::string::npos &&
               assetRegistry.find("std::vector<EditorAssetInfo> before") == std::string::npos &&
               assetRegistry.find("AccumulateFolderCounts") != std::string::npos,
               "asset registry watch still uses full-list copies or old folder counting")) return false;
    if (!Check(assetBrowser.find("operators->Assets().WatchIfDue") != std::string::npos,
               "asset browser watch is not routed through the throttled asset operator")) return false;
    if (!Check(sceneLayerHeader.find("SetSceneViewportActive") != std::string::npos &&
               sceneLayerHeader.find("SetGameViewportActive") != std::string::npos &&
               sceneLayer.find("if (m_SceneViewportActive)") != std::string::npos &&
               sceneLayer.find("if (m_GameViewportActive)") != std::string::npos &&
               editorLayer.find("SetSceneViewportActive(false)") != std::string::npos &&
               viewportPanel.find("SetSceneViewportActive(true)") != std::string::npos &&
               viewportPanel.find("SetGameViewportActive(true)") != std::string::npos,
               "editor viewport rendering is not controlled by active panel state")) return false;
    if (!Check(hierarchyPanel.find("m_SearchMatches") != std::string::npos &&
               hierarchyPanel.find("RebuildSearchCache") != std::string::npos &&
               hierarchyPanel.find("ActorMatchesFilter") == std::string::npos,
               "scene hierarchy search still recursively rematches per drawn actor")) return false;
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
    const auto modelSource = root / "cube.obj";
    {
        std::ofstream obj(modelSource, std::ios::binary);
        obj << "o Cube\n";
        obj << "v -0.5 -0.5 -0.5\n";
        obj << "v 0.5 -0.5 -0.5\n";
        obj << "v 0.5 0.5 -0.5\n";
        obj << "v -0.5 0.5 -0.5\n";
        obj << "v -0.5 -0.5 0.5\n";
        obj << "v 0.5 -0.5 0.5\n";
        obj << "v 0.5 0.5 0.5\n";
        obj << "v -0.5 0.5 0.5\n";
        obj << "f 1 3 2\n";
        obj << "f 1 4 3\n";
        obj << "f 5 6 7\n";
        obj << "f 5 7 8\n";
        obj << "f 1 2 6\n";
        obj << "f 1 6 5\n";
        obj << "f 4 8 7\n";
        obj << "f 4 7 3\n";
        obj << "f 2 3 7\n";
        obj << "f 2 7 6\n";
        obj << "f 1 5 8\n";
        obj << "f 1 8 4\n";
    }

    AssetImportService imports;
    std::string error;
    if (!Check(imports.OpenProject(root / "Project", &error),
               "asset import project open failed: " + error)) return false;
    const AssetImportReport first = imports.Import(source, "{\"srgb\":true}", &error);
    if (!Check(first.succeeded && !first.cacheHit && !first.record.uuid.empty(),
               "initial asset import failed: " + error)) return false;
    if (!Check(fs::is_regular_file(first.record.sourcePath + ".meta") &&
               fs::is_regular_file(first.record.artifactPath),
               "import did not create metadata and artifact")) return false;

    const AssetImportReport modelImport = imports.Import(modelSource, "{}", &error);
    if (!Check(modelImport.succeeded && !modelImport.cacheHit &&
               modelImport.record.type == "model",
               "model import failed: " + error)) return false;
    const fs::path disabledSidecarPath =
        fs::path(modelImport.record.artifactPath).parent_path() /
        (fs::path(modelImport.record.artifactPath).stem().string() + ".sdfvox.xml");
    if (!Check(!fs::is_regular_file(disabledSidecarPath),
               "model import should not create SDF/voxel XML sidecar while disabled")) return false;

    imports.SetSdfVoxelBakingEnabled(true);
    std::vector<std::string> bakeFailures;
    if (!Check(imports.BakeSdfVoxelForImportedModels(&bakeFailures) == 1 &&
               bakeFailures.empty(),
               "SDF/Voxel bake toggle did not reimport the model")) return false;
    const AssetRecord* bakedRecord = imports.GetDatabase().FindByUuid(modelImport.record.uuid);
    if (!Check(bakedRecord != nullptr, "baked model record is missing")) return false;
    const fs::path sidecarPath =
        fs::path(bakedRecord->artifactPath).parent_path() /
        (fs::path(bakedRecord->artifactPath).stem().string() + ".sdfvox.xml");
    if (!Check(fs::is_regular_file(sidecarPath),
               "SDF/Voxel bake toggle did not create XML sidecar")) return false;

    AssetManager::Get().Clear();
    ModelHandle importedModel =
        AssetManager::Get().Load<ModelAsset>(bakedRecord->artifactPath);
    MeshAsset* importedMesh = importedModel ? importedModel->GetMeshPtr() : nullptr;
    if (!Check(importedMesh && importedMesh->HasSdfVoxelData(),
               "imported model mesh did not discover SDF/voxel sidecar")) return false;
    if (!Check(importedMesh->LoadSdfVoxelData(&error) &&
               importedMesh->GetSdfVoxelData() &&
               importedMesh->GetSdfVoxelData()->Valid() &&
               importedMesh->GetSdfVoxelData()->resolution == MeshSdfVoxelData::kMediumResolution,
               "imported model mesh failed to load SDF/voxel sidecar: " + error)) return false;
    const auto sidecarWriteTime = fs::last_write_time(sidecarPath, ec);
    const AssetImportReport cachedModel = imports.Reimport(modelImport.record.uuid, &error);
    if (!Check(cachedModel.succeeded && cachedModel.cacheHit,
               "model reimport did not hit cache after SDF/voxel bake")) return false;
    const auto sidecarWriteTimeAfterCache = fs::last_write_time(sidecarPath, ec);
    if (!Check(sidecarWriteTime == sidecarWriteTimeAfterCache,
               "cached model reimport unexpectedly rewrote SDF/voxel sidecar")) return false;

    const fs::path directContentModel = root / "Project/Content/Direct/direct.obj";
    fs::create_directories(directContentModel.parent_path());
    fs::copy_file(modelSource, directContentModel,
                  fs::copy_options::overwrite_existing, ec);
    bakeFailures.clear();
    const size_t bakedWithDirectContent =
        imports.BakeSdfVoxelForImportedModels(&bakeFailures);
    const fs::path directContentSidecar =
        directContentModel.parent_path() /
        (directContentModel.stem().string() + ".sdfvox.xml");
    if (!Check(bakedWithDirectContent >= 1 && bakeFailures.empty() &&
               fs::is_regular_file(directContentSidecar),
               "SDF/Voxel bake toggle did not discover direct Content model sources")) return false;

    const fs::path directGltf = root / "Project/Content/DirectGltf/tri.gltf";
    const fs::path directGltfBin = directGltf.parent_path() / "tri.bin";
    fs::create_directories(directGltf.parent_path());
    std::vector<uint8_t> gltfBinary;
    auto appendFloat = [&gltfBinary](float value) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        gltfBinary.insert(gltfBinary.end(), bytes, bytes + sizeof(float));
    };
    auto appendU16 = [&gltfBinary](uint16_t value) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        gltfBinary.insert(gltfBinary.end(), bytes, bytes + sizeof(uint16_t));
    };
    auto align4 = [&gltfBinary]() {
        while ((gltfBinary.size() % 4) != 0) gltfBinary.push_back(0);
    };
    const size_t posOffset = gltfBinary.size();
    for (float v : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f}) {
        appendFloat(v);
    }
    const size_t normalOffset = gltfBinary.size();
    for (float v : {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f}) {
        appendFloat(v);
    }
    const size_t indexOffset = gltfBinary.size();
    for (uint16_t v : {0, 1, 2}) appendU16(v);
    align4();
    {
        std::ofstream bin(directGltfBin, std::ios::binary);
        bin.write(reinterpret_cast<const char*>(gltfBinary.data()),
                  static_cast<std::streamsize>(gltfBinary.size()));
    }
    nlohmann::json gltf = {
        {"asset", {{"version", "2.0"}}},
        {"buffers", nlohmann::json::array({{
            {"uri", "tri.bin"},
            {"byteLength", gltfBinary.size()}
        }})},
        {"bufferViews", nlohmann::json::array({
            {{"buffer", 0}, {"byteOffset", posOffset}, {"byteLength", 36}, {"target", 34962}},
            {{"buffer", 0}, {"byteOffset", normalOffset}, {"byteLength", 36}, {"target", 34962}},
            {{"buffer", 0}, {"byteOffset", indexOffset}, {"byteLength", 6}, {"target", 34963}}
        })},
        {"accessors", nlohmann::json::array({
            {{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"},
             {"min", nlohmann::json::array({0, 0, 0})},
             {"max", nlohmann::json::array({1, 1, 0})}},
            {{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
            {{"bufferView", 2}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}}
        })},
        {"meshes", nlohmann::json::array({{
            {"primitives", nlohmann::json::array({{
                {"attributes", {{"POSITION", 0}, {"NORMAL", 1}}},
                {"indices", 2}
            }})}
        }})},
        {"nodes", nlohmann::json::array({{{"mesh", 0}}})},
        {"scenes", nlohmann::json::array({{{"nodes", nlohmann::json::array({0})}}})},
        {"scene", 0}
    };
    std::ofstream(directGltf) << gltf.dump(2);
    bakeFailures.clear();
    if (!Check(imports.BakeSdfVoxelForImportedModels(&bakeFailures) >= 1 &&
               bakeFailures.empty(),
               "SDF/Voxel bake did not process direct Content glTF dependencies")) return false;
    const AssetRecord* gltfRecord =
        imports.GetDatabase().FindBySourcePath(directGltf.generic_string());
    if (!Check(gltfRecord && fs::is_regular_file(
                   fs::path(gltfRecord->artifactPath).parent_path() / "tri.bin"),
               "glTF artifact dependencies were not copied beside the cached artifact")) return false;
    AssetManager::Get().Clear();
    ModelHandle cachedGltfModel =
        AssetManager::Get().Load<ModelAsset>(gltfRecord->artifactPath);
    if (!Check(cachedGltfModel.IsValid() &&
               cachedGltfModel->GetMesh() &&
               cachedGltfModel->GetMesh()->HasSdfVoxelData(),
               "cached glTF artifact failed to load after dependency copy")) return false;

    const AssetImportReport cached = imports.Reimport(first.record.uuid, &error);
    if (!Check(cached.succeeded && cached.cacheHit &&
               cached.record.uuid == first.record.uuid,
               "reimport did not preserve uuid or hit DDC")) return false;

    AssetRecord material;
    material.uuid = "material-uuid";
    material.sourcePath = (root / "Project/Content/Test.mat").generic_string();
    material.type = "material";
    material.dependencies = {first.record.uuid};
    fs::create_directories(root / "Project/Content");
    std::ofstream(material.sourcePath) << "{}";
    if (!Check(imports.GetDatabase().Upsert(material, &error) &&
               imports.GetDatabase().Save(&error),
               "asset database dependency update failed: " + error)) return false;
    if (!Check(imports.GetDatabase().GetReferencers(first.record.uuid).size() == 1,
               "asset database reverse dependency lookup failed")) return false;

    AssetDatabase reopened;
    if (!Check(reopened.Open(root / "Project/.myengine/AssetDatabase.json", &error),
               "asset database reload failed: " + error)) return false;
    const AssetRecord* restored = reopened.FindByUuid(first.record.uuid);
    const bool valid = restored && restored->artifactHash == first.record.artifactHash &&
        reopened.GetReferencers(first.record.uuid).size() == 1;
    if (!Check(valid, "asset database round trip lost identity or dependencies")) return false;

    AssetDatabaseValidationReport validation;
    if (!Check(reopened.ValidateAgainstProject(root / "Project", validation),
               "valid asset database failed project validation: " + validation.Summary())) return false;

    std::ofstream(first.record.artifactPath, std::ios::binary | std::ios::trunc) << "tampered";
    reopened.ValidateAgainstProject(root / "Project", validation);
    const bool detectedHashMismatch = std::any_of(
        validation.issues.begin(), validation.issues.end(),
        [](const AssetDatabaseValidationIssue& issue) {
            return issue.code == AssetDatabaseValidationIssueCode::ArtifactHashMismatch;
        });
    if (!Check(detectedHashMismatch,
               "asset database validation missed an artifact hash mismatch")) return false;

    fs::remove(first.record.sourcePath);
    const AssetImportReport missing = imports.Reimport(first.record.uuid, &error);
    if (!Check(!missing.succeeded &&
               missing.record.state == AssetImportState::MissingSource &&
               missing.record.artifactPath == first.record.artifactPath,
               "failed reimport did not retain previous artifact record")) return false;

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
    if (!Check(imports.GetDatabase().Upsert(cycleA, &error) &&
               imports.GetDatabase().Upsert(cycleB, &error),
               "failed to add cycle validation records: " + error)) return false;
    imports.GetDatabase().ValidateAgainstProject(root / "Project", validation);
    const bool detectedCycle = std::any_of(
        validation.issues.begin(), validation.issues.end(),
        [](const AssetDatabaseValidationIssue& issue) {
            return issue.code == AssetDatabaseValidationIssueCode::DependencyCycle;
        });
    if (!Check(detectedCycle, "asset database validation missed a dependency cycle")) return false;

    EditorAssetRegistry registry;
    registry.SetRoot(root / "Project/Content");
    registry.Refresh();
    const auto assets = registry.GetAssets();
    const auto sourceFolderAssets = registry.GetAssetsInFolder("SourceAssets", true);
    const bool registrySawImport = std::any_of(
        assets.begin(), assets.end(),
        [&first](const EditorAssetInfo& info) {
            return info.uuid == first.record.uuid && info.imported &&
                   !info.artifactPath.empty() && !info.diagnostics.empty();
        });
    const bool sourceFolderSawImport = std::any_of(
        sourceFolderAssets.begin(), sourceFolderAssets.end(),
        [&first](const EditorAssetInfo& info) {
            return info.uuid == first.record.uuid && info.imported &&
                   !info.diagnostics.empty();
        });
    if (!Check(registrySawImport,
               "asset registry did not surface database import state or diagnostics")) return false;
    if (!Check(sourceFolderSawImport,
               "asset registry folder query did not surface SourceAssets diagnostics")) return false;

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
            if (panel.id == id) return &panel;
        }
        return nullptr;
    };
    const EditorScriptPanelSpec* engineTool = findPanel("engineTool");
    if (!Check(engineTool && engineTool->title == "Project Tool Override" &&
               engineTool->callback == "DrawProjectTool",
               "project script did not override non-core tool panel by id")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(findPanel("toolbar") == nullptr && findPanel("log") == nullptr,
               "core panel ids were accepted as scripted tool panels")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(findPanel("custom") != nullptr,
               "project script did not append custom panel")) {
        fs::remove_all(root, ec);
        return false;
    }

    {
        std::ofstream script(projectScripts / "Broken.as");
        script << "void RegisterEditor(EditorRegistry@ registry) { broken script";
    }
    if (!Check(!domain.ReloadIfChanged(&error),
               "invalid editor script reload unexpectedly succeeded")) {
        fs::remove_all(root, ec);
        return false;
    }
    engineTool = findPanel("engineTool");
    const bool registryPreserved = engineTool && engineTool->title == "Project Tool Override" &&
                                   findPanel("custom") != nullptr;
    fs::remove_all(root, ec);
    return Check(registryPreserved,
                 "failed editor script reload did not preserve last valid registry");
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

    const std::string* toolbarBody =
        domain.GetRegistry().FindPanelBodyCallback("toolbar");
    if (!Check(toolbarBody && *toolbarBody == "DrawEngineToolbar",
               "project script overrode core toolbar PanelBody despite policy")) {
        fs::remove_all(root, ec);
        return false;
    }

    const bool hasProjectAppend = std::any_of(
        domain.GetRegistry().GetPanels().begin(),
        domain.GetRegistry().GetPanels().end(),
        [](const EditorScriptPanelSpec& panel) {
            return panel.id == "customPanel" && panel.callback == "DrawCustom";
        });
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

    if (!Check(domain.ReloadIfChanged(&error),
               "editor binding facade reload failed: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(domain.ExecutePanelBody("toolbar", context, &error),
               "editor binding facade reloaded script failed: " + error)) {
        fs::remove_all(root, ec);
        return false;
    }

    const bool statePersisted =
        context.GetSelection().GetAssetPath() ==
        std::filesystem::path("Content/StateKept.asset").generic_string();
    fs::remove_all(root, ec);
    return Check(statePersisted,
                 "PanelState did not persist across editor script reload");
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
    const bool hasProjectTool = std::any_of(
        registry.GetPanels().begin(), registry.GetPanels().end(),
        [](const EditorScriptPanelSpec& panel) {
            return panel.id == "projectTool" && panel.callback == "DrawProjectTool";
        });
    const bool rejectedCoreTool = std::none_of(
        registry.GetPanels().begin(), registry.GetPanels().end(),
        [](const EditorScriptPanelSpec& panel) {
            return panel.id == "assetBrowser";
        });
    if (!Check(hasProjectTool && rejectedCoreTool,
               "project ToolPanel registration did not preserve core panel boundary")) {
        fs::remove_all(root, ec);
        return false;
    }
    if (!Check(registry.GetMenus().size() == 1 &&
               registry.GetToolbarItems().size() == 1 &&
               registry.GetInspectors().size() == 1 &&
               registry.GetAssetContextMenus().size() == 1 &&
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
        context.GetSelection().GetAssetPath() ==
        std::filesystem::path("Content/ProjectTool.asset").generic_string();
    fs::remove_all(root, ec);
    return Check(executed, "scripted extension callback did not run through facade");
}

bool TestDefaultEditorScriptCompilesAndRegistersToolbarBody() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "EngineContent/Editor/Scripts/DefaultEditor.as")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path()) break;
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
    if (!Check(domain.Load(root / "EngineContent/Editor/Scripts",
                           root / "Content/Editor/Scripts", &error),
               "default editor AngelScript failed to compile: " + error)) {
        return false;
    }
    const std::string* toolbarBody =
        domain.GetRegistry().FindPanelBodyCallback("toolbar");
    return Check(toolbarBody && *toolbarBody == "DrawToolbar",
                 "default editor script did not register toolbar PanelBody");
}

bool TestEditorUIFacadeDoesNotExposeRawImGuiContract() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "src/Editor/EditorUI/EditorUIFacade.cpp")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path()) break;
            current = current.parent_path();
        }
        return std::filesystem::path{};
    };

    const std::filesystem::path root = findRepositoryRoot();
    std::ifstream source(root / "src/Editor/EditorUI/EditorUIFacade.cpp", std::ios::binary);
    if (!Check(static_cast<bool>(source), "failed to open EditorUIFacade source")) return false;
    std::stringstream buffer;
    buffer << source.rdbuf();
    const std::string text = buffer.str();
    if (!Check(text.find("SetDefaultNamespace(\"UI\")") != std::string::npos,
               "UI facade does not register the UI namespace")) return false;
    if (!Check(text.find("SetDefaultNamespace(\"ImGui\")") == std::string::npos,
               "UI facade exposes raw ImGui namespace to scripts")) return false;
    if (!Check(text.find("ImGui::") != std::string::npos,
               "UI facade source contract test did not inspect ImGui wrapper usage")) return false;
    if (!Check(text.find("SetDefaultNamespace(\"PanelState\")") != std::string::npos &&
               text.find("SetDefaultNamespace(\"Hierarchy\")") != std::string::npos &&
               text.find("SetDefaultNamespace(\"AssetBrowser\")") != std::string::npos &&
               text.find("SetDefaultNamespace(\"DragDrop\")") != std::string::npos &&
               text.find("SetDefaultNamespace(\"Transaction\")") != std::string::npos,
               "UI facade does not expose the editor binding namespaces")) return false;
    if (!Check(text.find("ExecuteCommand(") == std::string::npos &&
               text.find("WatchForChanges(") == std::string::npos &&
               text.find("GetSelection().Select") == std::string::npos,
               "AS facade mutating bindings bypass editor operators")) return false;
    return true;
}

bool TestEditorLayerKeepsCppPanelsWithScriptSidecar() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "src/Editor/EditorLayer.cpp")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path()) break;
            current = current.parent_path();
        }
        return std::filesystem::path{};
    };

    const std::filesystem::path root = findRepositoryRoot();
    std::ifstream source(root / "src/Editor/EditorLayer.cpp", std::ios::binary);
    if (!Check(static_cast<bool>(source), "failed to open EditorLayer source")) return false;
    std::stringstream buffer;
    buffer << source.rdbuf();
    const std::string text = buffer.str();
    if (!Check(text.find("std::make_unique<ToolbarPanel>()") != std::string::npos,
               "EditorLayer no longer creates the native toolbar panel")) return false;
    if (!Check(text.find("std::make_unique<SceneViewportPanel>(gizmo)") != std::string::npos,
               "EditorLayer no longer creates the native scene viewport panel")) return false;
    if (!Check(text.find("std::make_unique<GameViewportPanel>()") != std::string::npos,
               "EditorLayer no longer creates the native game viewport panel")) return false;
    if (!Check(text.find("std::make_unique<SceneHierarchyPanel>()") != std::string::npos,
               "EditorLayer no longer creates the native hierarchy panel")) return false;
    if (!Check(text.find("std::make_unique<InspectorPanel>(gizmo)") != std::string::npos,
               "EditorLayer no longer creates the native inspector panel")) return false;
    if (!Check(text.find("std::make_unique<AssetBrowserPanel>()") != std::string::npos,
               "EditorLayer no longer creates the native asset browser panel")) return false;
    if (!Check(text.find("ScriptedEditorPanel") == std::string::npos,
               "EditorLayer must not replace native panels with scripted dock panels")) return false;
    if (!Check(text.find("std::make_unique<ScriptedToolPanel>") != std::string::npos,
               "EditorLayer does not append scripted tool panels for extensions")) return false;
    if (!Check(text.find("Sidecar domain") != std::string::npos,
               "EditorLayer script domain is not documented as sidecar-only")) return false;
    return true;
}

bool TestEditorOperatorSourceContracts() {
    auto findRepositoryRoot = [] {
        std::filesystem::path current = std::filesystem::current_path();
        for (;;) {
            if (std::filesystem::exists(current / "xmake.lua") &&
                std::filesystem::exists(current / "src/Editor/EditorOperators.cpp")) {
                return current;
            }
            if (!current.has_parent_path() || current == current.parent_path()) break;
            current = current.parent_path();
        }
        return std::filesystem::path{};
    };
    const auto readSource = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    };

    const std::filesystem::path root = findRepositoryRoot();
    const std::string contextHeader = readSource(root / "src/Editor/EditorContext.h");
    const std::string operatorsSource = readSource(root / "src/Editor/EditorOperators.cpp");
    const std::string assetBrowser = readSource(root / "src/Editor/Panels/AssetBrowserPanel.cpp");
    const std::string uiFacade = readSource(root / "src/Editor/EditorUI/EditorUIFacade.cpp");
    if (!Check(!contextHeader.empty() && !operatorsSource.empty() &&
               !assetBrowser.empty() && !uiFacade.empty(),
               "operator source contract files were not found")) return false;
    if (!Check(contextHeader.find("GetOperators()") != std::string::npos,
               "EditorContext does not expose editor operators")) return false;
    if (!Check(operatorsSource.find("EditorCommandStack") != std::string::npos &&
               operatorsSource.find("WatchForChanges()") != std::string::npos &&
               operatorsSource.find("EditorSelectionOperator::SelectActor") != std::string::npos,
               "EditorOperators does not centralize command/watch/selection behavior")) return false;
    if (!Check(assetBrowser.find("registry->WatchForChanges()") == std::string::npos &&
               assetBrowser.find("operators->Assets().WatchIfDue") != std::string::npos &&
               assetBrowser.find("operators->Selection().SelectAsset") != std::string::npos,
               "AssetBrowser still bypasses operator watch or selection paths")) return false;
    if (!Check(uiFacade.find("SetDefaultNamespace(\"Commands\")") != std::string::npos &&
               uiFacade.find("operators->Commands().CreateActor") != std::string::npos &&
               uiFacade.find("context->GetSelection().SelectActorID(actorID)") == std::string::npos,
               "AngelScript facade does not route command/selection bindings through operators")) {
        return false;
    }
    return true;
}

MYENGINE_REGISTER_TEST("Editor", "TestEditorCommandStackAndSelection", TestEditorCommandStackAndSelection);
MYENGINE_REGISTER_TEST("Editor", "TestEditorOperatorsSelectionAndCommands", TestEditorOperatorsSelectionAndCommands);
MYENGINE_REGISTER_TEST("Editor", "TestEditorSelectObjectEvents", TestEditorSelectObjectEvents);
MYENGINE_REGISTER_TEST("Editor", "TestEditorSceneSnapshotCommands", TestEditorSceneSnapshotCommands);
MYENGINE_REGISTER_TEST("Editor", "TestEditorMoveActorCommandUndoRedo", TestEditorMoveActorCommandUndoRedo);
MYENGINE_REGISTER_TEST("Editor", "TestEditorContextWorldRouting", TestEditorContextWorldRouting);
MYENGINE_REGISTER_TEST("Editor", "TestEditorGizmoRowVectorLocalConversion", TestEditorGizmoRowVectorLocalConversion);
MYENGINE_REGISTER_TEST("Editor", "TestEditorServiceActionAndInspectorRegistries", TestEditorServiceActionAndInspectorRegistries);
MYENGINE_REGISTER_TEST("Editor", "TestEditorShortcutMapAndWorkspacePersistence", TestEditorShortcutMapAndWorkspacePersistence);
MYENGINE_REGISTER_TEST("Editor", "TestEditorAppearancePreferencesAndScale", TestEditorAppearancePreferencesAndScale);
MYENGINE_REGISTER_TEST("Editor", "TestEditorStatusBarTextAndActionRouting", TestEditorStatusBarTextAndActionRouting);
MYENGINE_REGISTER_TEST("Editor", "TestEditorInspectorSelectionRouting", TestEditorInspectorSelectionRouting);
MYENGINE_REGISTER_TEST("Editor", "TestInspectorPanelSnapshotsAreInputGated", TestInspectorPanelSnapshotsAreInputGated);
MYENGINE_REGISTER_TEST("Editor", "TestEditorProfilerBufferAndSourceContracts", TestEditorProfilerBufferAndSourceContracts);
MYENGINE_REGISTER_TEST("Editor", "TestEditorLayoutConfigAndStatePersistence", TestEditorLayoutConfigAndStatePersistence);
MYENGINE_REGISTER_TEST("Editor", "TestEditorProjectAndAssetRegistry", TestEditorProjectAndAssetRegistry);
MYENGINE_REGISTER_TEST("Editor", "TestEditorAssetOperatorCommandsAndWatch", TestEditorAssetOperatorCommandsAndWatch);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPerformanceSourceContracts", TestEditorPerformanceSourceContracts);
MYENGINE_REGISTER_TEST("Editor", "TestProductionAssetDatabaseAndImportPipeline", TestProductionAssetDatabaseAndImportPipeline);
MYENGINE_REGISTER_TEST("Editor", "TestEditorAngelScriptDomainRegistryOverrideAndRollback", TestEditorAngelScriptDomainRegistryOverrideAndRollback);
MYENGINE_REGISTER_TEST("Editor", "TestEditorPanelBodyRegistrationAndProjectCoreOverridePolicy", TestEditorPanelBodyRegistrationAndProjectCoreOverridePolicy);
MYENGINE_REGISTER_TEST("Editor", "TestEditorScriptBindingsAndPanelStatePersistence", TestEditorScriptBindingsAndPanelStatePersistence);
MYENGINE_REGISTER_TEST("Editor", "TestEditorScriptExtensionPointRegistryAndExecution", TestEditorScriptExtensionPointRegistryAndExecution);
MYENGINE_REGISTER_TEST("Editor", "TestDefaultEditorScriptCompilesAndRegistersToolbarBody", TestDefaultEditorScriptCompilesAndRegistersToolbarBody);
MYENGINE_REGISTER_TEST("Editor", "TestEditorUIFacadeDoesNotExposeRawImGuiContract", TestEditorUIFacadeDoesNotExposeRawImGuiContract);
MYENGINE_REGISTER_TEST("Editor", "TestEditorLayerKeepsCppPanelsWithScriptSidecar", TestEditorLayerKeepsCppPanelsWithScriptSidecar);
MYENGINE_REGISTER_TEST("Editor", "TestEditorOperatorSourceContracts", TestEditorOperatorSourceContracts);

} // namespace
