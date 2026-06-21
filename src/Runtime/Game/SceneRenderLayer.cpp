#include "Game/SceneRenderLayer.h"
#include "Audio/AudioEngine.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureAsset.h"
#include "Assets/MaterialAsset.h"
#include "Scene/MeshRendererComponent.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Physics/BoxColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Scripting/ScriptComponent.h"
#include "Core/Logger.h"
#include "Input/Input.h"
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_gamepad.h>
#include <algorithm>
#include <cmath>
#include <vector>

// --------------------------------------------------------------------------
SceneRenderLayer::SceneRenderLayer(IRenderContext* context,
                                   int viewportWidth, int viewportHeight)
    : SceneLayer("SceneRenderLayer")
    , m_RenderContext(context)
    , m_Renderer(context, context, context)
    , m_VpW(viewportWidth)
    , m_VpH(viewportHeight)
{}

void SceneRenderLayer::SetPresentEnabled(bool enabled)
{
    m_PresentEnabled = enabled;
    m_Renderer.SetOutputOffscreen(!enabled);
}

void SceneRenderLayer::OnAttach() {
    SceneLayer::OnAttach();
    if (m_RenderContext && m_VpH > 0) {
        m_VpX = 0;
        m_VpY = 0;
        m_Camera.SetCameraMode(CameraMode::Fly);
        m_Camera.LookAt({ 0.0f, 0.0f, -4.0f }, { 0.0f, 0.0f, 0.0f });
        m_Camera.SetPerspective(60.0f,
            static_cast<float>(m_VpW) / static_cast<float>(m_VpH),
            0.1f,
            1000.0f);
        if (auto* commands = m_RenderContext->GetGraphicsCommandList())
            commands->SetViewport(static_cast<float>(m_VpX), static_cast<float>(m_VpY),
                                  static_cast<float>(m_VpW), static_cast<float>(m_VpH));
    }
    if (m_VpW > 0 && m_VpH > 0) {
        m_Renderer.Resize(static_cast<uint32_t>(m_VpW), static_cast<uint32_t>(m_VpH));
        m_RendererW = m_VpW;
        m_RendererH = m_VpH;
    }
    Logger::Info("[SceneRenderLayer] attached (", m_VpW, "x", m_VpH, ")");
}

void SceneRenderLayer::OnDetach() {
    SceneLayer::OnDetach();
}

void SceneRenderLayer::OnUpdate(float dt) {
    SceneLayer::OnUpdate(dt);

    if (!m_ViewportInputEnabled) {
        m_RmbDown = false;
        AudioEngine::Get().SetListenerTransform(
            m_Camera.GetPosition(), m_Camera.GetForward(), m_Camera.GetCamUp());
        return;
    }

    const float moveSpeed = 6.0f;
    if (Input::IsKeyDown(SDL_SCANCODE_W)) m_Camera.MoveForward( moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_S)) m_Camera.MoveForward(-moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_D)) m_Camera.MoveRight( moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_A)) m_Camera.MoveRight(-moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_Q)) m_Camera.MoveUp( moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_E)) m_Camera.MoveUp(-moveSpeed * dt);

    if (Input::IsMousePressed(3))  { m_RmbDown = true; }
    if (Input::IsMouseReleased(3)) { m_RmbDown = false; }
    if (m_RmbDown) {
        const int rx = Input::GetMouseRelX(), ry = Input::GetMouseRelY();
        if (rx != 0 || ry != 0) {
            const float lookSens = 0.25f;
            m_Camera.Rotate(-static_cast<float>(rx) * lookSens,
                            -static_cast<float>(ry) * lookSens);
        }
    }

    const SDL_JoystickID pad = Input::GetPrimaryGamepadId();
    if (pad != 0 && Input::IsGamepadConnected(pad)) {
        const float lx = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        const float ly = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        const float rx = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
        const float ry = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
        const float rt = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        const float lt = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);

        constexpr float kStickDead = 0.15f;
        if (std::fabs(lx) > kStickDead || std::fabs(ly) > kStickDead) {
            m_Camera.MoveRight(lx * moveSpeed * dt);
            m_Camera.MoveForward(-ly * moveSpeed * dt);
        }
        if (std::fabs(rx) > kStickDead || std::fabs(ry) > kStickDead) {
            m_Camera.Rotate(-rx * 120.0f * dt, -ry * 120.0f * dt);
        }

        const float dolly = rt - lt;
        if (std::fabs(dolly) > 0.05f) {
            m_Camera.MoveForward(dolly * 4.0f * dt);
        }
    }

    AudioEngine::Get().SetListenerTransform(
        m_Camera.GetPosition(), m_Camera.GetForward(), m_Camera.GetCamUp());
}

