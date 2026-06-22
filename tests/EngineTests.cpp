#include "Assets/AssetManager.h"
#include "Assets/AssetMeta.h"
#include "Core/Memory/LinearAllocator.h"
#include "Assets/ShaderAsset.h"
#include "Assets/PrefabAsset.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Audio/AudioSourceComponent.h"
#include "Core/Memory/MemoryService.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/CrashHandler.h"
#include "Camera/Camera.h"
#include "Camera/CameraComponent.h"
#include "Game/DefaultSceneFactory.h"
#include "Game/GameViewport.h"
#include "Game/SceneLayer.h"
#include "Game/SceneViewportController.h"
#include "Input/Input.h"
#include "Math/Mat4Inverse.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/CollisionShapes.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Project/PublishTargets.h"
#include "Project/ProjectConfig.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scene/PrefabSystem.h"
#include "Scene/ComponentRegistry.h"
#include "Scripting/ScriptComponent.h"
#include "Assets/ScriptAsset.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/GpuUploadQueue.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorLuaScriptService.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorService.h"
#include "Editor/EditorViewportControllers.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/ProjectPublisher.h"
#include "Editor/CookDependencyGraph.h"
#include "Core/Sha256.h"
#include "Project/CookedProjectCache.h"
#include "Project/CookManifest.h"
#include "Project/ContentArchive.h"
#include "Project/ContentPathPolicy.h"
#include "Project/RuntimeCompatibility.h"
#include "Project/RuntimeDependencies.h"
#include "TestHarness.h"
#include "Miscs/IconsManager.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Render/UIDrawList.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

namespace {

bool TestPublishHardeningPrimitives() {
    Sha256 sha;
    sha.Update("abc", 3);
    if (!Check(Sha256::ToHex(sha.Final()) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
               "SHA-256 known vector mismatch")) return false;

    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "myengine_publish_hardening_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Content/Scenes");
    std::ofstream(root / "outside.bin") << "outside";
    fs::path resolved;
    std::string error;
    if (!Check(!ContentPathPolicy::ResolveContained(root / "Content", "../outside.bin",
                                                    resolved, &error),
               "Content path traversal was accepted")) return false;

    std::ofstream(root / "Content/Scenes/Main.scene.json")
        << R"({"actors":[{"components":[{"data":{"language":"angelscript","scriptPath":"Content/Scripts/missing.as"}}]}]})";
    PublishPreflightReport preflight;
    if (!Check(!CookDependencyGraph::Validate(root, preflight) &&
               !preflight.errors.empty() &&
               preflight.errors.front().code == PublishIssueCode::MissingDependency,
               "publish preflight accepted a missing script dependency")) return false;

    CookManifest manifest;
    manifest.project = "ContractTest";
    manifest.projectId = "project-id";
    manifest.engineVersion = RuntimeCompatibility::kEngineVersion;
    manifest.buildId = RuntimeCompatibility::kBuildId;
    manifest.contentSchemaVersion = RuntimeCompatibility::kContentSchemaVersion;
    manifest.archiveFormatVersion = RuntimeCompatibility::kArchiveFormatVersion;
    manifest.configuration = RuntimeCompatibility::kConfiguration;
    manifest.requiredBackends = {"d3d11", "d3d12"};
    manifest.runtimeDependenciesHash = std::string(64, '0');
    manifest.archiveHash = std::string(64, '1');
    manifest.startupScene = "Content/Scenes/Main.scene.json";
    manifest.files = {{manifest.startupScene, 0, std::string(64, '2')}};
    if (!Check(manifest.Validate(&error), "valid Manifest v2 was rejected: " + error)) return false;
    manifest.version = 1;
    if (!Check(!manifest.Validate(&error) && error.find("unsupported") != std::string::npos,
               "legacy Manifest v1 was not rejected explicitly")) return false;
    manifest.version = CookManifest::kCurrentVersion;
    manifest.requiredBackends = {"d3d11"};
    if (!Check(!manifest.Validate(&error), "manifest with missing D3D12 backend was accepted")) return false;

    fs::create_directories(root / "Runtime");
    const auto dependency = root / "Runtime/test.dll";
    std::ofstream(dependency, std::ios::binary) << "dependency";
    RuntimeDependencyManifest dependencies;
    dependencies.files.push_back({"test.dll", "x64", fs::file_size(dependency),
                                  Sha256::HashFile(dependency, &error)});
    if (!Check(dependencies.ValidateFiles(root / "Runtime", &error),
               "valid runtime dependency was rejected: " + error)) return false;
    std::ofstream(dependency, std::ios::binary | std::ios::app) << "tampered";
    if (!Check(!dependencies.ValidateFiles(root / "Runtime", &error),
               "tampered runtime dependency was accepted")) return false;
    fs::remove_all(root, ec);
    return true;
}

bool TestShaderAssetFormats() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_shader_asset_test";
    std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root);
    auto write = [&](const char* name, const char* json) {
        const fs::path path = root / name; std::ofstream(path) << json; return path;
    };
    const auto graphics = write("Mesh.shader",
        R"({"type":"Shader","version":1,"stages":{"vertex":{"source":"Mesh.hlsl","entry":"VSMain"},"pixel":{"source":"Mesh.hlsl","entry":"PSMain"}},"defines":[]})");
    auto asset = LoadShaderAssetFromFile(graphics.string());
    if (!Check(asset && !asset->IsCooked() && !asset->IsCompute(),
               "valid graphics shader description was rejected")) return false;
    const auto compute = write("Compute.shader",
        R"({"type":"Shader","version":1,"stages":{"compute":{"source":"Compute.hlsl","entry":"CSMain"}},"defines":[]})");
    if (!Check(LoadShaderAssetFromFile(compute.string())->IsCompute(),
               "valid compute shader description was rejected")) return false;
    const char* invalid[] = {
        R"({"type":"Shader","version":2,"stages":{"compute":{"source":"A.hlsl","entry":"CSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"vertex":{"source":"A.hlsl","entry":"VSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"compute":{"source":"../A.hlsl","entry":"CSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"compute":{"source":"C:/A.hlsl","entry":"CSMain"},"pixel":{"source":"A.hlsl","entry":"PSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"compute":{"source":"A.hlsl","entry":"CSMain"},"compute":{"source":"B.hlsl","entry":"CSMain"}}})",
        R"({"type":"Shader","version":1,"stages":{"geometry":{"source":"A.hlsl","entry":"GSMain"}}})"
    };
    for (size_t i = 0; i < std::size(invalid); ++i)
        if (!Check(!LoadShaderAssetFromFile(write(("Invalid" + std::to_string(i) + ".shader").c_str(), invalid[i]).string()),
                   "invalid shader description was accepted")) return false;
    std::array<std::array<std::vector<uint8_t>, 3>, 2> blobs{};
    for (auto& backend : blobs) { backend[0] = {1,2,3}; backend[1] = {4,5}; }
    ShaderAsset cooked(graphics.string());
    cooked.SetCooked(ShaderAsset::kVertexMask | ShaderAsset::kPixelMask, 42, std::move(blobs));
    const fs::path cookedPath = root / "Cooked.shader"; std::string error;
    if (!Check(SaveCookedShaderAsset(cooked, cookedPath, &error), error)) return false;
    auto loaded = LoadShaderAssetFromFile(cookedPath.string());
    if (!Check(loaded && loaded->IsCooked() &&
               loaded->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).size() == 2,
               "cooked shader container round-trip failed")) return false;
    { std::ofstream append(cookedPath, std::ios::binary | std::ios::app); append.put('x'); }
    if (!Check(!LoadShaderAssetFromFile(cookedPath.string()),
               "corrupt shader container was accepted")) return false;
    fs::remove_all(root, ec); return true;
}

bool TestSceneSerializationRegression() {
    AssetManager::Get().Clear();

    Scene scene("SerializeCase");
    Actor* parent = scene.CreateActor("Parent");
    parent->GetTransform().position = Vec3 { 2.0f, 3.0f, 4.0f };

    Actor* child = scene.CreateActor("Child", parent);
    child->GetTransform().position = Vec3 { 1.0f, 0.0f, 0.0f };

    auto* mr = parent->AddComponent<MeshRendererComponent>();
    mr->SetMesh(AssetManager::Get().GetCubeMesh());
    mr->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    auto* script = parent->AddComponent<ScriptComponent>();
    script->SetSource(
        "class Script {\n"
        "  void Update(float dt) { Actor::Rotate(Vec3(0, 90 * dt, 0)); }\n"
        "}\n");
    auto* body = parent->AddComponent<RigidBodyComponent>();
    body->SetVelocity({ 1.0f, 2.0f, 3.0f });
    parent->AddComponent<BoxColliderComponent>();
    auto* light = parent->AddComponent<LightComponent>();
    light->SetLightType(LightType::Spot);
    light->SetColor({ 0.7f, 0.8f, 1.0f });
    light->SetIntensity(4.0f);
    light->SetRange(12.0f);
    light->SetDirection({ 0.0f, -1.0f, 0.0f });
    light->SetInnerConeAngle(18.0f);
    light->SetOuterConeAngle(32.0f);
    auto* post = parent->AddComponent<PostProcessComponent>();
    post->SetExposure(1.4f);
    post->SetGamma(2.0f);
    post->SetToneMappingEnabled(false);
    post->SetAntiAliasingStrength(0.6f);
    post->SetSSAORadius(2.4f);
    post->SetSSAOBias(0.04f);
    post->SetSSAOPower(2.2f);
    post->SetSSAOIntensity(0.75f);

    const std::string json = SceneSerializer::SaveToString(scene);

    Scene loaded("Loaded");
    if (!Check(SceneSerializer::LoadFromString(loaded, json), "LoadFromString failed")) return false;

    if (!Check(loaded.GetName() == "SerializeCase", "scene name mismatch")) return false;
    if (!Check(loaded.ActorCount() == 2, "actor count mismatch")) return false;

    Actor* loadedParent = loaded.FindByName("Parent");
    Actor* loadedChild = loaded.FindByName("Child");
    if (!Check(loadedParent != nullptr, "parent not found")) return false;
    if (!Check(loadedChild != nullptr, "child not found")) return false;
    if (!Check(loadedChild->GetParent() == loadedParent, "child-parent relation mismatch")) return false;

    const Vec3 p = loadedParent->GetTransform().position;
    if (!Check(NearlyEqual(p.x, 2.0f) && NearlyEqual(p.y, 3.0f) && NearlyEqual(p.z, 4.0f),
               "parent transform mismatch")) return false;

    auto* loadedMr = loadedParent->GetComponent<MeshRendererComponent>();
    if (!Check(loadedMr != nullptr, "MeshRenderer missing after deserialize")) return false;
    if (!Check(loadedMr->IsValid(), "MeshRenderer handles are invalid after deserialize")) return false;
    auto* loadedScript = loadedParent->GetComponent<ScriptComponent>();
    if (!Check(loadedScript && loadedScript->IsCompiled(), "Script missing after deserialize")) return false;
    auto* loadedBody = loadedParent->GetComponent<RigidBodyComponent>();
    if (!Check(loadedBody && NearlyEqual(loadedBody->GetVelocity().y, 2.0f),
               "RigidBody missing after deserialize")) return false;
    if (!Check(loadedParent->GetComponent<BoxColliderComponent>() != nullptr,
               "BoxCollider missing after deserialize")) return false;
    auto* loadedLight = loadedParent->GetComponent<LightComponent>();
    if (!Check(loadedLight && loadedLight->GetLightType() == LightType::Spot,
               "Light missing after deserialize")) return false;
    if (!Check(NearlyEqual(loadedLight->GetIntensity(), 4.0f) &&
               NearlyEqual(loadedLight->GetRange(), 12.0f) &&
               NearlyEqual(loadedLight->GetInnerConeAngle(), 18.0f) &&
               NearlyEqual(loadedLight->GetOuterConeAngle(), 32.0f) &&
               NearlyEqual(loadedLight->GetColor().z, 1.0f),
               "Light fields mismatch after deserialize")) return false;
    auto* loadedPost = loadedParent->GetComponent<PostProcessComponent>();
    if (!Check(loadedPost, "PostProcess missing after deserialize")) return false;
    if (!Check(!loadedPost->IsToneMappingEnabled() &&
               NearlyEqual(loadedPost->GetExposure(), 1.4f) &&
               NearlyEqual(loadedPost->GetGamma(), 2.0f) &&
               NearlyEqual(loadedPost->GetAntiAliasingStrength(), 0.6f) &&
               NearlyEqual(loadedPost->GetSSAORadius(), 2.4f) &&
               NearlyEqual(loadedPost->GetSSAOBias(), 0.04f) &&
               NearlyEqual(loadedPost->GetSSAOPower(), 2.2f) &&
               NearlyEqual(loadedPost->GetSSAOIntensity(), 0.75f),
               "PostProcess fields mismatch after deserialize")) return false;

    return true;
}

bool TestBuiltinSceneMaterialRoundTrip() {
    AssetManager::Get().Clear();

    Scene scene("BuiltinMaterialScene");
    Actor* actor = scene.CreateActor("BuiltinMaterialActor");
    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    renderer->SetMesh(AssetManager::Get().GetCubeMesh());

    auto material = MaterialAsset::CreateDefault("SceneOnlyTestMat");
    material->SetTexture("BaseColorMap", AssetManager::Get().GetWhiteTexture());
    material->SetParam("BaseColor", MaterialParam::FromColor({0.2f, 0.4f, 0.6f}));
    material->SetParam("Metallic", MaterialParam::FromFloat(0.3f));
    material->SetParam("Roughness", MaterialParam::FromFloat(0.7f));
    renderer->SetMaterial(AssetManager::Get().Register(std::move(material)));

    const std::string json = SceneSerializer::SaveToString(scene);
    AssetManager::Get().Clear();

    Scene loaded("LoadedBuiltinMaterialScene");
    if (!Check(SceneSerializer::LoadFromString(loaded, json),
               "builtin material scene deserialize failed")) return false;

    Actor* loadedActor = loaded.FindByName("BuiltinMaterialActor");
    if (!Check(loadedActor != nullptr, "builtin material actor missing after deserialize")) return false;
    auto* loadedRenderer = loadedActor->GetComponent<MeshRendererComponent>();
    if (!Check(loadedRenderer != nullptr, "builtin material renderer missing after deserialize")) return false;
    if (!Check(loadedRenderer->GetMaterial().IsValid(), "builtin material handle invalid after deserialize")) return false;
    if (!Check(loadedRenderer->GetMaterial()->GetPath() == "__builtin__/SceneOnlyTestMat",
               "builtin material path mismatch after deserialize")) return false;
    if (!Check(loadedRenderer->GetMaterial()->GetTexture("BaseColorMap").IsValid(),
               "builtin material texture missing after deserialize")) return false;
    if (!Check(NearlyEqual(loadedRenderer->GetMaterial()->GetFloat("Metallic", 0.0f), 0.3f) &&
               NearlyEqual(loadedRenderer->GetMaterial()->GetFloat("Roughness", 0.0f), 0.7f) &&
               NearlyEqual(loadedRenderer->GetMaterial()->GetColor("BaseColor").x, 0.2f),
               "builtin material parameters mismatch after deserialize")) return false;
    return true;
}

bool TestScriptRuntimeLifecycle() {
    Scene scene("ScriptCase");
    Actor* actor = scene.CreateActor("Scripted");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetSource(
        "class Script {\n"
        "  void Start() { Actor::SetPosition(Vec3(1, 2, 3)); }\n"
        "  void Update(float dt) {\n"
        "    Actor::Translate(Vec3(2 * dt, 0, 0));\n"
        "    Actor::Rotate(Vec3(0, 90 * dt, 0));\n"
        "  }\n"
        "}\n");
    if (!Check(script->IsCompiled(), "script should compile: " + script->GetLastError())) return false;

    scene.OnUpdate(0.5f);
    const Transform& transform = actor->GetTransform();
    if (!Check(NearlyEqual(transform.position.x, 2.0f) &&
               NearlyEqual(transform.position.y, 2.0f),
               "script start/update position mismatch")) return false;
    if (!Check(NearlyEqual(transform.rotation.y, 45.0f),
               "script update rotation mismatch")) return false;
    nlohmann::json serialized;
    script->Serialize(serialized);
    return Check(serialized.value("language", std::string{}) == "angelscript" &&
                 serialized.value("className", std::string{}) == "Script",
                 "AngelScript schema fields missing");
}

bool TestAngelScriptFilesErrorsAndPhysicsBindings() {
    Scene scene("AngelBindings");

    Actor* target = scene.CreateActor("Target");
    target->GetTransform().position = { 0.0f, 0.0f, 0.0f };
    target->AddComponent<BoxColliderComponent>();

    Actor* actor = scene.CreateActor("Caster");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetSource(
        "class Script {\n"
        "  void Start() {\n"
        "    RaycastHit hit = Physics::Raycast(Vec3(0, 0, 5), Vec3(0, 0, -1), 20);\n"
        "    if (hit.hit) Actor::SetPosition(Vec3(hit.distance, 0, 0));\n"
        "  }\n"
        "}\n");
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(actor->GetTransform().position.x > 0.0f,
               "AngelScript Physics::Raycast binding failed: " + script->GetLastError())) return false;

    script->SetSource(
        "class Script {\n"
        "  void Update(float dt) { Script@ other = null; other.Update(dt); }\n"
        "}\n");
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(!script->IsCompiled(), "AngelScript runtime error should disable script")) return false;
    if (!Check(script->GetLastError().find("AngelScript execution failed") != std::string::npos,
               "AngelScript error should include execution diagnostic")) return false;

    const auto path = std::filesystem::temp_directory_path() / "myengine_hot_reload.as";
    {
        std::ofstream output(path, std::ios::binary);
        output << "class Script { void Update(float dt) { Actor::Translate(Vec3(1 * dt, 0, 0)); } }\n";
    }
    actor->GetTransform().position = {};
    script->SetScriptPath(path.string());
    scene.OnUpdate(1.0f);
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 1.0f),
               "AngelScript file initial run failed")) return false;
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "class Script { void Update(float dt) { Actor::Translate(Vec3(3 * dt, 0, 0)); } }\n";
    }
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now());
    scene.OnUpdate(1.0f);
    std::filesystem::remove(path);
    return Check(NearlyEqual(actor->GetTransform().position.x, 4.0f),
                 "AngelScript file hot reload failed");
}

