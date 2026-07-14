#include "Renderer/GBufferPass.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/MeshShader.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"

#include <algorithm>
#include <cstring>

namespace {

struct GBufferPerDrawConstants {
    float viewProj[16];
    float world[16];
    float baseColor[4];
    float material[4];
    float emissive[4];
    float mapFlags[4];
    float boneMatrices[128][16];
    float skinInfo[4];
    float normalMatrix[16];
};

void FillColorConstants(float out[4], const MaterialAsset& material, const char* name, const Vec3& fallback,
                        float fallbackAlpha = 1.0f) {
    const MaterialParam color = material.GetParam(name, MaterialParam::FromColor(fallback, fallbackAlpha));
    out[0] = color.data[0];
    out[1] = color.data[1];
    out[2] = color.data[2];
    out[3] = color.data[3];
}

} // namespace

GBufferPass::GBufferPass(IRHIDevice* device) : RenderPass(device), m_ResourceCache(device) {
}

void GBufferPass::Resize(uint32_t width, uint32_t height) {
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    if (m_Width == width && m_Height == height)
        return;
    m_Width = width;
    m_Height = height;
    m_Albedo.reset();
    m_AlbedoRtv.reset();
    m_AlbedoSrv.reset();
    m_Normal.reset();
    m_NormalRtv.reset();
    m_NormalSrv.reset();
    m_Material.reset();
    m_MaterialRtv.reset();
    m_MaterialSrv.reset();
    m_Emissive.reset();
    m_EmissiveRtv.reset();
    m_EmissiveSrv.reset();
    m_GBufferState = RHIResourceState::Undefined;
}

bool GBufferPass::PrepareGraphResources() {
    return EnsureResources();
}

GBufferPass::GraphResources GBufferPass::GetGraphResources() const {
    GraphResources resources;
    resources.albedo = m_Albedo;
    resources.albedoRtv = m_AlbedoRtv;
    resources.albedoSrv = m_AlbedoSrv;
    resources.normal = m_Normal;
    resources.normalRtv = m_NormalRtv;
    resources.normalSrv = m_NormalSrv;
    resources.material = m_Material;
    resources.materialRtv = m_MaterialRtv;
    resources.materialSrv = m_MaterialSrv;
    resources.emissive = m_Emissive;
    resources.emissiveRtv = m_EmissiveRtv;
    resources.emissiveSrv = m_EmissiveSrv;
    resources.initialState = m_GBufferState;
    return resources;
}

void GBufferPass::MarkGraphResourcesShaderResource() {
    m_GBufferState = RHIResourceState::ShaderResource;
}

bool GBufferPass::EnsureResources() {
    if (!Device())
        return false;
    if (m_Albedo && m_AlbedoRtv && m_AlbedoSrv && m_Normal && m_NormalRtv && m_NormalSrv && m_Material &&
        m_MaterialRtv && m_MaterialSrv && m_Emissive && m_EmissiveRtv && m_EmissiveSrv)
        return true;

    auto createTarget = [&](const char* name, RHIFormat format, std::shared_ptr<GpuTexture>& texture,
                            std::shared_ptr<GpuTextureView>& rtv, std::shared_ptr<GpuTextureView>& srv) -> bool {
        RHITextureDesc desc;
        desc.width = m_Width;
        desc.height = m_Height;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.format = format;
        desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
        desc.debugName = name;
        texture = Device()->CreateTexture(desc);
        if (!texture) {
            Logger::Error("[GBufferPass] Failed to create texture: ", name);
            return false;
        }
        RHITextureViewDesc viewDesc;
        viewDesc.usage = RHIResourceUsage::RenderTarget;
        rtv = Device()->CreateTextureView(texture, viewDesc);
        if (!rtv) {
            Logger::Error("[GBufferPass] Failed to create render target view: ", name);
            return false;
        }
        RHITextureViewDesc srvDesc;
        srvDesc.usage = RHIResourceUsage::ShaderResource;
        srv = Device()->CreateTextureView(texture, srvDesc);
        if (!srv) {
            Logger::Error("[GBufferPass] Failed to create shader-resource view: ", name);
            return false;
        }
        return true;
    };

    return createTarget("GBufferAlbedo", RHIFormat::RGBA8UNorm, m_Albedo, m_AlbedoRtv, m_AlbedoSrv) &&
           createTarget("GBufferNormal", RHIFormat::RGBA16Float, m_Normal, m_NormalRtv, m_NormalSrv) &&
           createTarget("GBufferMaterial", RHIFormat::RGBA8UNorm, m_Material, m_MaterialRtv, m_MaterialSrv) &&
           createTarget("GBufferEmissive", RHIFormat::RGBA16Float, m_Emissive, m_EmissiveRtv, m_EmissiveSrv);
}

GpuShader* GBufferPass::GetOrCreateShader() {
    if (!Device())
        return nullptr;
    if (!m_ShaderHandle) {
        m_ShaderHandle =
            ShaderManager::Get().GetOrCreate(EngineShaders::kGBuffer, k_MeshVertexLayout, k_MeshVertexLayoutCount);
    }
    if (!m_ShaderHandle || !m_ShaderHandle->shader)
        return nullptr;
    if (m_ShaderVersion != m_ShaderHandle->version) {
        m_ShaderVersion = m_ShaderHandle->version;
        for (auto& pipeline : m_Pipelines)
            pipeline.reset();
    }
    return m_ShaderHandle->shader.get();
}

