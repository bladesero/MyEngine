#include "Game/SceneRenderLayer.h"
#include "Renderer/MeshShader.h"
#include "Renderer/IRenderContext.h"
#include "Assets/AssetManager.h"
#include "Assets/MeshAsset.h"
#include "Assets/MaterialAsset.h"
#include "Core/Logger.h"
#include "Core/Math.h"
#include "Input/Input.h"
#include <SDL3/SDL_scancode.h>
#include <cstring>

// --------------------------------------------------------------------------
SceneRenderLayer::SceneRenderLayer(IRenderContext* context,
                                   int viewportWidth, int viewportHeight)
    : SceneLayer("SceneRenderLayer")
    , m_RenderContext(context)
    , m_VpW(viewportWidth)
    , m_VpH(viewportHeight)
{
}

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
    m_DefaultMeshShader.reset();
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
        Actor* cube = GetScene().CreateActor("Cube");
        auto* mr = cube->AddComponent<MeshRendererComponent>();
        mr->SetMesh(AssetManager::Get().GetCubeMesh());
        mr->SetMaterial(AssetManager::Get().GetDefaultMaterial());
        Logger::Info("[SceneRenderLayer] added demo Cube with MeshRenderer");
    }
}

GpuShader* SceneRenderLayer::GetOrCreateMeshShader() {
    if (m_DefaultMeshShader) return m_DefaultMeshShader.get();
    if (!m_RenderContext) return nullptr;
    m_DefaultMeshShader = m_RenderContext->CreateShader(
        k_MeshHLSL, "VSMain", "PSMain",
        k_MeshVertexLayout, k_MeshVertexLayoutCount);
    return m_DefaultMeshShader.get();
}

void SceneRenderLayer::EnsureMeshUploaded(MeshAsset* mesh) {
    if (!mesh || mesh->IsUploaded() || !m_RenderContext) return;
    const auto& verts = mesh->GetVertices();
    const auto& idx   = mesh->GetIndices();
    if (verts.empty()) return;

    uint32_t vbBytes = static_cast<uint32_t>(verts.size() * sizeof(MeshVertex));
    auto vb = m_RenderContext->CreateVertexBuffer(
        verts.data(), vbBytes, sizeof(MeshVertex));
    mesh->SetVertexBuffer(vb);

    if (!idx.empty()) {
        uint32_t ibBytes = static_cast<uint32_t>(idx.size() * sizeof(uint32_t));
        auto ib = m_RenderContext->CreateIndexBuffer(idx.data(), ibBytes);
        mesh->SetIndexBuffer(ib);
    }
}

void SceneRenderLayer::OnRender() {
    if (!m_RenderContext) return;

    GpuShader* shader = GetOrCreateMeshShader();
    if (!shader) return;

    m_RenderContext->BeginFrame(0.12f, 0.12f, 0.18f);

    Mat4 viewProj = m_Camera.GetViewProj();

    GetScene().ForEach([this, viewProj, shader](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* mr = actor.GetComponent<MeshRendererComponent>();
        if (!mr || !mr->IsValid()) return;

        MeshAsset*    mesh = mr->GetMesh().Get();
        MaterialAsset* mat = mr->GetMaterial().Get();
        if (!mesh || !mat) return;

        EnsureMeshUploaded(mesh);
        if (!mesh->GetVertexBuffer()) return;

        Mat4 world = actor.GetWorldMatrix();
        Mat4 mvp   = world * viewProj;

        MeshPerDrawConstants constants;
        // Upload row-major MVP; HLSL mul(pos, g_MVP) matches engine row-vector convention.
        memcpy(constants.mvp, mvp.Data(), 64);
        Vec3 baseColor = mat->GetColor("BaseColor", Vec3::One());
        constants.baseColor[0] = baseColor.x;
        constants.baseColor[1] = baseColor.y;
        constants.baseColor[2] = baseColor.z;
        constants.baseColor[3] = 1.0f;

        m_RenderContext->BindShader(shader);
        m_RenderContext->BindVertexBuffer(mesh->GetVertexBuffer());
        if (mesh->GetIndexBuffer()) {
            m_RenderContext->BindIndexBuffer(mesh->GetIndexBuffer());
            for (const auto& sm : mesh->GetSubMeshes()) {
                m_RenderContext->SetVSConstants(&constants, sizeof(constants));
                m_RenderContext->DrawIndexed(sm.indexCount, sm.indexOffset,
                                             static_cast<uint32_t>(sm.vertexOffset));
            }
        } else {
            m_RenderContext->BindIndexBuffer(nullptr);
            for (const auto& sm : mesh->GetSubMeshes()) {
                m_RenderContext->SetVSConstants(&constants, sizeof(constants));
                m_RenderContext->Draw(sm.indexCount, sm.vertexOffset);
            }
        }
    });

    m_RenderContext->EndFrame();
}
