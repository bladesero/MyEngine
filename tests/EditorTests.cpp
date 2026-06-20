#include "TestHarness.h"

#include "Editor/EditorAction.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorService.h"
#include "Editor/EditorViewportControllers.h"
#include "Physics/BoxColliderComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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
        bool CanDraw(const EditorSelection& selection) const override {
            return selection.HasActor();
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
    if (!Check(!sections.GetSections()[0]->CanDraw(selection),
               "inspector section filtering mismatch")) return false;
    Actor* selected = scene.CreateActor("SectionSelection");
    selection.SelectActorID(selected->GetID());
    return Check(sections.GetSections()[0]->CanDraw(selection),
                 "inspector section did not accept actor selection");
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

    EditorAssetRegistry registry;
    registry.SetRoot(content);
    registry.Refresh();
    if (!Check(registry.GetAssets(EditorAssetType::Model).size() == 1,
               "asset registry model classification failed")) return false;
    if (!Check(registry.GetAssets(EditorAssetType::Texture).size() == 1,
               "asset registry texture classification failed")) return false;
    if (!Check(registry.GetAssets(EditorAssetType::Material).size() == 1,
               "asset registry material classification failed")) return false;

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
    if (!Check(std::filesystem::exists(content / "Models" / "source.obj") &&
               std::filesystem::exists(content / "Models" / "source_1.obj"),
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

MYENGINE_REGISTER_TEST("Editor", "TestEditorCommandStackAndSelection", TestEditorCommandStackAndSelection);
MYENGINE_REGISTER_TEST("Editor", "TestEditorSceneSnapshotCommands", TestEditorSceneSnapshotCommands);
MYENGINE_REGISTER_TEST("Editor", "TestEditorGizmoRowVectorLocalConversion", TestEditorGizmoRowVectorLocalConversion);
MYENGINE_REGISTER_TEST("Editor", "TestEditorServiceActionAndInspectorRegistries", TestEditorServiceActionAndInspectorRegistries);
MYENGINE_REGISTER_TEST("Editor", "TestEditorProjectAndAssetRegistry", TestEditorProjectAndAssetRegistry);

} // namespace