GpuGraphicsPipeline* GBufferPass::GetOrCreatePipeline(const MaterialAsset& material) {
    if (!Device() || !m_ShaderHandle || !m_ShaderHandle->shader)
        return nullptr;
    const size_t index = (material.IsTwoSided() ? 1u : 0u) | (material.IsWireframe() ? 2u : 0u);
    auto& pipeline = m_Pipelines[index];
    if (!pipeline) {
        GraphicsPipelineDesc desc;
        desc.shader = m_ShaderHandle->shader;
        desc.colorFormats = {
            RHIFormat::RGBA8UNorm,
            RHIFormat::RGBA16Float,
            RHIFormat::RGBA8UNorm,
            RHIFormat::RGBA16Float,
        };
        desc.depthFormat = RHIFormat::D24S8;
        desc.rasterizer.cullMode = material.IsTwoSided() ? RHICullMode::None : RHICullMode::Back;
        desc.rasterizer.fillMode = material.IsWireframe() ? RHIFillMode::Wireframe : RHIFillMode::Solid;
        desc.blend.attachments.resize(desc.colorFormats.size());
        for (auto& attachment : desc.blend.attachments) {
            attachment.blendEnable = false;
        }
        pipeline = Device()->CreateGraphicsPipeline(desc);
    }
    return pipeline.get();
}

void GBufferPass::Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera) {
    if (!Device())
        return;
    GpuShader* shader = GetOrCreateShader();
    if (!shader)
        return;

    m_ResourceCache.SetDevice(Device());
    m_ResourceCache.ResetFrameStats();
    m_ResourceCache.EnsureNamedBindingDefaults();

    const Mat4 viewProj = camera.GetViewProj();
    const SceneRenderCollection collection = m_Collector.Collect(scene, camera);

    for (const SceneRenderItem& item : collection.opaqueItems) {
        Actor* actor = item.actor;
        MeshAsset* mesh = item.mesh;
        const SubMesh* subMesh = item.subMesh;
        MaterialAsset* material = item.material;
        if (!actor || !mesh || !subMesh || !material || material->GetBlendMode() == BlendMode::Transparent) {
            continue;
        }

        m_ResourceCache.EnsureMeshUploaded(mesh);
        if (!mesh->GetVertexBuffer())
            continue;

        GpuGraphicsPipeline* pipeline = GetOrCreatePipeline(*material);
        if (!pipeline)
            continue;
        commands.SetGraphicsPipeline(pipeline);
        commands.BindVertexBuffer(mesh->GetVertexBuffer());

        const Mat4 world = actor->GetWorldMatrix();
        Mat4 normalMatrix = Mat4::Identity();
        if (Mat4Invert(world, normalMatrix)) {
            normalMatrix = normalMatrix.Transposed();
        }

        GBufferPerDrawConstants constants{};
        std::memcpy(constants.viewProj, viewProj.Data(), sizeof(constants.viewProj));
        std::memcpy(constants.world, world.Data(), sizeof(constants.world));
        std::memcpy(constants.normalMatrix, normalMatrix.Data(), sizeof(constants.normalMatrix));
        FillColorConstants(constants.baseColor, *material, "BaseColor", Vec3::One());
        constants.material[0] = std::clamp(material->GetFloat("Metallic", 0.0f), 0.0f, 1.0f);
        constants.material[1] = std::clamp(material->GetFloat("Roughness", 0.5f), 0.04f, 1.0f);
        constants.material[2] = (std::max)(0.0f, material->GetFloat("AmbientOcclusion", 1.0f));
        constants.material[3] = material->GetAlphaThreshold();
        const Vec3 emissive = material->GetColor("Emissive", Vec3::Zero());
        constants.emissive[0] = emissive.x;
        constants.emissive[1] = emissive.y;
        constants.emissive[2] = emissive.z;
        constants.emissive[3] = material->GetBlendMode() == BlendMode::AlphaTest ? 1.0f : 0.0f;

        auto bindings = Device()->CreateBindGroup(m_ShaderHandle->shader);
        if (bindings) {
            const char* textureSlots[] = {"BaseColorMap", "NormalMap", "MetallicRoughnessMap", "OcclusionMap",
                                          "EmissiveMap"};
            const char* textureNames[] = {"g_BaseColorMap", "g_NormalMap", "g_MetallicRoughnessMap", "g_OcclusionMap",
                                          "g_EmissiveMap"};
            const char* samplerNames[] = {"g_Sampler", "g_NormalSampler", "g_MetallicRoughnessSampler",
                                          "g_OcclusionSampler", "g_EmissiveSampler"};
            for (uint32_t i = 0; i < 5; ++i) {
                TextureAsset* texture = nullptr;
                GpuTexture* gpuTexture = nullptr;
                if (material->HasTexture(textureSlots[i])) {
                    texture = material->GetTexture(textureSlots[i]).Get();
                    if (texture) {
                        m_ResourceCache.EnsureTextureUploaded(texture);
                        gpuTexture = static_cast<GpuTexture*>(texture->GetGpuHandle());
                    }
                }
                if (i > 0) {
                    constants.mapFlags[i - 1] = gpuTexture ? 1.0f : 0.0f;
                }
                bindings->SetTexture(textureNames[i], m_ResourceCache.GetTextureView(gpuTexture));
                bindings->SetSampler(samplerNames[i], texture ? m_ResourceCache.GetSamplerForTexture(texture)
                                                              : m_ResourceCache.GetLinearSampler());
            }
            bindings->SetConstants("PerDraw", &constants, sizeof(constants));
            commands.SetBindGroup(0, bindings.get());
        }

        if (mesh->GetIndexBuffer()) {
            commands.BindIndexBuffer(mesh->GetIndexBuffer());
            commands.DrawIndexed(subMesh->indexCount, subMesh->indexOffset,
                                 static_cast<uint32_t>(subMesh->vertexOffset));
        } else {
            commands.BindIndexBuffer(nullptr);
            commands.Draw(subMesh->indexCount, subMesh->vertexOffset);
        }
    }
}
