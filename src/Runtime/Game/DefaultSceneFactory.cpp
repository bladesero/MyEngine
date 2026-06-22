#include "Game/DefaultSceneFactory.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptComponent.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

void DefaultSceneFactory::PopulateIfEmpty(Scene& scene)
{
    if (scene.ActorCount() != 0) {
        return;
    }

    constexpr int kTexSize = 16;
    constexpr int kCellSize = 2;
    std::vector<uint8_t> pixels(kTexSize * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            const bool light = ((x / kCellSize) + (y / kCellSize)) % 2 == 0;
            const int idx = (y * kTexSize + x) * 4;
            pixels[idx + 0] = light ? 230 : 50;
            pixels[idx + 1] = light ? 130 : 50;
            pixels[idx + 2] = light ?  30 : 50;
            pixels[idx + 3] = 255;
        }
    }
    auto checkerTex = std::make_shared<TextureAsset>("__builtin__/Checker");
    checkerTex->SetName("Checker");
    TextureDesc td;
    td.width = kTexSize;
    td.height = kTexSize;
    td.sRGB = false;
    checkerTex->SetPixelData(std::move(pixels), td);
    TextureHandle checkerHandle = AssetManager::Get().Register(std::move(checkerTex));

    Actor* sun = scene.CreateActor("Sun");
    auto* sunLight = sun->AddComponent<LightComponent>();
    sunLight->SetLightType(LightType::Directional);
    sunLight->SetDirection(Vec3{ -0.55f, -1.0f, -0.45f });
    sunLight->SetColor(Vec3{ 1.0f, 0.96f, 0.88f });
    sunLight->SetIntensity(3.0f);
    sunLight->SetCastShadows(true);

    Actor* postActor = scene.CreateActor("PostProcess");
    auto* post = postActor->AddComponent<PostProcessComponent>();
    post->SetExposure(1.0f);
    post->SetGamma(2.2f);
    post->SetToneMappingEnabled(true);
    post->SetSaturation(1.0f);
    post->SetContrast(1.0f);
    post->SetAntiAliasingStrength(1.0f);
    post->SetSSAORadius(1.5f);
    post->SetSSAOPower(1.8f);
    post->SetSSAOIntensity(0.8f);

    Actor* cube1 = scene.CreateActor("Cube1");
    cube1->GetTransform().position = Vec3{ 0.0f, 0.5f, 0.0f };
    auto* mr1 = cube1->AddComponent<MeshRendererComponent>();
    mr1->SetMesh(AssetManager::Get().GetCubeMesh());
    auto mat1 = MaterialAsset::CreateDefault("CheckerMat");
    mat1->SetTexture("BaseColorMap", checkerHandle);
    mat1->SetParam("Metallic", MaterialParam::FromFloat(0.15f));
    mat1->SetParam("Roughness", MaterialParam::FromFloat(0.35f));
    mr1->SetMaterial(AssetManager::Get().Register(std::move(mat1)));
    auto* script = cube1->AddComponent<ScriptComponent>();
    script->SetScriptPath("Content/Scripts/RotatingCube.as");
    script->SetClassName("RotatingCube");

    Actor* cube2 = scene.CreateActor("Cube2");
    cube2->GetTransform().position = Vec3{ 2.0f, 0.5f, 0.0f };
    auto* mr2 = cube2->AddComponent<MeshRendererComponent>();
    mr2->SetMesh(AssetManager::Get().GetCubeMesh());
    auto mat2 = MaterialAsset::CreateDefault("DynamicPbrMat");
    mat2->SetTexture("BaseColorMap", AssetManager::Get().GetWhiteTexture());
    mat2->SetParam("BaseColor", MaterialParam::FromColor({0.1f, 0.7f, 1.0f}));
    mat2->SetParam("Metallic", MaterialParam::FromFloat(0.75f));
    mat2->SetParam("Roughness", MaterialParam::FromFloat(0.2f));
    mr2->SetMaterial(AssetManager::Get().Register(std::move(mat2)));
    auto* dynamicBody = cube2->AddComponent<RigidBodyComponent>();
    dynamicBody->SetRestitution(0.25f);
    cube2->AddComponent<BoxColliderComponent>();

    Actor* plane = scene.CreateActor("ShadowPlane");
    plane->GetTransform().position = Vec3{ 0.0f, 0.0f, 0.0f };
    plane->GetTransform().rotation = Vec3{ -90.0f, 0.0f, 0.0f };
    plane->GetTransform().scale = Vec3{ 12.0f, 12.0f, 12.0f };
    auto* planeMr = plane->AddComponent<MeshRendererComponent>();
    planeMr->SetMesh(AssetManager::Get().GetQuadMesh());
    auto planeMat = MaterialAsset::CreateDefault("GroundMat");
    planeMat->SetTexture("BaseColorMap", AssetManager::Get().GetWhiteTexture());
    planeMat->SetParam("BaseColor", MaterialParam::FromColor({0.55f, 0.55f, 0.52f}));
    planeMat->SetParam("Metallic", MaterialParam::FromFloat(0.0f));
    planeMat->SetParam("Roughness", MaterialParam::FromFloat(0.9f));
    planeMat->SetParam("AmbientOcclusion", MaterialParam::FromFloat(1.0f));
    planeMr->SetMaterial(AssetManager::Get().Register(std::move(planeMat)));
    auto* groundBody = plane->AddComponent<RigidBodyComponent>();
    groundBody->SetBodyType(BodyType::Static);
    groundBody->SetUseGravity(false);
    groundBody->SetFriction(0.9f);
    groundBody->SetRestitution(0.0f);
    auto* groundCollider = plane->AddComponent<BoxColliderComponent>();
    groundCollider->SetHalfExtents({ 0.5f, 0.5f, 0.01f });

    Actor* skinnedActor = scene.CreateActor("SkinnedCube");
    skinnedActor->GetTransform().position = Vec3{ -2.0f, 0.5f, 0.0f };
    auto* skinned = skinnedActor->AddComponent<SkinnedMeshRendererComponent>();
    const MeshHandle cubeMesh = AssetManager::Get().GetCubeMesh();
    skinned->SetSourceMesh(cubeMesh);
    auto skinMat = MaterialAsset::CreateDefault("SkinPbrMat");
    skinMat->SetTexture("BaseColorMap", AssetManager::Get().GetWhiteTexture());
    skinMat->SetParam("BaseColor", MaterialParam::FromColor({0.85f, 0.25f, 0.15f}));
    skinMat->SetParam("Metallic", MaterialParam::FromFloat(0.05f));
    skinMat->SetParam("Roughness", MaterialParam::FromFloat(0.5f));
    skinned->SetMaterial(AssetManager::Get().Register(std::move(skinMat)));

    std::vector<Bone> bones(2);
    bones[0].name = "Root";
    bones[1].name = "Upper";
    bones[1].parent = 0;
    bones[1].bindTranslation = Vec3{ 0.0f, 0.5f, 0.0f };
    bones[1].inverseBind = Mat4::Translation(0.0f, -0.5f, 0.0f);
    std::vector<SkinWeight> weights(cubeMesh->VertexCount());
    for (size_t i = 0; i < weights.size(); ++i) {
        if (cubeMesh->GetVertices()[i].position.y > 0.0f) {
            weights[i].boneIndices[0] = 1;
        }
    }
    skinned->SetSkeleton(std::move(bones), std::move(weights));

    AnimationClip clip;
    clip.name = "Bend";
    clip.duration = 2.0f;
    BoneTrack upperTrack;
    upperTrack.boneIndex = 1;
    upperTrack.keys = {
        { 0.0f, Vec3{0.0f, 0.5f, 0.0f}, Quat::FromAxisAngle(Vec3::Forward(), -0.45f), Vec3::One() },
        { 1.0f, Vec3{0.0f, 0.5f, 0.0f}, Quat::FromAxisAngle(Vec3::Forward(),  0.45f), Vec3::One() },
        { 2.0f, Vec3{0.0f, 0.5f, 0.0f}, Quat::FromAxisAngle(Vec3::Forward(), -0.45f), Vec3::One() },
    };
    clip.tracks.push_back(std::move(upperTrack));
    skinned->SetAnimation(std::move(clip));

    Logger::Info("[DefaultSceneFactory] added scripted PBR, physics and skinned-mesh demo");
}
