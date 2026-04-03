#include "Game/SceneRenderLayer.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureAsset.h"
#include "Assets/MaterialAsset.h"
#include "Scene/MeshRendererComponent.h"
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
    , m_Renderer(context)
    , m_VpW(viewportWidth)
    , m_VpH(viewportHeight)
{}

void SceneRenderLayer::OnAttach() {
    SceneLayer::OnAttach();
    if (m_RenderContext && m_VpH > 0) {
        m_VpX = 0;
        m_VpY = 0;
        m_Camera.LookAt({ 0.0f, 0.0f, -4.0f }, { 0.0f, 0.0f, 0.0f });
        m_Camera.SetPerspective(60.0f,
            static_cast<float>(m_VpW) / static_cast<float>(m_VpH),
            0.1f,
            1000.0f);
        m_RenderContext->SetViewport(
            static_cast<float>(m_VpX), static_cast<float>(m_VpY),
            static_cast<float>(m_VpW), static_cast<float>(m_VpH));
    }
    if (m_VpW > 0 && m_VpH > 0) {
        m_Renderer.Resize(static_cast<uint32_t>(m_VpW), static_cast<uint32_t>(m_VpH));
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
        return;
    }

    // Simple orbit: Q/E dolly, right-mouse orbit
    if (Input::IsKeyDown(SDL_SCANCODE_Q)) m_Camera.Dolly( 2.0f * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_E)) m_Camera.Dolly(-2.0f * dt);
    if (Input::IsMousePressed(3))  { m_RmbDown = true; }
    if (Input::IsMouseReleased(3)) { m_RmbDown = false; }
    if (m_RmbDown) {
        int rx = Input::GetMouseRelX(), ry = Input::GetMouseRelY();
        if (rx != 0 || ry != 0)
            m_Camera.Orbit(static_cast<float>(rx) * 0.3f,
                           static_cast<float>(ry) * 0.3f);
    }

    const SDL_JoystickID pad = Input::GetPrimaryGamepadId();
    if (pad != 0 && Input::IsGamepadConnected(pad)) {
        const float lx = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        const float ly = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        const float rt = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        const float lt = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);

        if (std::fabs(lx) > 0.15f || std::fabs(ly) > 0.15f) {
            m_Camera.Orbit(lx * 120.0f * dt, ly * 120.0f * dt);
        }

        const float dolly = rt - lt;
        if (std::fabs(dolly) > 0.05f) {
            m_Camera.Dolly(dolly * 4.0f * dt);
        }
    }
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
        }

        if (m_RenderContext) {
            if (GpuSwapChain* swapChain = m_RenderContext->GetSwapChain()) {
                swapChain->Resize(static_cast<uint32_t>(windowW),
                                  static_cast<uint32_t>(windowH));
            }
            m_RenderContext->SetViewport(
                static_cast<float>(m_VpX), static_cast<float>(m_VpY),
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

        // First cube - checkerboard texture
        Actor* cube1 = GetScene().CreateActor("Cube1");
        cube1->GetTransform().position = Vec3{ 0.0f, 0.5f, 0.0f };
        auto* mr1 = cube1->AddComponent<MeshRendererComponent>();
        mr1->SetMesh(AssetManager::Get().GetCubeMesh());
        auto mat1 = MaterialAsset::CreateDefault("CheckerMat");
        mat1->SetTexture("BaseColorMap", checkerHandle);
        mr1->SetMaterial(AssetManager::Get().Register(std::move(mat1)));

        // Second cube offset in X and colored differently
        Actor* cube2 = GetScene().CreateActor("Cube2");
        cube2->GetTransform().position = Vec3{ 2.0f, 0.5f, 0.0f };
        auto* mr2 = cube2->AddComponent<MeshRendererComponent>();
        mr2->SetMesh(AssetManager::Get().GetCubeMesh());
        auto mat2 = AssetManager::Get().GetDefaultMaterial();
        if (mat2) {
            mat2->SetParam("BaseColor", MaterialParam::FromColor({0.1f, 0.7f, 1.0f}));
            mr2->SetMaterial(mat2);
        } else {
            mr2->SetMaterial(AssetManager::Get().GetDefaultMaterial());
        }

        // Ground plane to validate shadow projection in editor.
        Actor* plane = GetScene().CreateActor("ShadowPlane");
        plane->GetTransform().position = Vec3{ 0.0f, -0.5f, 0.0f };
        plane->GetTransform().rotation = Vec3{ -90.0f, 0.0f, 0.0f };
        plane->GetTransform().scale    = Vec3{ 12.0f, 12.0f, 12.0f };
        auto* planeMr = plane->AddComponent<MeshRendererComponent>();
        planeMr->SetMesh(AssetManager::Get().GetQuadMesh());
        auto planeMat = MaterialAsset::CreateDefault("GroundMat");
        planeMat->SetParam("BaseColor", MaterialParam::FromColor({0.8f, 0.8f, 0.8f}));
        planeMat->SetTexture("BaseColorMap", AssetManager::Get().GetWhiteTexture());
        planeMr->SetMaterial(AssetManager::Get().Register(std::move(planeMat)));

        Logger::Info("[SceneRenderLayer] added demo cubes + plane for shadow test");
    }
}

void SceneRenderLayer::OnRender() {
    if (!m_RenderContext) return;
    if (m_VpW > 0 && m_VpH > 0) {
        m_RenderContext->SetViewport(
            static_cast<float>(m_VpX), static_cast<float>(m_VpY),
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
        m_RenderContext->SetViewport(
            static_cast<float>(m_VpX), static_cast<float>(m_VpY),
            static_cast<float>(m_VpW), static_cast<float>(m_VpH));
    }
    m_Renderer.Resize(static_cast<uint32_t>(m_VpW), static_cast<uint32_t>(m_VpH));
}

void SceneRenderLayer::SetViewportInputEnabled(bool enabled)
{
    m_ViewportInputEnabled = enabled;
    if (!enabled) {
        m_RmbDown = false;
    }
}
