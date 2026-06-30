#include "Renderer/ForwardRenderPasses.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/TextureAsset.h"
#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/MainPass.h"
#include "Renderer/MeshShader.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

struct LegacyPerDrawConstants {
    float mvp[16];
    float baseColor[4];
};

struct ShadowedPerDrawConstants {
    float viewProj[16];
    float world[16];
    float lightViewProj[16];
    float lightViewProjCascade[3][16];
    float cascadeSplits[4];
    float spotLightViewProj[16];
    float baseColor[4];
    float lightDirection[4];
    float lightColor[4];
    float cameraPosition[4];
    float material[4];
    float emissive[4];
    float mapFlags[4];
    float pointLightPositions[4][4];
    float pointLightColors[4][4];
    float spotLightPositions[4][4];
    float spotLightDirections[4][4];
    float spotLightColors[4][4];
    float spotLightParams[4][4];
    float lightInfo[4];
    float pointShadowPosition[4];
    float shadowInfo[4];
    float shadowIntensity[4];
    float postProcess[4];
    float postProcess2[4];
    float instanceWorld[64][16];
    float instanceNormal[64][16];
    float drawInfo[4];
    float boneMatrices[128][16];
    float skinInfo[4];
    float iblInfo[4];
    float normalMatrix[16];
    float cameraForward[4];
};

constexpr uint32_t kMainTextureSlotCount = 9;

void FillColorConstants(float out[4], const MaterialAsset& material,
                        const char* name, const Vec3& fallback,
                        float fallbackAlpha = 1.0f)
{
    const MaterialParam color = material.GetParam(
        name, MaterialParam::FromColor(fallback, fallbackAlpha));
    out[0] = color.data[0];
    out[1] = color.data[1];
    out[2] = color.data[2];
    out[3] = color.data[3];
}

} // namespace

