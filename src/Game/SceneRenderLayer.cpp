#include "Game/SceneRenderLayer.h"
#include "Assets/AssetManager.h"
#include "Core/Logger.h"
#include "Input/Input.h"
#include <SDL3/SDL_scancode.h>

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
        m_Camera.LookAt({ 0.0f, 0.0f, -4.0f }, { 0.0f, 0.0f, 0.0f });
        m_Camera.SetPerspective(60.0f,
            static_cast<float>(m_VpW) / static_cast<float>(m_VpH),
            0.1f,
            1000.0f);
        m_RenderContext->SetViewport(0, 0,
            static_cast<float>(m_VpW), static_cast<float>(m_VpH));
    }
    Logger::Info("[SceneRenderLayer] attached (", m_VpW, "x", m_VpH, ")");
}

void SceneRenderLayer::OnDetach() {
    SceneLayer::OnDetach();
}

void SceneRenderLayer::OnUpdate(float dt) {
    SceneLayer::OnUpdate(dt);
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
}

void SceneRenderLayer::OnEvent(Event& event) {
    SceneLayer::OnEvent(event);
    if (event.type == EventType::WindowResize && m_VpH > 0) {
        m_VpW = event.resize.width;
        m_VpH = event.resize.height;
        m_Camera.SetAspect(static_cast<float>(m_VpW) / static_cast<float>(m_VpH));
        if (m_RenderContext)
            m_RenderContext->SetViewport(0, 0,
                static_cast<float>(m_VpW), static_cast<float>(m_VpH));
    }
}

void SceneRenderLayer::OnSceneLoaded() {
    SceneLayer::OnSceneLoaded();
    if (GetScene().ActorCount() == 0) {
        // First cube at origin
        Actor* cube1 = GetScene().CreateActor("Cube1");
        cube1->GetTransform().position = Vec3::Zero();
        auto* mr1 = cube1->AddComponent<MeshRendererComponent>();
        mr1->SetMesh(AssetManager::Get().GetCubeMesh());
        mr1->SetMaterial(AssetManager::Get().GetDefaultMaterial());

        // Second cube offset in X and colored differently
        Actor* cube2 = GetScene().CreateActor("Cube2");
        cube2->GetTransform().position = Vec3{ 2.0f, 0.0f, 0.0f };
        auto* mr2 = cube2->AddComponent<MeshRendererComponent>();
        mr2->SetMesh(AssetManager::Get().GetCubeMesh());
        auto mat2 = AssetManager::Get().GetDefaultMaterial();
        if (mat2) {
            mat2->SetParam("BaseColor", MaterialParam::FromColor({0.1f, 0.7f, 1.0f}));
            mr2->SetMaterial(mat2);
        } else {
            mr2->SetMaterial(AssetManager::Get().GetDefaultMaterial());
        }

        Logger::Info("[SceneRenderLayer] added demo cubes with MeshRenderer");
    }
}

void SceneRenderLayer::OnRender() {
    if (!m_RenderContext) return;
    m_Renderer.RenderScene(GetScene(), m_Camera, m_PresentEnabled);
}