void SceneRenderLayer::OnEvent(Event& event) {
    SceneLayer::OnEvent(event);
    if (event.type == EventType::WindowResize) {
        const int windowW = event.resize.width;
        const int windowH = event.resize.height;
        if (windowW <= 0 || windowH <= 0) return;

        if (!m_UseEditorViewport) {
            m_VpX = 0;
            m_VpY = 0;
            m_VpW = windowW;
            m_VpH = windowH;
            m_Camera.SetAspect(static_cast<float>(m_VpW) / static_cast<float>(m_VpH));
            m_Renderer.Resize(static_cast<uint32_t>(m_VpW),
                              static_cast<uint32_t>(m_VpH));
            m_RendererW = m_VpW;
            m_RendererH = m_VpH;
        }

        if (m_RenderContext) {
            if (GpuSwapChain* swapChain = m_RenderContext->GetSwapChain()) {
                swapChain->Resize(static_cast<uint32_t>(windowW),
                                  static_cast<uint32_t>(windowH));
            }
            if (auto* commands = m_RenderContext->GetGraphicsCommandList())
                commands->SetViewport(static_cast<float>(m_VpX), static_cast<float>(m_VpY),
                                      static_cast<float>(m_VpW), static_cast<float>(m_VpH));
        }
    }
}

void SceneRenderLayer::OnSceneLoaded() {
    SceneLayer::OnSceneLoaded();
    if (GetScene().ActorCount() == 0) {
        // Build a 16x16 checkerboard texture (orange / dark-grey)
        constexpr int kTexSize = 16;
        constexpr int kCellSize = 2; // cells of 2x2 pixels each
        std::vector<uint8_t> pixels(kTexSize * kTexSize * 4);
        for (int y = 0; y < kTexSize; ++y) {
            for (int x = 0; x < kTexSize; ++x) {
                const bool light = ((x / kCellSize) + (y / kCellSize)) % 2 == 0;
                int idx = (y * kTexSize + x) * 4;
                pixels[idx + 0] = light ? 230 : 50;   // R
                pixels[idx + 1] = light ? 130 : 50;   // G
                pixels[idx + 2] = light ?  30 : 50;   // B
                pixels[idx + 3] = 255;
            }
        }
        auto checkerTex = std::make_shared<TextureAsset>("__builtin__/Checker");
        checkerTex->SetName("Checker");
        TextureDesc td;
        td.width  = kTexSize;
        td.height = kTexSize;
        td.sRGB   = false;
        checkerTex->SetPixelData(std::move(pixels), td);
        TextureHandle checkerHandle = AssetManager::Get().Register(std::move(checkerTex));

        Actor* sun = GetScene().CreateActor("Sun");
        auto* sunLight = sun->AddComponent<LightComponent>();
        sunLight->SetLightType(LightType::Directional);
        sunLight->SetDirection(Vec3{ -0.55f, -1.0f, -0.45f });
        sunLight->SetColor(Vec3{ 1.0f, 0.96f, 0.88f });
        sunLight->SetIntensity(3.0f);
        sunLight->SetCastShadows(true);

        Actor* postActor = GetScene().CreateActor("PostProcess");
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

        // First cube - checkerboard texture
        Actor* cube1 = GetScene().CreateActor("Cube1");
        cube1->GetTransform().position = Vec3{ 0.0f, 0.5f, 0.0f };
        auto* mr1 = cube1->AddComponent<MeshRendererComponent>();
        mr1->SetMesh(AssetManager::Get().GetCubeMesh());
        auto mat1 = MaterialAsset::CreateDefault("CheckerMat");
        mat1->SetTexture("BaseColorMap", checkerHandle);
        mat1->SetParam("Metallic", MaterialParam::FromFloat(0.15f));
        mat1->SetParam("Roughness", MaterialParam::FromFloat(0.35f));
        mr1->SetMaterial(AssetManager::Get().Register(std::move(mat1)));
        auto* script = cube1->AddComponent<ScriptComponent>();
        script->SetSource(
            "class Script {\n"
            "  void Update(float dt) { Actor::Rotate(Vec3(0, 35 * dt, 0)); }\n"
            "}\n");

        // Second cube offset in X and colored differently
        Actor* cube2 = GetScene().CreateActor("Cube2");
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

        // Ground plane to validate shadow projection in editor.
        Actor* plane = GetScene().CreateActor("ShadowPlane");
        plane->GetTransform().position = Vec3{ 0.0f, 0.0f, 0.0f };
        plane->GetTransform().rotation = Vec3{ -90.0f, 0.0f, 0.0f };
        plane->GetTransform().scale    = Vec3{ 12.0f, 12.0f, 12.0f };
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

        Actor* skinnedActor = GetScene().CreateActor("SkinnedCube");
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

        Logger::Info("[SceneRenderLayer] added scripted PBR, physics and skinned-mesh demo");
    }
}

