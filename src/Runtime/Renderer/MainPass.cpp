#include "Renderer/MainPass.h"

#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/TextureAsset.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Core/Logger.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/ForwardRenderPasses.h"
#include "Renderer/MeshShader.h"
#include "Renderer/SceneLighting.h"
#include "Renderer/ShaderManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#ifdef MYENGINE_PLATFORM_WINDOWS
#endif

namespace {

struct SkyConstants {
    float forward[4];
    float right[4];
    float up[4];
    float sunDirection[4];
    float parameters[4];
    float skyTint[4];
    float horizonTint[4];
    float groundTint[4];
};

constexpr uint32_t kMainTextureSlotCount = 9;
const char* kMainTextureNames[kMainTextureSlotCount] = {"g_BaseColorMap",         "g_ShadowMap",      "g_NormalMap",
                                                        "g_MetallicRoughnessMap", "g_OcclusionMap",   "g_EmissiveMap",
                                                        "g_SpotShadowMap",        "g_PointShadowMap", "g_IBLCubemap"};
const char* kMainSamplerNames[kMainTextureSlotCount] = {
    "g_Sampler",          "g_ShadowSampler",   "g_NormalSampler",     "g_MetallicRoughnessSampler",
    "g_OcclusionSampler", "g_EmissiveSampler", "g_SpotShadowSampler", "g_PointShadowSampler",
    "g_IBLSampler"};

void AppendPointer(std::string& out, const void* ptr) {
    out += std::to_string(reinterpret_cast<uintptr_t>(ptr));
}

} // namespace

MainPass::MainPass(IRHIDevice* device)
    : RenderPass(device), m_ResourceCache(device), m_SkyPass(std::make_unique<SkyPass>(*this)),
      m_ForwardOpaquePass(std::make_unique<ForwardOpaquePass>(*this)),
      m_ForwardTransparentPass(std::make_unique<ForwardTransparentPass>(*this)) {
}

MainPass::~MainPass() = default;

void MainPass::Resize(uint32_t, uint32_t) {
    // Viewport is controlled by SceneRenderLayer through the active command list.
}

void MainPass::SetShadowInput(const Mat4& lightViewProj, const Vec3& lightDirection, bool directionalShadowEnabled,
                              GpuTexture* shadowMap, const Mat4& spotLightViewProj, int spotShadowIndex,
                              GpuTexture* spotShadowMap, const Vec3& pointShadowPosition, float pointShadowRange,
                              int pointShadowIndex, GpuTexture* pointShadowMap, const Mat4* cascadeViewProj,
                              uint32_t cascadeCount, const float* cascadeSplits) {
    m_LightViewProj = lightViewProj;
    m_LightDirection = lightDirection;
    m_DirectionalShadowEnabled = directionalShadowEnabled;
    m_ShadowMap = shadowMap;
    m_SpotLightViewProj = spotLightViewProj;
    m_SpotShadowIndex = spotShadowIndex;
    m_SpotShadowMap = spotShadowMap;
    m_PointShadowPosition = pointShadowPosition;
    m_PointShadowRange = pointShadowRange;
    m_PointShadowIndex = pointShadowIndex;
    m_PointShadowMap = pointShadowMap;
    for (uint32_t i = 0; i < 3; ++i) {
        m_LightViewProjCascade[i] = (cascadeViewProj && i < cascadeCount) ? cascadeViewProj[i] : Mat4::Identity();
    }
    if (cascadeSplits) {
        std::memcpy(m_CascadeSplits, cascadeSplits, sizeof(m_CascadeSplits));
    } else {
        std::memset(m_CascadeSplits, 0, sizeof(m_CascadeSplits));
    }
}

void MainPass::SetCascadeShadowInput(const Mat4* cascadeViewProj, uint32_t cascadeCount, const float* cascadeSplits) {
    const uint32_t count = (std::min)(cascadeCount, 3u);
    for (uint32_t i = 0; i < 3; ++i) {
        m_LightViewProjCascade[i] = (cascadeViewProj && i < count) ? cascadeViewProj[i] : Mat4::Identity();
    }
    if (cascadeSplits) {
        for (uint32_t i = 0; i < 4; ++i) {
            m_CascadeSplits[i] = cascadeSplits[i];
        }
    } else {
        std::memset(m_CascadeSplits, 0, sizeof(m_CascadeSplits));
    }
}

