#include "Renderer/MainPass.h"

#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Renderer/MeshShader.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"

#include <cstring>

#ifdef MYENGINE_PLATFORM_WINDOWS
#include "Renderer/D3D11Context.h"
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

#ifdef MYENGINE_PLATFORM_WINDOWS
constexpr const char* k_ShadowedMainPassHLSL = R"HLSL(

cbuffer PerDraw : register(b0)
{
    row_major float4x4 g_MVP;
    row_major float4x4 g_World;
    row_major float4x4 g_LightViewProj;
    float4 g_BaseColor;
    float4 g_LightDirection;
};

Texture2D    g_BaseColorMap : register(t0);
SamplerState g_Sampler      : register(s0);
Texture2D    g_ShadowMap    : register(t1);
SamplerState g_ShadowSampler : register(s1);

struct VSIn
{
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float3 tangent : TANGENT;
    float2 uv      : TEXCOORD0;
};

struct VSOut
{
    float4 pos      : SV_POSITION;
    float3 normalW  : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 lightPos : TEXCOORD1;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    float4 worldPos = mul(float4(v.pos, 1.0f), g_World);
    o.pos      = mul(worldPos, g_MVP);
    o.lightPos = mul(worldPos, g_LightViewProj);
    o.normalW  = normalize(mul(float4(v.normal, 0.0f), g_World).xyz);
    o.uv       = v.uv;
    return o;
}

float4 PSMain(VSOut p) : SV_TARGET
{
    float4 texColor = g_BaseColorMap.Sample(g_Sampler, p.uv);
    float3 L = normalize(-g_LightDirection.xyz);
    float3 N = normalize(p.normalW);
    float NdotL = saturate(dot(N, L));
    float diffuse = 0.2f + 0.8f * NdotL;

    float shadow = 1.0f;
    float3 proj = p.lightPos.xyz / max(p.lightPos.w, 1e-5f);
    float2 suv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    if (suv.x >= 0.0f && suv.x <= 1.0f &&
        suv.y >= 0.0f && suv.y <= 1.0f &&
        proj.z >= 0.0f && proj.z <= 1.0f) {
        const float bias = max(0.0020f, 0.0100f * (1.0f - NdotL));
        float shadowDepth = g_ShadowMap.Sample(g_ShadowSampler, suv).r;
        shadow = ((proj.z - bias) <= shadowDepth) ? 1.0f : 0.35f;
    }

    float3 lit = texColor.rgb * g_BaseColor.rgb * diffuse * shadow;
    return float4(lit, texColor.a * g_BaseColor.a);
}

)HLSL";
#endif

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
    if (m_MainShader) return m_MainShader.get();
    if (!Context()) return nullptr;

#ifdef MYENGINE_PLATFORM_WINDOWS
    if (dynamic_cast<D3D11Context*>(Context()) != nullptr) {
        m_MainShader = Context()->CreateShader(
            k_ShadowedMainPassHLSL,
            "VSMain", "PSMain",
            k_MeshVertexLayout, k_MeshVertexLayoutCount);
        if (m_MainShader) {
            m_ShaderMode = ShaderMode::ShadowedD3D11;
            return m_MainShader.get();
        }
        Logger::Warn("[MainPass] Shadowed D3D11 shader failed; fallback to legacy shader");
    }
#endif

    m_MainShader = Context()->CreateShader(
        k_MeshShaderSource, "VSMain", "PSMain",
        k_MeshVertexLayout, k_MeshVertexLayoutCount);
    if (m_MainShader) {
        m_ShaderMode = ShaderMode::Legacy;
    }
    return m_MainShader.get();
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
            std::memcpy(constants.mvp, mvp.Data(), sizeof(constants.mvp));
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