bool TestAngelScriptAssetsClassesAndFields() {
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "myengine_script_asset_fields.as";
    {
        std::ofstream output(path, std::ios::binary);
        output
            << "class Rotator {\n"
            << "  float speed = 35.0f;\n"
            << "  Vec3 axis = Vec3(0, 1, 0);\n"
            << "  string label = \"cube\";\n"
            << "  Rotator@ ignored;\n"
            << "  void Update(float dt) { Actor::Rotate(axis * speed * dt); }\n"
            << "}\n"
            << "class Mover {\n"
            << "  int steps = 2;\n"
            << "  void Update(float dt) { Actor::Translate(Vec3(steps * dt, 0, 0)); }\n"
            << "}\n";
    }

    auto asset = AssetManager::Get().Load<ScriptAsset>(path.string());
    if (!Check(asset.IsValid(), "script asset should load: " + (asset.Get() ? asset->GetLastError() : std::string{}))) return false;
    if (!Check(asset->GetClasses().size() == 2, "script asset should discover two classes")) return false;

    bool foundRotator = false;
    for (const auto& cls : asset->GetClasses()) {
        if (cls.name != "Rotator") continue;
        foundRotator = true;
        bool speed = false, axis = false, label = false, ignored = false;
        for (const auto& field : cls.fields) {
            if (field.name == "speed" && field.type == ScriptFieldType::Float &&
                NearlyEqual(field.defaultValue.get<float>(), 35.0f)) speed = true;
            if (field.name == "axis" && field.type == ScriptFieldType::Vec3 &&
                field.defaultValue.is_array() && NearlyEqual(field.defaultValue[1].get<float>(), 1.0f)) axis = true;
            if (field.name == "label" && field.type == ScriptFieldType::String &&
                field.defaultValue == "cube") label = true;
            if (field.name == "ignored") ignored = true;
        }
        if (!(speed && axis && label && !ignored)) {
            std::ostringstream fields;
            for (const auto& field : cls.fields) {
                fields << field.name << ":" << field.declaration << " ";
            }
            return Check(false, "script field reflection mismatch: " + fields.str());
        }
    }
    if (!Check(foundRotator, "Rotator class missing from script asset")) return false;

    Scene scene("ScriptAssetFields");
    Actor* actor = scene.CreateActor("Scripted");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetScriptPath(path.string());
    script->SetClassName("Rotator");
    if (!Check(script->IsCompiled(), "script asset component should compile: " + script->GetLastError())) return false;
    if (!Check(script->SetPropertyValue("speed", 10.0f), "script property edit failed")) return false;
    if (!Check(script->SetPropertyValue("axis", nlohmann::json::array({0.0f, 2.0f, 0.0f})),
               "script Vec3 property edit failed")) return false;
    scene.OnUpdate(1.0f);
    if (!Check(NearlyEqual(actor->GetTransform().rotation.y, 20.0f),
               "script reflected properties were not applied")) return false;

    nlohmann::json data;
    script->Serialize(data);
    if (!Check(!data.contains("source") &&
               data.value("className", std::string{}) == "Rotator" &&
               data.contains("properties") &&
               NearlyEqual(data["properties"]["speed"].get<float>(), 10.0f),
               "file script serialization shape mismatch")) return false;

    Scene commandScene("ScriptCommand");
    Actor* commandActor = commandScene.CreateActor("CommandActor");
    EditorContext context(&commandScene);
    EditorCommandStack stack;
    context.SetCommandStack(&stack);
    nlohmann::json initialData = {
        {"language", "angelscript"},
        {"scriptPath", path.string()},
        {"className", "Mover"},
        {"properties", {{"steps", 4}}},
        {"state", nlohmann::json::object()}
    };
    if (!Check(stack.ExecuteCommand(std::make_unique<AddComponentCommand>(
            commandActor->GetID(), "Script", initialData), context),
            "AddComponentCommand should create script component")) return false;
    auto* added = commandActor->GetComponent<ScriptComponent>();
    if (!Check(added && added->IsCompiled() && added->GetClassName() == "Mover",
               "script component command initial data failed")) return false;
    commandScene.OnUpdate(1.0f);
    fs::remove(path);
    return Check(NearlyEqual(commandActor->GetTransform().position.x, 4.0f),
                 "command-created script did not execute reflected int property");
}

bool TestEditorLuaScriptService() {
    Scene scene("EditorLua");
    EditorContext context(&scene);
    EditorCommandStack stack;
    context.SetCommandStack(&stack);
    EditorLuaScriptService service;
    service.OnAttach(context);
    std::string error;
    if (!Check(service.RunSource(
            "local id = Scene.create_actor('LuaActor')\n"
            "Selection.select_actor(id)\n"
            "Scene.set_selected_position(3, 4, 5)\n",
            "EditorLuaTest", &error),
            "editor Lua script failed: " + error)) return false;
    Actor* actor = scene.FindByName("LuaActor");
    if (!Check(actor && NearlyEqual(actor->GetTransform().position.x, 3.0f) &&
               context.GetSelection().GetActorID() == actor->GetID(),
               "editor Lua did not create/select/move actor")) return false;
    if (!Check(stack.CanUndo() && stack.Undo(context),
               "editor Lua command was not undoable")) return false;
    service.OnDetach();
    return true;
}

bool TestLegacyLuaScriptCompatibility() {
    nlohmann::json legacy = {
        {"source", "function Update(dt) Actor.translate(1 * dt, 0, 0) end\n"},
        {"scriptPath", ""},
        {"inspector", nlohmann::json::object({{"speed", 2.0}})},
        {"state", nlohmann::json::object({{"updates", 3}})}
    };
    ScriptComponent script;
    script.Deserialize(legacy);
    if (!Check(script.IsLegacyLua() && !script.IsCompiled(),
               "legacy Lua script should be retained but not compiled")) return false;
    if (!Check(script.GetLastError().find("Legacy Lua gameplay scripts") != std::string::npos,
               "legacy Lua diagnostic missing")) return false;
    nlohmann::json saved;
    script.Serialize(saved);
    return Check(!saved.contains("language") &&
                 saved.value("source", std::string{}) == legacy["source"].get<std::string>() &&
                 saved["inspector"].value("speed", 0.0) == 2.0,
                 "legacy Lua fields were not preserved");
}

bool TestPhysicsGroundCollision() {
    Scene scene("PhysicsCase");

    Actor* ground = scene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    ground->AddComponent<BoxColliderComponent>();

    Actor* box = scene.CreateActor("Box");
    box->GetTransform().position = { 0.0f, 2.0f, 0.0f };
    auto* body = box->AddComponent<RigidBodyComponent>();
    body->SetRestitution(0.0f);
    box->AddComponent<BoxColliderComponent>();

    for (int i = 0; i < 180; ++i) {
        scene.OnUpdate(1.0f / 60.0f);
    }

    if (!Check(box->GetTransform().position.y >= 0.49f,
               "dynamic box penetrated static ground")) return false;
    return Check(std::fabs(body->GetVelocity().y) < 0.2f,
                 "dynamic box did not settle on ground");
}

bool TestExtendedCollisionShapes() {
    OrientedBox box;
    box.center = Vec3::Zero();
    const float angle = 35.0f * kDeg2Rad;
    box.axes[0] = Vec3{ std::cos(angle), 0.0f, -std::sin(angle) };
    box.axes[1] = Vec3::Up();
    box.axes[2] = Vec3{ std::sin(angle), 0.0f, std::cos(angle) };

    SphereShape sphere;
    sphere.center = { 0.8f, 0.0f, 0.0f };
    sphere.radius = 0.5f;
    ContactManifold contact;
    if (!Check(Collide(sphere, box, contact) && contact.depth > 0.0f,
               "sphere/OBB narrow phase failed")) return false;

    CapsuleShape capsuleA;
    capsuleA.pointA = { 0.0f, -1.0f, 0.0f };
    capsuleA.pointB = { 0.0f, 1.0f, 0.0f };
    capsuleA.radius = 0.35f;
    CapsuleShape capsuleB = capsuleA;
    capsuleB.pointA.x = capsuleB.pointB.x = 0.5f;
    if (!Check(Collide(capsuleA, capsuleB, contact),
               "capsule/capsule narrow phase failed")) return false;

    Scene scene("SphereGround");
    Actor* ground = scene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    ground->GetTransform().rotation = { 0.0f, 0.0f, 0.1f };
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    auto* groundCollider = ground->AddComponent<BoxColliderComponent>();
    groundCollider->SetHalfExtents({ 3.0f, 0.5f, 3.0f });

    Actor* ball = scene.CreateActor("Ball");
    ball->GetTransform().position = { 0.0f, 2.0f, 0.0f };
    auto* ballBody = ball->AddComponent<RigidBodyComponent>();
    auto* ballCollider = ball->AddComponent<SphereColliderComponent>();
    ballCollider->SetRadius(0.5f);
    for (int i = 0; i < 180; ++i) scene.OnUpdate(1.0f / 60.0f);
    return Check(ball->GetTransform().position.y > 0.35f &&
                 std::fabs(ballBody->GetVelocity().y) < 0.3f,
                 "sphere did not settle on rotated OBB ground");
}

class CollisionProbeComponent final : public Component {
public:
    const char* GetTypeName() const override { return "CollisionProbe"; }
    void OnCollisionEvent(const CollisionEvent& event) override {
        if (event.phase == CollisionEventPhase::Enter) ++enters;
        else if (event.phase == CollisionEventPhase::Stay) ++stays;
        else if (event.phase == CollisionEventPhase::Exit) ++exits;
        lastWasTrigger = event.trigger;
    }
    int enters = 0;
    int stays = 0;
    int exits = 0;
    bool lastWasTrigger = false;
};

class ThrowingDeserializeComponent final : public Component {
public:
    const char* GetTypeName() const override { return "ThrowingDeserialize"; }
    void Deserialize(const nlohmann::json& data) override {
        (void)data;
        throw std::runtime_error("deserialize failed intentionally");
    }
};

bool TestPhysicsBroadPhaseTriggersAndSleep() {
    Scene scene("PhysicsFeatures");
    scene.GetPhysicsWorld().SetGravity(Vec3::Zero());

    Actor* trigger = scene.CreateActor("Trigger");
    auto* triggerBody = trigger->AddComponent<RigidBodyComponent>();
    triggerBody->SetBodyType(BodyType::Static);
    auto* triggerCollider = trigger->AddComponent<BoxColliderComponent>();
    triggerCollider->SetTrigger(true);
    triggerCollider->SetLayer(2);
    triggerCollider->SetLayerMask(4);

    Actor* mover = scene.CreateActor("Mover");
    auto* moverBody = mover->AddComponent<RigidBodyComponent>();
    moverBody->SetUseGravity(false);
    auto* moverCollider = mover->AddComponent<SphereColliderComponent>();
    moverCollider->SetLayer(4);
    moverCollider->SetLayerMask(2);
    auto* probe = mover->AddComponent<CollisionProbeComponent>();

    scene.OnUpdate(1.0f / 60.0f);
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(probe->enters == 1 && probe->stays >= 1 && probe->lastWasTrigger,
               "trigger enter/stay events failed")) return false;
    if (!Check(NearlyEqual(mover->GetTransform().position.x, 0.0f),
               "trigger incorrectly applied positional correction")) return false;

    mover->GetTransform().position = { 10.0f, 0.0f, 0.0f };
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(probe->exits == 1, "trigger exit event failed")) return false;

    moverCollider->SetLayerMask(0);
    mover->GetTransform().position = Vec3::Zero();
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(probe->enters == 1, "layer mask did not filter collision pair")) return false;

    Scene sleepScene("Sleep");
    Actor* ground = sleepScene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    auto* groundShape = ground->AddComponent<BoxColliderComponent>();
    groundShape->SetHalfExtents({ 4.0f, 0.5f, 4.0f });

    Actor* bodyActor = sleepScene.CreateActor("SleepingBody");
    bodyActor->GetTransform().position = { 0.0f, 1.0f, 0.0f };
    auto* body = bodyActor->AddComponent<RigidBodyComponent>();
    body->SetFriction(1.0f);
    body->SetVelocity({ 1.0f, 0.0f, 0.0f });
    bodyActor->AddComponent<SphereColliderComponent>();
    for (int i = 0; i < 360; ++i) sleepScene.OnUpdate(1.0f / 60.0f);
    return Check(body->IsSleeping() && body->GetVelocity().LengthSq() < 1e-6f,
                 "resting rigid body did not enter sleep state");
}

bool TestRaycastAndCharacterController() {
    Scene rayScene("Raycast");
    Actor* ignored = rayScene.CreateActor("IgnoredNear");
    ignored->GetTransform().position = { 0.0f, 0.0f, 2.0f };
    ignored->AddComponent<SphereColliderComponent>()->SetLayer(2);
    Actor* target = rayScene.CreateActor("Target");
    target->GetTransform().position = { 0.0f, 0.0f, 4.0f };
    target->AddComponent<SphereColliderComponent>()->SetLayer(4);

    RaycastHit hit;
    Ray ray;
    ray.origin = Vec3::Zero();
    ray.direction = Vec3::Forward();
    if (!Check(rayScene.GetPhysicsWorld().Raycast(rayScene, ray, 10.0f, 4, hit),
               "layer-filtered raycast missed target")) return false;
    if (!Check(hit.actor == target && NearlyEqual(hit.distance, 3.5f, 0.05f),
               "raycast did not return nearest permitted collider")) return false;

    Scene controllerScene("Controller");
    Actor* ground = controllerScene.CreateActor("Ground");
    ground->GetTransform().position = { 0.0f, -0.5f, 0.0f };
    auto* groundBody = ground->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    ground->AddComponent<BoxColliderComponent>()->SetHalfExtents({ 5.0f, 0.5f, 5.0f });

    Actor* player = controllerScene.CreateActor("Player");
    player->GetTransform().position = { 0.0f, 2.0f, 0.0f };
    auto* capsule = player->AddComponent<CapsuleColliderComponent>();
    capsule->SetRadius(0.5f);
    capsule->SetHalfHeight(0.5f);
    auto* controller = player->AddComponent<CharacterControllerComponent>();
    controller->Move({ 1.0f, 0.0f, 0.0f });
    for (int i = 0; i < 180; ++i) controllerScene.OnUpdate(1.0f / 60.0f);
    return Check(controller->IsGrounded() &&
                 player->GetTransform().position.x > 2.0f &&
                 player->GetTransform().position.y > 0.9f,
                 "character controller did not move and settle on ground");
}

bool TestGpuSkinningAnimationBlend() {
    std::vector<MeshVertex> vertices(3);
    vertices[0].position = { 0.0f, 0.0f, 0.0f };
    vertices[1].position = { 0.0f, 1.0f, 0.0f };
    vertices[2].position = { 1.0f, 0.0f, 0.0f };
    std::vector<uint32_t> indices = { 0, 1, 2 };
    std::vector<SubMesh> submeshes = { { 0, 3, 0, 0, "Triangle" } };

    auto source = std::make_shared<MeshAsset>("__builtin__/SkinnedTriangle");
    source->SetGeometry(std::move(vertices), std::move(indices), std::move(submeshes));
    MeshHandle sourceHandle = AssetManager::Get().Register(source);

    Scene scene("SkinCase");
    Actor* actor = scene.CreateActor("Skinned");
    auto* skinned = actor->AddComponent<SkinnedMeshRendererComponent>();
    skinned->SetSourceMesh(sourceHandle);
    skinned->SetMaterial(AssetManager::Get().GetDefaultMaterial());

    std::vector<Bone> bones(1);
    bones[0].name = "Root";
    skinned->SetSkeleton(std::move(bones), std::vector<SkinWeight>(3));

    AnimationClip clip;
    clip.name = "Move";
    clip.duration = 1.0f;
    clip.looping = false;
    BoneTrack track;
    track.boneIndex = 0;
    track.keys = {
        { 0.0f, Vec3::Zero(), Quat::Identity(), Vec3::One() },
        { 1.0f, Vec3{1.0f, 0.0f, 0.0f}, Quat::Identity(), Vec3::One() },
    };
    clip.tracks.push_back(std::move(track));
    skinned->SetAnimation(std::move(clip));
    skinned->SetAnimationTime(1.0f);

    MeshAsset* result = skinned->GetRenderMesh();
    if (!Check(result && result->VertexCount() == 3, "skinned render mesh missing")) return false;
    if (!Check(NearlyEqual(result->GetVertices()[0].position.x, 0.0f),
               "GPU skinning unexpectedly modified CPU vertex data")) return false;
    if (!Check(skinned->UsesGpuSkinning() &&
               NearlyEqual(skinned->GetSkinMatrices()[0].TransformPoint(Vec3::Zero()).x, 1.0f),
               "GPU skinning pose matrix mismatch")) return false;

    AnimationClip blendClip;
    blendClip.name = "MoveFurther";
    blendClip.duration = 1.0f;
    BoneTrack blendTrack;
    blendTrack.boneIndex = 0;
    blendTrack.keys = {
        { 0.0f, Vec3{3.0f, 0.0f, 0.0f}, Quat::Identity(), Vec3::One() }
    };
    blendClip.tracks.push_back(std::move(blendTrack));
    skinned->SetBlendAnimation(std::move(blendClip), 0.5f);
    if (!Check(NearlyEqual(
            skinned->GetSkinMatrices()[0].TransformPoint(Vec3::Zero()).x, 2.0f),
            "animation blend pose mismatch")) return false;

    MeshHandle builtinCube = AssetManager::Get().GetCubeMesh();
    skinned->SetSourceMesh(builtinCube);
    std::vector<Bone> serialBones(1);
    serialBones[0].name = "Root";
    skinned->SetSkeleton(std::move(serialBones),
                         std::vector<SkinWeight>(builtinCube->VertexCount()));

    Scene loaded("LoadedSkin");
    if (!Check(SceneSerializer::LoadFromString(loaded, SceneSerializer::SaveToString(scene)),
               "skinned scene deserialize failed")) return false;
    auto* loadedSkin = loaded.FindByName("Skinned")->GetComponent<SkinnedMeshRendererComponent>();
    if (!Check(loadedSkin && loadedSkin->GetWeights().size() == builtinCube->VertexCount(),
               "skin weights missing after deserialize")) return false;
    return Check(loadedSkin->GetAnimation().tracks.size() == 1,
                 "animation tracks missing after deserialize");
}

bool TestPbrMaterialParameters() {
    auto material = MaterialAsset::CreateDefault("PbrTest");
    material->SetParam("Metallic", MaterialParam::FromFloat(0.8f));
    material->SetParam("Roughness", MaterialParam::FromFloat(0.25f));
    if (!Check(NearlyEqual(material->GetFloat("Metallic", 0.0f), 0.8f),
               "PBR metallic parameter mismatch")) return false;
    if (!Check(NearlyEqual(material->GetFloat("Roughness", 1.0f), 0.25f),
               "PBR roughness parameter mismatch")) return false;
    return Check(NearlyEqual(material->GetFloat("AmbientOcclusion", 0.7f), 0.7f),
                 "material default parameter fallback mismatch");
}

bool TestMaterialAssetFileRoundTrip() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_material_asset_test";
    fs::remove_all(root);
    fs::create_directories(root);

    const fs::path texPath = root / "base.ppm";
    {
        std::ofstream output(texPath, std::ios::binary);
        output << "P6\n1 1\n255\n";
        const char pixel[3] = {
            static_cast<char>(255),
            static_cast<char>(64),
            static_cast<char>(32)
        };
        output.write(pixel, sizeof(pixel));
    }

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    TextureHandle texture = manager.Load<TextureAsset>(texPath.string());
    if (!Check(texture.IsValid(), "material test texture load failed")) return false;
    const fs::path shaderPath = root / "material.shader";
    std::ofstream(shaderPath) << R"({"type":"Shader","version":1,"stages":{"vertex":{"source":"material.hlsl","entry":"VSMain"},"pixel":{"source":"material.hlsl","entry":"PSMain"}},"defines":[]})";
    ShaderAssetHandle shaderAsset = manager.Load<ShaderAsset>(shaderPath.string());
    if (!Check(shaderAsset.IsValid(), "material test shader load failed")) return false;

    const fs::path matPath = root / "test.mat";
    auto material = std::make_shared<MaterialAsset>(matPath.string());
    material->SetName("RoundTrip");
    material->SetBlendMode(BlendMode::Transparent);
    material->SetTwoSided(true);
    material->SetAlphaThreshold(0.33f);
    material->SetParam("BaseColor", MaterialParam::FromVec4(0.2f, 0.3f, 0.4f, 0.5f));
    material->SetParam("Metallic", MaterialParam::FromFloat(0.9f));
    material->SetParam("Roughness", MaterialParam::FromFloat(0.21f));
    material->SetParam("AmbientOcclusion", MaterialParam::FromFloat(0.8f));
    material->SetTexture("BaseColorMap", texture);
    material->SetShaderAsset(shaderAsset);
    if (!Check(SaveMaterialAssetToFile(*material, matPath.string()),
               "material save failed")) return false;

    manager.Clear();
    MaterialHandle loaded = manager.Load<MaterialAsset>(matPath.string());
    if (!Check(loaded.IsValid(), "material load failed")) return false;
    if (!Check(loaded->GetBlendMode() == BlendMode::Transparent && loaded->IsTwoSided(),
               "material render state roundtrip failed")) return false;
    if (!Check(NearlyEqual(loaded->GetAlphaThreshold(), 0.33f) &&
               NearlyEqual(loaded->GetFloat("Metallic"), 0.9f) &&
               NearlyEqual(loaded->GetFloat("Roughness"), 0.21f) &&
               NearlyEqual(loaded->GetFloat("AmbientOcclusion"), 0.8f),
               "material scalar roundtrip failed")) return false;
    if (!Check(loaded->GetTexture("BaseColorMap").IsValid(),
               "material texture slot roundtrip failed")) return false;
    if (!Check(loaded->GetShaderAsset().IsValid(),
               "material shader asset reference roundtrip failed")) return false;

    manager.Clear();
    fs::remove_all(root);
    return true;
}

