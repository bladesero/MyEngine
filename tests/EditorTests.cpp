#include "TestHarness.h"

#include "Editor/EditorAction.h"
#include "Editor/AssetImportService.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorService.h"
#include "Editor/EditorShortcutMap.h"
#include "Editor/EditorViewportControllers.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/InspectorSections.h"
#include "Physics/BoxColliderComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
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

bool TestEditorInspectorSelectionRouting() {
    const auto root = std::filesystem::temp_directory_path()
        / "myengine_inspector_selection_routing";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    const auto materialPath = root / "Selected.mat";
    const auto texturePath = root / "Selected.png";
    const auto modelPath = root / "Selected.obj";
    const auto jsonPath = root / "Selected.json";
    std::ofstream(materialPath) << "{}";
    std::ofstream(texturePath) << "png";
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
    return Check(modelRouted && jsonRouted,
                 "generic asset selection routing mismatch");
}

bool TestEditorProjectAndAssetRegistry() {
    const auto root = std::filesystem::temp_directory_path() /
        ("myengine_editor_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto content = root / "Content";
    std::filesystem::create_directories(content / "Models");
    std::filesystem::create_directories(content / "Textures");
    std::filesystem::create_directories(content / "Materials");
    std::ofstream(content / "Models" / "test.gltf") << "{}";
    std::ofstream(content / "Textures" / "test.png") << "png";
    std::ofstream(content / "Materials" / "test.mat") << "{}";
    std::ofstream(content / "Models" / "test.gltf.meta") << "{}";

    EditorAssetRegistry registry;
    registry.SetRoot(content);
    registry.Refresh();
    if (!Check(registry.GetAssets(EditorAssetType::Model).size() == 1,
               "asset registry model classification failed")) return false;
    if (!Check(registry.GetAssets(EditorAssetType::Texture).size() == 1,
               "asset registry texture classification failed")) return false;
    if (!Check(registry.GetAssets(EditorAssetType::Material).size() == 1,
               "asset registry material classification failed")) return false;
    if (!Check(registry.GetAssets().size() == 3,
               "asset registry exposed metadata files")) return false;

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
    if (!Check(project.SaveState(), "editor project state save failed")) return false;

    EditorProject loaded;
    if (!Check(loaded.Open(root), "editor project state load failed")) return false;
    const bool stateMatches =
        loaded.GetLastScenePath() == "Content/Scenes/test.json" &&
        loaded.GetState().selectedAssetPath == "Models/test.gltf" &&
        !loaded.GetState().showLog;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return Check(stateMatches, "editor project state persistence mismatch");
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
    if (!Check(imports.OpenProject(root / "Project", &error),
               "asset import project open failed: " + error)) return false;
    const AssetImportReport first = imports.Import(source, "{\"srgb\":true}", &error);
    if (!Check(first.succeeded && !first.cacheHit && !first.record.uuid.empty(),
               "initial asset import failed: " + error)) return false;
    if (!Check(fs::is_regular_file(first.record.sourcePath + ".meta") &&
               fs::is_regular_file(first.record.artifactPath),
               "import did not create metadata and artifact")) return false;

    const AssetImportReport cached = imports.Reimport(first.record.uuid, &error);
    if (!Check(cached.succeeded && cached.cacheHit &&
               cached.record.uuid == first.record.uuid,
               "reimport did not preserve uuid or hit DDC")) return false;

    AssetRecord material;
    material.uuid = "material-uuid";
    material.sourcePath = (root / "Project/Content/Test.mat").generic_string();
    material.type = "material";
    material.dependencies = {first.record.uuid};
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
    fs::remove_all(root, ec);
    return Check(valid, "asset database round trip lost identity or dependencies");
}

MYENGINE_REGISTER_TEST("Editor", "TestEditorCommandStackAndSelection", TestEditorCommandStackAndSelection);
MYENGINE_REGISTER_TEST("Editor", "TestEditorSelectObjectEvents", TestEditorSelectObjectEvents);
MYENGINE_REGISTER_TEST("Editor", "TestEditorSceneSnapshotCommands", TestEditorSceneSnapshotCommands);
MYENGINE_REGISTER_TEST("Editor", "TestEditorGizmoRowVectorLocalConversion", TestEditorGizmoRowVectorLocalConversion);
MYENGINE_REGISTER_TEST("Editor", "TestEditorServiceActionAndInspectorRegistries", TestEditorServiceActionAndInspectorRegistries);
MYENGINE_REGISTER_TEST("Editor", "TestEditorShortcutMapAndWorkspacePersistence", TestEditorShortcutMapAndWorkspacePersistence);
MYENGINE_REGISTER_TEST("Editor", "TestEditorInspectorSelectionRouting", TestEditorInspectorSelectionRouting);
MYENGINE_REGISTER_TEST("Editor", "TestEditorProjectAndAssetRegistry", TestEditorProjectAndAssetRegistry);
MYENGINE_REGISTER_TEST("Editor", "TestProductionAssetDatabaseAndImportPipeline", TestProductionAssetDatabaseAndImportPipeline);

} // namespace
