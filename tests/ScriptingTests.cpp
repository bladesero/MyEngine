#include "Assets/AssetManager.h"
#include "Assets/ScriptAsset.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Input/Input.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptProfiler.h"
#include "Scripting/ScriptComponent.h"
#include "Scripting/AngelScriptRuntime.h"
#include "UI/Core/UISystem.h"
#include "TestHarness.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace {

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::filesystem::path FindRepoRoot()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> starts = {fs::current_path(), gExecutableDirectory};
    for (const fs::path& start : starts) {
        for (fs::path cursor = fs::absolute(start); !cursor.empty(); cursor = cursor.parent_path()) {
            if (fs::is_regular_file(cursor / "xmake.lua") &&
                fs::is_regular_file(cursor / "src" / "Runtime" / "Scripting" / "AngelScriptRuntime.cpp")) {
                return cursor;
            }
            if (cursor == cursor.parent_path()) break;
        }
    }
    return fs::current_path();
}

bool WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << text;
    return true;
}

bool TestAngelScriptValueTypeSourceContracts()
{
    const std::filesystem::path scriptingRoot =
        FindRepoRoot() / "src" / "Runtime" / "Scripting";
    std::string source = ReadText(scriptingRoot / "AngelScriptRuntime.cpp");
    const std::filesystem::path bindingsRoot = scriptingRoot / "Bindings";
    for (const char* bindingFile : {
             "AngelScriptActorTransformBindings.cpp",
             "AngelScriptSceneBindings.cpp",
             "AngelScriptPhysicsBindings.cpp",
             "AngelScriptGameplayBindings.cpp",
             "AngelScriptNavigationBindings.cpp",
             "AngelScriptAssetsSaveGameBindings.cpp",
             "AngelScriptDebugProfilerBindings.cpp",
             "AngelScriptInputBindings.cpp",
             "AngelScriptUIBindings.cpp",
         }) {
        source += '\n';
        source += ReadText(bindingsRoot / bindingFile);
    }
    return Check(source.find("ActorHandle CreateActor(const string &in)") != std::string::npos &&
                 source.find("SceneCreateActorGeneric") != std::string::npos &&
                 source.find("asCALL_GENERIC") != std::string::npos &&
                 source.find("ActorHandle InstantiatePrefab") != std::string::npos &&
                 source.find("SceneInstantiatePrefabGeneric") != std::string::npos &&
                 source.find("ActorHandle FindByTag(const string &in)") != std::string::npos &&
                 source.find("SceneFindByTagGeneric") != std::string::npos &&
                 source.find("ActorHandle FindNearestWithComponent") != std::string::npos &&
                 source.find("SceneFindNearestWithComponentGeneric") != std::string::npos &&
                 source.find("#include \"Editor/") == std::string::npos,
                 "ActorHandle-returning AngelScript bindings should use generic wrappers");
}

bool TestAngelScriptIncludesAndDependencyHotReload()
{
    namespace fs = std::filesystem;
    const fs::path project = fs::temp_directory_path() / "myengine_as_include_hot_reload";
    std::error_code ec;
    fs::remove_all(project, ec);
    const fs::path common = project / "Content" / "Scripts" / "Common" / "Shared.as";
    const fs::path main = project / "Content" / "Scripts" / "Main.as";
    if (!WriteText(common, "int SharedValue() { return 3; }\n")) return false;
    if (!WriteText(main,
        "#include \"Content/Scripts/Common/Shared.as\"\n"
        "class Script {\n"
        "  void Update(float dt) { Actor::SetPosition(Vec3(float(SharedValue()), 0, 0)); }\n"
        "}\n")) return false;

    AssetManager::Get().SetProjectRoot(project);
    auto scriptAsset = LoadScriptAssetFromFile(main.string());
    if (!Check(scriptAsset && !scriptAsset->GetClasses().empty() &&
               scriptAsset->GetDependencies().size() == 1 &&
               !scriptAsset->GetDiagnostics().HasErrors(),
               "ScriptAsset should discover classes and include dependencies")) {
        AssetManager::Get().SetProjectRoot({});
        return false;
    }
    Scene scene("IncludeReload");
    Actor* actor = scene.CreateActor("Scripted");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetScriptPath(main.string());
    if (!Check(script->IsCompiled(), "included script should compile: " + script->GetLastError())) {
        AssetManager::Get().SetProjectRoot({});
        return false;
    }
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 3.0f),
               "included helper should drive script result")) {
        AssetManager::Get().SetProjectRoot({});
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    if (!WriteText(common, "int SharedValue() { return 7; }\n")) {
        AssetManager::Get().SetProjectRoot({});
        return false;
    }
    scene.OnUpdate(1.0f / 60.0f);
    scene.OnUpdate(1.0f / 60.0f);
    const bool ok = script->IsCompiled() && NearlyEqual(actor->GetTransform().position.x, 7.0f);
    AssetManager::Get().SetProjectRoot({});
    fs::remove_all(project, ec);
    return Check(ok, "include dependency hot reload should preserve and update runtime script: " + script->GetLastError());
}