bool TestAssetManagerSharedAcrossRuntimeBoundary() {
    auto mesh = MeshAsset::CreateTriangle("__dll_shared_mesh");
    const std::string path = mesh->GetPath();
    MeshHandle registered = AssetManager::Get().Register(mesh);

    Scene source("DllBoundary");
    Actor* actor = source.CreateActor("SharedMesh");
    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    renderer->SetMesh(registered);
    renderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());

    Scene loaded("Loaded");
    if (!Check(SceneSerializer::LoadFromString(
            loaded, SceneSerializer::SaveToString(source)),
            "DLL boundary scene load failed")) return false;

    auto* loadedRenderer =
        loaded.FindByName("SharedMesh")->GetComponent<MeshRendererComponent>();
    if (!Check(loadedRenderer && loadedRenderer->GetMesh().IsValid(),
               "runtime DLL could not see executable-registered asset")) return false;
    return Check(loadedRenderer->GetMesh()->GetPath() == path,
                 "shared asset path changed across DLL boundary");
}

bool TestComponentRegistry() {
    ComponentRegistry& registry = ComponentRegistry::Get();
    const char* required[] = {
        "MeshRenderer", "SkinnedMeshRenderer", "Script", "RigidBody", "BoxCollider",
        "SphereCollider", "CapsuleCollider", "CharacterController", "Camera", "Light", "PostProcess",
        "AudioSource"
    };
    for (const char* type : required) {
        if (!Check(registry.IsRegistered(type),
                   std::string("component type not registered: ") + type)) return false;
    }

    Scene scene("Registry");
    Actor* actor = scene.CreateActor("Actor");
    Component* component = registry.Create("Script", *actor);
    if (!Check(component != nullptr, "registry factory returned null")) return false;
    if (!Check(std::string(component->GetTypeName()) == "Script",
               "registry created wrong component type")) return false;
    if (!Check(actor->HasComponentType("Script"),
               "actor type-name component query failed")) return false;
    if (!Check(actor->GetComponentByTypeName("Script") == component,
               "actor type-name component lookup returned wrong component")) return false;

    Component* duplicate = registry.Create("Script", *actor);
    if (!Check(duplicate == component,
               "registry duplicate create should return existing component")) return false;
    if (!Check(actor->RemoveComponentByTypeName("Script"),
               "actor type-name component remove failed")) return false;
    return Check(!actor->HasComponentType("Script"),
                 "actor type-name component remove left component behind");
}

bool TestSceneRunStates() {
    SceneLayer layer("RunStateTest");
    Scene* editorScene = &layer.GetEditorScene();
    Actor* actor = editorScene->CreateActor("Scripted");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetSource(
        "class Script {\n"
        "  void Update(float dt) { Actor::Translate(Vec3(2 * dt, 0, 0)); }\n"
        "}\n");
    layer.MarkDirty();

    layer.OnUpdate(0.5f);
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 0.0f),
               "Edit mode should not simulate scene")) return false;

    if (!Check(layer.BeginPlay(), "BeginPlay failed")) return false;
    if (!Check(&layer.GetEditorScene() == editorScene,
               "BeginPlay replaced the editor scene")) return false;
    if (!Check(layer.HasPlayWorld() && layer.GetPlayScene(),
               "BeginPlay did not create PlayWorld")) return false;
    Actor* runtimeActor = layer.GetPlayScene()->FindByName("Scripted");
    if (!Check(runtimeActor && runtimeActor != actor,
               "PlayWorld did not clone the edit scene")) return false;
    layer.OnUpdate(0.5f);
    if (!Check(NearlyEqual(runtimeActor->GetTransform().position.x, 1.0f),
               "Play mode did not update runtime scene")) return false;
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 0.0f),
               "PlayWorld update leaked into EditorWorld")) return false;

    layer.PausePlay();
    layer.OnUpdate(0.5f);
    if (!Check(NearlyEqual(runtimeActor->GetTransform().position.x, 1.0f),
               "Pause mode advanced scene")) return false;
    if (!Check(layer.StepPlay(), "StepPlay failed")) return false;
    layer.OnUpdate(0.5f);
    if (!Check(NearlyEqual(runtimeActor->GetTransform().position.x, 2.0f),
               "Step did not advance exactly one update")) return false;
    layer.OnUpdate(0.5f);
    if (!Check(NearlyEqual(runtimeActor->GetTransform().position.x, 2.0f),
               "paused scene advanced after Step")) return false;

    layer.StopPlay();
    Actor* restored = layer.GetEditorScene().FindByName("Scripted");
    if (!Check(layer.IsEditing() && !layer.HasPlayWorld() && restored,
               "StopPlay did not restore Edit mode")) return false;
    if (!Check(restored == actor && &layer.GetEditorScene() == editorScene,
               "StopPlay should keep the original EditorWorld alive")) return false;
    if (!Check(NearlyEqual(restored->GetTransform().position.x, 0.0f),
               "runtime changes leaked into edit scene")) return false;
    return Check(layer.IsDirty(), "edit dirty state was not restored");
}

struct MockBuffer final : GpuBuffer {};
struct MockBufferView final : GpuBufferView {};
struct MockShader final : GpuShader {};
struct MockTexture final : GpuTexture {};
struct MockTextureView final : GpuTextureView {};
struct MockSampler final : GpuSampler {};
class MockTimestampPool final : public GpuTimestampQueryPool {
public:
    uint32_t GetCount() const override { return 4; }
    uint64_t GetFrequency() const override { return 1000000; }
    bool ReadResults(uint32_t first, uint32_t count, std::vector<uint64_t>& ticks) override {
        if (first + count > 4) return false;
        ticks.resize(count); for (uint32_t i = 0; i < count; ++i) ticks[i] = first + i;
        return true;
    }
};

class MockReadbackTicket final : public GpuReadbackTicket {
public:
    explicit MockReadbackTicket(std::vector<uint8_t> bytes)
        : m_Bytes(std::move(bytes)) {}
    bool IsReady() const override { return ready; }
    bool Read(std::vector<uint8_t>& data) override {
        if (!ready) return false;
        data = m_Bytes;
        return true;
    }
    uint32_t GetSize() const override { return static_cast<uint32_t>(m_Bytes.size()); }
    bool ready = false;
private:
    std::vector<uint8_t> m_Bytes;
};

class MockCommandList final : public GpuCommandList {
public:
    void BindShader(GpuShader*) override { ++shaderBinds; }
    void BindVertexBuffer(GpuBuffer*) override { ++vertexBinds; }
    void BindIndexBuffer(GpuBuffer*) override {}
    void SetVSConstants(const void*, uint32_t) override { ++constantUpdates; }
    void Draw(uint32_t, uint32_t) override { ++drawCalls; }
    void DrawIndexed(uint32_t, uint32_t, uint32_t) override { ++drawCalls; }
    void DrawInstanced(uint32_t, uint32_t instanceCount, uint32_t) override {
        ++drawCalls;
        submittedInstances += instanceCount;
    }
    void DrawIndexedInstanced(uint32_t, uint32_t instanceCount,
                              uint32_t, uint32_t) override {
        ++drawCalls;
        submittedInstances += instanceCount;
    }
    void SetViewport(float, float, float, float) override {}
    void BindPSTexture(uint32_t, GpuTexture*) override {}
    void SetBlendMode(GpuBlendMode mode) override { blendModes.push_back(mode); }
    void Transition(GpuResource*, RHIResourceState before,
                    RHIResourceState after) override {
        transitions.emplace_back(before, after);
    }
    void BeginRendering(const RenderingInfo&) override { ++renderingScopes; }
    void EndRendering() override { --renderingScopes; }
    void SetGraphicsPipeline(GpuGraphicsPipeline* pipeline) override {
        ++pipelineBinds;
        pipelineBlendEnabled.push_back(pipeline && !pipeline->desc.blend.attachments.empty() &&
                                       pipeline->desc.blend.attachments[0].blendEnable);
    }
    void SetComputePipeline(GpuComputePipeline*) override { ++computePipelineBinds; }
    void SetBindGroup(uint32_t, GpuBindGroup*) override { ++bindGroupBinds; }
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        ++dispatches; dispatchGroups = {x, y, z};
    }
    void UAVBarrier(GpuResource*) override { ++uavBarriers; }
    void CopyTexture(GpuTexture*, const RHITextureRegion& dst,
                     GpuTexture*, const RHITextureRegion& src) override {
        copiedDst = dst; copiedSrc = src; ++textureRegionCopies;
    }
    void DrawIndirect(GpuBuffer*, uint64_t) override { ++indirectDraws; }
    void DrawIndexedIndirect(GpuBuffer*, uint64_t) override { ++indirectDraws; }
    void WriteTimestamp(GpuTimestampQueryPool*, uint32_t) override { ++timestamps; }
    void ResolveTimestamps(GpuTimestampQueryPool*, uint32_t, uint32_t) override { ++timestampResolves; }

    int shaderBinds = 0;
    int vertexBinds = 0;
    int constantUpdates = 0;
    int drawCalls = 0;
    int submittedInstances = 0;
    std::vector<GpuBlendMode> blendModes;
    std::vector<std::pair<RHIResourceState, RHIResourceState>> transitions;
    int renderingScopes = 0;
    int pipelineBinds = 0;
    std::vector<bool> pipelineBlendEnabled;
    int computePipelineBinds = 0;
    int bindGroupBinds = 0;
    int dispatches = 0;
    int uavBarriers = 0;
    int textureRegionCopies = 0;
    int indirectDraws = 0;
    int timestamps = 0;
    int timestampResolves = 0;
    RHITextureRegion copiedDst{}, copiedSrc{};
    std::array<uint32_t, 3> dispatchGroups{};
};

class MockRenderContext final : public IRenderContext {
public:
    bool Init(IWindow*) override { return true; }
    void Shutdown() override {}
    RHIBackend GetBackend() const override { return RHIBackend::Unknown; }
    void BeginFrame(float, float, float, float) override { ++beginFrames; }
    void EndFrame() override { ++endFrames; }
    GpuCommandList* GetGraphicsCommandList() override { return &commands; }
    std::shared_ptr<GpuBuffer> CreateVertexBuffer(
        const void*, uint32_t, uint32_t) override {
        ++vertexUploads;
        return std::make_shared<MockBuffer>();
    }
    std::shared_ptr<GpuBuffer> CreateIndexBuffer(
        const void*, uint32_t) override {
        ++indexUploads;
        return std::make_shared<MockBuffer>();
    }
    std::shared_ptr<GpuShader> CreateShader(
        const std::string&, const std::string&, const std::string&,
        const VertexElement*, uint32_t) override {
        ++shaderCreates;
        return std::make_shared<MockShader>();
    }
    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void*, int, int) override {
        ++textureUploads;
        return std::make_shared<MockTexture>();
    }
    bool UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer, uint64_t offset,
                      const void* data, uint64_t size) override {
        if (!buffer || !data || offset + size > bufferBytes.size()) return false;
        std::memcpy(bufferBytes.data() + offset, data, static_cast<size_t>(size)); return true;
    }
    std::shared_ptr<GpuTexture> UploadTexture(
        const RHITextureDesc& desc, const RHITextureSubresourceData*, uint32_t count) override {
        if (!count) return nullptr;
        auto texture = std::make_shared<MockTexture>(); texture->desc = desc; return texture;
    }
    RHIDeviceCapabilities GetCapabilities() const override {
        RHIDeviceCapabilities caps; caps.maxColorAttachments = 8;
        caps.timestampQueries = caps.indirectDraw = true; return caps;
    }
    std::shared_ptr<GpuTimestampQueryPool> CreateTimestampQueryPool(uint32_t count) override {
        return count <= 4 ? std::make_shared<MockTimestampPool>() : nullptr;
    }
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override {
        auto texture = std::make_shared<MockTexture>(); texture->desc = desc;
        ++graphTextureCreates; return texture;
    }
    std::shared_ptr<GpuTextureView> CreateTextureView(
        const std::shared_ptr<GpuTexture>& texture,
        const RHITextureViewDesc& desc) override {
        auto view = std::make_shared<MockTextureView>();
        view->texture = texture; view->desc = desc; return view;
    }
    std::shared_ptr<GpuBuffer> CreateBuffer(
        const RHIBufferDesc& desc, const void* initialData = nullptr) override {
        auto buffer = std::make_shared<MockBuffer>(); buffer->desc = desc;
        bufferBytes.resize(desc.size);
        if (initialData && desc.size) std::memcpy(bufferBytes.data(), initialData, desc.size);
        ++bufferCreates;
        return buffer;
    }
    std::shared_ptr<GpuBufferView> CreateBufferView(
        const std::shared_ptr<GpuBuffer>& buffer,
        const RHIBufferViewDesc& desc) override {
        if (!buffer || !desc.elementCount) return nullptr;
        auto view = std::make_shared<MockBufferView>();
        view->buffer = buffer; view->desc = desc;
        return view;
    }
    std::shared_ptr<GpuShader> CreateComputeShaderFromBytecode(
        const void*, size_t) override {
        auto shader = std::make_shared<MockShader>();
        ++computeShaderCreates;
        return shader;
    }
    std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(
        const std::shared_ptr<GpuBuffer>& buffer) override {
        if (!buffer) return nullptr;
        auto ticket = std::make_shared<MockReadbackTicket>(bufferBytes);
        lastReadback = ticket;
        return ticket;
    }

    MockCommandList commands;
    int beginFrames = 0;
    int endFrames = 0;
    int vertexUploads = 0;
    int indexUploads = 0;
    int shaderCreates = 0;
    int textureUploads = 0;
    int graphTextureCreates = 0;
    int bufferCreates = 0;
    int computeShaderCreates = 0;
    std::vector<uint8_t> bufferBytes;
    std::shared_ptr<MockReadbackTicket> lastReadback;
};

bool TestIconsManagerSvgRasterizeIcoAndUploadCache()
{
    namespace fs = std::filesystem;
    IconsManager& icons = IconsManager::Get();
    icons.Clear();
    icons.SetIconRoot(fs::current_path() / "EngineContent" / "Editor" / "Icons");

    if (!Check(fs::is_regular_file(icons.ResolveIconPath(IconsManager::kEditorIcon)),
               "engine-editor.svg was not resolved")) return false;

    const char* required[] = {
        IconsManager::kEditorIcon,
        IconsManager::kPlayerIcon,
        IconsManager::kCookerIcon,
        "play-start"
    };
    for (const char* icon : required) {
        for (int size : {16, 32, 64}) {
            auto pixels = icons.Rasterize(icon, size, {220, 40, 60, 255});
            if (!Check(pixels && pixels->width == size && pixels->height == size &&
                       pixels->rgba8.size() == static_cast<size_t>(size * size * 4),
                       std::string("failed to rasterize icon: ") + icon)) return false;
            bool hasVisiblePixel = false;
            bool hasTintedPixel = false;
            for (size_t i = 0; i + 3 < pixels->rgba8.size(); i += 4) {
                if (pixels->rgba8[i + 3] != 0) {
                    hasVisiblePixel = true;
                    if (pixels->rgba8[i] > pixels->rgba8[i + 1] &&
                        pixels->rgba8[i] > pixels->rgba8[i + 2]) {
                        hasTintedPixel = true;
                    }
                }
            }
            if (!Check(hasVisiblePixel && hasTintedPixel,
                       std::string("icon did not produce tinted visible pixels: ") + icon)) {
                return false;
            }
        }
    }

    if (!Check(!icons.Rasterize("__missing_icon__", 32),
               "missing icon should fail without producing pixels")) return false;

    const fs::path output = fs::temp_directory_path() / "myengine_icon_test.ico";
    std::error_code ec;
    fs::remove(output, ec);
    if (!Check(icons.WriteIco(IconsManager::kEditorIcon, output,
                              std::vector<int>{16, 24, 32, 48, 64, 128, 256}),
               "ico generation failed")) return false;
    std::ifstream ico(output, std::ios::binary);
    unsigned char header[6] = {};
    ico.read(reinterpret_cast<char*>(header), sizeof(header));
    const int count = header[4] | (header[5] << 8);
    if (!Check(ico.good() && count == 7 && fs::file_size(output) > 1024,
               "ico did not contain all requested image sizes")) return false;
    fs::remove(output, ec);

    MockRenderContext context;
    GpuTextureView* first = icons.GetOrUpload(context, "play-start", 24, {255, 255, 255, 255});
    GpuTextureView* second = icons.GetOrUpload(context, "play-start", 24, {255, 255, 255, 255});
    GpuTextureView* third = icons.GetOrUpload(context, "play-start", 24, {128, 255, 128, 255});
    return Check(first && second && third && first == second && first != third &&
                 context.textureUploads == 2,
                 "icon upload cache key did not include name/size/color");
}

bool TestExtendedRHIContracts() {
    MockRenderContext context;
    RHIBufferDesc bufferDesc; bufferDesc.size = 16;
    auto buffer = context.CreateBuffer(bufferDesc);
    const uint32_t value = 0x12345678u;
    if (!Check(context.UpdateBuffer(buffer, 4, &value, sizeof(value)) &&
               std::memcmp(context.bufferBytes.data() + 4, &value, sizeof(value)) == 0,
               "RHI partial buffer update failed")) return false;
    RHITextureDesc textureDesc; textureDesc.width = 8; textureDesc.height = 8;
    uint32_t pixels[64]{};
    RHITextureSubresourceData upload{pixels, 8u * 4u, 8u * 8u * 4u, 0, 0};
    auto source = context.UploadTexture(textureDesc, &upload, 1);
    auto destination = context.CreateTexture(textureDesc);
    RHITextureRegion srcRegion{1, 2, 0, 3, 4, 1, 0, 0};
    RHITextureRegion dstRegion{4, 1, 0, 3, 4, 1, 0, 0};
    context.commands.CopyTexture(destination.get(), dstRegion, source.get(), srcRegion);
    context.commands.DrawIndirect(buffer.get(), 0);
    context.commands.DrawIndexedIndirect(buffer.get(), 0);
    auto timestamps = context.CreateTimestampQueryPool(4);
    context.commands.WriteTimestamp(timestamps.get(), 0);
    context.commands.ResolveTimestamps(timestamps.get(), 0, 1);
    std::vector<uint64_t> ticks;
    const auto caps = context.GetCapabilities();
    return Check(source && context.commands.textureRegionCopies == 1 &&
                 context.commands.copiedSrc.x == 1 && context.commands.copiedDst.x == 4 &&
                 context.commands.indirectDraws == 2 && context.commands.timestamps == 1 &&
                 context.commands.timestampResolves == 1 && timestamps &&
                 timestamps->ReadResults(0, 1, ticks) && ticks.size() == 1 &&
                 caps.maxColorAttachments == 8 && caps.indirectDraw && caps.timestampQueries,
                 "extended RHI transfer/query/indirect contracts were not preserved");
}