void MainPass::SetEnvironmentInput(GpuTexture* environmentCubemap, std::shared_ptr<GpuBufferView> sh2Buffer,
                                   const float* sh2Coefficients) {
    m_EnvironmentCubemap = environmentCubemap;
    m_EnvironmentSH2Buffer = sh2Buffer;
    if (sh2Coefficients) {
        std::memcpy(m_EnvironmentSH2, sh2Coefficients, sizeof(m_EnvironmentSH2));
    } else {
        std::memset(m_EnvironmentSH2, 0, sizeof(m_EnvironmentSH2));
    }
}

void MainPass::SetEnvironmentSettings(const SceneEnvironmentData& environment) {
    m_EnvironmentSettings = environment;
}

void MainPass::SetProbeInput(std::shared_ptr<GpuTextureView> reflectionAtlas,
                             std::shared_ptr<GpuBufferView> reflectionMetadata,
                             std::shared_ptr<GpuBufferView> shVolumeMetadata,
                             std::shared_ptr<GpuBufferView> shCoefficients, uint32_t reflectionCount,
                             uint32_t shVolumeCount, uint32_t reflectionMipCount) {
    const bool changed = m_ProbeReflectionAtlas != reflectionAtlas || m_ProbeReflectionMetadata != reflectionMetadata ||
                         m_ProbeSHVolumeMetadata != shVolumeMetadata || m_ProbeSHCoefficients != shCoefficients ||
                         m_ProbeReflectionCount != reflectionCount || m_ProbeSHVolumeCount != shVolumeCount ||
                         m_ProbeReflectionMipCount != reflectionMipCount;
    m_ProbeReflectionAtlas = std::move(reflectionAtlas);
    m_ProbeReflectionMetadata = std::move(reflectionMetadata);
    m_ProbeSHVolumeMetadata = std::move(shVolumeMetadata);
    m_ProbeSHCoefficients = std::move(shCoefficients);
    m_ProbeReflectionCount = reflectionCount;
    m_ProbeSHVolumeCount = shVolumeCount;
    m_ProbeReflectionMipCount = reflectionMipCount;
    if (changed)
        m_MaterialBindGroups.clear();
}

void MainPass::SetSunDirection(const Vec3& direction) {
    if (direction.LengthSq() < 1e-8f)
        return;
    m_SunDirection = direction.Normalized();
}

void MainPass::EnsureMeshUploaded(MeshAsset* mesh) {
    m_ResourceCache.EnsureMeshUploaded(mesh);
}

void MainPass::EnsureTextureUploaded(TextureAsset* tex) {
    m_ResourceCache.EnsureTextureUploaded(tex);
}

void MainPass::EnsureNamedBindingDefaults() {
    m_ResourceCache.EnsureNamedBindingDefaults();
}

std::shared_ptr<GpuTextureView> MainPass::GetTextureView(GpuTexture* texture) {
    return m_ResourceCache.GetTextureView(texture);
}

std::shared_ptr<GpuSampler> MainPass::GetSamplerForTexture(TextureAsset* texture) {
    return m_ResourceCache.GetSamplerForTexture(texture);
}

bool MainPass::CanReuseMaterialBindGroups() const {
    if (!Device())
        return false;
    const RHIBackend backend = Device()->GetBackend();
    return backend == RHIBackend::D3D11 || backend == RHIBackend::D3D12 || backend == RHIBackend::Metal ||
           backend == RHIBackend::Unknown;
}

