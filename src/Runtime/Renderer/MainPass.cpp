#include "Renderer/MainPass.h"

#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Renderer/MeshShader.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"

#include <cstring>

#ifdef MYENGINE_PLATFORM_WINDOWS
#include "Renderer/D3D11Context.h"
#include "ShaderBytecodeWindows.h"
#endif

namespace {

struct LegacyPerDrawConstants {
    float mvp[16];
    float baseColor[4];
};

struct ShadowedPerDrawConstants {
    float mvp[16];
    float world[16];
    float lightViewProj[16];
    float baseColor[4];
    float lightDirection[4];
};

} // namespace

MainPass::MainPass(IRenderContext* context)
    : RenderPass(context)
{}

void MainPass::Resize(uint32_t, uint32_t)
{
    // Viewport is controlled by SceneRenderLayer via IRenderContext::SetViewport.
}

void MainPass::SetShadowInput(const Mat4& lightViewProj,
                              const Vec3& lightDirection,
                              GpuTexture* shadowMap)
{
    m_LightViewProj = lightViewProj;
    m_LightDirection = lightDirection;
    m_ShadowMap = shadowMap;
}

void MainPass::EnsureMeshUploaded(MeshAsset* mesh)
{
    if (!mesh || mesh->IsUploaded() || !Context()) return;

    const auto& verts = mesh->GetVertices();
    const auto& idx   = mesh->GetIndices();
    if (verts.empty()) return;

    const uint32_t vbBytes = static_cast<uint32_t>(verts.size() * sizeof(MeshVertex));
    mesh->SetVertexBuffer(Context()->CreateVertexBuffer(verts.data(), vbBytes, sizeof(MeshVertex)));

    if (!idx.empty()) {
        const uint32_t ibBytes = static_cast<uint32_t>(idx.size() * sizeof(uint32_t));
        mesh->SetIndexBuffer(Context()->CreateIndexBuffer(idx.data(), ibBytes));
    }
}

void MainPass::EnsureTextureUploaded(TextureAsset* tex)
{
    if (!tex || !Context()) return;
    if (tex->HasGpuHandle()) return;
    if (m_TexCache.count(tex)) return;

    const auto& pixels = tex->GetPixelData();
    if (pixels.empty()) return;

    auto gpuTex = Context()->UploadTexture2D(
        pixels.data(), tex->GetWidth(), tex->GetHeight());
    if (gpuTex) {
        tex->SetGpuHandle(gpuTex.get());
        m_TexCache[tex] = std::move(gpuTex);
    }
}