bool TestRenderGraphValidationAndExecution() {
    MockRenderContext context;
    RenderGraph graph(context);
    RHITextureDesc desc;
    desc.width = 128; desc.height = 64;
    desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    const RGTextureHandle sceneColor = graph.CreateTexture("SceneColor", desc);
    std::vector<std::string> executed;
    graph.AddPass("Main", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(sceneColor, RHILoadOp::Clear, RHIStoreOp::Store,
                           {0.1f, 0.2f, 0.3f, 1.0f});
    }, [&](GpuCommandList&, const RenderGraphResources& resources) {
        if (resources.GetTexture(sceneColor)) executed.push_back("Main");
    });
    graph.AddPass("Composite", [&](RenderGraphBuilder& builder) {
        builder.ReadTexture(sceneColor);
    }, [&](GpuCommandList&, const RenderGraphResources& resources) {
        if (resources.GetView(sceneColor)) executed.push_back("Composite");
    });
    if (!Check(graph.Compile(), "RenderGraph compile failed: " + graph.GetLastError())) return false;
    if (!Check(graph.GetExecutionOrder() == std::vector<std::string>({"Main", "Composite"}),
               "RenderGraph execution order mismatch")) return false;
    if (!Check(graph.Execute(context.commands), "RenderGraph execute failed: " + graph.GetLastError())) return false;
    if (!Check(executed == std::vector<std::string>({"Main", "Composite"}) &&
               context.graphTextureCreates == 1 && context.commands.renderingScopes == 0,
               "RenderGraph did not execute through the RHI resource path")) return false;
    if (!Check(context.commands.transitions.size() == 2 &&
               context.commands.transitions[0].second == RHIResourceState::RenderTarget &&
               context.commands.transitions[1].second == RHIResourceState::ShaderResource,
               "RenderGraph state transitions mismatch")) return false;

    graph.Reset();
    const RGTextureHandle reused = graph.CreateTexture("SceneColor", desc);
    graph.AddPass("Rewrite", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(reused, RHILoadOp::Clear);
    }, {});
    if (!Check(graph.Execute(context.commands) && context.graphTextureCreates == 1,
               "RenderGraph did not reuse a descriptor-compatible transient texture")) return false;

    RenderGraph invalid(context);
    const RGTextureHandle unread = invalid.CreateTexture("Unread", desc);
    invalid.AddPass("InvalidRead", [&](RenderGraphBuilder& builder) {
        builder.ReadTexture(unread);
    }, {});
    return Check(!invalid.Compile() &&
                 invalid.GetLastError().find("uninitialized") != std::string::npos,
                 "RenderGraph accepted an uninitialized texture read");
}

bool TestNamedShaderBindings() {
    auto shader = std::make_shared<MockShader>();
    shader->reflection.bindings = {
        {"FrameConstants", ShaderBindingType::ConstantBuffer, 0, 1, 16, ShaderStageVertex},
        {"SceneColor", ShaderBindingType::Texture, 0, 1, 0, ShaderStagePixel},
        {"LinearClamp", ShaderBindingType::Sampler, 0, 1, 0, ShaderStagePixel}};
    GpuBindGroup bindings(shader);
    float constants[4] = {};
    if (!Check(!bindings.SetConstants("FrameConstants", constants, 12),
               "named binding accepted an invalid constant-buffer size")) return false;
    if (!Check(bindings.SetConstants("FrameConstants", constants, sizeof(constants)),
               "named constant binding failed")) return false;
    std::string error;
    if (!Check(!bindings.Validate(&error) && error.find("SceneColor") != std::string::npos,
               "bind group did not report a missing reflected binding")) return false;
    auto texture = std::make_shared<MockTexture>();
    auto view = std::make_shared<MockTextureView>(); view->texture = texture;
    auto sampler = std::make_shared<MockSampler>();
    return Check(bindings.SetTexture("SceneColor", view) &&
                 bindings.SetSampler("LinearClamp", sampler) && bindings.Validate(&error),
                 "complete named bind group failed validation: " + error);
}

bool TestBackendIndependentPassRecording() {
    MockRenderContext context;
    auto shader = std::make_shared<MockShader>();
    GraphicsPipelineDesc pipelineDesc; pipelineDesc.shader = shader;
    pipelineDesc.topology = RHIPrimitiveTopology::TriangleStrip;
    pipelineDesc.depthStencil.depthCompareOp = RHICompareOp::GreaterEqual;
    pipelineDesc.depthStencil.stencilEnable = true;
    pipelineDesc.depthStencil.frontFace.passOp = RHIStencilOp::Replace;
    pipelineDesc.depthStencil.stencilReference = 7;
    pipelineDesc.rasterizer.cullMode = RHICullMode::Front;
    pipelineDesc.rasterizer.frontFace = RHIFrontFace::CounterClockwise;
    pipelineDesc.rasterizer.depthBias = 8;
    pipelineDesc.blend.attachments[0].blendEnable = true;
    pipelineDesc.blend.attachments[0].srcColorFactor = RHIBlendFactor::One;
    pipelineDesc.blend.attachments[0].dstColorFactor = RHIBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.multisample.sampleCount = 4;
    auto pipeline = context.CreateGraphicsPipeline(pipelineDesc);
    if (!Check(pipeline &&
               pipeline->desc.topology == RHIPrimitiveTopology::TriangleStrip &&
               pipeline->desc.depthStencil.depthCompareOp == RHICompareOp::GreaterEqual &&
               pipeline->desc.depthStencil.frontFace.passOp == RHIStencilOp::Replace &&
               pipeline->desc.depthStencil.stencilReference == 7 &&
               pipeline->desc.rasterizer.cullMode == RHICullMode::Front &&
               pipeline->desc.rasterizer.depthBias == 8 &&
               pipeline->desc.blend.attachments[0].blendEnable &&
               pipeline->desc.multisample.sampleCount == 4,
               "graphics pipeline state was not preserved by the RHI")) return false;
    auto bindings = context.CreateBindGroup(shader);
    RHITextureDesc targetDesc;
    targetDesc.width = 32; targetDesc.height = 32;
    targetDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    RenderGraph graph(context);
    const auto target = graph.CreateTexture("ValidationTarget", targetDesc);
    graph.AddPass("RHIValidationPass", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(target, RHILoadOp::Clear);
    }, [&](GpuCommandList& commands, const RenderGraphResources&) {
        commands.SetGraphicsPipeline(pipeline.get());
        commands.SetBindGroup(0, bindings.get());
        commands.Draw(3);
    });
    return Check(graph.Execute(context.commands) && context.commands.pipelineBinds == 1 &&
                 context.commands.bindGroupBinds == 1 && context.commands.drawCalls == 1,
                 "backend-independent validation pass did not record the expected RHI commands");
}

bool TestComputeStorageBufferAndAsyncReadback() {
    MockRenderContext context;
    const std::array<uint32_t, 4> initial = {1, 2, 3, 4};
    RHIBufferDesc bufferDesc;
    bufferDesc.size = sizeof(initial); bufferDesc.stride = sizeof(uint32_t);
    bufferDesc.usage = RHIResourceUsage::UnorderedAccess |
                       RHIResourceUsage::ShaderResource |
                       RHIResourceUsage::CopySource;
    auto buffer = context.CreateBuffer(bufferDesc, initial.data());
    RHIBufferViewDesc viewDesc;
    viewDesc.elementCount = static_cast<uint32_t>(initial.size());
    viewDesc.usage = RHIResourceUsage::UnorderedAccess;
    auto view = context.CreateBufferView(buffer, viewDesc);
    auto shader = std::make_shared<MockShader>();
    shader->reflection.bindings = {
        {"SHOutput", ShaderBindingType::StorageBuffer, 0, 1, 0, ShaderStageCompute}};
    auto bindings = context.CreateBindGroup(shader);
    if (!Check(view && bindings->SetStorageBuffer("SHOutput", view),
               "compute storage-buffer named binding failed")) return false;
    std::string error;
    if (!Check(bindings->Validate(&error), "compute bind group validation failed: " + error)) return false;
    ComputePipelineDesc pipelineDesc; pipelineDesc.shader = shader;
    auto pipeline = context.CreateComputePipeline(pipelineDesc);
    auto* commands = context.GetGraphicsCommandList();
    commands->SetComputePipeline(pipeline.get());
    commands->SetBindGroup(0, bindings.get());
    commands->Dispatch(2, 3, 4);
    commands->UAVBarrier(buffer.get());
    if (!Check(context.commands.computePipelineBinds == 1 &&
               context.commands.dispatches == 1 && context.commands.uavBarriers == 1 &&
               context.commands.dispatchGroups == std::array<uint32_t, 3>{2, 3, 4},
               "compute pass did not record the expected RHI commands")) return false;
    auto ticket = context.ReadbackBufferAsync(buffer);
    std::vector<uint8_t> bytes;
    if (!Check(ticket && !ticket->IsReady() && !ticket->Read(bytes),
               "async readback completed synchronously")) return false;
    context.lastReadback->ready = true;
    return Check(ticket->IsReady() && ticket->Read(bytes) && bytes.size() == sizeof(initial) &&
                 std::memcmp(bytes.data(), initial.data(), sizeof(initial)) == 0,
                 "async readback returned incorrect buffer contents");
}

bool TestRenderGraphComputeBufferDependencies() {
    MockRenderContext context;
    RenderGraph graph(context);
    RHIBufferDesc desc;
    desc.size = 9 * 16; desc.stride = 16;
    desc.usage = RHIResourceUsage::UnorderedAccess |
                 RHIResourceUsage::ShaderResource |
                 RHIResourceUsage::CopySource;
    const auto output = graph.CreateBuffer("SHOutput", desc);
    std::vector<std::string> executed;
    graph.AddPass("ProjectSH", [&](RenderGraphBuilder& builder) {
        builder.ReadWriteUAV(output);
    }, [&](GpuCommandList& commands, const RenderGraphResources& resources) {
        if (resources.GetBuffer(output) && resources.GetBufferView(output)) {
            commands.Dispatch(1, 1, 1); executed.push_back("ProjectSH");
        }
    });
    graph.AddPass("ConsumeSH", [&](RenderGraphBuilder& builder) {
        builder.ReadBuffer(output);
    }, [&](GpuCommandList&, const RenderGraphResources& resources) {
        if (resources.GetBuffer(output)) executed.push_back("ConsumeSH");
    });
    if (!Check(graph.Execute(context.commands) &&
               executed == std::vector<std::string>({"ProjectSH", "ConsumeSH"}) &&
               context.bufferCreates == 1 && context.commands.dispatches == 1,
               "RenderGraph compute-buffer dependency execution failed")) return false;
    RenderGraph invalid(context);
    const auto unread = invalid.CreateBuffer("UnreadBuffer", desc);
    invalid.AddPass("InvalidBufferRead", [&](RenderGraphBuilder& builder) {
        builder.ReadBuffer(unread);
    }, {});
    return Check(!invalid.Compile() &&
                 invalid.GetLastError().find("uninitialized buffer") != std::string::npos,
                 "RenderGraph accepted an uninitialized buffer read");
}