bool TestAngelScriptSceneAndComponentFacadeV3()
{
    Scene scene("FacadeV3");
    Actor* target = scene.CreateActor("Target");
    target->GetTransform().position = {1.0f, 0.0f, 0.0f};
    auto* collider = target->AddComponent<SphereColliderComponent>();
    collider->SetLayer(2);
    auto* body = target->AddComponent<RigidBodyComponent>();
    (void)body;
    Actor* walker = scene.CreateActor("Walker");
    auto* controller = walker->AddComponent<CharacterControllerComponent>();
    (void)controller;

    Actor* driver = scene.CreateActor("Driver");
    auto* script = driver->AddComponent<ScriptComponent>();
    script->SetClassName("DriverScript");
    script->SetSource(
        "class DriverScript {\n"
        "  int ok = 0;\n"
        "  void Start() {\n"
        "    ActorHandle target = Scene::FindByName(\"Target\");\n"
        "    ActorHandle walker = Scene::FindByName(\"Walker\");\n"
        "    Collider::SetTrigger(target, true);\n"
        "    Collider::SetLayer(target, 8);\n"
        "    Scene::SetLayer(target, 8);\n"
        "    ActorHandle parent = Scene::CreateActor(\"RuntimeParent\");\n"
        "    Scene::SetParent(target, parent);\n"
        "    RigidBody::SetVelocity(target, Vec3(1, 2, 3));\n"
        "    RigidBody::SetUseGravity(target, false);\n"
        "    CharacterController::Move(walker, Vec3(0, 5, 0));\n"
        "    ActorHandleArray@ withBody = Scene::FindAllWithComponent(\"RigidBody\");\n"
        "    ActorHandleArray@ nearby = Scene::FindInRadius(Vec3(0, 0, 0), 5.0f, 8);\n"
        "    if (withBody.Length() == 1 && nearby.Length() == 1 && Scene::GetLayer(target) == 8 && Collider::IsTrigger(target)) ok = 1;\n"
        "  }\n"
        "}\n");
    if (!Check(script->IsCompiled(), "facade v3 script should compile: " + script->GetLastError())) return false;
    scene.OnUpdate(1.0f / 60.0f);
    scene.OnUpdate(1.0f / 60.0f);

    Actor* parent = scene.FindByName("RuntimeParent");
    const nlohmann::json props = script->GetProperties();
    const bool ok = parent && target->GetParent() == parent &&
        collider->IsTrigger() && collider->GetLayer() == 8 &&
        target->GetComponent<RigidBodyComponent>()->GetVelocity().y > 1.9f &&
        !target->GetComponent<RigidBodyComponent>()->UsesGravity() &&
        NearlyEqual(walker->GetComponent<CharacterControllerComponent>()->GetVelocity().y, 5.0f) &&
        props.value("ok", 0) == 1;
    return Check(ok,
                 "Scene/component facade v3 behavior failed: parent=" + std::string(parent ? "true" : "false") +
                 " parented=" + std::string(parent && target->GetParent() == parent ? "true" : "false") +
                 " trigger=" + std::string(collider->IsTrigger() ? "true" : "false") +
                 " layer=" + std::to_string(collider->GetLayer()) +
                 " velY=" + std::to_string(target->GetComponent<RigidBodyComponent>()->GetVelocity().y) +
                 " gravity=" + std::string(target->GetComponent<RigidBodyComponent>()->UsesGravity() ? "true" : "false") +
                 " ccY=" + std::to_string(walker->GetComponent<CharacterControllerComponent>()->GetVelocity().y) +
                 " okProp=" + std::to_string(props.value("ok", 0)) +
                 " lastError=" + script->GetLastError());
}

