#include "Renderer/Renderer.h"

#include "Assets/AssetManager.h"
#include "Assets/MeshAsset.h"
#include "Assets/MaterialAsset.h"
#include "Core/Math.h"
#include "Core/Logger.h"

#include <cstring>

void Renderer::EnsureMeshUploaded(MeshAsset* mesh)
{
    if (!mesh || mesh->IsUploaded() || !m_Context) return;

    const auto& verts = mesh->GetVertices();
    const auto& idx   = mesh->GetIndices();
    if (verts.empty()) return;

    const uint32_t vbBytes = static_cast<uint32_t>(verts.size() * sizeof(MeshVertex));
    auto vb = m_Context->CreateVertexBuffer(verts.data(), vbBytes, sizeof(MeshVertex));
    mesh->SetVertexBuffer(vb);

    if (!idx.empty()) {
        const uint32_t ibBytes = static_cast<uint32_t>(idx.size() * sizeof(uint32_t));
        auto ib = m_Context->CreateIndexBuffer(idx.data(), ibBytes);
        mesh->SetIndexBuffer(ib);
    }
}

GpuShader* Renderer::GetOrCreateMeshShader()
{
    if (m_MeshShader) return m_MeshShader.get();
    if (!m_Context)   return nullptr;

    m_MeshShader = m_Context->CreateShader(
        k_MeshShaderSource, "VSMain", "PSMain",
        k_MeshVertexLayout, k_MeshVertexLayoutCount);
    return m_MeshShader.get();
}

void Renderer::EnsureTextureUploaded(TextureAsset* tex)
{
    if (!tex || !m_Context) return;
    if (tex->HasGpuHandle()) return;   // already uploaded this session
    if (m_TexCache.count(tex)) return; // shared_ptr already alive

    const auto& pixels = tex->GetPixelData();
    if (pixels.empty()) return;

    auto gpuTex = m_Context->UploadTexture2D(
        pixels.data(), tex->GetWidth(), tex->GetHeight());
    if (gpuTex) {
        tex->SetGpuHandle(gpuTex.get());
        m_TexCache[tex] = std::move(gpuTex);
    }
}

void Renderer::RenderScene(const Scene& scene, const Camera& camera, bool present)
{
    if (!m_Context) return;

    GpuShader* shader = GetOrCreateMeshShader();
    if (!shader) return;

    // Clear + set common state (viewport is controlled by caller / engine).
    m_Context->BeginFrame(0.12f, 0.12f, 0.18f);

    const Mat4 viewProj = camera.GetViewProj();

    scene.ForEach([this, viewProj, shader](Actor& actor) {
        if (!actor.IsActive()) return;

        auto* mr = actor.GetComponent<MeshRendererComponent>();
        if (!mr || !mr->IsValid()) return;

        MeshAsset*     mesh = mr->GetMesh().Get();
        MaterialAsset* mat  = mr->GetMaterial().Get();
        if (!mesh || !mat) return;

        EnsureMeshUploaded(mesh);
        if (!mesh->GetVertexBuffer()) return;

        const Mat4 world = actor.GetWorldMatrix();
        const Mat4 mvp   = world * viewProj;

        MeshPerDrawConstants constants{};
        std::memcpy(constants.mvp, mvp.Data(), 64);

        const Vec3 baseColor = mat->GetColor("BaseColor", Vec3::One());
        constants.baseColor[0] = baseColor.x;
        constants.baseColor[1] = baseColor.y;
        constants.baseColor[2] = baseColor.z;
        constants.baseColor[3] = 1.0f;

        m_Context->BindShader(shader);
        m_Context->BindVertexBuffer(mesh->GetVertexBuffer());

        // Bind BaseColorMap texture (slot 0)
        GpuTexture* gpuTex = nullptr;
        if (mat->HasTexture("BaseColorMap")) {
            TextureAsset* texAsset = mat->GetTexture("BaseColorMap").Get();
            if (texAsset) {
                EnsureTextureUploaded(texAsset);
                gpuTex = static_cast<GpuTexture*>(texAsset->GetGpuHandle());
            }
        }
        m_Context->BindPSTexture(0, gpuTex);

        if (mesh->GetIndexBuffer()) {
            m_Context->BindIndexBuffer(mesh->GetIndexBuffer());
            for (const auto& sm : mesh->GetSubMeshes()) {
                m_Context->SetVSConstants(&constants, sizeof(constants));
                m_Context->DrawIndexed(sm.indexCount, sm.indexOffset,
                                       static_cast<uint32_t>(sm.vertexOffset));
            }
        } else {
            m_Context->BindIndexBuffer(nullptr);
            for (const auto& sm : mesh->GetSubMeshes()) {
                m_Context->SetVSConstants(&constants, sizeof(constants));
                m_Context->Draw(sm.indexCount, sm.vertexOffset);
            }
        }
    });

    if (present) {
        m_Context->EndFrame();
    }
}