bool TestHeadlessRendering() {
    AssetManager::Get().Clear();
    Scene scene("HeadlessRender");
    Actor* actor = scene.CreateActor("Cube");
    auto* meshRenderer = actor->AddComponent<MeshRendererComponent>();
    meshRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    meshRenderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    Actor* culledActor = scene.CreateActor("CulledCube");
    culledActor->GetTransform().position = { 10000.0f, 0.0f, 0.0f };
    auto* culledRenderer = culledActor->AddComponent<MeshRendererComponent>();
    culledRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    culledRenderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    Actor* transparentActor = scene.CreateActor("TransparentCube");
    transparentActor->GetTransform().position = { 0.0f, 0.0f, 1.0f };
    auto* transparentRenderer = transparentActor->AddComponent<MeshRendererComponent>();
    transparentRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    auto transparentMaterial = MaterialAsset::CreateDefault("TransparentTest");
    transparentMaterial->SetBlendMode(BlendMode::Transparent);
    transparentRenderer->SetMaterial(AssetManager::Get().Register(transparentMaterial));

    Camera camera;
    camera.LookAt({ 0.0f, 0.0f, -4.0f }, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    Renderer renderer(&context, &context, &context);
    int queuedUploadRuns = 0;
    GpuUploadQueue::Get().Enqueue([&queuedUploadRuns](IRHIDevice& uploadContext) {
        ++queuedUploadRuns;
        const uint8_t pixel[4] = { 255, 255, 255, 255 };
        uploadContext.UploadTexture2D(pixel, 1, 1);
    });
    renderer.RenderScene(scene, camera, true);

    if (!Check(queuedUploadRuns == 1 && GpuUploadQueue::Get().PendingCount() == 0,
               "GPU upload queue was not consumed on the render thread")) return false;
    if (!Check(context.beginFrames == 1 && context.endFrames == 1,
               "headless renderer frame lifecycle mismatch")) return false;
    if (!Check(context.shaderCreates >= 1, "headless shader was not created")) return false;
    if (!Check(context.vertexUploads == 1 && context.indexUploads == 1,
               "headless mesh upload mismatch")) return false;
    if (!Check(context.textureUploads == 3,
               "headless texture uploads missing material or named-binding fallback")) return false;
    if (!Check(context.commands.drawCalls == 3,
               "frustum culling emitted an unexpected draw count")) return false;
    const auto transparentPipeline = std::find(
        context.commands.pipelineBlendEnabled.begin(),
        context.commands.pipelineBlendEnabled.end(), true);
    return Check(transparentPipeline != context.commands.pipelineBlendEnabled.end() &&
                 transparentPipeline != context.commands.pipelineBlendEnabled.begin() &&
                 std::count(context.commands.pipelineBlendEnabled.begin(),
                            context.commands.pipelineBlendEnabled.end(), true) == 1,
                 "opaque/transparent render ordering or blend state mismatch");
}

bool TestCrashReportWriting() {
    CrashHandler::Install("MyEngineTests");
    const std::string path =
        CrashHandler::WriteDiagnosticReport("automated crash pipeline test");
    CrashHandler::Uninstall();
    if (!Check(!path.empty() && std::filesystem::exists(path),
               "crash diagnostic report was not created")) return false;

    std::ifstream input(path);
    const std::string text(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const bool valid =
        text.find("application=MyEngineTests") != std::string::npos &&
        text.find("automated crash pipeline test") != std::string::npos;
    input.close();
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    return Check(valid, "crash diagnostic report content mismatch");
}

bool TestTransformHierarchyWorldPosition() {
    Scene scene("TransformCase");
    Actor* root = scene.CreateActor("Root");
    root->GetTransform().position = Vec3 { 10.0f, 0.0f, 0.0f };

    Actor* child = scene.CreateActor("Child", root);
    child->GetTransform().position = Vec3 { 1.0f, 2.0f, 3.0f };

    const Vec3 world = child->GetWorldPosition();
    if (!Check(
        NearlyEqual(world.x, 11.0f) &&
        NearlyEqual(world.y, 2.0f) &&
        NearlyEqual(world.z, 3.0f),
        "child world position mismatch")) return false;

    Actor* rotated = scene.CreateActor("Rotated");
    rotated->GetTransform().position = Vec3{ 2.0f, 0.0f, 0.0f };
    rotated->GetTransform().rotation = Vec3{ 0.0f, 90.0f, 0.0f };
    rotated->GetTransform().scale = Vec3{ 2.0f, 3.0f, 4.0f };
    const Mat4 rotatedWorld = rotated->GetWorldMatrix();
    const Vec3 rotatedOrigin = rotatedWorld.TransformPoint(Vec3::Zero());
    if (!Check(NearlyEqual(rotatedOrigin.x, 2.0f) &&
               NearlyEqual(rotatedOrigin.y, 0.0f) &&
               NearlyEqual(rotatedOrigin.z, 0.0f),
               "rotated transform should not rotate its translation")) return false;
    if (!Check(NearlyEqual(rotatedWorld.TransformDir(Vec3::Right()).Length(), 2.0f) &&
               NearlyEqual(rotatedWorld.TransformDir(Vec3::Up()).Length(), 3.0f) &&
               NearlyEqual(rotatedWorld.TransformDir(Vec3::Forward()).Length(), 4.0f),
               "row-vector transform scale axes mismatch")) return false;

    Actor* rotatedParent = scene.CreateActor("RotatedParent");
    rotatedParent->GetTransform().position = Vec3{ 5.0f, 0.0f, 0.0f };
    rotatedParent->GetTransform().rotation = Vec3{ 0.0f, 90.0f, 0.0f };
    rotatedParent->GetTransform().scale = Vec3{ 2.0f, 3.0f, 4.0f };
    Actor* nested = scene.CreateActor("Nested", rotatedParent);
    nested->GetTransform().position = Vec3{ 0.0f, 0.0f, 1.0f };
    const Vec3 nestedWorld = nested->GetWorldPosition();
    if (!Check(NearlyEqual(nestedWorld.x, 9.0f) &&
               NearlyEqual(nestedWorld.y, 0.0f) &&
               NearlyEqual(nestedWorld.z, 0.0f),
               "row-vector parent-child matrix order mismatch")) return false;

    Mat4 invParent{};
    if (!Check(Mat4Invert(rotatedParent->GetWorldMatrix(), invParent),
               "parent matrix inversion failed")) return false;
    const Mat4 recoveredLocal = nested->GetWorldMatrix() * invParent;
    const Vec3 recoveredOrigin = recoveredLocal.TransformPoint(Vec3::Zero());
    if (!Check(NearlyEqual(recoveredOrigin.x, 0.0f) &&
               NearlyEqual(recoveredOrigin.y, 0.0f) &&
               NearlyEqual(recoveredOrigin.z, 1.0f),
               "row-vector world-to-local recovery order mismatch")) return false;
    return true;
}

bool TestCameraViewportProjectionStability() {
    Camera camera;
    camera.LookAt({ 0.0f, 0.0f, -4.0f }, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

    if (!Check(NearlyEqual(camera.GetForward().x, 0.0f) &&
               NearlyEqual(camera.GetForward().y, 0.0f) &&
               NearlyEqual(camera.GetForward().z, 1.0f),
               "camera forward basis mismatch")) return false;
    if (!Check(NearlyEqual(camera.GetRight().x, 1.0f) &&
               NearlyEqual(camera.GetRight().y, 0.0f) &&
               NearlyEqual(camera.GetRight().z, 0.0f),
               "camera right basis mismatch")) return false;
    if (!Check(NearlyEqual(camera.GetCamUp().x, 0.0f) &&
               NearlyEqual(camera.GetCamUp().y, 1.0f) &&
               NearlyEqual(camera.GetCamUp().z, 0.0f),
               "camera up basis mismatch")) return false;
    camera.Rotate(0.0f, 0.0f);
    if (!Check(NearlyEqual(camera.GetForward().x, 0.0f) &&
               NearlyEqual(camera.GetForward().y, 0.0f) &&
               camera.GetForward().z > 0.99f,
               "camera fly rotation basis jumps after LookAt")) return false;

    Ray center;
    Ray right;
    Ray left;
    Ray top;
    Ray bottom;
    if (!Check(camera.BuildRayFromNdc(0.0f, 0.0f, center), "center ray failed")) return false;
    if (!Check(camera.BuildRayFromNdc(1.0f, 0.0f, right), "right ray failed")) return false;
    if (!Check(camera.BuildRayFromNdc(-1.0f, 0.0f, left), "left ray failed")) return false;
    if (!Check(camera.BuildRayFromNdc(0.0f, 1.0f, top), "top ray failed")) return false;
    if (!Check(camera.BuildRayFromNdc(0.0f, -1.0f, bottom), "bottom ray failed")) return false;

    if (!Check(NearlyEqual(center.direction.x, 0.0f) &&
               NearlyEqual(center.direction.y, 0.0f) &&
               center.direction.z > 0.99f,
               "center ray direction mismatch")) return false;
    if (!Check(right.direction.x > 0.0f && left.direction.x < 0.0f,
               "horizontal NDC ray directions are flipped")) return false;
    if (!Check(top.direction.y > 0.0f && bottom.direction.y < 0.0f,
               "vertical NDC ray directions are flipped")) return false;

    const Mat4 viewProj = camera.GetViewProj();
    auto checkProjected = [&](const Ray& ray, float expectedX, float expectedY) {
        Vec4 clip = viewProj.Transform(Vec4::FromVec3(ray.origin, 1.0f));
        if (std::fabs(clip.w) < 1e-8f) return false;
        clip = clip * (1.0f / clip.w);
        return NearlyEqual(clip.x, expectedX, 1e-3f) &&
               NearlyEqual(clip.y, expectedY, 1e-3f) &&
               clip.z >= -1e-4f && clip.z <= 1.0f + 1e-4f;
    };

    if (!Check(checkProjected(center, 0.0f, 0.0f), "center ray projection drift")) return false;
    if (!Check(checkProjected(right, 1.0f, 0.0f), "right ray projection drift")) return false;
    if (!Check(checkProjected(left, -1.0f, 0.0f), "left ray projection drift")) return false;
    if (!Check(checkProjected(top, 0.0f, 1.0f), "top ray projection drift")) return false;
    return Check(checkProjected(bottom, 0.0f, -1.0f), "bottom ray projection drift");
}

bool TestCameraComponentAndGameViewport() {
    Scene scene("CameraViewport");
    GameViewport viewport(nullptr, nullptr, nullptr);
    viewport.Initialize(800, 400);
    viewport.ResolveFrameCamera(scene);
    if (!Check(!viewport.HasMainCamera(), "empty scene reported a main camera")) return false;
    if (!Check(NearlyEqual(viewport.GetCamera().GetAspect(), 2.0f),
               "fallback camera aspect mismatch")) return false;

    Actor* inactive = scene.CreateActor("InactiveCamera");
    auto* inactiveCamera = inactive->AddComponent<CameraComponent>();
    inactiveCamera->SetMainCamera(true);
    inactive->SetActive(false);

    Actor* actor = scene.CreateActor("MainCamera");
    actor->GetTransform().position = {1.0f, 2.0f, -3.0f};
    actor->GetTransform().rotation = {0.0f, 0.0f, 0.0f};
    auto* camera = actor->AddComponent<CameraComponent>();
    camera->SetMainCamera(true);
    camera->SetFovYDegrees(72.0f);
    camera->SetNearClip(0.25f);
    camera->SetFarClip(500.0f);
    camera->SetClearColor({0.2f, 0.3f, 0.4f});

    viewport.ResolveFrameCamera(scene);
    if (!Check(viewport.HasMainCamera() && viewport.GetMainCameraComponent() == camera,
               "game viewport did not resolve enabled main camera")) return false;
    if (!Check(NearlyEqual(viewport.GetCamera().GetFovY(), 72.0f) &&
               NearlyEqual(viewport.GetCamera().GetNear(), 0.25f) &&
               NearlyEqual(viewport.GetCamera().GetFar(), 500.0f),
               "game viewport camera settings mismatch")) return false;

    const std::string serialized = SceneSerializer::SaveToString(scene);
    Scene loaded("LoadedCamera");
    if (!Check(SceneSerializer::LoadFromString(loaded, serialized),
               "camera component scene load failed")) return false;
    Actor* loadedActor = loaded.FindByName("MainCamera");
    auto* loadedCamera = loadedActor ? loadedActor->GetComponent<CameraComponent>() : nullptr;
    return Check(loadedCamera && loadedCamera->IsMainCamera() &&
                 NearlyEqual(loadedCamera->GetFovYDegrees(), 72.0f) &&
                 NearlyEqual(loadedCamera->GetClearColor().y, 0.3f),
                 "camera component round trip mismatch");
}

bool TestSceneViewportControllerRayStability() {
    SceneViewportController viewport(nullptr, nullptr, nullptr);
    viewport.Initialize(800, 600);

    int x = -1, y = -1, w = 0, h = 0;
    viewport.GetViewportRect(x, y, w, h);
    if (!Check(x == 0 && y == 0 && w == 800 && h == 600,
               "initial viewport rect mismatch")) return false;

    Ray center;
    if (!Check(viewport.BuildRayFromScreen(400.0f, 300.0f, center),
               "center screen ray failed")) return false;
    if (!Check(NearlyEqual(center.direction.x, 0.0f) &&
               NearlyEqual(center.direction.y, 0.0f) &&
               center.direction.z > 0.99f,
               "center screen ray direction drifted")) return false;

    viewport.SetViewportRect(100, 50, 400, 200);
    viewport.GetViewportRect(x, y, w, h);
    if (!Check(x == 100 && y == 50 && w == 400 && h == 200,
               "editor viewport rect mismatch")) return false;

    Ray topLeft;
    Ray bottomRight;
    if (!Check(viewport.BuildRayFromScreen(100.0f, 50.0f, topLeft),
               "top-left editor ray failed")) return false;
    if (!Check(viewport.BuildRayFromScreen(500.0f, 250.0f, bottomRight),
               "bottom-right editor ray failed")) return false;
    if (!Check(topLeft.direction.x < 0.0f && topLeft.direction.y > 0.0f,
               "top-left editor ray direction mismatch")) return false;
    if (!Check(bottomRight.direction.x > 0.0f && bottomRight.direction.y < 0.0f,
               "bottom-right editor ray direction mismatch")) return false;

    viewport.FrameDirection(SceneViewDirection::Front, Vec3::Zero(), 8.0f);
    if (!Check(NearlyEqual(viewport.GetCamera().GetForward().x, 0.0f) &&
               NearlyEqual(viewport.GetCamera().GetForward().y, 0.0f) &&
               viewport.GetCamera().GetForward().z > 0.99f,
               "front view direction mismatch")) return false;
    viewport.FrameDirection(SceneViewDirection::Top, Vec3::Zero(), 8.0f);
    if (!Check(viewport.GetCamera().GetForward().y < -0.99f,
               "top view direction mismatch")) return false;
    const Vec3 orbitTarget {1.0f, 2.0f, 3.0f};
    viewport.FrameDirection(SceneViewDirection::Front, orbitTarget, 8.0f);
    const Vec3 beforeOrbitPosition = viewport.GetCamera().GetPosition();
    const float beforeOrbitDistance = (beforeOrbitPosition - orbitTarget).Length();
    viewport.OrbitAroundFocus(orbitTarget, 30.0f, -12.0f);
    const Vec3 afterOrbitPosition = viewport.GetCamera().GetPosition();
    const float afterOrbitDistance = (afterOrbitPosition - orbitTarget).Length();
    if (!Check(NearlyEqual(beforeOrbitDistance, afterOrbitDistance, 1e-3f),
               "scene viewport orbit changed focus distance")) return false;
    if (!Check((afterOrbitPosition - beforeOrbitPosition).Length() > 0.1f,
               "scene viewport orbit did not move camera")) return false;
    if (!Check((viewport.GetCamera().GetTarget() - orbitTarget).Length() < 1e-3f,
               "scene viewport orbit target mismatch")) return false;

    const float aspect = viewport.GetCamera().GetAspect();
    viewport.ToggleProjectionMode();
    if (!Check(viewport.IsOrthographic() &&
               viewport.GetCamera().GetProjectionMode() == ProjectionMode::Orthographic,
               "scene viewport did not switch to orthographic")) return false;
    if (!Check(NearlyEqual(
            viewport.GetCamera().GetOrthoWidth() / viewport.GetCamera().GetOrthoHeight(),
            aspect,
            1e-3f),
            "orthographic aspect mismatch")) return false;
    Ray orthoCenter;
    if (!Check(viewport.BuildRayFromScreen(300.0f, 150.0f, orthoCenter),
               "orthographic center ray failed")) return false;

    viewport.SetViewportRect(100, 50, 300, 300);
    const float squareAspect = viewport.GetCamera().GetAspect();
    if (!Check(NearlyEqual(
            viewport.GetCamera().GetOrthoWidth() / viewport.GetCamera().GetOrthoHeight(),
            squareAspect,
            1e-3f),
            "orthographic resize did not preserve aspect")) return false;

    viewport.ToggleProjectionMode();
    return Check(!viewport.IsOrthographic() &&
                 viewport.GetCamera().GetProjectionMode() == ProjectionMode::Perspective &&
                 NearlyEqual(viewport.GetCamera().GetAspect(), squareAspect),
                 "scene viewport did not restore perspective aspect");
}

bool TestDefaultSceneFactoryLeavesScenesUnmodified() {
    Scene scene("DefaultFactory");
    DefaultSceneFactory::PopulateIfEmpty(scene);
    if (!Check(scene.ActorCount() == 0,
               "default scene factory should leave empty scenes empty")) return false;

    scene.CreateActor("UserActor");
    const size_t count = scene.ActorCount();
    DefaultSceneFactory::PopulateIfEmpty(scene);
    return Check(scene.ActorCount() == count,
                 "default scene factory should not mutate authored scenes");
}

bool TestInputBoundaries() {
    Input::Flush();

    if (!Check(!Input::IsKeyDown(-1), "negative key index should be false")) return false;
    if (!Check(!Input::IsKeyDown(Input::k_MaxKeys), "overflow key index should be false")) return false;
    if (!Check(!Input::IsMouseDown(0), "mouse button 0 should be invalid")) return false;
    if (!Check(!Input::IsMouseDown(Input::k_MaxButtons), "overflow mouse index should be false")) return false;

    const int key = Input::k_MaxKeys - 1;
    Input::OnKeyUp(key);
    Input::Flush();
    Input::OnKeyDown(key);
    if (!Check(Input::IsKeyDown(key), "key should be down")) return false;
    if (!Check(Input::IsKeyPressed(key), "key should be pressed on transition")) return false;
    Input::Flush();
    if (!Check(!Input::IsKeyPressed(key), "key pressed should clear next frame")) return false;
    Input::OnKeyUp(key);
    if (!Check(Input::IsKeyReleased(key), "key release transition failed")) return false;

    const int btn = Input::k_MaxButtons - 1;
    Input::OnMouseButton(btn, false);
    Input::Flush();
    Input::OnMouseButton(btn, true);
    if (!Check(Input::IsMouseDown(btn), "mouse button should be down")) return false;
    if (!Check(Input::IsMousePressed(btn), "mouse button pressed transition failed")) return false;
    Input::Flush();
    Input::OnMouseButton(btn, false);
    if (!Check(Input::IsMouseReleased(btn), "mouse button release transition failed")) return false;

    return true;
}

bool TestGamepadStateTransitions() {
    const SDL_JoystickID pad = 42;

    Input::OnGamepadAdded(pad);
    if (!Check(Input::IsGamepadConnected(pad), "gamepad should be connected after add")) return false;
    if (!Check(Input::GetGamepadCount() == 1, "gamepad count should be 1")) return false;

    Input::Flush();
    Input::OnGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH, true);
    if (!Check(Input::IsGamepadButtonDown(pad, SDL_GAMEPAD_BUTTON_SOUTH), "gamepad button should be down")) return false;
    if (!Check(Input::IsGamepadButtonPressed(pad, SDL_GAMEPAD_BUTTON_SOUTH), "gamepad button press transition failed")) return false;

    Input::Flush();
    if (!Check(!Input::IsGamepadButtonPressed(pad, SDL_GAMEPAD_BUTTON_SOUTH), "pressed should clear next frame")) return false;

    Input::OnGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH, false);
    if (!Check(Input::IsGamepadButtonReleased(pad, SDL_GAMEPAD_BUTTON_SOUTH), "gamepad button release transition failed")) return false;

    Input::OnGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, 16384);
    if (!Check(NearlyEqual(Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), 0.5f, 0.02f),
               "gamepad axis normalization failed")) return false;

    Input::OnGamepadRemoved(pad);
    if (!Check(!Input::IsGamepadConnected(pad), "gamepad should be disconnected after remove")) return false;

    return true;
}

bool TestAudioSourceComponentSerialization() {
    Scene scene("AudioSourceRoundTrip");
    Actor* actor = scene.CreateActor("Emitter");
    auto* audio = actor->AddComponent<AudioSourceComponent>();
    audio->SetClipPath("Content/Audio/beep.wav");
    audio->SetPlayOnStart(false);
    audio->SetLoop(true);
    audio->SetSpatial(false);
    audio->SetVolume(0.35f);
    audio->SetPitch(1.25f);
    audio->SetMinDistance(2.0f);
    audio->SetMaxDistance(25.0f);

    Scene loaded("LoadedAudio");
    if (!Check(SceneSerializer::LoadFromString(loaded, SceneSerializer::SaveToString(scene)),
               "audio source scene roundtrip failed")) return false;
    Actor* loadedActor = loaded.FindByName("Emitter");
    auto* loadedAudio = loadedActor ? loadedActor->GetComponent<AudioSourceComponent>() : nullptr;
    if (!Check(loadedAudio, "audio source component was not restored")) return false;
    return Check(loadedAudio->GetClipPath() == "Content/Audio/beep.wav" &&
                 !loadedAudio->GetPlayOnStart() &&
                 loadedAudio->GetLoop() &&
                 !loadedAudio->GetSpatial() &&
                 NearlyEqual(loadedAudio->GetVolume(), 0.35f) &&
                 NearlyEqual(loadedAudio->GetPitch(), 1.25f) &&
                 NearlyEqual(loadedAudio->GetMinDistance(), 2.0f) &&
                 NearlyEqual(loadedAudio->GetMaxDistance(), 25.0f),
                 "audio source fields changed after serialization");
}

bool TestInputActionMapJsonAndEvaluation() {
    namespace fs = std::filesystem;
    Input::Shutdown();
    Input::SetDefaultActionMap();

    Input::OnKeyUp(SDL_SCANCODE_SPACE);
    Input::Flush();
    Input::OnKeyDown(SDL_SCANCODE_SPACE);
    if (!Check(Input::IsActionDown("Jump"), "Jump action should be down from keyboard")) return false;
    if (!Check(Input::IsActionPressed("Jump"), "Jump action should be pressed from keyboard")) return false;
    Input::Flush();
    if (!Check(!Input::IsActionPressed("Jump"), "Jump pressed should clear next frame")) return false;
    Input::OnKeyUp(SDL_SCANCODE_SPACE);
    if (!Check(Input::IsActionReleased("Jump"), "Jump action should be released from keyboard")) return false;

    Input::Flush();
    Input::OnKeyDown(SDL_SCANCODE_D);
    Input::OnKeyDown(SDL_SCANCODE_W);
    const Math::Vec2 move = Input::GetAxis2D("Move");
    if (!Check(NearlyEqual(move.x, 1.0f) && NearlyEqual(move.y, 1.0f),
               "Move action should combine keyboard X/Y bindings")) return false;
    Input::OnKeyUp(SDL_SCANCODE_D);
    Input::OnKeyUp(SDL_SCANCODE_W);

    const auto path = fs::temp_directory_path() /
        ("myengine_input_map_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    {
        std::ofstream output(path);
        output << R"({
  "version": 1,
  "actions": [
    {
      "name": "Throttle",
      "type": "Axis1D",
      "bindings": [
        { "source": "Keyboard/A", "scale": -2.0 },
        { "source": "Gamepad/LeftX", "deadZone": 0.25 }
      ]
    },
    {
      "name": "Look",
      "type": "Axis2D",
      "bindings": [
        { "x": "Mouse/DeltaX", "y": "Mouse/DeltaY" },
        { "x": "Gamepad/RightX", "y": "Gamepad/RightY", "deadZone": 0.15, "scaleY": -1.0 }
      ]
    }
  ]
})";
    }
    std::string error;
    if (!Check(Input::LoadActionMapFromFile(path, &error), "custom input map load failed: " + error)) return false;
    Input::Flush();
    Input::OnKeyDown(SDL_SCANCODE_A);
    if (!Check(NearlyEqual(Input::GetAxis1D("Throttle"), -1.0f),
               "Axis1D keyboard scale should clamp")) return false;
    Input::OnKeyUp(SDL_SCANCODE_A);

    const SDL_JoystickID pad = 77;
    Input::OnGamepadAdded(pad);
    Input::OnGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, 1000);
    if (!Check(NearlyEqual(Input::GetAxis1D("Throttle"), 0.0f),
               "Axis1D gamepad dead zone should zero small input")) return false;
    Input::OnGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, 20000);
    if (!Check(Input::GetAxis1D("Throttle") > 0.55f,
               "Axis1D gamepad axis should evaluate above dead zone")) return false;

    Input::OnMouseMove(10, 12, 2, -3);
    const Math::Vec2 look = Input::GetAxis2D("Look");
    if (!Check(NearlyEqual(look.x, 1.0f) && NearlyEqual(look.y, -1.0f),
               "Axis2D mouse delta should clamp")) return false;

    InputActionMap invalid;
    const nlohmann::json invalidJson = {
        {"version", 1},
        {"actions", nlohmann::json::array({
            {{"name", "Bad"}, {"type", "Button"},
             {"bindings", nlohmann::json::array({{{"source", "Keyboard/NoSuchKey"}}})}}
        })},
    };
    if (!Check(!invalid.LoadFromJson(invalidJson, &error),
               "invalid input source should fail parsing")) return false;

    std::error_code ec;
    fs::remove(path, ec);
    Input::Shutdown();
    Input::SetDefaultActionMap();
    return true;
}

bool TestAssetFileImporters() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_asset_import_test";
    fs::create_directories(root);

    const fs::path texPath = root / "albedo.ppm";
    const fs::path mtlPath = root / "tri.mtl";
    const fs::path objPath = root / "tri.obj";

    {
        std::ofstream tex(texPath, std::ios::binary);
        tex << "P6\n2 2\n255\n";
        const unsigned char pixels[] = {
            255, 0,   0,
            0,   255, 0,
            0,   0,   255,
            255, 255, 255,
        };
        tex.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    }

    {
        std::ofstream mtl(mtlPath, std::ios::binary);
        mtl << "newmtl Material0\n";
        mtl << "Kd 1 1 1\n";
        mtl << "map_Kd albedo.ppm\n";
    }

    {
        std::ofstream obj(objPath, std::ios::binary);
        obj << "mtllib tri.mtl\n";
        obj << "o Tri\n";
        obj << "usemtl Material0\n";
        obj << "v 0 0 0\n";
        obj << "v 1 0 0\n";
        obj << "v 0 1 0\n";
        obj << "vt 0 0\n";
        obj << "vt 1 0\n";
        obj << "vt 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "vn 0 0 1\n";
        obj << "f 1/1/1 2/2/2 3/3/3\n";
    }

    AssetManager& am = AssetManager::Get();
    auto tex = am.Load<TextureAsset>(texPath.string());
    if (!Check(tex.IsValid(), "texture import should succeed")) return false;
    if (!Check(tex->GetWidth() == 2 && tex->GetHeight() == 2, "texture dimensions mismatch")) return false;
    if (!Check(tex->GetPixelData().size() == 16, "texture pixel data size mismatch")) return false;

    auto model = am.Load<ModelAsset>(objPath.string());
    if (!Check(model.IsValid(), "model import should succeed")) return false;
    if (!Check(model->GetMesh() && model->GetMesh()->VertexCount() == 3, "model vertex count mismatch")) return false;
    if (!Check(model->MaterialCount() == 1, "model material count mismatch")) return false;
    if (!Check(model->GetMaterial(0).IsValid(), "model material should be valid")) return false;
    if (!Check(model->GetMaterial(0)->HasTexture("BaseColorMap"), "material should keep imported texture")) return false;

    am.Clear();
    fs::remove_all(root);
    return true;
}

template<typename T>
void AppendBinary(std::vector<uint8_t>& output, const std::vector<T>& values) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(values.data());
    output.insert(output.end(), bytes, bytes + values.size() * sizeof(T));
}

