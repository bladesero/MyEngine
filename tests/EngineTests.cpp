#include "Assets/AssetManager.h"
#include "Core/Memory/LinearAllocator.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Core/Memory/MemoryService.h"
#include "Core/Memory/PoolAllocator.h"
#include "Core/CrashHandler.h"
#include "Camera/Camera.h"
#include "Game/SceneLayer.h"
#include "Input/Input.h"
#include "Math/Mat4Inverse.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/CapsuleColliderComponent.h"
#include "Physics/CharacterControllerComponent.h"
#include "Physics/CollisionShapes.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/SphereColliderComponent.h"
#include "Project/ProjectConfig.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scripting/ScriptComponent.h"
#include "Renderer/Renderer.h"
#include "Renderer/GpuUploadQueue.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorAssetRegistry.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorInspectorSection.h"
#include "Editor/EditorImportService.h"
#include "Editor/EditorProject.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorService.h"
#include "Editor/EditorViewportControllers.h"
#include "Editor/EditorWorkspace.h"
#include "Editor/ProjectPublisher.h"
#include "Project/CookedProjectCache.h"
#include "Project/CookManifest.h"
#include "Project/ContentArchive.h"

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

bool NearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

bool Check(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << '\n';
        return false;
    }
    return true;
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
    script->SetSource("function Update(dt) Actor.rotate(0, 90 * dt, 0) end\n");
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

bool TestScriptRuntimeLifecycle() {
    Scene scene("ScriptCase");
    Actor* actor = scene.CreateActor("Scripted");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetSource(
        "Inspector = { speed = 2.0 }\n"
        "State = { updates = 0, fixed = 0 }\n"
        "function Awake() State.awake = true end\n"
        "function Start() Actor.set_position(1, 2, 3) end\n"
        "function Update(dt)\n"
        "  Actor.translate(Inspector.speed * dt, 0, 0)\n"
        "  Actor.rotate(0, 90 * dt, 0)\n"
        "  State.updates = State.updates + 1\n"
        "end\n"
        "function FixedUpdate(dt) State.fixed = State.fixed + 1 end\n");
    if (!Check(script->IsCompiled(), "script should compile")) return false;

    scene.OnUpdate(0.5f);
    const Transform& transform = actor->GetTransform();
    if (!Check(NearlyEqual(transform.position.x, 2.0f) &&
               NearlyEqual(transform.position.y, 2.0f),
               "script start/update position mismatch")) return false;
    if (!Check(NearlyEqual(transform.rotation.y, 45.0f),
               "script update rotation mismatch")) return false;
    const auto state = script->GetInstanceState();
    if (!Check(script->GetInspectorFields().value("speed", 0.0) == 2.0,
               "Lua inspector field missing")) return false;
    return Check(state.value("awake", false) && state.value("updates", 0) == 1,
                 "Lua lifecycle state mismatch");
}

bool TestLuaScriptFilesErrorsAndPhysicsBindings() {
    Scene scene("LuaBindings");

    Actor* target = scene.CreateActor("Target");
    target->GetTransform().position = { 0.0f, 0.0f, 0.0f };
    target->AddComponent<BoxColliderComponent>();

    Actor* actor = scene.CreateActor("Caster");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetSource(
        "State = {}\n"
        "function Start()\n"
        "  local hit = Physics.raycast(0, 0, 5, 0, 0, -1, 20)\n"
        "  State.hit = hit and hit.actorName or ''\n"
        "end\n");
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(script->GetInstanceState().value("hit", std::string{}) == "Target",
               "Lua Physics.raycast binding failed")) return false;

    script->SetSource("function Update(dt) error('boom') end\n");
    scene.OnUpdate(1.0f / 60.0f);
    if (!Check(!script->IsCompiled(), "Lua runtime error should disable script")) return false;
    if (!Check(script->GetLastError().find("stack traceback") != std::string::npos,
               "Lua error should include traceback")) return false;

    const auto path = std::filesystem::temp_directory_path() / "myengine_hot_reload.lua";
    {
        std::ofstream output(path, std::ios::binary);
        output << "State = {}\nfunction Update(dt) Actor.translate(1 * dt, 0, 0) end\n";
    }
    script->SetScriptPath(path.string());
    scene.OnUpdate(1.0f);
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 1.0f),
               "Lua script file initial run failed")) return false;
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "State = {}\nfunction Update(dt) Actor.translate(3 * dt, 0, 0) end\n";
    }
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now());
    scene.OnUpdate(1.0f);
    std::filesystem::remove(path);
    return Check(NearlyEqual(actor->GetTransform().position.x, 4.0f),
                 "Lua script file hot reload failed");
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
        "SphereCollider", "CapsuleCollider", "CharacterController", "Light", "PostProcess"
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
    Actor* actor = layer.GetScene().CreateActor("Scripted");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetSource("function Update(dt) Actor.translate(2 * dt, 0, 0) end\n");
    layer.MarkDirty();

    layer.OnUpdate(0.5f);
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 0.0f),
               "Edit mode should not simulate scene")) return false;

    if (!Check(layer.BeginPlay(), "BeginPlay failed")) return false;
    Actor* runtimeActor = layer.GetScene().FindByName("Scripted");
    if (!Check(runtimeActor && runtimeActor != actor,
               "Play mode did not clone the edit scene")) return false;
    layer.OnUpdate(0.5f);
    if (!Check(NearlyEqual(runtimeActor->GetTransform().position.x, 1.0f),
               "Play mode did not update runtime scene")) return false;

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
    Actor* restored = layer.GetScene().FindByName("Scripted");
    if (!Check(layer.IsEditing() && restored,
               "StopPlay did not restore Edit mode")) return false;
    if (!Check(NearlyEqual(restored->GetTransform().position.x, 0.0f),
               "runtime changes leaked into edit scene")) return false;
    return Check(layer.IsDirty(), "edit dirty state was not restored");
}