GpuShader* MainPass::GetOrCreateShader()
{
    if (!Context()) return nullptr;

#ifdef MYENGINE_PLATFORM_WINDOWS
    ShaderManager::Get().SetContext(Context());

    if (dynamic_cast<D3D11Context*>(Context()) != nullptr) {
        if (!m_MainShaderHandle) {
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(
                "src/Runtime/Renderer/Shaders/ShadowedMainPass.hlsl",
                "VSMain", "PSMain",
                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::ShadowedD3D11;
        }
        if ((!m_MainShaderHandle || !m_MainShaderHandle->shader) && m_ShaderMode == ShaderMode::ShadowedD3D11) {
            Logger::Warn("[MainPass] Shadowed D3D11 shader failed; fallback to legacy shader");
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(
                "src/Runtime/Renderer/Shaders/Mesh.hlsl",
                "VSMain", "PSMain",
                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::Legacy;
        }
    } else {
        if (!m_MainShaderHandle) {
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(
                "src/Runtime/Renderer/Shaders/Mesh.hlsl",
                "VSMain", "PSMain",
                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::Legacy;
        }
    }
#else
    if (!m_MainShaderHandle) {
        m_MainShaderHandle = std::make_shared<ShaderHandle>();
        m_MainShaderHandle->shader = Context()->CreateShader(
            k_MeshShaderSource, "VSMain", "PSMain",
            k_MeshVertexLayout, k_MeshVertexLayoutCount);
    }
#endif

    if (!m_MainShaderHandle) return nullptr;
    if (m_MainShaderVersion != m_MainShaderHandle->version) {
        m_MainShaderVersion = m_MainShaderHandle->version;
    }
    if (m_MainShaderHandle->shader && m_ShaderMode == ShaderMode::Unknown) {
        m_ShaderMode = ShaderMode::Legacy;
    }
    return m_MainShaderHandle->shader.get();
}

void MainPass::Execute(const Scene& scene, const Camera& camera)
{
    if (!Context()) return;
    GpuCommandList* cmd = Context()->GetGraphicsCommandList();
    if (!cmd) return;

    GpuShader* shader = GetOrCreateShader();
    if (!shader) return;

    Context()->BeginFrame(0.12f, 0.12f, 0.18f);

    const Mat4 viewProj = camera.GetViewProj();
    cmd->BindShader(shader);

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* mr = actor.GetComponent<MeshRendererComponent>();
        if (!mr || !mr->IsValid()) return;

        MeshAsset* mesh = mr->GetMesh().Get();
        MaterialAsset* mat = mr->GetMaterial().Get();
        if (!mesh || !mat) return;

        EnsureMeshUploaded(mesh);
        if (!mesh->GetVertexBuffer()) return;

        const Mat4 world = actor.GetWorldMatrix();
        const Mat4 mvp = world * viewProj;

        cmd->BindVertexBuffer(mesh->GetVertexBuffer());

        GpuTexture* baseColorTexture = nullptr;
        if (mat->HasTexture("BaseColorMap")) {
            TextureAsset* texAsset = mat->GetTexture("BaseColorMap").Get();
            if (texAsset) {
                EnsureTextureUploaded(texAsset);
                baseColorTexture = static_cast<GpuTexture*>(texAsset->GetGpuHandle());
            }
        }
        cmd->BindPSTexture(0, baseColorTexture);

        if (m_ShaderMode == ShaderMode::ShadowedD3D11) {
            GpuTexture* shadowTexture = m_ShadowMap ? m_ShadowMap : baseColorTexture;
            cmd->BindPSTexture(1, shadowTexture);

            ShadowedPerDrawConstants constants{};
            std::memcpy(constants.mvp, viewProj.Data(), sizeof(constants.mvp));
            std::memcpy(constants.world, world.Data(), sizeof(constants.world));
            std::memcpy(constants.lightViewProj, m_LightViewProj.Data(), sizeof(constants.lightViewProj));

            const Vec3 baseColor = mat->GetColor("BaseColor", Vec3::One());
            constants.baseColor[0] = baseColor.x;
            constants.baseColor[1] = baseColor.y;
            constants.baseColor[2] = baseColor.z;
            constants.baseColor[3] = 1.0f;

            constants.lightDirection[0] = m_LightDirection.x;
            constants.lightDirection[1] = m_LightDirection.y;
            constants.lightDirection[2] = m_LightDirection.z;
            constants.lightDirection[3] = 0.0f;

            if (mesh->GetIndexBuffer()) {
                cmd->BindIndexBuffer(mesh->GetIndexBuffer());
                for (const auto& sm : mesh->GetSubMeshes()) {
                    cmd->SetVSConstants(&constants, sizeof(constants));
                    cmd->DrawIndexed(sm.indexCount, sm.indexOffset,
                                     static_cast<uint32_t>(sm.vertexOffset));
                }
            } else {
                cmd->BindIndexBuffer(nullptr);
                for (const auto& sm : mesh->GetSubMeshes()) {
                    cmd->SetVSConstants(&constants, sizeof(constants));
                    cmd->Draw(sm.indexCount, sm.vertexOffset);
                }
            }
        } else {
            LegacyPerDrawConstants constants{};
            std::memcpy(constants.mvp, mvp.Data(), sizeof(constants.mvp));
            const Vec3 baseColor = mat->GetColor("BaseColor", Vec3::One());
            constants.baseColor[0] = baseColor.x;
            constants.baseColor[1] = baseColor.y;
            constants.baseColor[2] = baseColor.z;
            constants.baseColor[3] = 1.0f;

            if (mesh->GetIndexBuffer()) {
                cmd->BindIndexBuffer(mesh->GetIndexBuffer());
                for (const auto& sm : mesh->GetSubMeshes()) {
                    cmd->SetVSConstants(&constants, sizeof(constants));
                    cmd->DrawIndexed(sm.indexCount, sm.indexOffset,
                                     static_cast<uint32_t>(sm.vertexOffset));
                }
            } else {
                cmd->BindIndexBuffer(nullptr);
                for (const auto& sm : mesh->GetSubMeshes()) {
                    cmd->SetVSConstants(&constants, sizeof(constants));
                    cmd->Draw(sm.indexCount, sm.vertexOffset);
                }
            }
        }
    });

    if (m_PresentEnabled) {
        Context()->EndFrame();
    }
}