std::shared_ptr<GpuBindGroup>
MainPass::GetOrCreateMaterialBindGroup(GpuShader* shader, const MaterialAsset& material, bool shadowedPbr,
                                       const std::array<GpuTexture*, 9>& textures,
                                       const std::array<TextureAsset*, 9>& textureAssets) {
    if (!Device() || !shader)
        return nullptr;
    EnsureNamedBindingDefaults();

    std::string signature;
    signature.reserve(256);
    signature += shadowedPbr ? "pbr:" : "legacy:";
    AppendPointer(signature, shader);
    signature += ':';
    AppendPointer(signature, &material);
    signature += ':';
    AppendPointer(signature, m_EnvironmentSH2Buffer.get());
    signature += ':';
    AppendPointer(signature, m_ProbeReflectionAtlas.get());
    signature += ':';
    AppendPointer(signature, m_ProbeReflectionMetadata.get());
    signature += ':';
    AppendPointer(signature, m_ProbeSHVolumeMetadata.get());
    signature += ':';
    AppendPointer(signature, m_ProbeSHCoefficients.get());
    signature += ':';
    auto linearSampler = m_ResourceCache.GetLinearSampler();
    auto shadowSampler = m_ResourceCache.GetShadowSampler();
    AppendPointer(signature, linearSampler.get());
    signature += ':';
    AppendPointer(signature, shadowSampler.get());
    const uint32_t slotCount = shadowedPbr ? kMainTextureSlotCount : 1u;
    for (uint32_t slot = 0; slot < slotCount; ++slot) {
        signature += '|';
        AppendPointer(signature, textures[slot]);
        if (slot == 1 || slot == 6 || slot == 7) {
            signature += ":shadow";
        } else if (textureAssets[slot]) {
            MaterialResourceCache::AppendSamplerDesc(
                signature, MaterialResourceCache::SamplerDescForTexture(*textureAssets[slot]));
        } else {
            signature += ":linear";
        }
    }

    auto createBindGroup = [&]() -> std::shared_ptr<GpuBindGroup> {
        ++m_LastStats.bindGroupCreates;
        auto bindings = Device()->CreateBindGroup(m_MainShaderHandle ? m_MainShaderHandle->shader : nullptr);
        if (!bindings)
            return nullptr;
        for (uint32_t slot = 0; slot < slotCount; ++slot) {
            bindings->SetTexture(kMainTextureNames[slot], GetTextureView(textures[slot]));
            std::shared_ptr<GpuSampler> sampler = linearSampler;
            if (slot == 1 || slot == 6 || slot == 7) {
                sampler = shadowSampler;
            } else if (textureAssets[slot]) {
                sampler = GetSamplerForTexture(textureAssets[slot]);
            }
            bindings->SetSampler(kMainSamplerNames[slot], sampler);
        }
        if (shadowedPbr) {
            if (!bindings->SetBuffer("g_EnvironmentSH2", m_EnvironmentSH2Buffer)) {
                if (!m_LoggedEnvironmentSHBindingFailure) {
                    Logger::Error("[MainPass] Failed to bind g_EnvironmentSH2: shaderMode=",
                                  static_cast<int>(m_ShaderMode), " environmentView=", m_EnvironmentSH2Buffer ? 1 : 0);
                    m_LoggedEnvironmentSHBindingFailure = true;
                }
            }
            if (m_ProbeReflectionAtlas)
                bindings->SetTexture("g_LocalReflectionProbes", m_ProbeReflectionAtlas);
            if (m_ProbeReflectionMetadata)
                bindings->SetBuffer("g_LocalReflectionProbeData", m_ProbeReflectionMetadata);
            if (m_ProbeSHVolumeMetadata)
                bindings->SetBuffer("g_LocalSHProbeVolumes", m_ProbeSHVolumeMetadata);
            if (m_ProbeSHCoefficients)
                bindings->SetBuffer("g_LocalSHCoefficients", m_ProbeSHCoefficients);
        }
        return bindings;
    };

    if (!CanReuseMaterialBindGroups()) {
        return createBindGroup();
    }

    auto& entry = m_MaterialBindGroups[&material];
    if (!entry.bindGroup || entry.signature != signature) {
        entry.bindGroup = createBindGroup();
        entry.signature = signature;
    }
    return entry.bindGroup;
}