bool TestGltfImportAndStableMeta() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_gltf_import_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path gltfPath = root / "skinned_triangle.gltf";
    const fs::path binPath = root / "skinned_triangle.bin";

    std::vector<uint8_t> binary;
    AppendBinary(binary, std::vector<float>{
        0,0,0, 1,0,0, 0,1,0
    }); // 0: positions, 36 bytes
    AppendBinary(binary, std::vector<float>{
        0,0,1, 0,0,1, 0,0,1
    }); // 36: normals
    AppendBinary(binary, std::vector<float>{
        0,0, 1,0, 0,1
    }); // 72: uv
    AppendBinary(binary, std::vector<uint16_t>{
        0,0,0,0, 0,0,0,0, 0,0,0,0
    }); // 96: joints
    AppendBinary(binary, std::vector<float>{
        1,0,0,0, 1,0,0,0, 1,0,0,0
    }); // 120: weights
    AppendBinary(binary, std::vector<uint16_t>{ 0,1,2 }); // 168: indices
    binary.resize(176, 0); // 4-byte align
    AppendBinary(binary, std::vector<float>{
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    }); // 176: inverse bind
    AppendBinary(binary, std::vector<float>{ 0,1 }); // 240: times
    AppendBinary(binary, std::vector<float>{
        0,0,0, 1,0,0
    }); // 248: translations

    {
        std::ofstream output(binPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(binary.data()),
                     static_cast<std::streamsize>(binary.size()));
    }

    nlohmann::json gltf;
    gltf["asset"] = { { "version", "2.0" }, { "generator", "MyEngineTests" } };
    gltf["buffers"] = nlohmann::json::array({
        { { "uri", "skinned_triangle.bin" }, { "byteLength", binary.size() } }
    });
    gltf["bufferViews"] = nlohmann::json::array({
        { { "buffer",0 }, { "byteOffset",0 },   { "byteLength",36 } },
        { { "buffer",0 }, { "byteOffset",36 },  { "byteLength",36 } },
        { { "buffer",0 }, { "byteOffset",72 },  { "byteLength",24 } },
        { { "buffer",0 }, { "byteOffset",96 },  { "byteLength",24 } },
        { { "buffer",0 }, { "byteOffset",120 }, { "byteLength",48 } },
        { { "buffer",0 }, { "byteOffset",168 }, { "byteLength",6 } },
        { { "buffer",0 }, { "byteOffset",176 }, { "byteLength",64 } },
        { { "buffer",0 }, { "byteOffset",240 }, { "byteLength",8 } },
        { { "buffer",0 }, { "byteOffset",248 }, { "byteLength",24 } },
    });
    gltf["accessors"] = nlohmann::json::array({
        { { "bufferView",0 }, { "componentType",5126 }, { "count",3 }, { "type","VEC3" },
          { "min", nlohmann::json::array({0,0,0}) }, { "max", nlohmann::json::array({1,1,0}) } },
        { { "bufferView",1 }, { "componentType",5126 }, { "count",3 }, { "type","VEC3" } },
        { { "bufferView",2 }, { "componentType",5126 }, { "count",3 }, { "type","VEC2" } },
        { { "bufferView",3 }, { "componentType",5123 }, { "count",3 }, { "type","VEC4" } },
        { { "bufferView",4 }, { "componentType",5126 }, { "count",3 }, { "type","VEC4" } },
        { { "bufferView",5 }, { "componentType",5123 }, { "count",3 }, { "type","SCALAR" } },
        { { "bufferView",6 }, { "componentType",5126 }, { "count",1 }, { "type","MAT4" } },
        { { "bufferView",7 }, { "componentType",5126 }, { "count",2 }, { "type","SCALAR" },
          { "min", nlohmann::json::array({0}) }, { "max", nlohmann::json::array({1}) } },
        { { "bufferView",8 }, { "componentType",5126 }, { "count",2 }, { "type","VEC3" } },
    });
    gltf["materials"] = nlohmann::json::array({
        {
            { "name","RedMetal" },
            { "pbrMetallicRoughness", {
                { "baseColorFactor", nlohmann::json::array({0.8,0.1,0.05,1.0}) },
                { "metallicFactor",0.7 },
                { "roughnessFactor",0.25 }
            }}
        }
    });
    gltf["meshes"] = nlohmann::json::array({
        {
            { "name","Triangle" },
            { "primitives", nlohmann::json::array({
                {
                    { "attributes", {
                        { "POSITION",0 }, { "NORMAL",1 }, { "TEXCOORD_0",2 },
                        { "JOINTS_0",3 }, { "WEIGHTS_0",4 }
                    }},
                    { "indices",5 }, { "material",0 }
                }
            })}
        }
    });
    gltf["nodes"] = nlohmann::json::array({
        { { "name","RootBone" } },
        { { "name","MeshNode" }, { "mesh",0 }, { "skin",0 } }
    });
    gltf["skins"] = nlohmann::json::array({
        { { "name","Skin" }, { "joints",nlohmann::json::array({0}) },
          { "inverseBindMatrices",6 } }
    });
    gltf["animations"] = nlohmann::json::array({
        {
            { "name","Move" },
            { "samplers", nlohmann::json::array({
                { { "input",7 }, { "output",8 }, { "interpolation","LINEAR" } }
            })},
            { "channels", nlohmann::json::array({
                { { "sampler",0 }, { "target", { { "node",0 }, { "path","translation" } } } }
            })}
        }
    });
    gltf["scenes"] = nlohmann::json::array({
        { { "nodes",nlohmann::json::array({0,1}) } }
    });
    gltf["scene"] = 0;
    {
        std::ofstream output(gltfPath);
        output << gltf.dump(2);
    }

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    AssetMeta meta = AssetMeta::Create(gltfPath.string());
    if (!Check(AssetMeta::Save(meta), "failed to author glTF metadata")) return false;
    ModelHandle model = manager.Load<ModelAsset>(gltfPath.string());
    if (!Check(model.IsValid(), "glTF model import failed")) return false;
    if (!Check(model->GetMesh()->VertexCount() == 3 && model->GetMesh()->IndexCount() == 3,
               "glTF geometry mismatch")) return false;
    if (!Check(model->MaterialCount() == 1 &&
               NearlyEqual(model->GetMaterial(0)->GetFloat("Metallic"), 0.7f),
               "glTF PBR material mismatch")) return false;
    if (!Check(model->HasSkin() && model->GetBones().size() == 1 &&
               model->GetSkinWeights().size() == 3,
               "glTF skin import mismatch")) return false;
    if (!Check(model->GetAnimations().size() == 1 &&
               model->GetAnimations()[0].tracks.size() == 1,
               "glTF animation import mismatch")) return false;
    if (!Check(model->GetMesh()->GetVertices()[0].tangent.LengthSq() > 0.5f,
               "glTF tangent generation failed")) return false;
    if (!Check(model->GetDependencies().size() >= 2,
               "glTF model dependencies were not tracked")) return false;
    if (!Check(!model->GetUuid().empty() &&
               fs::exists(gltfPath.string() + ".meta"),
               "glTF stable metadata was not generated")) return false;

    const AssetID firstID = model->GetID();
    const std::string firstUuid = model->GetUuid();
    manager.Clear();
    model = manager.Load<ModelAsset>(gltfPath.string());
    if (!Check(model.IsValid() && model->GetID() == firstID &&
               model->GetUuid() == firstUuid,
               "asset UUID changed after reload")) return false;

    manager.Clear();
    fs::remove_all(root);
    return true;
}

bool TestAssetAsyncLoadingAndHotReload() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_asset_reload_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path texturePath = root / "reload.ppm";

    auto writeTexture = [&texturePath](uint8_t red, uint8_t green, uint8_t blue) {
        std::ofstream output(texturePath, std::ios::binary);
        output << "P6\n1 1\n255\n";
        const char pixel[3] = {
            static_cast<char>(red),
            static_cast<char>(green),
            static_cast<char>(blue)
        };
        output.write(pixel, sizeof(pixel));
    };

    writeTexture(255, 0, 0);
    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    std::shared_ptr<Asset> loaded = manager.LoadAsync(texturePath.string()).get();
    auto texture = std::dynamic_pointer_cast<TextureAsset>(loaded);
    if (!Check(texture && texture->IsReady(), "async texture load failed")) return false;
    if (!Check(texture->GetVersion() == 1 && texture->GetPixelData()[0] == 255,
               "async texture contents mismatch")) return false;

    TextureAsset* originalAddress = texture.get();
    const auto previousWriteTime = fs::last_write_time(texturePath);
    writeTexture(0, 255, 0);
    fs::last_write_time(texturePath, previousWriteTime + std::chrono::seconds(2));

    if (!Check(manager.PollHotReload() == 1, "hot reload did not detect source change")) return false;
    auto reloaded = manager.GetByPath<TextureAsset>(texturePath.string());
    if (!Check(reloaded.Get() == originalAddress,
               "hot reload invalidated an existing asset handle")) return false;
    if (!Check(reloaded->GetVersion() == 2 && reloaded->GetPixelData()[0] == 0 &&
               reloaded->GetPixelData()[1] == 255,
               "hot reload did not update texture contents or version")) return false;

    manager.Unload(texturePath.string());
    if (!Check(!manager.IsLoaded(texturePath.string()),
               "path-based unload failed for UUID-backed asset")) return false;
    manager.Clear();
    fs::remove_all(root);
    return true;
}

bool TestAssetManagerFailureRollback() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "myengine_asset_failure_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path assetPath = root / "rollback.robustasset";
    {
        std::ofstream output(assetPath, std::ios::binary);
        output << "version 1";
    }

    AssetManager& manager = AssetManager::Get();
    manager.Clear();
    int loaderMode = 0;
    manager.RegisterLoader("robustasset", [&loaderMode](const std::string& path) -> std::shared_ptr<Asset> {
        if (loaderMode == 1) {
            return std::static_pointer_cast<Asset>(MeshAsset::CreateTriangle("WrongType"));
        }
        if (loaderMode == 2) {
            throw std::runtime_error("loader failed intentionally");
        }
        auto texture = std::make_shared<TextureAsset>(path);
        TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        texture->SetPixelData({ 17, 0, 0, 255 }, desc);
        return std::static_pointer_cast<Asset>(texture);
    });
    manager.RegisterLoader("throwasset", [](const std::string& path) -> std::shared_ptr<Asset> {
        (void)path;
        throw std::runtime_error("async loader failed intentionally");
    });

    TextureHandle texture = manager.Load<TextureAsset>(assetPath.string());
    if (!Check(texture.IsValid(), "initial robust asset load failed")) return false;
    TextureAsset* original = texture.Get();
    const uint64_t version = texture->GetVersion();
    if (!Check(texture->GetPixelData()[0] == 17, "initial robust asset contents mismatch")) return false;

    const auto previousWriteTime = fs::last_write_time(assetPath);
    {
        std::ofstream output(assetPath, std::ios::binary | std::ios::trunc);
        output << "wrong type";
    }
    fs::last_write_time(assetPath, previousWriteTime + std::chrono::seconds(2));
    loaderMode = 1;
    if (!Check(manager.PollHotReload() == 0, "wrong-type reload should fail")) return false;
    if (!Check(texture.Get() == original && texture->GetVersion() == version &&
               texture->GetPixelData()[0] == 17,
               "wrong-type reload mutated existing asset")) return false;

    {
        std::ofstream output(assetPath, std::ios::binary | std::ios::trunc);
        output << "throw";
    }
    fs::last_write_time(assetPath, previousWriteTime + std::chrono::seconds(4));
    loaderMode = 2;
    if (!Check(manager.PollHotReload() == 0, "throwing reload should fail")) return false;
    if (!Check(texture.Get() == original && texture->GetVersion() == version &&
               texture->GetPixelData()[0] == 17,
               "throwing reload mutated existing asset")) return false;

    const fs::path throwPath = root / "async.throwasset";
    {
        std::ofstream output(throwPath, std::ios::binary);
        output << "throw";
    }
    std::shared_ptr<Asset> asyncResult = manager.LoadAsync(throwPath.string()).get();
    if (!Check(!asyncResult && !manager.IsLoaded(throwPath.string()),
               "async loader exception should not cache an asset")) return false;

    manager.Clear();
    fs::remove_all(root);
    return true;
}

bool TestSceneSerializerMalformedDataIsolation() {
    ComponentRegistry::Get().Register("ThrowingDeserialize", [] {
        return std::make_unique<ThrowingDeserializeComponent>();
    });

    const std::string json = R"json(
{
  "name": "MalformedScene",
  "actors": [
    "not an actor",
    {
      "id": 1,
      "name": "Survivor",
      "active": true,
      "transform": { "position": ["bad", 2, 3] },
      "components": [
        { "type": "ThrowingDeserialize", "enabled": true, "data": {} },
        { "type": "MissingComponentType", "enabled": true, "data": {} },
        "not a component"
      ]
    }
  ],
  "nextID": 2
}
)json";

    Scene scene("Before");
    if (!Check(SceneSerializer::LoadFromString(scene, json),
               "malformed scene should load with isolated failures")) return false;
    Actor* survivor = scene.FindByName("Survivor");
    if (!Check(survivor != nullptr, "survivor actor missing after malformed load")) return false;
    if (!Check(NearlyEqual(survivor->GetTransform().position.x, 0.0f),
               "malformed Vec3 should fall back to default")) return false;
    return Check(!survivor->HasComponentType("ThrowingDeserialize"),
                 "failed component deserialize should remove component");
}

bool TestScriptHotReloadFailureRollback() {
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "myengine_script_reload_rollback.as";
    {
        std::ofstream output(path, std::ios::binary);
        output << "class Script { void Update(float dt) { Actor::Translate(Vec3(1 * dt, 0, 0)); } }\n";
    }

    Scene scene("ScriptRollback");
    Actor* actor = scene.CreateActor("Scripted");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetScriptPath(path.string());
    scene.OnUpdate(1.0f);
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 1.0f),
               "initial script file update failed: " + script->GetLastError())) return false;

    const auto previousWriteTime = fs::last_write_time(path);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "class Script { void Update(\n";
    }
    fs::last_write_time(path, previousWriteTime + std::chrono::seconds(2));
    scene.OnUpdate(1.0f);
    fs::remove(path);

    if (!Check(script->IsCompiled(), "bad hot reload should keep previous runtime compiled")) return false;
    if (!Check(!script->GetLastError().empty(), "bad hot reload should report an error")) return false;
    return Check(NearlyEqual(actor->GetTransform().position.x, 2.0f),
                 "bad hot reload did not keep previous script running");
}

bool TestMeshDerivedData() {
    auto mesh = MeshAsset::CreateCube("DerivedDataCube");
    if (!Check(mesh->GetLods().size() >= 2,
               "mesh LOD chain was not generated")) return false;
    if (!Check(mesh->GetLod(0).indices.size() == mesh->GetIndices().size() &&
               mesh->GetLod(1).indices.size() < mesh->GetLod(0).indices.size(),
               "mesh LOD index counts are invalid")) return false;
    const MeshColliderData& collider = mesh->GetColliderData();
    if (!Check(collider.vertices.size() == 8 && collider.indices.size() == 36,
               "automatic box collider data was not generated")) return false;
    return Check(NearlyEqual(collider.bounds.min.x, -0.5f) &&
                 NearlyEqual(collider.bounds.max.x, 0.5f),
                 "automatic collider bounds mismatch");
}

bool TestTextureDerivedData() {
    TextureDesc desc;
    desc.width = 4;
    desc.height = 4;
    std::vector<uint8_t> pixels(4 * 4 * 4, 255);
    auto texture = std::make_shared<TextureAsset>("__builtin__/DerivedTexture");
    texture->SetPixelData(std::move(pixels), desc);
    if (!Check(texture->GetMipLevels() == 3 && texture->GetMips().size() == 3,
               "texture mip chain was not generated")) return false;
    if (!Check(texture->GetMips()[1].width == 2 && texture->GetMips()[2].width == 1,
               "texture mip dimensions are invalid")) return false;
    return Check(texture->GetCompressedMip(0).size() == 8 &&
                 texture->GetCompressedMip(2).size() == 8,
                 "BC1 texture compression output size mismatch");
}

bool TestMemoryLinearAllocator() {
    LinearAllocator arena;
    if (!Check(arena.Init(4096), "LinearAllocator::Init failed")) return false;
    void* p1 = arena.Allocate(64, 8);
    void* p2 = arena.Allocate(128, 16);
    if (!Check(p1 != nullptr && p2 != nullptr, "LinearAllocator bump failed")) return false;
    if (!Check(reinterpret_cast<std::uintptr_t>(p2) > reinterpret_cast<std::uintptr_t>(p1),
               "LinearAllocator ordering unexpected")) return false;
    arena.Reset();
    void* p3 = arena.Allocate(4000, 8);
    if (!Check(p3 != nullptr, "LinearAllocator reuse after Reset failed")) return false;
    arena.Shutdown();
    return true;
}

bool TestMemoryPoolAllocator() {
    PoolAllocator<int> pool(4);
    int* a = pool.Allocate(1);
    int* b = pool.Allocate(2);
    if (!Check(a && b, "PoolAllocator Allocate failed")) return false;
    if (!Check(pool.LiveCount() == 2, "PoolAllocator live count")) return false;
    pool.Free(a);
    if (!Check(pool.LiveCount() == 1, "PoolAllocator after one Free")) return false;
    int* c = pool.Allocate(3);
    if (!Check(c && *c == 3, "PoolAllocator recycle slot")) return false;
    pool.Free(b);
    pool.Free(c);
    if (!Check(pool.LiveCount() == 0, "PoolAllocator empty")) return false;
    if (!Check(pool.Allocate(42) != nullptr, "PoolAllocator fill after empty")) return false;
    return true;
}

bool TestMemoryServiceHeapRoundTrip() {
    void* p = MemoryService::Get().Allocate(AllocTag::Test, 32, 8, __FILE__, __LINE__);
    if (!Check(p != nullptr, "MemoryService::Allocate failed")) {
        return false;
    }
    std::memset(p, 0xAB, 32);
    MemoryService::Get().Free(p, __FILE__, __LINE__);
    return true;
}

bool TestSceneAndAssetMemoryCounters() {
    MemoryService::Get().SetSceneActorBudget(1000);
    Scene sc("MemScene");
    Actor* a = sc.CreateActor("A");
    Actor* b = sc.CreateActor("B");
    (void)a;
    (void)b;
    if (!Check(MemoryService::Get().GetSceneLiveActorCount() == 2, "scene live actor count")) return false;
    sc.DestroyActor(a);
    if (!Check(MemoryService::Get().GetSceneLiveActorCount() == 1, "scene count after DestroyActor")) return false;
    sc.Clear();
    if (!Check(MemoryService::Get().GetSceneLiveActorCount() == 0, "scene count after Clear")) return false;

    AssetManager& am = AssetManager::Get();
    am.Clear();
    (void)am.GetCubeMesh();
    if (!Check(am.GetEstimatedAssetCpuBytes() > 0, "asset CPU estimate after builtin mesh")) return false;
    if (!Check(am.GetEstimatedAssetCpuBytesByType(AssetType::Mesh) > 0, "per-type mesh bucket")) return false;
    am.Clear();
    if (!Check(am.GetEstimatedAssetCpuBytes() == 0, "asset CPU zero after Clear")) return false;
    return true;
}

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