class ForwardDrawExecutor {
public:
static void Draw(MainPass& mainPass,
                 GpuCommandList& commands,
                 const Camera& camera,
                 const std::vector<SceneRenderItem>& items,
                 const ForwardRenderContext& context,
                 bool allowInstancing)
{
    if (!mainPass.Device() || !context.sceneLights || !context.postProcess) return;
    GpuShader* shader = mainPass.GetOrCreateShader();
    if (!shader) return;

    const Mat4 viewProj = camera.GetViewProj();
    const SceneLightData& sceneLights = *context.sceneLights;
    const ScenePostProcessData& postProcess = *context.postProcess;
    const bool backendAllowsInstancing =
        allowInstancing && mainPass.Device()->GetBackend() != RHIBackend::Metal;

    for (size_t itemIndex = 0; itemIndex < items.size();) {
        const SceneRenderItem& item = items[itemIndex];
        size_t instanceCount = 1;
        if (backendAllowsInstancing &&
            mainPass.m_ShaderMode == MainPass::ShaderMode::ShadowedPbr &&
            item.skin == nullptr &&
            item.material->GetBlendMode() != BlendMode::Transparent) {
            while (instanceCount < 64 &&
                   itemIndex + instanceCount < items.size()) {
                const SceneRenderItem& candidate = items[itemIndex + instanceCount];
                if (candidate.mesh != item.mesh ||
                    candidate.subMeshIndex != item.subMeshIndex ||
                    candidate.material != item.material ||
                    candidate.skin != nullptr ||
                    candidate.material->GetBlendMode() == BlendMode::Transparent) {
                    break;
                }
                ++instanceCount;
            }
        }

        Actor& actor = *item.actor;
        MeshAsset* mesh = item.mesh;
        const SubMesh* subMesh = item.subMesh;
        MaterialAsset* mat = item.material;
        if (!subMesh || !mesh || !mat) {
            itemIndex += instanceCount;
            continue;
        }

        if (mat->GetShaderAsset().IsValid()) {
            auto custom = ShaderManager::Get().GetOrCreate(
                mat->GetShaderAsset()->GetPath(), k_MeshVertexLayout, k_MeshVertexLayoutCount);
            if (custom && custom->shader) mat->SetShader(custom->shader);
        }

        mainPass.EnsureMeshUploaded(mesh);
        if (!mesh->GetVertexBuffer()) {
            itemIndex += instanceCount;
            continue;
        }

        GpuShader* drawShader = mat->HasShader() ? mat->GetShader() : shader;
        if (drawShader) {
            if (drawShader == shader) {
                auto* pipeline = mainPass.GetOrCreateMainPipeline(
                    mat->GetBlendMode() == BlendMode::Transparent,
                    mat->IsTwoSided(), mat->IsWireframe());
                if (!pipeline) {
                    itemIndex += instanceCount;
                    continue;
                }
                commands.SetGraphicsPipeline(pipeline);
            } else {
                auto* pipeline = mainPass.GetOrCreateMaterialPipeline(*mat);
                if (!pipeline) {
                    itemIndex += instanceCount;
                    continue;
                }
                commands.SetGraphicsPipeline(pipeline);
            }
        }

        const Mat4 world = actor.GetWorldMatrix();
        const Mat4 mvp = world * viewProj;
        Mat4 normalMatrix = Mat4::Identity();
        if (Mat4Invert(world, normalMatrix)) {
            normalMatrix = normalMatrix.Transposed();
        }

        commands.BindVertexBuffer(mesh->GetVertexBuffer());

        GpuTexture* baseColorTexture = nullptr;
        TextureAsset* baseColorAsset = nullptr;
        std::array<GpuTexture*, kMainTextureSlotCount> namedTextures{};
        std::array<TextureAsset*, kMainTextureSlotCount> namedTextureAssets{};
        if (mat->HasTexture("BaseColorMap")) {
            TextureAsset* texAsset = mat->GetTexture("BaseColorMap").Get();
            if (texAsset) {
                mainPass.EnsureTextureUploaded(texAsset);
                baseColorAsset = texAsset;
                baseColorTexture = static_cast<GpuTexture*>(texAsset->GetGpuHandle());
            }
        }
        namedTextures[0] = baseColorTexture;
        namedTextureAssets[0] = baseColorAsset;

        if (mainPass.m_ShaderMode == MainPass::ShaderMode::ShadowedPbr) {
            namedTextures[1] = mainPass.m_ShadowMap ? mainPass.m_ShadowMap : baseColorTexture;

            const char* mapSlots[] = {
                "NormalMap", "MetallicRoughnessMap", "OcclusionMap", "EmissiveMap"
            };
            float mapFlags[4] = {};
            for (uint32_t mapIndex = 0; mapIndex < 4; ++mapIndex) {
                GpuTexture* gpuTexture = nullptr;
                if (mat->HasTexture(mapSlots[mapIndex])) {
                    TextureAsset* texture = mat->GetTexture(mapSlots[mapIndex]).Get();
                    if (texture) {
                        mainPass.EnsureTextureUploaded(texture);
                        gpuTexture = static_cast<GpuTexture*>(texture->GetGpuHandle());
                        mapFlags[mapIndex] = gpuTexture ? 1.0f : 0.0f;
                        namedTextureAssets[2 + mapIndex] = texture;
                    }
                }
                namedTextures[2 + mapIndex] = gpuTexture;
            }
            namedTextures[6] = mainPass.m_SpotShadowMap;
            namedTextures[7] = mainPass.m_PointShadowMap;

            GpuTexture* iblTexture = nullptr;
            float iblEnabled = 0.0f;
            if (mainPass.m_EnvironmentCubemap && mainPass.m_EnvironmentCubemap->IsCube()) {
                iblTexture = mainPass.m_EnvironmentCubemap;
                iblEnabled = 1.0f;
            } else if (mat->HasTexture("IBLCubemap")) {
                TextureAsset* iblAsset = mat->GetTexture("IBLCubemap").Get();
                if (iblAsset) {
                    mainPass.EnsureTextureUploaded(iblAsset);
                    iblTexture = static_cast<GpuTexture*>(iblAsset->GetGpuHandle());
                    iblEnabled = (iblTexture && iblTexture->IsCube()) ? 1.0f : 0.0f;
                    if (iblEnabled > 0.5f) namedTextureAssets[8] = iblAsset;
                }
            }
            namedTextures[8] = iblEnabled > 0.5f ? iblTexture : nullptr;

            ShadowedPerDrawConstants constants{};
            std::memcpy(constants.viewProj, viewProj.Data(), sizeof(constants.viewProj));
            std::memcpy(constants.world, world.Data(), sizeof(constants.world));
            std::memcpy(constants.lightViewProj, mainPass.m_LightViewProj.Data(),
                        sizeof(constants.lightViewProj));
            for (uint32_t cascade = 0; cascade < 3; ++cascade) {
                std::memcpy(constants.lightViewProjCascade[cascade],
                            mainPass.m_LightViewProjCascade[cascade].Data(),
                            sizeof(constants.lightViewProjCascade[cascade]));
            }
            std::memcpy(constants.cascadeSplits, mainPass.m_CascadeSplits,
                        sizeof(constants.cascadeSplits));
            std::memcpy(constants.spotLightViewProj, mainPass.m_SpotLightViewProj.Data(),
                        sizeof(constants.spotLightViewProj));

            FillColorConstants(constants.baseColor, *mat, "BaseColor", Vec3::One());

            constants.lightDirection[0] = sceneLights.direction.x;
            constants.lightDirection[1] = sceneLights.direction.y;
            constants.lightDirection[2] = sceneLights.direction.z;
            constants.lightDirection[3] = sceneLights.directionalIntensity;
            constants.lightColor[0] = sceneLights.color.x;
            constants.lightColor[1] = sceneLights.color.y;
            constants.lightColor[2] = sceneLights.color.z;
            constants.lightColor[3] = 1.0f;

            const Vec3 cameraPosition = camera.GetPosition();
            constants.cameraPosition[0] = cameraPosition.x;
            constants.cameraPosition[1] = cameraPosition.y;
            constants.cameraPosition[2] = cameraPosition.z;
            constants.cameraPosition[3] = 1.0f;
            const Vec3 cameraForward = camera.GetForward();
            constants.cameraForward[0] = cameraForward.x;
            constants.cameraForward[1] = cameraForward.y;
            constants.cameraForward[2] = cameraForward.z;
            constants.cameraForward[3] = 0.0f;

            constants.material[0] = std::clamp(mat->GetFloat("Metallic", 0.0f), 0.0f, 1.0f);
            constants.material[1] = std::clamp(mat->GetFloat("Roughness", 0.5f), 0.04f, 1.0f);
            constants.material[2] = (std::max)(0.0f, mat->GetFloat("AmbientOcclusion", 1.0f));
            constants.material[3] = mat->GetAlphaThreshold();

            const Vec3 emissive = mat->GetColor("Emissive", Vec3::Zero());
            constants.emissive[0] = emissive.x;
            constants.emissive[1] = emissive.y;
            constants.emissive[2] = emissive.z;
            constants.emissive[3] =
                mat->GetBlendMode() == BlendMode::AlphaTest ? 1.0f : 0.0f;
            std::memcpy(constants.mapFlags, mapFlags, sizeof(mapFlags));

            const size_t pointCount = (std::min)(sceneLights.pointLights.size(), size_t{4});
            for (size_t i = 0; i < pointCount; ++i) {
                const ScenePointLight& point = sceneLights.pointLights[i];
                constants.pointLightPositions[i][0] = point.position.x;
                constants.pointLightPositions[i][1] = point.position.y;
                constants.pointLightPositions[i][2] = point.position.z;
                constants.pointLightPositions[i][3] = point.range;
                constants.pointLightColors[i][0] = point.color.x;
                constants.pointLightColors[i][1] = point.color.y;
                constants.pointLightColors[i][2] = point.color.z;
                constants.pointLightColors[i][3] = point.intensity;
            }
            const size_t spotCount = (std::min)(sceneLights.spotLights.size(), size_t{4});
            for (size_t i = 0; i < spotCount; ++i) {
                const SceneSpotLight& spot = sceneLights.spotLights[i];
                constants.spotLightPositions[i][0] = spot.position.x;
                constants.spotLightPositions[i][1] = spot.position.y;
                constants.spotLightPositions[i][2] = spot.position.z;
                constants.spotLightPositions[i][3] = spot.range;
                constants.spotLightDirections[i][0] = spot.direction.x;
                constants.spotLightDirections[i][1] = spot.direction.y;
                constants.spotLightDirections[i][2] = spot.direction.z;
                constants.spotLightDirections[i][3] = 0.0f;
                constants.spotLightColors[i][0] = spot.color.x;
                constants.spotLightColors[i][1] = spot.color.y;
                constants.spotLightColors[i][2] = spot.color.z;
                constants.spotLightColors[i][3] = spot.intensity;
                constants.spotLightParams[i][0] = spot.innerConeCos;
                constants.spotLightParams[i][1] = spot.outerConeCos;
            }

            constants.lightInfo[0] = static_cast<float>(pointCount);
            constants.lightInfo[1] = sceneLights.ambientIntensity;
            constants.lightInfo[2] = static_cast<float>(spotCount);
            constants.pointShadowPosition[0] = mainPass.m_PointShadowPosition.x;
            constants.pointShadowPosition[1] = mainPass.m_PointShadowPosition.y;
            constants.pointShadowPosition[2] = mainPass.m_PointShadowPosition.z;
            constants.pointShadowPosition[3] = mainPass.m_PointShadowRange;
            constants.shadowInfo[0] =
                (mainPass.m_DirectionalShadowEnabled && mainPass.m_ShadowMap) ? 1.0f : 0.0f;
            constants.shadowInfo[1] = static_cast<float>(mainPass.m_SpotShadowIndex);
            constants.shadowInfo[2] = static_cast<float>(mainPass.m_PointShadowIndex);
            constants.shadowInfo[3] = 0.05f;
            constants.shadowIntensity[0] = sceneLights.directionalShadowIntensity;
            if (mainPass.m_SpotShadowIndex >= 0 &&
                static_cast<size_t>(mainPass.m_SpotShadowIndex) < sceneLights.spotLights.size()) {
                constants.shadowIntensity[1] =
                    sceneLights.spotLights[static_cast<size_t>(
                        mainPass.m_SpotShadowIndex)].shadowIntensity;
            } else {
                constants.shadowIntensity[1] = 1.0f;
            }
            if (mainPass.m_PointShadowIndex >= 0 &&
                static_cast<size_t>(mainPass.m_PointShadowIndex) < sceneLights.pointLights.size()) {
                constants.shadowIntensity[2] =
                    sceneLights.pointLights[static_cast<size_t>(
                        mainPass.m_PointShadowIndex)].shadowIntensity;
            } else {
                constants.shadowIntensity[2] = 1.0f;
            }
            constants.shadowIntensity[3] = 1.0f;
            constants.postProcess[0] = postProcess.exposure;
            constants.postProcess[1] = postProcess.gamma;
            constants.postProcess[2] = postProcess.toneMapping;
            constants.postProcess[3] = postProcess.vignette;
            constants.postProcess2[0] = postProcess.saturation;
            constants.postProcess2[1] = postProcess.contrast;
            constants.postProcess2[2] = postProcess.antiAliasingStrength;
            constants.postProcess2[3] = mainPass.m_HdrPassthrough ? -1.0f : 0.0f;
            constants.drawInfo[0] = instanceCount > 1 ? 1.0f : 0.0f;
            constants.drawInfo[1] = constants.shadowInfo[0];
            for (size_t instance = 0; instance < instanceCount; ++instance) {
                const Mat4 instanceMatrix =
                    items[itemIndex + instance].actor->GetWorldMatrix();
                std::memcpy(constants.instanceWorld[instance], instanceMatrix.Data(),
                            sizeof(constants.instanceWorld[instance]));
                Mat4 instanceNormalMatrix = Mat4::Identity();
                if (Mat4Invert(instanceMatrix, instanceNormalMatrix)) {
                    instanceNormalMatrix = instanceNormalMatrix.Transposed();
                }
                std::memcpy(constants.instanceNormal[instance], instanceNormalMatrix.Data(),
                            sizeof(constants.instanceNormal[instance]));
            }
            if (item.skin && item.skin->UsesGpuSkinning()) {
                const auto& matrices = item.skin->GetSkinMatrices();
                const size_t boneCount = (std::min)(matrices.size(), size_t{128});
                constants.skinInfo[0] = static_cast<float>(boneCount);
                for (size_t bone = 0; bone < boneCount; ++bone) {
                    std::memcpy(constants.boneMatrices[bone], matrices[bone].Data(),
                                sizeof(constants.boneMatrices[bone]));
                }
            }
            constants.iblInfo[0] = iblEnabled;
            constants.iblInfo[1] = mat->GetFloat("IBLIntensity", 1.0f);
            std::memcpy(constants.normalMatrix, normalMatrix.Data(),
                        sizeof(constants.normalMatrix));

            mainPass.EnsureNamedBindingDefaults();
            auto bindings = mainPass.GetOrCreateMaterialBindGroup(
                drawShader, *mat, true, namedTextures, namedTextureAssets);
            if (bindings) {
                bindings->SetConstants("PerDraw", &constants, sizeof(constants));
                if (!mainPass.m_LoggedEnvironmentState) {
                    Logger::Info("[MainPass] Environment IBL state: cube=",
                                 (mainPass.m_EnvironmentCubemap &&
                                  mainPass.m_EnvironmentCubemap->IsCube()) ? 1 : 0,
                                 " shView=", mainPass.m_EnvironmentSH2Buffer ? 1 : 0,
                                 " iblEnabled=", iblEnabled > 0.5f ? 1 : 0);
                    mainPass.m_LoggedEnvironmentState = true;
                }
                commands.SetBindGroup(0, bindings.get());
            }

            if (mesh->GetIndexBuffer()) {
                commands.BindIndexBuffer(mesh->GetIndexBuffer());
                commands.DrawIndexedInstanced(
                    subMesh->indexCount, static_cast<uint32_t>(instanceCount),
                    subMesh->indexOffset, static_cast<uint32_t>(subMesh->vertexOffset));
                ++mainPass.m_LastStats.drawCalls;
            } else {
                commands.BindIndexBuffer(nullptr);
                commands.DrawInstanced(
                    subMesh->indexCount, static_cast<uint32_t>(instanceCount),
                    subMesh->vertexOffset);
                ++mainPass.m_LastStats.drawCalls;
            }
        } else {
            LegacyPerDrawConstants constants{};
            std::memcpy(constants.mvp, mvp.Data(), sizeof(constants.mvp));
            FillColorConstants(constants.baseColor, *mat, "BaseColor", Vec3::One());
            mainPass.EnsureNamedBindingDefaults();
            auto bindings = mainPass.GetOrCreateMaterialBindGroup(
                drawShader, *mat, false, namedTextures, namedTextureAssets);
            if (bindings) {
                if (!bindings->SetConstants("PerDraw", &constants, sizeof(constants))) {
                    Logger::Error("[MainPass] Failed to bind PerDraw constants");
                }
                commands.SetBindGroup(0, bindings.get());
            }

            if (mesh->GetIndexBuffer()) {
                commands.BindIndexBuffer(mesh->GetIndexBuffer());
                commands.DrawIndexed(subMesh->indexCount, subMesh->indexOffset,
                                     static_cast<uint32_t>(subMesh->vertexOffset));
                ++mainPass.m_LastStats.drawCalls;
            } else {
                commands.BindIndexBuffer(nullptr);
                commands.Draw(subMesh->indexCount, subMesh->vertexOffset);
                ++mainPass.m_LastStats.drawCalls;
            }
        }

        itemIndex += instanceCount;
    }
}
};

SkyPass::SkyPass(MainPass& mainPass)
    : m_MainPass(mainPass)
{}

void SkyPass::Execute(GpuCommandList& commands, const Camera& camera)
{
    m_MainPass.RenderSky(camera, commands);
}

ForwardOpaquePass::ForwardOpaquePass(MainPass& mainPass)
    : m_MainPass(mainPass)
{}

void ForwardOpaquePass::Execute(
    GpuCommandList& commands,
    const Scene&,
    const Camera& camera,
    const std::vector<SceneRenderItem>& items,
    const ForwardRenderContext& context)
{
    ForwardDrawExecutor::Draw(m_MainPass, commands, camera, items, context, true);
}

ForwardTransparentPass::ForwardTransparentPass(MainPass& mainPass)
    : m_MainPass(mainPass)
{}

void ForwardTransparentPass::Execute(
    GpuCommandList& commands,
    const Scene&,
    const Camera& camera,
    const std::vector<SceneRenderItem>& items,
    const ForwardRenderContext& context)
{
    ForwardDrawExecutor::Draw(m_MainPass, commands, camera, items, context, false);
}