GpuShader* MainPass::GetOrCreateShader() {
    if (!Device())
        return nullptr;

#ifdef MYENGINE_PLATFORM_WINDOWS
    const bool supportsWindowsPbr = Device()->GetBackend() == RHIBackend::D3D11 ||
                                    Device()->GetBackend() == RHIBackend::D3D12 ||
                                    Device()->GetBackend() == RHIBackend::Vulkan;
    if (supportsWindowsPbr) {
        if (!m_MainShaderHandle) {
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kShadowedMainPass, k_MeshVertexLayout,
                                                                  k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::ShadowedPbr;
        }
        if ((!m_MainShaderHandle || !m_MainShaderHandle->shader) && m_ShaderMode == ShaderMode::ShadowedPbr) {
            Logger::Warn("[MainPass] PBR shader failed; fallback to legacy shader");
            m_MainShaderHandle =
                ShaderManager::Get().GetOrCreate(EngineShaders::kMesh, k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::Legacy;
        }
    } else {
        if (!m_MainShaderHandle) {
            m_MainShaderHandle =
                ShaderManager::Get().GetOrCreate(EngineShaders::kMesh, k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::Legacy;
        }
    }
#else
    if (Device()->GetBackend() == RHIBackend::Metal) {
        if (!m_MainShaderHandle) {
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kShadowedMainPass, k_MeshVertexLayout,
                                                                  k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::ShadowedPbr;
        }
        if ((!m_MainShaderHandle || !m_MainShaderHandle->shader) && m_ShaderMode == ShaderMode::ShadowedPbr) {
            Logger::Warn("[MainPass] Metal PBR shader failed; fallback to legacy shader");
            m_MainShaderHandle = std::make_shared<ShaderHandle>();
            m_MainShaderHandle->shader = Device()->CreateShader(k_MeshShaderSource, "VSMain", "PSMain",
                                                                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            if (m_MainShaderHandle->shader)
                ++m_MainShaderHandle->version;
            m_ShaderMode = ShaderMode::Legacy;
        }
    } else if (!m_MainShaderHandle) {
        m_MainShaderHandle = std::make_shared<ShaderHandle>();
        m_MainShaderHandle->shader =
            Device()->CreateShader(k_MeshShaderSource, "VSMain", "PSMain", k_MeshVertexLayout, k_MeshVertexLayoutCount);
        if (m_MainShaderHandle->shader)
            ++m_MainShaderHandle->version;
        m_ShaderMode = ShaderMode::Legacy;
    }
#endif

    if (!m_MainShaderHandle)
        return nullptr;
    if (m_MainShaderVersion != m_MainShaderHandle->version) {
        m_MainShaderVersion = m_MainShaderHandle->version;
        for (auto& pipeline : m_MainPipelines)
            pipeline.reset();
        m_MaterialBindGroups.clear();
    }
    if (m_MainShaderHandle->shader && m_ShaderMode == ShaderMode::Unknown) {
        m_ShaderMode = ShaderMode::Legacy;
    }
    return m_MainShaderHandle->shader.get();
}

void MainPass::SetHdrPassthrough(bool passthrough) {
    if (m_HdrPassthrough == passthrough)
        return;
    m_HdrPassthrough = passthrough;
    for (auto& pipeline : m_MainPipelines)
        pipeline.reset();
    m_SkyPipeline.reset();
}

GpuGraphicsPipeline* MainPass::GetOrCreateMainPipeline(bool transparent, bool twoSided, bool wireframe) {
    if (!Device() || !m_MainShaderHandle || !m_MainShaderHandle->shader)
        return nullptr;
    const size_t index = (transparent ? 1u : 0u) | (twoSided ? 2u : 0u) | (wireframe ? 4u : 0u);
    auto& pipeline = m_MainPipelines[index];
    if (!pipeline) {
        GraphicsPipelineDesc desc;
        desc.shader = m_MainShaderHandle->shader;
        desc.colorFormats = {m_HdrPassthrough ? RHIFormat::RGBA16Float : RHIFormat::RGBA8UNorm};
        desc.depthFormat = RHIFormat::D24S8;
        desc.depthStencil.depthWriteEnable = !transparent;
        desc.rasterizer.cullMode = twoSided ? RHICullMode::None : RHICullMode::Back;
        desc.rasterizer.fillMode = wireframe ? RHIFillMode::Wireframe : RHIFillMode::Solid;
        desc.blend.attachments[0].blendEnable = transparent;
        if (transparent) {
            desc.blend.attachments[0].srcAlphaFactor = RHIBlendFactor::Zero;
            desc.blend.attachments[0].dstAlphaFactor = RHIBlendFactor::OneMinusSrcAlpha;
        }
        pipeline = Device()->CreateGraphicsPipeline(desc);
    }
    return pipeline.get();
}

GpuGraphicsPipeline* MainPass::GetOrCreateMaterialPipeline(const MaterialAsset& material) {
    return GetOrCreateMaterialPipeline(material, material.GetShaderHandle(), material.GetBlendMode(),
                                       material.IsTwoSided(), material.IsWireframe());
}

GpuGraphicsPipeline* MainPass::GetOrCreateMaterialPipeline(const MaterialAsset& material,
                                                           const std::shared_ptr<GpuShader>& shader,
                                                           BlendMode blendMode, bool twoSided, bool wireframe) {
    if (!Device() || !shader)
        return nullptr;
    auto& pipeline = m_MaterialPipelines[&material];
    const bool transparent = blendMode == BlendMode::Transparent;
    const RHICullMode cull = twoSided ? RHICullMode::None : RHICullMode::Back;
    const RHIFillMode fill = wireframe ? RHIFillMode::Wireframe : RHIFillMode::Solid;
    const RHIFormat colorFormat = m_HdrPassthrough ? RHIFormat::RGBA16Float : RHIFormat::RGBA8UNorm;
    if (!pipeline || pipeline->desc.shader.get() != shader.get() || pipeline->desc.colorFormats.empty() ||
        pipeline->desc.colorFormats[0] != colorFormat || pipeline->desc.rasterizer.cullMode != cull ||
        pipeline->desc.rasterizer.fillMode != fill || pipeline->desc.blend.attachments[0].blendEnable != transparent) {
        GraphicsPipelineDesc desc;
        desc.shader = shader;
        desc.colorFormats = {colorFormat};
        desc.depthFormat = RHIFormat::D24S8;
        desc.depthStencil.depthWriteEnable = !transparent;
        desc.rasterizer.cullMode = cull;
        desc.rasterizer.fillMode = fill;
        desc.blend.attachments[0].blendEnable = transparent;
        if (transparent) {
            desc.blend.attachments[0].srcAlphaFactor = RHIBlendFactor::Zero;
            desc.blend.attachments[0].dstAlphaFactor = RHIBlendFactor::OneMinusSrcAlpha;
        }
        pipeline = Device()->CreateGraphicsPipeline(desc);
    }
    return pipeline.get();
}

GpuGraphicsPipeline* MainPass::GetOrCreateSkyPipeline() {
    if (!Device() || !m_SkyShaderHandle || !m_SkyShaderHandle->shader)
        return nullptr;
    if (m_SkyShaderVersion != m_SkyShaderHandle->version) {
        m_SkyShaderVersion = m_SkyShaderHandle->version;
        m_SkyPipeline.reset();
    }
    if (!m_SkyPipeline) {
        GraphicsPipelineDesc desc;
        desc.shader = m_SkyShaderHandle->shader;
        desc.colorFormats = {m_HdrPassthrough ? RHIFormat::RGBA16Float : RHIFormat::RGBA8UNorm};
        desc.depthFormat = RHIFormat::D24S8;
        desc.depthStencil.depthTestEnable = false;
        desc.depthStencil.depthWriteEnable = false;
        desc.rasterizer.cullMode = RHICullMode::None;
        m_SkyPipeline = Device()->CreateGraphicsPipeline(desc);
    }
    return m_SkyPipeline.get();
}

GpuShader* MainPass::GetOrCreateSkyShader() {
    if (!Device())
        return nullptr;
    if (!m_SkyShaderHandle) {
        m_SkyShaderHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kProceduralSky, nullptr, 0);
    }
    return m_SkyShaderHandle ? m_SkyShaderHandle->shader.get() : nullptr;
}

void MainPass::RenderSky(const Camera& camera, GpuCommandList& cmd) {
    GpuShader* skyShader = GetOrCreateSkyShader();
    if (!skyShader)
        return;
    SkyConstants constants{};
    const Vec3 forward = camera.GetForward();
    const Vec3 right = camera.GetRight();
    const Vec3 up = camera.GetCamUp();
    constants.forward[0] = forward.x;
    constants.forward[1] = forward.y;
    constants.forward[2] = forward.z;
    constants.right[0] = right.x;
    constants.right[1] = right.y;
    constants.right[2] = right.z;
    constants.up[0] = up.x;
    constants.up[1] = up.y;
    constants.up[2] = up.z;
    constants.sunDirection[0] = m_SunDirection.x;
    constants.sunDirection[1] = m_SunDirection.y;
    constants.sunDirection[2] = m_SunDirection.z;
    constants.sunDirection[3] = 0.0f;
    constants.parameters[0] = std::tan(camera.GetFovY() * kDeg2Rad * 0.5f);
    constants.parameters[1] = camera.GetAspect();
    constants.parameters[2] = m_EnvironmentSettings.skyIntensity;
    constants.parameters[3] = m_HdrPassthrough ? 1.0f : 0.0f;
    constants.skyTint[0] = m_EnvironmentSettings.skyTint.x;
    constants.skyTint[1] = m_EnvironmentSettings.skyTint.y;
    constants.skyTint[2] = m_EnvironmentSettings.skyTint.z;
    constants.horizonTint[0] = m_EnvironmentSettings.horizonTint.x;
    constants.horizonTint[1] = m_EnvironmentSettings.horizonTint.y;
    constants.horizonTint[2] = m_EnvironmentSettings.horizonTint.z;
    constants.groundTint[0] = m_EnvironmentSettings.groundTint.x;
    constants.groundTint[1] = m_EnvironmentSettings.groundTint.y;
    constants.groundTint[2] = m_EnvironmentSettings.groundTint.z;

    GpuGraphicsPipeline* skyPipeline = GetOrCreateSkyPipeline();
    if (!skyPipeline)
        return;
    cmd.SetGraphicsPipeline(skyPipeline);
    cmd.BindVertexBuffer(nullptr);
    cmd.BindIndexBuffer(nullptr);
    ++m_LastStats.bindGroupCreates;
    auto bindings = Device()->CreateBindGroup(m_SkyShaderHandle ? m_SkyShaderHandle->shader : nullptr);
    if (bindings && bindings->SetConstants("SkyConstants", &constants, sizeof(constants)))
        cmd.SetBindGroup(0, bindings.get());
    cmd.Draw(3);
    ++m_LastStats.drawCalls;
}

void MainPass::Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera) {
    m_LastStats = {};
    if (!Device())
        return;

    GpuShader* shader = GetOrCreateShader();
    if (!shader)
        return;

    // BeginFrame/EndFrame moved to Renderer for offscreen pipeline.

    const SceneEnvironmentData environment = CollectSceneEnvironmentData(scene);
    SetEnvironmentSettings(environment);
    const SceneLightData sceneLights = CollectSceneLights(scene, environment);
    const ScenePostProcessData postProcess = CollectScenePostProcessData(scene);
    if (m_SkyPass) {
        m_SkyPass->Execute(commands, camera);
    }

    m_ResourceCache.ResetFrameStats();
    const SceneRenderCollection collection = m_SceneCollector.Collect(scene, camera);
    m_LastStats.submittedSubMeshes = collection.submittedSubMeshes;
    m_LastStats.culledSubMeshes = collection.culledSubMeshes;

    ForwardRenderContext forwardContext;
    forwardContext.sceneLights = &sceneLights;
    forwardContext.postProcess = &postProcess;
    if (m_ForwardOpaquePass) {
        m_ForwardOpaquePass->Execute(commands, scene, camera, collection.opaqueItems, forwardContext);
    }
    if (m_ForwardTransparentPass) {
        m_ForwardTransparentPass->Execute(commands, scene, camera, collection.transparentItems, forwardContext);
    }

    const MaterialResourceCacheStats& resourceStats = m_ResourceCache.GetFrameStats();
    m_LastStats.textureUploads += resourceStats.textureUploads;
    m_LastStats.textureUploadBytes += resourceStats.textureUploadBytes;
    m_LastStats.textureUploadMs += resourceStats.textureUploadMs;

    // EndFrame called by Renderer after PostProcessPass.
}

void MainPass::ExecuteTransparentOnly(GpuCommandList& commands, const Scene& scene, const Camera& camera,
                                      const Mat4* viewProjection) {
    m_LastStats = {};
    if (!Device())
        return;

    GpuShader* shader = GetOrCreateShader();
    if (!shader)
        return;

    const SceneEnvironmentData environment = CollectSceneEnvironmentData(scene);
    SetEnvironmentSettings(environment);
    const SceneLightData sceneLights = CollectSceneLights(scene, environment);
    const ScenePostProcessData postProcess = CollectScenePostProcessData(scene);

    m_ResourceCache.ResetFrameStats();
    const SceneRenderCollection collection = m_SceneCollector.Collect(scene, camera);
    m_LastStats.submittedSubMeshes = collection.submittedSubMeshes;
    m_LastStats.culledSubMeshes = collection.culledSubMeshes;

    ForwardRenderContext forwardContext;
    forwardContext.sceneLights = &sceneLights;
    forwardContext.postProcess = &postProcess;
    forwardContext.viewProjection = viewProjection;
    if (m_ForwardTransparentPass) {
        m_ForwardTransparentPass->Execute(commands, scene, camera, collection.transparentItems, forwardContext);
    }

    const MaterialResourceCacheStats& resourceStats = m_ResourceCache.GetFrameStats();
    m_LastStats.textureUploads += resourceStats.textureUploads;
    m_LastStats.textureUploadBytes += resourceStats.textureUploadBytes;
    m_LastStats.textureUploadMs += resourceStats.textureUploadMs;
}