bool TestProjectConfigAndPortableAssetPaths() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("myengine_project_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto scenes = root / "Content" / "Scenes";
    const auto meshes = root / "Content" / "Mesh";
    fs::create_directories(scenes);
    fs::create_directories(meshes);

    Scene startup("Startup");
    Scene alternate("Alternate");
    const auto startupPath = scenes / "Main.scene.json";
    const auto alternatePath = scenes / "Alternate.scene.json";
    if (!Check(SceneSerializer::SaveToFile(startup, startupPath.string()) &&
               SceneSerializer::SaveToFile(alternate, alternatePath.string()),
               "project test scene creation failed")) return false;

    ProjectConfig project;
    std::string error;
    if (!Check(project.Open(root, true, &error), "missing project should open in editor mode")) return false;
    project.SetName("ProjectTest");
    project.GetGraphicsSettings().backend = "d3d12";
    if (!Check(project.SetInputConfigPath("Content/Config/Input.input.json", &error),
               "project input config path save failed: " + error)) return false;
    if (!Check(project.SetStartupScene(startupPath, &error) && project.Save(&error),
               "project startup scene save failed: " + error)) return false;

    ProjectConfig loaded;
    if (!Check(loaded.Open(root, false, &error), "project manifest load failed: " + error)) return false;
    if (!Check(loaded.GetVersion() == ProjectConfig::kCurrentVersion &&
               loaded.GetName() == "ProjectTest" &&
               loaded.GetStartupScene() == "Content/Scenes/Main.scene.json" &&
               loaded.GetInputSettings().config == "Content/Config/Input.input.json" &&
               loaded.GetGraphicsSettings().backend == "d3d12",
               "project manifest fields mismatch")) return false;

    fs::path resolved;
    if (!Check(loaded.ResolveStartupScene(resolved, &error) &&
               resolved == startupPath.lexically_normal(),
               "startup scene resolution failed")) return false;
    if (!Check(loaded.ResolveScenePath("Content/Scenes/Alternate.scene.json", resolved, true, &error) &&
               resolved == alternatePath.lexically_normal(),
               "scene override did not take precedence")) return false;
    if (!Check(!loaded.ResolveScenePath(startupPath.string(), resolved, true, &error),
               "absolute startup scene path was accepted")) return false;
    if (!Check(!loaded.ResolveScenePath("Content/../outside.scene.json", resolved, false, &error),
               "traversal startup scene path was accepted")) return false;
    if (!Check(!loaded.ResolveScenePath("Content/Scenes/Missing.scene.json", resolved, true, &error),
               "missing startup scene was accepted")) return false;
    if (!Check(loaded.ResolveInputConfigPath(resolved, false, &error) &&
               resolved == (root / "Content" / "Config" / "Input.input.json").lexically_normal(),
               "input config resolution failed")) return false;
    if (!Check(!loaded.SetInputConfigPath("../Outside.input.json", &error),
               "traversal input config path was accepted")) return false;
    loaded.GetGraphicsSettings().backend = "vulkan";
    if (!Check(!loaded.Save(&error),
               "unsupported graphics backend was accepted")) return false;
    loaded.GetGraphicsSettings().backend = "d3d11";
    if (!Check(loaded.Save(&error), "failed to restore graphics backend")) return false;
    std::ofstream(root / ProjectConfig::kFileName)
        << R"({"version":999,"name":"Future","startupScene":"Content/Scenes/Main.scene.json"})";
    ProjectConfig unsupported;
    if (!Check(!unsupported.Open(root, false, &error),
               "unsupported project version was accepted")) return false;
    if (!Check(project.Save(&error), "failed to restore project manifest")) return false;

    AssetManager::Get().Clear();
    AssetManager::Get().SetProjectRoot(root);
    const auto meshPath = meshes / "Portable.mesh";
    std::ofstream(meshPath) << "mesh";
    auto mesh = std::make_shared<MeshAsset>(meshPath.string());
    mesh->SetGeometry({MeshVertex{}}, {0}, {});
    const MeshHandle meshHandle = AssetManager::Get().Register(std::move(mesh));
    Scene portableScene("Portable");
    Actor* actor = portableScene.CreateActor("PortableActor");
    actor->AddComponent<MeshRendererComponent>()->SetMesh(meshHandle);
    const std::string portableJson = SceneSerializer::SaveToString(portableScene);
    const auto parsed = nlohmann::json::parse(portableJson);
    const std::string storedMesh = parsed["actors"][0]["components"][0]["data"]["mesh"];
    if (!Check(storedMesh == "Content/Mesh/Portable.mesh",
               "new scene did not store a project-relative asset path")) return false;

    Scene portableLoaded;
    if (!Check(SceneSerializer::LoadFromString(portableLoaded, portableJson),
               "project-relative asset scene failed to load")) return false;
    auto* loadedRenderer = portableLoaded.FindByName("PortableActor")
        ? portableLoaded.FindByName("PortableActor")->GetComponent<MeshRendererComponent>() : nullptr;
    if (!Check(loadedRenderer && loadedRenderer->GetMesh().IsValid(),
               "project-relative mesh did not resolve")) return false;

    nlohmann::json legacy = parsed;
    legacy["actors"][0]["components"][0]["data"]["mesh"] = meshPath.string();
    Scene legacyLoaded;
    if (!Check(SceneSerializer::LoadFromString(legacyLoaded, legacy.dump()),
               "legacy absolute asset scene failed to load")) return false;
    auto* legacyRenderer = legacyLoaded.FindByName("PortableActor")
        ? legacyLoaded.FindByName("PortableActor")->GetComponent<MeshRendererComponent>() : nullptr;
    if (!Check(legacyRenderer && legacyRenderer->GetMesh().IsValid(),
               "legacy absolute mesh path compatibility failed")) return false;

    const auto scripts = root / "Content" / "Scripts";
    fs::create_directories(scripts);
    const auto scriptPath = scripts / "Portable.as";
    std::ofstream(scriptPath) << "class Script { void Update(float dt) {} }\n";
    ScriptComponent script;
    script.SetScriptPath(scriptPath.string());
    nlohmann::json scriptData;
    script.Serialize(scriptData);
    if (!Check(scriptData.value("scriptPath", std::string{}) ==
                   "Content/Scripts/Portable.as",
               "script path was not stored project-relative")) return false;
    ScriptComponent loadedScript;
    loadedScript.Deserialize(scriptData);
    if (!Check(fs::path(loadedScript.GetScriptPath()) == scriptPath.lexically_normal(),
               "project-relative script path did not resolve")) return false;

    AssetManager::Get().Clear();
    AssetManager::Get().SetProjectRoot({});
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    return true;
}

bool TestSceneColdLoadsModelSubAssetReferences() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("myengine_scene_subasset_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto models = root / "Content" / "Models";
    fs::create_directories(models);
    const auto objPath = models / "triangle.obj";
    const auto mtlPath = models / "triangle.mtl";
    std::ofstream(mtlPath)
        << "newmtl TestMat\n"
        << "Kd 0.2 0.6 0.9\n";
    std::ofstream(objPath)
        << "mtllib triangle.mtl\n"
        << "v 0 0 0\n"
        << "v 1 0 0\n"
        << "v 0 1 0\n"
        << "usemtl TestMat\n"
        << "f 1 2 3\n";

    AssetManager& assets = AssetManager::Get();
    assets.Clear();
    assets.SetProjectRoot(root);
    const ModelHandle model = assets.Load<ModelAsset>(objPath.string());
    if (!Check(model && model->GetMesh() && model->GetMaterial(0),
               "model fixture failed to import")) return false;

    Scene source("ColdSubAssets");
    Actor* actor = source.CreateActor("ImportedModel");
    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    renderer->SetMesh(model->GetMesh());
    renderer->SetMaterial(model->GetMaterial(0));
    const std::string serialized = SceneSerializer::SaveToString(source);
    const nlohmann::json saved = nlohmann::json::parse(serialized);
    const auto& savedData = saved["actors"][0]["components"][0]["data"];
    if (!Check(savedData.value("mesh", std::string{}) ==
                   "Content/Models/triangle.obj#mesh" &&
               savedData.value("material", std::string{}) ==
                   "Content/Models/triangle.obj#material-0",
               "model sub-assets were not serialized as stable project-relative paths")) {
        return false;
    }

    assets.Clear();
    Scene loaded;
    if (!Check(SceneSerializer::LoadFromString(loaded, serialized),
               "cold scene sub-asset deserialize failed")) return false;
    Actor* loadedActor = loaded.FindByName("ImportedModel");
    auto* loadedRenderer = loadedActor
        ? loadedActor->GetComponent<MeshRendererComponent>() : nullptr;
    if (!Check(loadedRenderer && loadedRenderer->GetMesh().IsValid() &&
               loadedRenderer->GetMaterial().IsValid(),
               "cold scene load lost imported mesh or material reference")) return false;

    SkinnedMeshRendererComponent skinned;
    skinned.SetSourceMesh(loadedRenderer->GetMesh());
    skinned.SetMaterial(loadedRenderer->GetMaterial());
    nlohmann::json skinnedData;
    skinned.Serialize(skinnedData);
    assets.Clear();
    SkinnedMeshRendererComponent coldSkinned;
    coldSkinned.Deserialize(skinnedData);
    if (!Check(coldSkinned.GetSourceMesh().IsValid() &&
               coldSkinned.GetMaterial().IsValid(),
               "cold skinned-mesh load lost imported sub-asset references")) return false;

    nlohmann::json legacy = nlohmann::json::parse(serialized);
    for (auto& component : legacy["actors"][0]["components"]) {
        if (component.value("type", std::string{}) == "MeshRenderer") {
            component["data"]["material"] = "__builtin__/TestMat";
        }
    }
    assets.Clear();
    Scene legacyLoaded;
    if (!Check(SceneSerializer::LoadFromString(legacyLoaded, legacy.dump()),
               "legacy imported material scene failed to deserialize")) return false;
    Actor* legacyActor = legacyLoaded.FindByName("ImportedModel");
    auto* legacyRenderer = legacyActor
        ? legacyActor->GetComponent<MeshRendererComponent>() : nullptr;
    const bool legacyResolved = legacyRenderer && legacyRenderer->GetMesh().IsValid() &&
        legacyRenderer->GetMaterial().IsValid() &&
        legacyRenderer->GetMaterial()->GetName() == "TestMat";

    assets.Clear();
    assets.SetProjectRoot({});
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    return Check(legacyResolved,
                 "legacy imported material reference was not recovered from its model");
}

bool TestWorkspaceCookAndPublish() {
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() /
        ("myengine_publish_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto projectRoot = base / "GameProject";
    const auto workspacePath = base / "settings" / "workspace.json";
    EditorWorkspace workspace(workspacePath);
    std::string error;
    if (!Check(workspace.CreateProject(projectRoot, "CookTest", &error),
               "workspace project creation failed: " + error)) return false;
    if (!Check(fs::is_regular_file(projectRoot / ProjectConfig::kFileName) &&
               fs::is_regular_file(projectRoot / "Content/Scenes/Main.scene.json"),
               "workspace did not create project files")) return false;

    EditorWorkspace reloaded(workspacePath);
    if (!Check(reloaded.Load(&error) && reloaded.GetRecentProjects().size() == 1 &&
               reloaded.GetRecentProjects()[0] == projectRoot.lexically_normal(),
               "workspace recent project persistence failed")) return false;

    const auto assetPath = projectRoot / "Content" / "Data" / "payload.bin";
    fs::create_directories(assetPath.parent_path());
    std::ofstream(assetPath, std::ios::binary) << "cooked payload";
    const auto scriptPath = projectRoot / "Content" / "Scripts" / "main.as";
    fs::create_directories(scriptPath.parent_path());
    std::ofstream(scriptPath) << "class Script { void Update(float dt) {} }\n";
    const auto editorScriptPath = projectRoot / "Content" / "Editor" / "Scripts" / "tool.lua";
    fs::create_directories(editorScriptPath.parent_path());
    std::ofstream(editorScriptPath) << "Editor.log('tool')\n";

    ProjectConfig project;
    if (!Check(project.Open(projectRoot, false, &error),
               "created project failed to reopen: " + error)) return false;
    project.GetPublishSettings().outputDirectory = (base / "Published").string();
    project.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
    if (!Check(project.Save(&error), "publish settings save failed: " + error)) return false;
    project.GetPublishSettings().target = "unsupported-target";
    if (!Check(!project.Save(&error), "unsupported publish target was accepted")) return false;
    project.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
    if (!Check(project.Save(&error), "failed to restore Windows publish target")) return false;

    const auto binaries = base / "Binaries";
    fs::create_directories(binaries);
#ifdef _WIN32
    const char* runtimeFiles[] = {"MyEnginePlayer.exe", "runtime.dll", "SDL3.dll"};
#elif defined(__APPLE__)
    const char* runtimeFiles[] = {"MyEnginePlayer", "libruntime.dylib", "libSDL3.dylib"};
#else
    const char* runtimeFiles[] = {"MyEnginePlayer", "libruntime.so", "libSDL3.so"};
#endif
    const auto builtBinaries = gExecutableDirectory;
    const auto engineContent = std::filesystem::current_path() / "EngineContent";
    for (const char* file : runtimeFiles) {
        fs::copy_file(builtBinaries / file, binaries / file,
                      fs::copy_options::overwrite_existing);
    }

    PublishReport report;
    const bool publishSucceeded = ProjectPublisher::Publish(
        project, binaries, engineContent, report, &error);
    if (!Check(publishSucceeded, "project publish failed: " + error)) return false;
    if (!Check(fs::is_regular_file(report.contentArchive) &&
               fs::is_regular_file(report.outputDirectory / "CookManifest.json") &&
               !fs::exists(report.outputDirectory / "Content") &&
               report.cookedFiles.size() >= 3 && report.contentBytes > 0,
               "publish output layout or cook report mismatch")) return false;
    for (const char* file : runtimeFiles) {
        if (!Check(fs::is_regular_file(report.outputDirectory / file),
                   std::string("published runtime file missing: ") + file)) return false;
    }

    CookManifest manifest;
    if (!Check(CookManifest::Load(report.outputDirectory / CookManifest::kFileName,
                                  manifest, &error),
               "published Cook manifest failed to load: " + error)) return false;
    if (!Check(manifest.project == project.GetName() &&
               manifest.startupScene == project.GetStartupScene() &&
               manifest.target == PublishTargets::kDefaultTargetId &&
               manifest.files.size() == report.cookedFiles.size(),
               "published Cook manifest fields mismatch")) return false;

    CookManifest invalidManifest = manifest;
    invalidManifest.version = 999;
    if (!Check(!invalidManifest.Validate(&error),
               "unknown Cook manifest version was accepted")) return false;
    invalidManifest = manifest;
    invalidManifest.startupScene = "Content/../Outside.scene.json";
    if (!Check(!invalidManifest.Validate(&error),
               "Cook manifest traversal path was accepted")) return false;

    const auto cacheBase = base / "CookedCache";
    CookedProjectMount firstMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase,
                                           firstMount, &error) && firstMount.rebuilt,
               "first cooked cache prepare failed: " + error)) return false;
    CookedProjectMount reusedMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase,
                                           reusedMount, &error) && !reusedMount.rebuilt &&
               reusedMount.projectRoot == firstMount.projectRoot,
               "valid cooked cache was not reused: " + error)) return false;

    const auto cachedPayload = firstMount.projectRoot / "Content/Data/payload.bin";
    std::ofstream(cachedPayload, std::ios::binary | std::ios::trunc) << "damaged payload";
    CookedProjectMount repairedMount;
    if (!Check(CookedProjectCache::Prepare(report.outputDirectory, cacheBase,
                                           repairedMount, &error) && repairedMount.rebuilt,
               "corrupt cooked cache was not rebuilt: " + error)) return false;
    std::ifstream repairedPayload(cachedPayload, std::ios::binary);
    const std::string repairedText((std::istreambuf_iterator<char>(repairedPayload)),
                                   std::istreambuf_iterator<char>());
    if (!Check(repairedText == "cooked payload",
               "rebuilt cooked cache did not restore payload")) return false;
    repairedPayload.close();

    std::error_code concurrentCleanup;
    fs::remove_all(cacheBase, concurrentCleanup);
    if (!Check(!concurrentCleanup && !fs::exists(cacheBase),
               "failed to reset cooked cache before concurrency test")) return false;
    CookedProjectMount concurrentMounts[2];
    std::string concurrentErrors[2];
    bool concurrentResults[2] = {false, false};
    std::thread first([&] {
        concurrentResults[0] = CookedProjectCache::Prepare(
            report.outputDirectory, cacheBase, concurrentMounts[0], &concurrentErrors[0]);
    });
    std::thread second([&] {
        concurrentResults[1] = CookedProjectCache::Prepare(
            report.outputDirectory, cacheBase, concurrentMounts[1], &concurrentErrors[1]);
    });
    first.join();
    second.join();
    if (!Check(concurrentResults[0] && concurrentResults[1] &&
               concurrentMounts[0].projectRoot == concurrentMounts[1].projectRoot,
               "concurrent cooked cache prepare failed: " + concurrentErrors[0] +
               " / " + concurrentErrors[1])) return false;

    const auto oldPackageMarker = report.outputDirectory / "previous-package.marker";
    std::ofstream(oldPackageMarker) << "keep";
    fs::remove(binaries / runtimeFiles[0]);
    PublishReport failedReport;
    if (!Check(!ProjectPublisher::Publish(project, binaries, engineContent, failedReport, &error) &&
               fs::is_regular_file(oldPackageMarker) &&
               !fs::exists(report.outputDirectory.string() + ".staging") &&
               !fs::exists(report.outputDirectory.string() + ".backup"),
               "failed publish damaged the previous package or left temporary output")) return false;
    fs::copy_file(builtBinaries / runtimeFiles[0], binaries / runtimeFiles[0],
                  fs::copy_options::overwrite_existing);
    PublishReport replacementReport;
    if (!Check(ProjectPublisher::Publish(project, binaries, engineContent, replacementReport, &error) &&
               !fs::exists(oldPackageMarker) &&
               !fs::exists(replacementReport.outputDirectory.string() + ".backup"),
               "transactional publish replacement failed: " + error)) return false;

    const fs::path interruptedBackup = replacementReport.outputDirectory.string() + ".backup";
    fs::rename(replacementReport.outputDirectory, interruptedBackup);
    fs::remove(binaries / runtimeFiles[0]);
    if (!Check(!ProjectPublisher::Publish(project, binaries, engineContent, failedReport, &error) &&
               fs::is_directory(replacementReport.outputDirectory) &&
               !fs::exists(interruptedBackup),
               "interrupted publish backup was not restored before preflight")) return false;
    fs::copy_file(builtBinaries / runtimeFiles[0], binaries / runtimeFiles[0],
                  fs::copy_options::overwrite_existing);

    const auto extracted = base / "Extracted";
    if (!Check(ContentArchive::Extract(report.contentArchive, extracted, &error),
               "Content archive extraction failed: " + error)) return false;
    if (!Check(fs::is_regular_file(extracted / "Content/Scenes/Main.scene.json") &&
               fs::is_regular_file(extracted / "Content/Data/payload.bin") &&
               fs::is_regular_file(extracted / "Content/Scripts/main.as") &&
               !fs::exists(extracted / "Content/Editor/Scripts/tool.lua") &&
               fs::is_regular_file(extracted / "Content/Engine/Shaders/Mesh.shader") &&
               !fs::exists(extracted / "Content/Engine/Shaders/Mesh.hlsl"),
               "cooked Content files were not restored")) return false;
    auto cookedShader = LoadShaderAssetFromFile(
        (extracted / "Content/Engine/Shaders/Mesh.shader").string());
    if (!Check(cookedShader && cookedShader->IsCooked() &&
               !cookedShader->GetBytecode(ShaderBackend::D3D11, ShaderStage::Vertex).empty() &&
               !cookedShader->GetBytecode(ShaderBackend::D3D12, ShaderStage::Pixel).empty(),
               "published shader does not contain both D3D backends")) return false;
    std::ifstream payload(extracted / "Content/Data/payload.bin", std::ios::binary);
    std::string payloadText((std::istreambuf_iterator<char>(payload)),
                            std::istreambuf_iterator<char>());
    if (!Check(payloadText == "cooked payload", "cooked payload content changed")) return false;

    const auto corrupt = base / "Corrupt.pak";
    fs::copy_file(report.contentArchive, corrupt);
    {
        std::fstream file(corrupt, std::ios::binary | std::ios::in | std::ios::out);
        char value = 0;
        file.seekg(-1, std::ios::end);
        file.read(&value, 1);
        value = static_cast<char>(value ^ 0x7f);
        file.seekp(-1, std::ios::end);
        file.write(&value, 1);
    }
    if (!Check(!ContentArchive::Extract(corrupt, base / "CorruptExtract", &error),
               "corrupt Content archive was accepted")) return false;

    const auto trailing = base / "Trailing.pak";
    fs::copy_file(report.contentArchive, trailing);
    std::ofstream(trailing, std::ios::binary | std::ios::app) << "trailing";
    if (!Check(!ContentArchive::Extract(trailing, base / "TrailingExtract", &error) &&
               error.find("trailing") != std::string::npos,
               "Content archive trailing data was accepted")) return false;

    const auto corruptPackage = base / "CorruptPackage";
    fs::copy(replacementReport.outputDirectory, corruptPackage,
             fs::copy_options::recursive);
    fs::copy_file(corrupt, corruptPackage / ContentArchive::kFileName,
                  fs::copy_options::overwrite_existing);
    CookedProjectMount rejectedMount;
    if (!Check(!CookedProjectCache::Prepare(corruptPackage, base / "RejectedCache",
                                            rejectedMount, &error),
               "package with archive hash mismatch was accepted")) return false;

    std::error_code cleanupError;
    fs::remove_all(base, cleanupError);
    return true;
}