bool TestAngelScriptSaveDataTaskAndDebug()
{
    namespace fs = std::filesystem;
    const fs::path project = fs::temp_directory_path() / "myengine_as_savedata_task";
    std::error_code ec;
    fs::remove_all(project, ec);
    fs::create_directories(project / "Content");
    AssetManager::Get().SetProjectRoot(project);

    Scene scene("SaveTask");
    Actor* actor = scene.CreateActor("Worker");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetSource(
        "class Script {\n"
        "  int fired = 0;\n"
        "  void Start() {\n"
        "    Debug::Log(\"save task test\");\n"
        "    SaveData::WriteJson(\"profile/state.json\", \"{\\\"score\\\":12}\");\n"
        "    Task::Delay(0.01f, \"OnDelay\");\n"
        "  }\n"
        "  void OnDelay() {\n"
        "    string json = SaveData::ReadJson(\"profile/state.json\");\n"
        "    if (SaveData::Exists(\"profile/state.json\") && json != \"{}\" && !SaveData::WriteJson(\"../blocked.json\", \"{}\")) fired = 1;\n"
        "  }\n"
        "  void Update(float dt) { if (fired == 1) Actor::SetPosition(Vec3(9, 0, 0)); }\n"
        "}\n");
    if (!Check(script->IsCompiled(), "SaveData/Task script should compile: " + script->GetLastError())) {
        AssetManager::Get().SetProjectRoot({});
        return false;
    }
    scene.OnUpdate(1.0f / 60.0f);
    scene.OnUpdate(1.0f / 60.0f);
    scene.OnUpdate(1.0f / 60.0f);
    const bool ok = NearlyEqual(actor->GetTransform().position.x, 9.0f) &&
        fs::is_regular_file(project / "Saved" / "ScriptData" / "profile" / "state.json") &&
        !fs::exists(project / "Saved" / "blocked.json");
    AssetManager::Get().SetProjectRoot({});
    fs::remove_all(project, ec);
    return Check(ok, "SaveData/Task/Debug scripting behavior failed: " + script->GetLastError());
}

bool TestAngelScriptSceneTagsTransformProfilerAndDiagnostics()
{
    ScriptProfiler::Reset();
    Scene scene("TagsTransform");
    Actor* driver = scene.CreateActor("Driver");
    auto* script = driver->AddComponent<ScriptComponent>();
    script->SetSource(
        "class Script {\n"
        "  int ok = 0;\n"
        "  void Start() {\n"
        "    ActorHandle self = Scene::GetSelf();\n"
        "    Tags::Set(\"player\");\n"
        "    Scene::SetLayer(self, 3);\n"
        "    Transform::SetScale(Vec3(2, 3, 4));\n"
        "    ActorHandle target = Scene::CreateActor(\"Target\");\n"
        "    Scene::SetTag(target, \"enemy\");\n"
        "    Scene::SetLayer(target, 7);\n"
        "    Components::Add(target, \"SphereCollider\");\n"
        "  }\n"
        "  void Update(float dt) {\n"
        "    ActorHandle self = Scene::GetSelf();\n"
        "    ActorHandle target = Scene::FindByTag(\"enemy\");\n"
        "    if (target.IsValid()) Transform::SetPosition(target, Vec3(5, 0, 0));\n"
        "    ActorHandle nearest = Scene::FindNearestWithComponent(\"SphereCollider\", Vec3(0, 0, 0), 10.0f);\n"
        "    ActorHandleArray@ enemies = Scene::FindAllByTag(\"enemy\");\n"
        "    ActorHandleArray@ layerActors = Scene::FindAllInLayer(7);\n"
        "    string stats = Profiler::GetScriptStatsJson();\n"
        "    string resources = Resources::GetStatsJson();\n"
        "    Debug::Log(\"gameplay\", \"tag transform test\");\n"
        "    Debug::LogOnce(\"tag-once\", \"once\");\n"
        "    Debug::LogThrottle(\"tag-throttle\", \"throttle\", 10.0f);\n"
        "    if (target.IsValid() && nearest == target && enemies.Length() == 1 && layerActors.Length() == 1 &&\n"
        "        Tags::Has(\"player\") && Tags::Get(target) == \"enemy\" && Scene::GetLayer(target) == 7 &&\n"
        "        Transform::GetScale(self).y == 3 && Scene::GetDistance(self, target) > 4.9f &&\n"
        "        !AudioListener::SetEnabled(target, true) && !Particle::Play(target) && stats != \"[]\") {\n"
        "      ok = 1;\n"
        "      Scene::DestroyDeferred(target);\n"
        "    }\n"
        "  }\n"
        "}\n");
    if (!Check(script->IsCompiled(), "tag/transform script should compile: " + script->GetLastError())) return false;
    scene.OnUpdate(1.0f / 60.0f);
    scene.OnUpdate(1.0f / 60.0f);
    const nlohmann::json props = script->GetProperties();
    if (!Check(props.value("ok", 0) == 1, "tag/transform script did not reach ok state: " + script->GetLastError())) return false;
    if (!Check(!scene.FindByName("Target"), "DestroyDeferred should remove target after safe-point")) return false;
    if (!Check(driver->GetTag() == "player" && driver->GetLayer() == 3 &&
               NearlyEqual(driver->GetTransform().scale.y, 3.0f),
               "actor tag/layer/transform facade did not mutate driver")) return false;
    const std::string serialized = SceneSerializer::SaveToString(scene);
    Scene loaded("LoadedTagsTransform");
    if (!Check(SceneSerializer::LoadFromString(loaded, serialized), "tag/layer scene roundtrip should load")) return false;
    Actor* loadedDriver = loaded.FindByName("Driver");
    if (!Check(loadedDriver && loadedDriver->GetTag() == "player" && loadedDriver->GetLayer() == 3,
               "tag/layer scene roundtrip failed")) return false;

    ScriptProfiler::Reset();
    return true;
}