void SceneRenderLayer::OnRender() {
    if (!m_RenderContext) return;
    if (m_VpW > 0 && m_VpH > 0 &&
        (m_RendererW != m_VpW || m_RendererH != m_VpH)) {
        m_Renderer.Resize(static_cast<uint32_t>(m_VpW),
                          static_cast<uint32_t>(m_VpH));
        m_RendererW = m_VpW;
        m_RendererH = m_VpH;
    }
    if (m_VpW > 0 && m_VpH > 0) {
        if (auto* commands = m_RenderContext->GetGraphicsCommandList())
            commands->SetViewport(static_cast<float>(m_VpX), static_cast<float>(m_VpY),
                                  static_cast<float>(m_VpW), static_cast<float>(m_VpH));
    }
    m_Renderer.RenderScene(GetScene(), m_Camera, m_PresentEnabled);
}

void SceneRenderLayer::SetEditorViewportRect(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    m_UseEditorViewport = true;

    const int clampedX = (std::max)(0, x);
    const int clampedY = (std::max)(0, y);
    if (m_VpX == clampedX && m_VpY == clampedY &&
        m_VpW == width && m_VpH == height) {
        return;
    }

    m_VpX = clampedX;
    m_VpY = clampedY;
    m_VpW = width;
    m_VpH = height;

    m_Camera.SetAspect(static_cast<float>(m_VpW) / static_cast<float>(m_VpH));

    if (m_RenderContext) {
        if (auto* commands = m_RenderContext->GetGraphicsCommandList())
            commands->SetViewport(static_cast<float>(m_VpX), static_cast<float>(m_VpY),
                                  static_cast<float>(m_VpW), static_cast<float>(m_VpH));
    }
}

void SceneRenderLayer::SetViewportInputEnabled(bool enabled)
{
    m_ViewportInputEnabled = enabled;
    if (!enabled) {
        m_RmbDown = false;
    }
}

void SceneRenderLayer::GetViewportRect(int& outX, int& outY, int& outW, int& outH) const
{
    outX = m_VpX;
    outY = m_VpY;
    outW = m_VpW;
    outH = m_VpH;
}

bool SceneRenderLayer::BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const
{
    if (m_VpW <= 0 || m_VpH <= 0) {
        return false;

    }
    if (screenX < static_cast<float>(m_VpX) || screenY < static_cast<float>(m_VpY) ||
        screenX > static_cast<float>(m_VpX + m_VpW) || screenY > static_cast<float>(m_VpY + m_VpH)) {
        return false;
    }

    // Match ImGui / ImGuizmo::ComputeCameraRay: NDC in [-1,1], Y up in NDC.
    const float mox = ((screenX - static_cast<float>(m_VpX)) / static_cast<float>(m_VpW)) * 2.0f - 1.0f;
    const float moy = (1.0f - ((screenY - static_cast<float>(m_VpY)) / static_cast<float>(m_VpH))) * 2.0f - 1.0f;

    return m_Camera.BuildRayFromNdc(mox, moy, outRay);
}