struct LifecycleProbeComponent : Component {
    static std::vector<std::string>* events;
    std::string label;
    const char* GetTypeName() const override { return "LifecycleProbe"; }
    void Deserialize(const nlohmann::json& data) override { label = data.value("label", std::string{}); }
    void Push(const char* event) { if (events) events->push_back(label + ":" + event); }
    void OnAttach() override { Push("attach"); }
    void OnInitialize() override { Push("initialize"); }
    void OnBeginPlay() override { Push("begin"); }
    void OnEnable() override { Push("enable"); }
    void OnStart() override { Push("start"); }
    void OnDisable() override { Push("disable"); }
    void OnEndPlay() override { Push("end"); }
    void OnDetach() override { Push("detach"); }
};
std::vector<std::string>* LifecycleProbeComponent::events = nullptr;

struct PriorityLifecycleProbeComponent final : LifecycleProbeComponent {
    const char* GetTypeName() const override { return "PriorityLifecycleProbe"; }
    int GetExecutionOrder() const override { return -100; }
};

struct DeferredMutationProbe final : Component {
    bool ran = false;
    const char* GetTypeName() const override { return "DeferredMutationProbe"; }
    void OnUpdate(float) override {
        if (ran) return;
        ran = true;
        Scene* scene = GetOwner()->GetScene();
        scene->QueueCreateActor(ActorCreateDesc{"SpawnedDuringUpdate"});
        scene->QueueDestroyActor(GetOwner()->GetHandle());
    }
};

bool TestActorHandleLifecycleAndDeferredMutation()
{
    ComponentRegistry::Get().Register("LifecycleProbe", [] {
        return std::make_unique<LifecycleProbeComponent>();
    });
    ComponentRegistry::Get().Register("PriorityLifecycleProbe", [] {
        return std::make_unique<PriorityLifecycleProbeComponent>();
    });
    std::vector<std::string> events;
    LifecycleProbeComponent::events = &events;

    {
        Scene ordering("Ordering");
        ActorCreateDesc desc;
        desc.components.push_back({"LifecycleProbe", true, {{"label", "normal"}}});
        desc.components.push_back({"PriorityLifecycleProbe", true, {{"label", "priority"}}});
        ordering.QueueCreateActor(desc);
        ordering.FlushCommands();
        const std::vector<std::string> expected = {
            "priority:attach", "normal:attach", "priority:initialize", "normal:initialize"
        };
        if (!Check(events == expected, "component priority and stable ordering mismatch")) return false;
    }
    events.clear();
    Scene scene("Lifecycle");

    ActorCreateDesc parentDesc;
    parentDesc.name = "Parent";
    parentDesc.components.push_back({"LifecycleProbe", true, {{"label", "parent"}}});
    const ActorHandle parent = scene.QueueCreateActor(parentDesc);
    ActorCreateDesc childDesc;
    childDesc.name = "Child";
    childDesc.parent = parent;
    childDesc.components.push_back({"LifecycleProbe", true, {{"label", "child"}}});
    const ActorHandle child = scene.QueueCreateActor(childDesc);
    if (!Check(!scene.TryGetActor(parent) && scene.FlushCommands(),
               "queued actor became visible before command flush")) return false;
    const std::vector<std::string> constructExpected = {
        "parent:attach", "parent:initialize", "child:attach", "child:initialize"
    };
    if (!Check(events == constructExpected, "batch construction lifecycle order mismatch")) return false;

    scene.BeginPlay();
    const std::vector<std::string> playTail = {
        "parent:begin", "child:begin", "parent:enable", "child:enable",
        "parent:start", "child:start"
    };
    if (!Check(std::equal(playTail.begin(), playTail.end(), events.end() - playTail.size()),
               "global begin/enable/start lifecycle order mismatch")) return false;

    scene.QueueSetActive(parent, false);
    scene.FlushCommands();
    if (!Check(events[events.size() - 2] == "child:disable" &&
               events.back() == "parent:disable", "hierarchy disable was not child-to-parent")) return false;
    scene.QueueSetActive(parent, true);
    scene.FlushCommands();
    if (!Check(events[events.size() - 2] == "parent:enable" &&
               events.back() == "child:enable", "hierarchy enable was not parent-to-child")) return false;

    const ActorHandle oldChild = child;
    scene.QueueDestroyActor(child);
    if (!Check(scene.TryGetActor(child) && scene.TryGetActor(child)->IsPendingDestroy(),
               "destroy did not mark actor pending immediately")) return false;
    scene.FlushCommands();
    const ActorHandle replacement = scene.QueueCreateActor(ActorCreateDesc{"Replacement"});
    scene.FlushCommands();
    if (!Check(!scene.TryGetActor(oldChild) && scene.TryGetActor(replacement) &&
               replacement != oldChild, "generation did not invalidate stale actor handle")) return false;

    Actor* mutator = scene.CreateActor("Mutator");
    const ActorHandle mutatorHandle = mutator->GetHandle();
    mutator->AddComponent<DeferredMutationProbe>();
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(!scene.TryGetActor(mutatorHandle) && scene.FindByName("SpawnedDuringUpdate"),
               "update-time structural commands were not safely deferred")) return false;
    LifecycleProbeComponent::events = nullptr;
    return true;
}

bool TestPrefabRoundTripOverridesAndValidation()
{
    namespace fs=std::filesystem;const fs::path project=fs::temp_directory_path()/"myengine_prefab_test";std::error_code ec;fs::remove_all(project,ec);fs::create_directories(project/"Content/Prefabs");AssetManager::Get().SetProjectRoot(project);
    Scene source("Source");Actor* sourceRoot=source.CreateActor("Vehicle");sourceRoot->AddComponent<MeshRendererComponent>();Actor* sourceChild=source.CreateActor("Wheel");sourceChild->SetParent(sourceRoot);sourceChild->GetTransform().position.x=2.0f;
    const fs::path path=project/"Content/Prefabs/Vehicle.prefab.json";std::string error;
    if(!Check(PrefabSystem::SaveSubtree(*sourceRoot,path,&error),"prefab save failed: "+error))return false;
    PrefabAsset asset;if(!Check(PrefabAsset::Load(path,asset,&error)&&asset.nodes.size()==2,"prefab round-trip failed: "+error))return false;

    Scene scene("Instances");Actor* first=PrefabSystem::Instantiate(scene,path,{},&error);Actor* second=PrefabSystem::Instantiate(scene,path,{},&error);
    if(!Check(first&&second&&first->GetID()!=second->GetID()&&scene.ActorCount()==4,"prefab instances did not receive independent actor identities"))return false;
    Actor* firstChild=first->GetChildren().front();firstChild->SetName("CustomWheel");first->RemoveComponentByTypeName("MeshRenderer");firstChild->AddComponent<LightComponent>();
    if(!Check(PrefabSystem::CaptureOverrides(*first,&error)&&!first->GetPrefabOverrides().empty(),"prefab override capture failed: "+error))return false;
    const uint64_t firstId=first->GetID(),secondId=second->GetID();
    for(auto& node:asset.nodes)if(node.localId!=asset.rootLocalId)node.name="SourceWheel";
    if(!Check(asset.Save(path,&error)&&PrefabSystem::RefreshInstances(scene,asset.uuid,&error),"prefab source refresh failed: "+error))return false;
    first=scene.FindByID(firstId);second=scene.FindByID(secondId);
    if(!Check(first&&second&&first->GetChildren().front()->GetName()=="CustomWheel"&&second->GetChildren().front()->GetName()=="SourceWheel","source propagation overwrote an override or missed an unoverridden instance"))return false;
    if(!Check(!first->GetComponent<MeshRendererComponent>()&&first->GetChildren().front()->GetComponent<LightComponent>()&&second->GetComponent<MeshRendererComponent>()&&!second->GetChildren().front()->GetComponent<LightComponent>(),"component add/remove overrides were not isolated per instance"))return false;

    if(!Check(PrefabSystem::RevertAll(*first,&error),"prefab revert failed: "+error))return false;first=scene.FindByID(firstId);second=scene.FindByID(secondId);
    if(!Check(first&&first->GetChildren().front()->GetName()=="SourceWheel"&&first->GetComponent<MeshRendererComponent>()&&!first->GetChildren().front()->GetComponent<LightComponent>(),"prefab revert did not restore source state"))return false;
    first->GetChildren().front()->SetName("AppliedWheel");if(!Check(PrefabSystem::ApplyAll(*first,&error),"prefab apply failed: "+error))return false;first=scene.FindByID(firstId);second=scene.FindByID(secondId);
    if(!Check(first&&second&&first->GetChildren().front()->GetName()=="AppliedWheel"&&second->GetChildren().front()->GetName()=="AppliedWheel"&&first->GetPrefabOverrides().empty(),"prefab apply did not update all instances"))return false;

    Actor* added=scene.CreateActor("AddedChild");added->SetParent(first);
    const std::string serialized=SceneSerializer::SaveToString(scene);const auto json=nlohmann::json::parse(serialized);
    if(!Check(json["actors"].size()==2&&json["actors"][0].contains("prefabInstance"),"scene duplicated prefab instance nodes"))return false;
    Scene loaded("Loaded");if(!Check(SceneSerializer::LoadFromString(loaded,serialized)&&loaded.ActorCount()==5,"prefab scene reload failed"))return false;
    Actor* loadedFirst=loaded.FindByID(first->GetID());if(!Check(loadedFirst&&loadedFirst->GetChildren().size()==2,"added prefab child override was not restored"))return false;
    if(!Check(PrefabSystem::Unpack(*loadedFirst,&error)&&!loadedFirst->IsPrefabInstance(),"prefab unpack failed: "+error))return false;

    auto mismatch=json;mismatch["actors"][0]["prefabInstance"]["uuid"]="wrong";Scene rejected;
    if(!Check(!SceneSerializer::LoadFromString(rejected,mismatch.dump())&&rejected.ActorCount()==0,"prefab UUID mismatch was accepted"))return false;
    Actor* container=source.CreateActor("Container");Actor* nested=PrefabSystem::Instantiate(source,path,{},&error);nested->SetParent(container);
    if(!Check(!PrefabSystem::SaveSubtree(*container,project/"Content/Prefabs/Nested.prefab.json",&error),"nested prefab was accepted"))return false;
    AssetManager::Get().SetProjectRoot({});fs::remove_all(project,ec);return true;
}

bool TestPrefabCookDependencyValidation()
{
    namespace fs=std::filesystem;const fs::path root=fs::temp_directory_path()/"myengine_prefab_cook_test";std::error_code ec;fs::remove_all(root,ec);fs::create_directories(root/"Content/Scenes");fs::create_directories(root/"Content/Prefabs");
    const std::string uuid="11111111-2222-4333-8444-555555555555";
    std::ofstream(root/"Content/Prefabs/Unit.prefab.json")<<nlohmann::json{{"version",1},{"uuid",uuid},{"rootLocalId","root"},{"nodes",nlohmann::json::array({{{"localId","root"},{"parentLocalId",""},{"name","Unit"},{"active",true},{"components",nlohmann::json::array()}}})}}.dump();
    std::ofstream(root/"Content/Prefabs/Unit.prefab.json.meta")<<nlohmann::json{{"uuid",uuid}}.dump();
    const nlohmann::json scene={{"actors",nlohmann::json::array({{{"id",1},{"prefabInstance",{{"asset","Content/Prefabs/Unit.prefab.json"},{"uuid",uuid},{"overrides",nlohmann::json::array()}}}}})}};
    std::ofstream(root/"Content/Scenes/Main.scene.json")<<scene.dump();PublishPreflightReport report;
    if(!Check(CookDependencyGraph::Validate(root,report)&&std::find(report.visitedAssets.begin(),report.visitedAssets.end(),"Content/Prefabs/Unit.prefab.json")!=report.visitedAssets.end(),"cook did not traverse prefab dependency: "+report.Summary()))return false;
    auto mismatch=scene;mismatch["actors"][0]["prefabInstance"]["uuid"]="wrong";std::ofstream(root/"Content/Scenes/Main.scene.json",std::ios::trunc)<<mismatch.dump();
    if(!Check(!CookDependencyGraph::Validate(root,report),"cook accepted a prefab UUID mismatch"))return false;
    fs::remove_all(root,ec);return true;
}

bool TestUICanvasComponentSerialization()
{
    if (!Check(ComponentRegistry::Get().IsRegistered("UICanvas"),
               "UICanvas component was not registered")) return false;

    Scene scene("UICanvasScene");
    Actor* actor = scene.CreateActor("HUD");
    auto* canvas = actor->AddComponent<UICanvasComponent>();
    if (!Check(canvas != nullptr, "failed to add UICanvasComponent")) return false;
    canvas->SetDocumentPath("Content/UI/HUD.rml");
    canvas->SetStylePaths({"Content/UI/HUD.rcss"});
    canvas->SetDefaultFontPaths({"Content/UI/Inter.ttf"});
    canvas->SetVisible(false);
    canvas->SetInteractive(false);
    canvas->SetSortOrder(4);
    canvas->SetInputMode(UIInputMode::UIOnly);

    Scene loaded("LoadedUI");
    if (!Check(SceneSerializer::LoadFromString(loaded, SceneSerializer::SaveToString(scene)),
               "UICanvas scene round-trip failed")) return false;
    Actor* loadedActor = loaded.FindByName("HUD");
    auto* loadedCanvas = loadedActor ? loadedActor->GetComponent<UICanvasComponent>() : nullptr;
    if (!Check(loadedCanvas != nullptr, "loaded UICanvasComponent missing")) return false;
    if (!Check(loadedCanvas->GetDocumentPath() == "Content/UI/HUD.rml",
               "UICanvas document path mismatch")) return false;
    if (!Check(!loadedCanvas->IsVisible() && !loadedCanvas->IsInteractive(),
               "UICanvas booleans mismatch")) return false;
    if (!Check(loadedCanvas->GetSortOrder() == 4 &&
               loadedCanvas->GetInputMode() == UIInputMode::UIOnly,
               "UICanvas sort/input mismatch")) return false;
    if (!Check(loadedCanvas->GetStylePaths().size() == 1 &&
               loadedCanvas->GetDefaultFontPaths().size() == 1,
               "UICanvas path arrays mismatch")) return false;
    return true;
}

bool TestUIDrawListBatchContainer()
{
    UIDrawList list;
    if (!Check(list.Empty(), "new UIDrawList should be empty")) return false;
    UIDrawCommand command;
    command.indexCount = 6;
    command.scissor = {1, 2, 3, 4, true};
    list.Add(command);
    if (!Check(!list.Empty() && list.Size() == 1, "UIDrawList add failed")) return false;
    list.Clear();
    return Check(list.Empty(), "UIDrawList clear failed");
}

MYENGINE_REGISTER_TEST("Scene", "TestSceneSerializationRegression", TestSceneSerializationRegression);
MYENGINE_REGISTER_TEST("Scene", "TestBuiltinSceneMaterialRoundTrip", TestBuiltinSceneMaterialRoundTrip);
MYENGINE_REGISTER_TEST("Scripting", "TestScriptRuntimeLifecycle", TestScriptRuntimeLifecycle);
MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptFilesErrorsAndPhysicsBindings", TestAngelScriptFilesErrorsAndPhysicsBindings);
MYENGINE_REGISTER_TEST("Scripting", "TestAngelScriptAssetsClassesAndFields", TestAngelScriptAssetsClassesAndFields);
MYENGINE_REGISTER_TEST("Scripting", "TestEditorLuaScriptService", TestEditorLuaScriptService);
MYENGINE_REGISTER_TEST("Scripting", "TestLegacyLuaScriptCompatibility", TestLegacyLuaScriptCompatibility);
MYENGINE_REGISTER_TEST("Animation", "TestGpuSkinningAnimationBlend", TestGpuSkinningAnimationBlend);
MYENGINE_REGISTER_TEST("Scene", "TestComponentRegistry", TestComponentRegistry);
MYENGINE_REGISTER_TEST("Scene", "TestCameraComponentAndGameViewport", TestCameraComponentAndGameViewport);
MYENGINE_REGISTER_TEST("Scene", "TestAudioSourceComponentSerialization", TestAudioSourceComponentSerialization);
MYENGINE_REGISTER_TEST("Scene", "TestSceneRunStates", TestSceneRunStates);
MYENGINE_REGISTER_TEST("Miscs", "TestIconsManagerSvgRasterizeIcoAndUploadCache", TestIconsManagerSvgRasterizeIcoAndUploadCache);
MYENGINE_REGISTER_TEST("Core", "TestCrashReportWriting", TestCrashReportWriting);
MYENGINE_REGISTER_TEST("Scene", "TestTransformHierarchyWorldPosition", TestTransformHierarchyWorldPosition);
MYENGINE_REGISTER_TEST("Camera", "TestCameraViewportProjectionStability", TestCameraViewportProjectionStability);
MYENGINE_REGISTER_TEST("Camera", "TestSceneViewportControllerRayStability", TestSceneViewportControllerRayStability);
MYENGINE_REGISTER_TEST("Scene", "TestDefaultSceneFactoryLeavesScenesUnmodified", TestDefaultSceneFactoryLeavesScenesUnmodified);
MYENGINE_REGISTER_TEST("Input", "TestInputBoundaries", TestInputBoundaries);
MYENGINE_REGISTER_TEST("Input", "TestGamepadStateTransitions", TestGamepadStateTransitions);
MYENGINE_REGISTER_TEST("Input", "TestInputActionMapJsonAndEvaluation", TestInputActionMapJsonAndEvaluation);
MYENGINE_REGISTER_TEST("Scene", "TestSceneSerializerMalformedDataIsolation", TestSceneSerializerMalformedDataIsolation);
MYENGINE_REGISTER_TEST("Scripting", "TestScriptHotReloadFailureRollback", TestScriptHotReloadFailureRollback);
MYENGINE_REGISTER_TEST("Core", "TestMemoryLinearAllocator", TestMemoryLinearAllocator);
MYENGINE_REGISTER_TEST("Core", "TestMemoryPoolAllocator", TestMemoryPoolAllocator);
MYENGINE_REGISTER_TEST("Core", "TestMemoryServiceHeapRoundTrip", TestMemoryServiceHeapRoundTrip);
MYENGINE_REGISTER_TEST("Core", "TestSceneAndAssetMemoryCounters", TestSceneAndAssetMemoryCounters);
MYENGINE_REGISTER_TEST("Scene", "TestSceneColdLoadsModelSubAssetReferences", TestSceneColdLoadsModelSubAssetReferences);
MYENGINE_REGISTER_TEST("Scene", "TestActorHandleLifecycleAndDeferredMutation", TestActorHandleLifecycleAndDeferredMutation);
MYENGINE_REGISTER_TEST("Scene", "TestPrefabRoundTripOverridesAndValidation", TestPrefabRoundTripOverridesAndValidation);
MYENGINE_REGISTER_TEST("Project", "TestPrefabCookDependencyValidation", TestPrefabCookDependencyValidation);
MYENGINE_REGISTER_TEST("UI", "TestUICanvasComponentSerialization", TestUICanvasComponentSerialization);
MYENGINE_REGISTER_TEST("UI", "TestUIDrawListBatchContainer", TestUIDrawListBatchContainer);

} // namespace