struct MockBuffer final : GpuBuffer {};
struct MockShader final : GpuShader {};
struct MockTexture final : GpuTexture {};

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

    int shaderBinds = 0;
    int vertexBinds = 0;
    int constantUpdates = 0;
    int drawCalls = 0;
    int submittedInstances = 0;
    std::vector<GpuBlendMode> blendModes;
};

class MockRenderContext final : public IRenderContext {
public:
    bool Init(IWindow*) override { return true; }
    void Shutdown() override {}
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

    MockCommandList commands;
    int beginFrames = 0;
    int endFrames = 0;
    int vertexUploads = 0;
    int indexUploads = 0;
    int shaderCreates = 0;
    int textureUploads = 0;
};

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
    Renderer renderer(&context);
    int queuedUploadRuns = 0;
    GpuUploadQueue::Get().Enqueue([&queuedUploadRuns](IRenderContext& uploadContext) {
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
    if (!Check(context.textureUploads == 2, "headless texture uploads missing")) return false;
    if (!Check(context.commands.drawCalls == 3,
               "frustum culling emitted an unexpected draw count")) return false;
    return Check(context.commands.blendModes.size() == 3 &&
                 context.commands.blendModes[0] == GpuBlendMode::Opaque &&
                 context.commands.blendModes[1] == GpuBlendMode::Opaque &&
                 context.commands.blendModes[2] == GpuBlendMode::Alpha,
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
    ComponentRegistry::Get().Register("ThrowingDeserialize", [](Actor& actor) {
        return actor.AddComponent<ThrowingDeserializeComponent>();
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
    const fs::path path = fs::temp_directory_path() / "myengine_script_reload_rollback.lua";
    {
        std::ofstream output(path, std::ios::binary);
        output << "State = {}\nfunction Update(dt) Actor.translate(1 * dt, 0, 0) end\n";
    }

    Scene scene("ScriptRollback");
    Actor* actor = scene.CreateActor("Scripted");
    auto* script = actor->AddComponent<ScriptComponent>();
    script->SetScriptPath(path.string());
    scene.OnUpdate(1.0f);
    if (!Check(NearlyEqual(actor->GetTransform().position.x, 1.0f),
               "initial script file update failed")) return false;

    const auto previousWriteTime = fs::last_write_time(path);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "function Update(\n";
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
    if (!Check(project.SetStartupScene(startupPath, &error) && project.Save(&error),
               "project startup scene save failed: " + error)) return false;

    ProjectConfig loaded;
    if (!Check(loaded.Open(root, false, &error), "project manifest load failed: " + error)) return false;
    if (!Check(loaded.GetVersion() == ProjectConfig::kCurrentVersion &&
               loaded.GetName() == "ProjectTest" &&
               loaded.GetStartupScene() == "Content/Scenes/Main.scene.json",
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
    const auto scriptPath = scripts / "Portable.lua";
    std::ofstream(scriptPath) << "function Update(dt) end\n";
    ScriptComponent script;
    script.SetScriptPath(scriptPath.string());
    nlohmann::json scriptData;
    script.Serialize(scriptData);
    if (!Check(scriptData.value("scriptPath", std::string{}) ==
                   "Content/Scripts/Portable.lua",
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
    const auto scriptPath = projectRoot / "Content" / "Scripts" / "main.lua";
    fs::create_directories(scriptPath.parent_path());
    std::ofstream(scriptPath) << "function Update(dt) end\n";

    ProjectConfig project;
    if (!Check(project.Open(projectRoot, false, &error),
               "created project failed to reopen: " + error)) return false;
    project.GetPublishSettings().outputDirectory = (base / "Published").string();
    project.GetPublishSettings().target = "windows-x64";
    if (!Check(project.Save(&error), "publish settings save failed: " + error)) return false;
    project.GetPublishSettings().target = "unsupported-target";
    if (!Check(!project.Save(&error), "unsupported publish target was accepted")) return false;
    project.GetPublishSettings().target = "windows-x64";
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
    for (const char* file : runtimeFiles) std::ofstream(binaries / file) << file;

    PublishReport report;
    if (!Check(ProjectPublisher::Publish(project, binaries, report, &error),
               "project publish failed: " + error)) return false;
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
               manifest.target == "windows-x64" &&
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
    if (!Check(!ProjectPublisher::Publish(project, binaries, failedReport, &error) &&
               fs::is_regular_file(oldPackageMarker) &&
               !fs::exists(report.outputDirectory.string() + ".staging") &&
               !fs::exists(report.outputDirectory.string() + ".backup"),
               "failed publish damaged the previous package or left temporary output")) return false;
    std::ofstream(binaries / runtimeFiles[0]) << runtimeFiles[0];
    PublishReport replacementReport;
    if (!Check(ProjectPublisher::Publish(project, binaries, replacementReport, &error) &&
               !fs::exists(oldPackageMarker) &&
               !fs::exists(replacementReport.outputDirectory.string() + ".backup"),
               "transactional publish replacement failed: " + error)) return false;

    const fs::path interruptedBackup = replacementReport.outputDirectory.string() + ".backup";
    fs::rename(replacementReport.outputDirectory, interruptedBackup);
    fs::remove(binaries / runtimeFiles[0]);
    if (!Check(!ProjectPublisher::Publish(project, binaries, failedReport, &error) &&
               fs::is_directory(replacementReport.outputDirectory) &&
               !fs::exists(interruptedBackup),
               "interrupted publish backup was not restored before preflight")) return false;
    std::ofstream(binaries / runtimeFiles[0]) << runtimeFiles[0];

    const auto extracted = base / "Extracted";
    if (!Check(ContentArchive::Extract(report.contentArchive, extracted, &error),
               "Content archive extraction failed: " + error)) return false;
    if (!Check(fs::is_regular_file(extracted / "Content/Scenes/Main.scene.json") &&
               fs::is_regular_file(extracted / "Content/Data/payload.bin") &&
               fs::is_regular_file(extracted / "Content/Scripts/main.lua"),
               "cooked Content files were not restored")) return false;
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

} // namespace

int main() {
    MemoryService::Get().Init();

    int failed = 0;

    if (!TestMemoryLinearAllocator()) { ++failed; }
    if (!TestMemoryPoolAllocator()) { ++failed; }
    if (!TestMemoryServiceHeapRoundTrip()) { ++failed; }
    if (!TestSceneAndAssetMemoryCounters()) { ++failed; }
    if (!TestEditorCommandStackAndSelection()) { ++failed; }
    if (!TestEditorSceneSnapshotCommands()) { ++failed; }
    if (!TestEditorGizmoRowVectorLocalConversion()) { ++failed; }
    if (!TestEditorServiceActionAndInspectorRegistries()) { ++failed; }
    if (!TestEditorProjectAndAssetRegistry()) { ++failed; }
    if (!TestProjectConfigAndPortableAssetPaths()) { ++failed; }
    if (!TestWorkspaceCookAndPublish()) { ++failed; }

    if (!TestSceneSerializationRegression()) { ++failed; }
    if (!TestScriptRuntimeLifecycle()) { ++failed; }
    if (!TestLuaScriptFilesErrorsAndPhysicsBindings()) { ++failed; }
    if (!TestPhysicsGroundCollision()) { ++failed; }
    if (!TestExtendedCollisionShapes()) { ++failed; }
    if (!TestPhysicsBroadPhaseTriggersAndSleep()) { ++failed; }
    if (!TestRaycastAndCharacterController()) { ++failed; }
    if (!TestGpuSkinningAnimationBlend()) { ++failed; }
    if (!TestPbrMaterialParameters()) { ++failed; }
    if (!TestMaterialAssetFileRoundTrip()) { ++failed; }
    if (!TestAssetManagerSharedAcrossRuntimeBoundary()) { ++failed; }
    if (!TestComponentRegistry()) { ++failed; }
    if (!TestSceneRunStates()) { ++failed; }
    if (!TestHeadlessRendering()) { ++failed; }
    if (!TestCrashReportWriting()) { ++failed; }
    if (!TestTransformHierarchyWorldPosition()) { ++failed; }
    if (!TestCameraViewportProjectionStability()) { ++failed; }
    if (!TestInputBoundaries()) { ++failed; }
    if (!TestGamepadStateTransitions()) { ++failed; }
    if (!TestAssetFileImporters()) { ++failed; }
    if (!TestGltfImportAndStableMeta()) { ++failed; }
    if (!TestAssetAsyncLoadingAndHotReload()) { ++failed; }
    if (!TestAssetManagerFailureRollback()) { ++failed; }
    if (!TestSceneSerializerMalformedDataIsolation()) { ++failed; }
    if (!TestScriptHotReloadFailureRollback()) { ++failed; }
    if (!TestMeshDerivedData()) { ++failed; }
    if (!TestTextureDerivedData()) { ++failed; }

    Input::Shutdown();

    MemoryService::Get().Shutdown();

    if (failed == 0) {
        std::cout << "[PASS] All tests passed\n";
        return 0;
    }

    std::cerr << "[FAIL] Total failed suites: " << failed << '\n';
    return 1;
}