bool TestAngelScriptSubtitleFacade()
{
    std::string glyphError;
    if(!Check(Input::LoadGlyphAtlasFromFile(InputGlyphAtlas::DefaultPath,&glyphError),
              "subtitle script test could not load glyph atlas: "+glyphError))return false;
    UISystem ui;
    AngelScriptRuntime::SetUISystem(&ui);
    Scene scene("SubtitleScript");
    Actor* actor=scene.CreateActor("Narrator");
    auto* script=actor->AddComponent<ScriptComponent>();
    script->SetSource(
        "class Script {\n"
        "  int ok = 0;\n"
        "  void Start() {\n"
        "    if (UI::ShowSubtitle(\"intro\", \"Guide\", \"Welcome\", 2.0f, 5)) {\n"
        "      string state = UI::GetSubtitleJson();\n"
        "      Input::SetGlyphLocale(\"zh-CN\");\n"
        "      string glyph = Input::ActionGlyphJson(\"Jump\");\n"
        "      string sourceGlyph = Input::SourceGlyphJson(\"Keyboard/Space\");\n"
        "      if (state != \"{}\" && glyph != \"{}\" && sourceGlyph != \"{}\" && Input::GlyphFamily() != \"\") ok = 1;\n"
        "      UI::ClearSubtitles();\n"
        "    }\n"
        "  }\n"
        "}\n");
    const bool compiled=script->IsCompiled();
    scene.OnUpdate(1.0f/60.0f);
    const nlohmann::json properties=script->GetProperties();
    AngelScriptRuntime::ClearUISystem(&ui);
    return Check(compiled&&properties.value("ok",0)==1&&!ui.GetSubtitleState().visible,
                 "AngelScript subtitle facade did not enqueue, inspect, and clear a cue: "+
                 script->GetLastError());
}

bool TestAngelScriptRuntimeDiagnostics()
{
    Scene scene("Diagnostics");
    Actor* actor = scene.CreateActor("Broken");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetSource(
        "class Script {\n"
        "  void Start() { int denom = 0; int value = 1 / denom; }\n"
        "}\n");
    if (!Check(script->IsCompiled(), "diagnostic script should compile before runtime failure")) return false;
    scene.OnUpdate(1.0f / 60.0f);
    return Check(!script->IsCompiled() &&
                 !script->GetLastError().empty() &&
                 script->GetDiagnostics().HasErrors(),
                 "runtime diagnostics should capture callback exception");
}

MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptValueTypeSourceContracts", TestAngelScriptValueTypeSourceContracts);
MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptIncludesAndDependencyHotReload", TestAngelScriptIncludesAndDependencyHotReload);
MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptSceneAndComponentFacadeV3", TestAngelScriptSceneAndComponentFacadeV3);
MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptSaveDataTaskAndDebug", TestAngelScriptSaveDataTaskAndDebug);
MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptSceneTagsTransformProfilerAndDiagnostics", TestAngelScriptSceneTagsTransformProfilerAndDiagnostics);
MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptSubtitleFacade", TestAngelScriptSubtitleFacade);
MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptRuntimeDiagnostics", TestAngelScriptRuntimeDiagnostics);

} // namespace
