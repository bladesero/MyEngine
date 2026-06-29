#include "Renderer/MainPass.h"

#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Assets/TextureAsset.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Core/Logger.h"
#include "Renderer/MeshShader.h"
#include "Renderer/LightComponent.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"
#include "Math/Mat4Inverse.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#ifdef MYENGINE_PLATFORM_WINDOWS
#endif

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

struct SkyConstants {
    float forward[4];
    float right[4];
    float up[4];
    float parameters[4];
};

bool GetRenderable(Actor& actor, MeshAsset*& mesh, MaterialAsset*& material,
                   SkinnedMeshRendererComponent*& skin)
{
    skin = nullptr;
    if (auto* skinned = actor.GetComponent<SkinnedMeshRendererComponent>()) {
        if (!skinned->IsEnabled() || !skinned->IsValid()) return false;
        mesh = skinned->GetRenderMesh();
        material = skinned->GetMaterial().Get();
        skin = skinned;
        return mesh && material;
    }
    if (auto* renderer = actor.GetComponent<MeshRendererComponent>()) {
        if (!renderer->IsEnabled() || !renderer->IsValid()) return false;
        mesh = renderer->GetMesh().Get();
        material = renderer->GetMaterial().Get();
        return mesh && material;
    }
    return false;
}

struct RenderItem {
    Actor* actor = nullptr;
    MeshAsset* mesh = nullptr;
    const SubMesh* subMesh = nullptr;
    uint32_t subMeshIndex = 0;
    MaterialAsset* material = nullptr;
    SkinnedMeshRendererComponent* skin = nullptr;
    float distanceSq = 0.0f;
};

constexpr uint32_t kMainTextureSlotCount = 9;
const char* kMainTextureNames[kMainTextureSlotCount] = {
    "g_BaseColorMap", "g_ShadowMap", "g_NormalMap",
    "g_MetallicRoughnessMap", "g_OcclusionMap", "g_EmissiveMap",
    "g_SpotShadowMap", "g_PointShadowMap", "g_IBLCubemap"
};
const char* kMainSamplerNames[kMainTextureSlotCount] = {
    "g_Sampler", "g_ShadowSampler", "g_NormalSampler",
    "g_MetallicRoughnessSampler", "g_OcclusionSampler", "g_EmissiveSampler",
    "g_SpotShadowSampler", "g_PointShadowSampler", "g_IBLSampler"
};

struct ScenePointLight {
    Vec3 position = Vec3::Zero();
    Vec3 color = Vec3::One();
    float intensity = 1.0f;
    float range = 8.0f;
};

struct SceneSpotLight {
    Vec3 position = Vec3::Zero();
    Vec3 direction = Vec3{ 0.0f, -1.0f, 0.0f };
    Vec3 color = Vec3::One();
    float intensity = 1.0f;
    float range = 8.0f;
    float innerConeCos = 0.9f;
    float outerConeCos = 0.8f;
};

struct SceneLightData {
    Vec3 direction = Vec3{ -0.55f, -1.0f, -0.45f }.Normalized();
    Vec3 color = Vec3::One();
    float directionalIntensity = 0.0f;
    float ambientIntensity = 1.0f;
    std::vector<ScenePointLight> pointLights;
    std::vector<SceneSpotLight> spotLights;
};

struct ScenePostProcessData {
    float exposure = 1.0f;
    float gamma = 2.2f;
    float toneMapping = 1.0f;
    float vignette = 0.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float antiAliasingStrength = 0.0f;
};

void FillColorConstants(float out[4], const MaterialAsset& material,
                        const char* name, const Vec3& fallback, float fallbackAlpha = 1.0f)
{
    const MaterialParam color = material.GetParam(
        name, MaterialParam::FromColor(fallback, fallbackAlpha));
    out[0] = color.data[0];
    out[1] = color.data[1];
    out[2] = color.data[2];
    out[3] = color.data[3];
}

RHIFilter FilterForTexture(const TextureAsset& texture)
{
    return texture.GetFilter() == TextureFilter::Nearest ? RHIFilter::Point : RHIFilter::Linear;
}

RHIAddressMode AddressForTexture(TextureWrap wrap)
{
    return wrap == TextureWrap::Clamp ? RHIAddressMode::Clamp : RHIAddressMode::Repeat;
}

RHISamplerDesc SamplerDescForTexture(const TextureAsset& texture)
{
    RHISamplerDesc desc;
    desc.filter = FilterForTexture(texture);
    desc.addressU = AddressForTexture(texture.GetWrapU());
    desc.addressV = AddressForTexture(texture.GetWrapV());
    desc.addressW = RHIAddressMode::Repeat;
    return desc;
}

bool SameSamplerDesc(const RHISamplerDesc& left, const RHISamplerDesc& right)
{
    return left.filter == right.filter &&
           left.addressU == right.addressU &&
           left.addressV == right.addressV &&
           left.addressW == right.addressW;
}

void AppendPointer(std::string& out, const void* ptr)
{
    out += std::to_string(reinterpret_cast<uintptr_t>(ptr));
}

void AppendSamplerDesc(std::string& out, const RHISamplerDesc& desc)
{
    out += ':';
    out += std::to_string(static_cast<int>(desc.filter));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressU));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressV));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressW));
}

SceneLightData CollectSceneLights(const Scene& scene)
{
    SceneLightData out;
    bool foundDirectional = false;
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* light = actor.GetComponent<LightComponent>();
        if (!light || !light->IsEnabled()) return;

        if (light->GetLightType() == LightType::Directional) {
            if (!foundDirectional) {
                out.direction = light->GetDirection();
                out.color = light->GetColor();
                out.directionalIntensity = light->GetIntensity();
                foundDirectional = true;
            }
            return;
        }

        if (light->GetLightType() == LightType::Point && out.pointLights.size() < 4) {
            ScenePointLight point;
            point.position = actor.GetWorldPosition();
            point.color = light->GetColor();
            point.intensity = light->GetIntensity();
            point.range = light->GetRange();
            out.pointLights.push_back(point);
            return;
        }

        if (light->GetLightType() == LightType::Spot && out.spotLights.size() < 4) {
            SceneSpotLight spot;
            spot.position = actor.GetWorldPosition();
            spot.direction = light->GetDirection();
            spot.color = light->GetColor();
            spot.intensity = light->GetIntensity();
            spot.range = light->GetRange();
            spot.innerConeCos = std::cos(light->GetInnerConeAngle() * kDeg2Rad);
            spot.outerConeCos = std::cos(light->GetOuterConeAngle() * kDeg2Rad);
            out.spotLights.push_back(spot);
        }
    });
    return out;
}

ScenePostProcessData CollectPostProcess(const Scene& scene)
{
    ScenePostProcessData out;
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive()) return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled()) return;
        out.exposure = post->GetExposure();
        out.gamma = post->GetGamma();
        out.toneMapping = post->IsToneMappingEnabled() ? 1.0f : 0.0f;
        out.vignette = post->GetVignette();
        out.saturation = post->GetSaturation();
        out.contrast = post->GetContrast();
        out.antiAliasingStrength = post->GetAntiAliasingStrength();
        found = true;
    });
    return out;
}

} // namespace

MainPass::MainPass(IRHIDevice* device)
    : RenderPass(device)
{}

void MainPass::Resize(uint32_t, uint32_t)
{
    // Viewport is controlled by SceneRenderLayer through the active command list.
}

void MainPass::SetShadowInput(const Mat4& lightViewProj,
                              const Vec3& lightDirection,
                              bool directionalShadowEnabled,
                              GpuTexture* shadowMap,
                              const Mat4& spotLightViewProj,
                              int spotShadowIndex,
                              GpuTexture* spotShadowMap,
                              const Vec3& pointShadowPosition,
                              float pointShadowRange,
                              int pointShadowIndex,
                              GpuTexture* pointShadowMap,
                              const Mat4* cascadeViewProj,
                              uint32_t cascadeCount,
                              const float* cascadeSplits)
{
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
        m_LightViewProjCascade[i] =
            (cascadeViewProj && i < cascadeCount) ? cascadeViewProj[i] : Mat4::Identity();
    }
    if (cascadeSplits) {
        std::memcpy(m_CascadeSplits, cascadeSplits, sizeof(m_CascadeSplits));
    } else {
        std::memset(m_CascadeSplits, 0, sizeof(m_CascadeSplits));
    }
}

void MainPass::SetCascadeShadowInput(const Mat4* cascadeViewProj, uint32_t cascadeCount,
                                     const float* cascadeSplits)
{
    const uint32_t count = (std::min)(cascadeCount, 3u);
    for (uint32_t i = 0; i < 3; ++i) {
        m_LightViewProjCascade[i] =
            (cascadeViewProj && i < count) ? cascadeViewProj[i] : Mat4::Identity();
    }
    if (cascadeSplits) {
        for (uint32_t i = 0; i < 4; ++i) {
            m_CascadeSplits[i] = cascadeSplits[i];
        }
    } else {
        std::memset(m_CascadeSplits, 0, sizeof(m_CascadeSplits));
    }
}

void MainPass::SetEnvironmentInput(GpuTexture* environmentCubemap,
                                   std::shared_ptr<GpuBufferView> sh2Buffer,
                                   const float* sh2Coefficients)
{
    m_EnvironmentCubemap = environmentCubemap;
    m_EnvironmentSH2Buffer = sh2Buffer;
    if (sh2Coefficients) {
        std::memcpy(m_EnvironmentSH2, sh2Coefficients, sizeof(m_EnvironmentSH2));
    } else {
        std::memset(m_EnvironmentSH2, 0, sizeof(m_EnvironmentSH2));
    }
}

void MainPass::EnsureMeshUploaded(MeshAsset* mesh)
{
    if (!mesh || mesh->IsUploaded() || !Device()) return;

    const auto& verts = mesh->GetVertices();
    const auto& idx   = mesh->GetIndices();
    if (verts.empty()) return;

    const uint32_t vbBytes = static_cast<uint32_t>(verts.size() * sizeof(MeshVertex));
    mesh->SetVertexBuffer(Device()->CreateVertexBuffer(verts.data(), vbBytes, sizeof(MeshVertex)));

    if (!idx.empty()) {
        const uint32_t ibBytes = static_cast<uint32_t>(idx.size() * sizeof(uint32_t));
        mesh->SetIndexBuffer(Device()->CreateIndexBuffer(idx.data(), ibBytes));
    }
}

void MainPass::EnsureTextureUploaded(TextureAsset* tex)
{
    if (!tex || !Device()) return;
    if (tex->HasGpuHandle()) return;
    if (m_TexCache.count(tex)) return;

    const auto& mips = tex->GetMips();
    if (mips.empty()) return;

    RHITextureDesc desc;
    desc.width = static_cast<uint32_t>(tex->GetWidth());
    desc.height = static_cast<uint32_t>(tex->GetHeight());
    desc.mipLevels = static_cast<uint32_t>(mips.size());
    desc.arrayLayers = 1;
    desc.format = RHIFormat::RGBA8UNorm;
    desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
    desc.debugName = tex->GetName();
    std::vector<RHITextureSubresourceData> subresources;
    subresources.reserve(mips.size());
    for (uint32_t mip = 0; mip < mips.size(); ++mip) {
        const TextureMipData& mipData = mips[mip];
        if (mipData.rgba8.empty() || mipData.width <= 0 || mipData.height <= 0) return;
        RHITextureSubresourceData source;
        source.data = mipData.rgba8.data();
        source.rowPitch = static_cast<uint32_t>(mipData.width * 4);
        source.slicePitch = static_cast<uint32_t>(mipData.rgba8.size());
        source.mipLevel = mip;
        source.arrayLayer = 0;
        subresources.push_back(source);
    }

    auto gpuTex = Device()->UploadTexture(
        desc, subresources.data(), static_cast<uint32_t>(subresources.size()));
    if (gpuTex) {
        ++m_LastStats.textureUploads;
        tex->SetGpuHandle(gpuTex.get());
        m_TexCache[tex] = std::move(gpuTex);
    }
}

void MainPass::EnsureNamedBindingDefaults()
{
    if (!Device() || m_DefaultTextureView) return;
    const uint8_t white[4] = {255, 255, 255, 255};
    m_DefaultTexture = Device()->UploadTexture2D(white, 1, 1);
    RHITextureViewDesc viewDesc; viewDesc.usage = RHIResourceUsage::ShaderResource;
    m_DefaultTextureView = Device()->CreateTextureView(m_DefaultTexture, viewDesc);
    RHISamplerDesc linear;
    m_LinearSampler = Device()->CreateSampler(linear);
    RHISamplerDesc shadow = linear;
    shadow.filter = RHIFilter::ComparisonLinear;
    shadow.addressU = shadow.addressV = shadow.addressW = RHIAddressMode::Clamp;
    m_ShadowSampler = Device()->CreateSampler(shadow);
}

std::shared_ptr<GpuTextureView> MainPass::GetTextureView(GpuTexture* texture)
{
    EnsureNamedBindingDefaults();
    if (!texture) return m_DefaultTextureView;
    auto found = m_TextureViews.find(texture);
    if (found != m_TextureViews.end()) return found->second;
    RHITextureViewDesc desc;
    desc.mipCount = texture->desc.mipLevels;
    desc.layerCount = texture->desc.arrayLayers;
    desc.usage = RHIResourceUsage::ShaderResource;
    auto view = Device()->CreateTextureView(
        std::shared_ptr<GpuTexture>(texture, [](GpuTexture*) {}), desc);
    if (view) m_TextureViews[texture] = view;
    return view ? view : m_DefaultTextureView;
}

std::shared_ptr<GpuSampler> MainPass::GetSamplerForTexture(TextureAsset* texture)
{
    EnsureNamedBindingDefaults();
    if (!Device() || !texture) return m_LinearSampler;
    const RHISamplerDesc desc = SamplerDescForTexture(*texture);
    auto found = m_TextureSamplers.find(texture);
    if (found != m_TextureSamplers.end() &&
        found->second && SameSamplerDesc(found->second->desc, desc)) {
        return found->second;
    }
    auto sampler = Device()->CreateSampler(desc);
    if (!sampler) return m_LinearSampler;
    m_TextureSamplers[texture] = sampler;
    return sampler;
}

bool MainPass::CanReuseMaterialBindGroups() const
{
    if (!Device()) return false;
    const RHIBackend backend = Device()->GetBackend();
    return backend == RHIBackend::D3D11 ||
           backend == RHIBackend::D3D12 ||
           backend == RHIBackend::Metal ||
           backend == RHIBackend::Unknown;
}

std::shared_ptr<GpuBindGroup> MainPass::GetOrCreateMaterialBindGroup(
    GpuShader* shader,
    const MaterialAsset& material,
    bool shadowedPbr,
    const std::array<GpuTexture*, 9>& textures,
    const std::array<TextureAsset*, 9>& textureAssets)
{
    if (!Device() || !shader) return nullptr;
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
    AppendPointer(signature, m_LinearSampler.get());
    signature += ':';
    AppendPointer(signature, m_ShadowSampler.get());
    const uint32_t slotCount = shadowedPbr ? kMainTextureSlotCount : 1u;
    for (uint32_t slot = 0; slot < slotCount; ++slot) {
        signature += '|';
        AppendPointer(signature, textures[slot]);
        if (slot == 1 || slot == 6 || slot == 7) {
            signature += ":shadow";
        } else if (textureAssets[slot]) {
            AppendSamplerDesc(signature, SamplerDescForTexture(*textureAssets[slot]));
        } else {
            signature += ":linear";
        }
    }

    auto createBindGroup = [&]() -> std::shared_ptr<GpuBindGroup> {
        ++m_LastStats.bindGroupCreates;
        auto bindings = Device()->CreateBindGroup(
            m_MainShaderHandle ? m_MainShaderHandle->shader : nullptr);
        if (!bindings) return nullptr;
        for (uint32_t slot = 0; slot < slotCount; ++slot) {
            bindings->SetTexture(kMainTextureNames[slot], GetTextureView(textures[slot]));
            std::shared_ptr<GpuSampler> sampler = m_LinearSampler;
            if (slot == 1 || slot == 6 || slot == 7) {
                sampler = m_ShadowSampler;
            } else if (textureAssets[slot]) {
                sampler = GetSamplerForTexture(textureAssets[slot]);
            }
            bindings->SetSampler(kMainSamplerNames[slot], sampler);
        }
        if (shadowedPbr) {
            if (!bindings->SetStorageBuffer("g_EnvironmentSH2", m_EnvironmentSH2Buffer)) {
                if (!m_LoggedEnvironmentSHBindingFailure) {
                    Logger::Error("[MainPass] Failed to bind g_EnvironmentSH2: shaderMode=",
                                  static_cast<int>(m_ShaderMode),
                                  " environmentView=", m_EnvironmentSH2Buffer ? 1 : 0);
                    m_LoggedEnvironmentSHBindingFailure = true;
                }
            }
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

GpuShader* MainPass::GetOrCreateShader()
{
    if (!Device()) return nullptr;

#ifdef MYENGINE_PLATFORM_WINDOWS
    const bool supportsWindowsPbr = Device()->GetBackend() == RHIBackend::D3D11 ||
                                    Device()->GetBackend() == RHIBackend::D3D12 ||
                                    Device()->GetBackend() == RHIBackend::Vulkan;
    if (supportsWindowsPbr) {
        if (!m_MainShaderHandle) {
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(
                "Content/Engine/Shaders/ShadowedMainPass.shader",
                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::ShadowedPbr;
        }
        if ((!m_MainShaderHandle || !m_MainShaderHandle->shader) &&
            m_ShaderMode == ShaderMode::ShadowedPbr) {
            Logger::Warn("[MainPass] PBR shader failed; fallback to legacy shader");
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(
                "Content/Engine/Shaders/Mesh.shader",
                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::Legacy;
        }
    } else {
        if (!m_MainShaderHandle) {
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(
                "Content/Engine/Shaders/Mesh.shader",
                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::Legacy;
        }
    }
#else
    if (Device()->GetBackend() == RHIBackend::Metal) {
        if (!m_MainShaderHandle) {
            m_MainShaderHandle = ShaderManager::Get().GetOrCreate(
                "Content/Engine/Shaders/ShadowedMainPass.shader",
                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            m_ShaderMode = ShaderMode::ShadowedPbr;
        }
        if ((!m_MainShaderHandle || !m_MainShaderHandle->shader) &&
            m_ShaderMode == ShaderMode::ShadowedPbr) {
            Logger::Warn("[MainPass] Metal PBR shader failed; fallback to legacy shader");
            m_MainShaderHandle = std::make_shared<ShaderHandle>();
            m_MainShaderHandle->shader = Device()->CreateShader(
                k_MeshShaderSource, "VSMain", "PSMain",
                k_MeshVertexLayout, k_MeshVertexLayoutCount);
            if (m_MainShaderHandle->shader) ++m_MainShaderHandle->version;
            m_ShaderMode = ShaderMode::Legacy;
        }
    } else if (!m_MainShaderHandle) {
        m_MainShaderHandle = std::make_shared<ShaderHandle>();
        m_MainShaderHandle->shader = Device()->CreateShader(
            k_MeshShaderSource, "VSMain", "PSMain",
            k_MeshVertexLayout, k_MeshVertexLayoutCount);
        if (m_MainShaderHandle->shader) ++m_MainShaderHandle->version;
        m_ShaderMode = ShaderMode::Legacy;
    }
#endif

    if (!m_MainShaderHandle) return nullptr;
    if (m_MainShaderVersion != m_MainShaderHandle->version) {
        m_MainShaderVersion = m_MainShaderHandle->version;
        for (auto& pipeline : m_MainPipelines) pipeline.reset();
        m_MaterialBindGroups.clear();
    }
    if (m_MainShaderHandle->shader && m_ShaderMode == ShaderMode::Unknown) {
        m_ShaderMode = ShaderMode::Legacy;
    }
    return m_MainShaderHandle->shader.get();
}

void MainPass::SetHdrPassthrough(bool passthrough)
{
    if (m_HdrPassthrough == passthrough) return;
    m_HdrPassthrough = passthrough;
    for (auto& pipeline : m_MainPipelines) pipeline.reset();
    m_SkyPipeline.reset();
}

GpuGraphicsPipeline* MainPass::GetOrCreateMainPipeline(
    bool transparent, bool twoSided, bool wireframe)
{
    if (!Device() || !m_MainShaderHandle || !m_MainShaderHandle->shader) return nullptr;
    const size_t index = (transparent ? 1u : 0u) |
                         (twoSided ? 2u : 0u) |
                         (wireframe ? 4u : 0u);
    auto& pipeline = m_MainPipelines[index];
    if (!pipeline) {
        GraphicsPipelineDesc desc;
        desc.shader = m_MainShaderHandle->shader;
        desc.colorFormats = {m_HdrPassthrough ? RHIFormat::RGBA16Float
                                               : RHIFormat::RGBA8UNorm};
        desc.depthFormat = RHIFormat::D24S8;
        desc.rasterizer.cullMode = twoSided ? RHICullMode::None : RHICullMode::Back;
        desc.rasterizer.fillMode = wireframe ? RHIFillMode::Wireframe : RHIFillMode::Solid;
        desc.blend.attachments[0].blendEnable = transparent;
        pipeline = Device()->CreateGraphicsPipeline(desc);
    }
    return pipeline.get();
}

GpuGraphicsPipeline* MainPass::GetOrCreateMaterialPipeline(const MaterialAsset& material)
{
    if (!Device() || !material.GetShader()) return nullptr;
    auto& pipeline = m_MaterialPipelines[&material];
    const bool transparent = material.GetBlendMode() == BlendMode::Transparent;
    const RHICullMode cull = material.IsTwoSided() ? RHICullMode::None : RHICullMode::Back;
    const RHIFillMode fill = material.IsWireframe() ? RHIFillMode::Wireframe : RHIFillMode::Solid;
    const RHIFormat colorFormat = m_HdrPassthrough ? RHIFormat::RGBA16Float
                                                    : RHIFormat::RGBA8UNorm;
    if (!pipeline || pipeline->desc.shader.get() != material.GetShader() ||
        pipeline->desc.colorFormats.empty() || pipeline->desc.colorFormats[0] != colorFormat ||
        pipeline->desc.rasterizer.cullMode != cull ||
        pipeline->desc.rasterizer.fillMode != fill ||
        pipeline->desc.blend.attachments[0].blendEnable != transparent) {
        GraphicsPipelineDesc desc;
        desc.shader = material.GetShaderHandle();
        desc.colorFormats = {colorFormat};
        desc.depthFormat = RHIFormat::D24S8;
        desc.rasterizer.cullMode = cull;
        desc.rasterizer.fillMode = fill;
        desc.blend.attachments[0].blendEnable = transparent;
        pipeline = Device()->CreateGraphicsPipeline(desc);
    }
    return pipeline.get();
}

GpuGraphicsPipeline* MainPass::GetOrCreateSkyPipeline()
{
    if (!Device() || !m_SkyShaderHandle || !m_SkyShaderHandle->shader) return nullptr;
    if (m_SkyShaderVersion != m_SkyShaderHandle->version) {
        m_SkyShaderVersion = m_SkyShaderHandle->version;
        m_SkyPipeline.reset();
    }
    if (!m_SkyPipeline) {
        GraphicsPipelineDesc desc;
        desc.shader = m_SkyShaderHandle->shader;
        desc.colorFormats = {m_HdrPassthrough ? RHIFormat::RGBA16Float
                                               : RHIFormat::RGBA8UNorm};
        desc.depthFormat = RHIFormat::D24S8;
        desc.depthStencil.depthTestEnable = false;
        desc.depthStencil.depthWriteEnable = false;
        desc.rasterizer.cullMode = RHICullMode::None;
        m_SkyPipeline = Device()->CreateGraphicsPipeline(desc);
    }
    return m_SkyPipeline.get();
}

GpuShader* MainPass::GetOrCreateSkyShader()
{
    if (!Device()) return nullptr;
    if (!m_SkyShaderHandle) {
        m_SkyShaderHandle = ShaderManager::Get().GetOrCreate(
            "Content/Engine/Shaders/ProceduralSky.shader", nullptr, 0);
    }
    return m_SkyShaderHandle ? m_SkyShaderHandle->shader.get() : nullptr;
}

void MainPass::RenderSky(const Camera& camera, GpuCommandList& cmd)
{
    GpuShader* skyShader = GetOrCreateSkyShader();
    if (!skyShader) return;
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
    constants.parameters[0] = std::tan(camera.GetFovY() * kDeg2Rad * 0.5f);
    constants.parameters[1] = camera.GetAspect();
    constants.parameters[2] = 1.0f;
    constants.parameters[3] = m_HdrPassthrough ? 1.0f : 0.0f;

    GpuGraphicsPipeline* skyPipeline = GetOrCreateSkyPipeline();
    if (!skyPipeline) return;
    cmd.SetGraphicsPipeline(skyPipeline);
    cmd.BindVertexBuffer(nullptr);
    cmd.BindIndexBuffer(nullptr);
    ++m_LastStats.bindGroupCreates;
    auto bindings = Device()->CreateBindGroup(
        m_SkyShaderHandle ? m_SkyShaderHandle->shader : nullptr);
    if (bindings && bindings->SetConstants("SkyConstants", &constants, sizeof(constants)))
        cmd.SetBindGroup(0, bindings.get());
    cmd.Draw(3);
    ++m_LastStats.drawCalls;
}

void MainPass::Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera)
{
    m_LastStats = {};
    if (!Device()) return;

    GpuShader* shader = GetOrCreateShader();
    if (!shader) return;

    // BeginFrame/EndFrame moved to Renderer for offscreen pipeline.

    const Mat4 viewProj = camera.GetViewProj();
    const SceneLightData sceneLights = CollectSceneLights(scene);
    const ScenePostProcessData postProcess = CollectPostProcess(scene);
    RenderSky(camera, commands);

    std::vector<RenderItem> opaqueItems;
    std::vector<RenderItem> transparentItems;
    auto addRenderItem = [&](Actor& actor, MeshAsset* mesh, const SubMesh& subMesh,
                             uint32_t subMeshIndex, MaterialAsset* mat,
                             SkinnedMeshRendererComponent* skin) {
        if (!mesh || !mat) return;
        RenderItem item;
        item.actor = &actor;
        item.mesh = mesh;
        item.subMesh = &subMesh;
        item.subMeshIndex = subMeshIndex;
        item.material = mat;
        item.skin = skin;
        item.distanceSq = (actor.GetWorldPosition() - camera.GetPosition()).LengthSq();
        ++m_LastStats.submittedSubMeshes;
        if (mat->GetBlendMode() == BlendMode::Transparent) {
            transparentItems.push_back(item);
        } else {
            opaqueItems.push_back(item);
        }
    };

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        if (auto* skinned = actor.GetComponent<SkinnedMeshRendererComponent>()) {
            if (!skinned->IsEnabled() || !skinned->IsValid()) return;
            MeshAsset* mesh = skinned->GetRenderMesh();
            MaterialAsset* mat = skinned->GetMaterial().Get();
            if (!mesh || !mat) return;
            const Mat4 world = actor.GetWorldMatrix();
            if (!camera.IsVisible(TransformAABB(mesh->GetAABB(), world))) return;
            const auto& subMeshes = mesh->GetSubMeshes();
            for (uint32_t i = 0; i < subMeshes.size(); ++i) {
                if (!camera.IsVisible(TransformAABB(subMeshes[i].bounds, world))) {
                    ++m_LastStats.culledSubMeshes;
                    continue;
                }
                addRenderItem(actor, mesh, subMeshes[i], i, mat, skinned);
            }
            return;
        }

        auto* renderer = actor.GetComponent<MeshRendererComponent>();
        if (!renderer || !renderer->IsEnabled() || !renderer->IsValid()) return;
        MeshAsset* mesh = renderer->GetMesh().Get();
        if (!mesh) return;
        const Mat4 world = actor.GetWorldMatrix();
        if (!camera.IsVisible(TransformAABB(mesh->GetAABB(), world))) return;
        const auto& subMeshes = mesh->GetSubMeshes();
        for (uint32_t i = 0; i < subMeshes.size(); ++i) {
            const SubMesh& subMesh = subMeshes[i];
            if (!camera.IsVisible(TransformAABB(subMesh.bounds, world))) {
                ++m_LastStats.culledSubMeshes;
                continue;
            }
            MaterialHandle material =
                renderer->GetMaterialForSlot(subMesh.materialSlot);
            addRenderItem(actor, mesh, subMesh, i, material.Get(), nullptr);
        }
    });
    std::sort(opaqueItems.begin(), opaqueItems.end(),
        [](const RenderItem& a, const RenderItem& b) {
            if (a.mesh != b.mesh) return a.mesh < b.mesh;
            if (a.subMeshIndex != b.subMeshIndex) return a.subMeshIndex < b.subMeshIndex;
            if (a.material != b.material) return a.material < b.material;
            return a.distanceSq < b.distanceSq;
        });
    std::sort(transparentItems.begin(), transparentItems.end(),
        [](const RenderItem& a, const RenderItem& b) {
            return a.distanceSq > b.distanceSq;
        });
    opaqueItems.insert(opaqueItems.end(), transparentItems.begin(), transparentItems.end());

    for (size_t itemIndex = 0; itemIndex < opaqueItems.size();) {
        const RenderItem& item = opaqueItems[itemIndex];
        size_t instanceCount = 1;
        const bool allowInstancing = Device()->GetBackend() != RHIBackend::Metal;
        if (allowInstancing &&
            m_ShaderMode == ShaderMode::ShadowedPbr &&
            item.skin == nullptr &&
            item.material->GetBlendMode() != BlendMode::Transparent) {
            while (instanceCount < 64 &&
                   itemIndex + instanceCount < opaqueItems.size()) {
                const RenderItem& candidate = opaqueItems[itemIndex + instanceCount];
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
        if (!subMesh) { itemIndex += instanceCount; continue; }
        if (mat->GetShaderAsset().IsValid()) {
            auto custom = ShaderManager::Get().GetOrCreate(
                mat->GetShaderAsset()->GetPath(), k_MeshVertexLayout, k_MeshVertexLayoutCount);
            if (custom && custom->shader) mat->SetShader(custom->shader);
        }
        EnsureMeshUploaded(mesh);
        if (!mesh->GetVertexBuffer()) { itemIndex += instanceCount; continue; }

        GpuShader* drawShader = shader;
        if (mat->HasShader()) {
            drawShader = mat->GetShader();
        }
        if (drawShader) {
            if (drawShader == shader) {
                auto* pipeline = GetOrCreateMainPipeline(
                    mat->GetBlendMode() == BlendMode::Transparent,
                    mat->IsTwoSided(), mat->IsWireframe());
                if (!pipeline) { itemIndex += instanceCount; continue; }
                commands.SetGraphicsPipeline(pipeline);
            } else {
                auto* pipeline = GetOrCreateMaterialPipeline(*mat);
                if (!pipeline) { itemIndex += instanceCount; continue; }
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
                EnsureTextureUploaded(texAsset);
                baseColorAsset = texAsset;
                baseColorTexture = static_cast<GpuTexture*>(texAsset->GetGpuHandle());
            }
        }
        namedTextures[0] = baseColorTexture;
        namedTextureAssets[0] = baseColorAsset;

        if (m_ShaderMode == ShaderMode::ShadowedPbr) {
            GpuTexture* shadowTexture = m_ShadowMap ? m_ShadowMap : baseColorTexture;
            namedTextures[1] = shadowTexture;

            const char* mapSlots[] = {
                "NormalMap", "MetallicRoughnessMap", "OcclusionMap", "EmissiveMap"
            };
            float mapFlags[4] = {};
            for (uint32_t mapIndex = 0; mapIndex < 4; ++mapIndex) {
                GpuTexture* gpuTexture = nullptr;
                if (mat->HasTexture(mapSlots[mapIndex])) {
                    TextureAsset* texture = mat->GetTexture(mapSlots[mapIndex]).Get();
                    if (texture) {
                        EnsureTextureUploaded(texture);
                        gpuTexture = static_cast<GpuTexture*>(texture->GetGpuHandle());
                        mapFlags[mapIndex] = gpuTexture ? 1.0f : 0.0f;
                        namedTextureAssets[2 + mapIndex] = texture;
                    }
                }
                namedTextures[2 + mapIndex] = gpuTexture;
            }
            namedTextures[6] = m_SpotShadowMap;
            namedTextures[7] = m_PointShadowMap;

            GpuTexture* iblTexture = nullptr;
            float iblEnabled = 0.0f;
            if (m_EnvironmentCubemap && m_EnvironmentCubemap->IsCube()) {
                iblTexture = m_EnvironmentCubemap;
                iblEnabled = 1.0f;
            } else if (mat->HasTexture("IBLCubemap")) {
                TextureAsset* iblAsset = mat->GetTexture("IBLCubemap").Get();
                if (iblAsset) {
                    EnsureTextureUploaded(iblAsset);
                    iblTexture = static_cast<GpuTexture*>(iblAsset->GetGpuHandle());
                    iblEnabled = (iblTexture && iblTexture->IsCube()) ? 1.0f : 0.0f;
                    if (iblEnabled > 0.5f) namedTextureAssets[8] = iblAsset;
                }
            }
            namedTextures[8] = iblEnabled > 0.5f ? iblTexture : nullptr;

            ShadowedPerDrawConstants constants{};
            std::memcpy(constants.viewProj, viewProj.Data(), sizeof(constants.viewProj));
            std::memcpy(constants.world, world.Data(), sizeof(constants.world));
            std::memcpy(constants.lightViewProj, m_LightViewProj.Data(), sizeof(constants.lightViewProj));
            for (uint32_t cascade = 0; cascade < 3; ++cascade) {
                std::memcpy(constants.lightViewProjCascade[cascade],
                            m_LightViewProjCascade[cascade].Data(),
                            sizeof(constants.lightViewProjCascade[cascade]));
            }
            std::memcpy(constants.cascadeSplits, m_CascadeSplits, sizeof(constants.cascadeSplits));
            std::memcpy(constants.spotLightViewProj, m_SpotLightViewProj.Data(), sizeof(constants.spotLightViewProj));

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
            constants.pointShadowPosition[0] = m_PointShadowPosition.x;
            constants.pointShadowPosition[1] = m_PointShadowPosition.y;
            constants.pointShadowPosition[2] = m_PointShadowPosition.z;
            constants.pointShadowPosition[3] = m_PointShadowRange;
            constants.shadowInfo[0] =
                (m_DirectionalShadowEnabled && m_ShadowMap) ? 1.0f : 0.0f;
            constants.shadowInfo[1] = static_cast<float>(m_SpotShadowIndex);
            constants.shadowInfo[2] = static_cast<float>(m_PointShadowIndex);
            constants.shadowInfo[3] = 0.05f;
            constants.postProcess[0] = postProcess.exposure;
            constants.postProcess[1] = postProcess.gamma;
            constants.postProcess[2] = postProcess.toneMapping;
            constants.postProcess[3] = postProcess.vignette;
            constants.postProcess2[0] = postProcess.saturation;
            constants.postProcess2[1] = postProcess.contrast;
            constants.postProcess2[2] = postProcess.antiAliasingStrength;
            constants.postProcess2[3] = m_HdrPassthrough ? -1.0f : 0.0f;
            constants.drawInfo[0] = instanceCount > 1 ? 1.0f : 0.0f;
            constants.drawInfo[1] = constants.shadowInfo[0];
            for (size_t instance = 0; instance < instanceCount; ++instance) {
                const Mat4 instanceMatrix =
                    opaqueItems[itemIndex + instance].actor->GetWorldMatrix();
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

            EnsureNamedBindingDefaults();
            auto bindings = GetOrCreateMaterialBindGroup(
                drawShader, *mat, true, namedTextures, namedTextureAssets);
            if (bindings) {
                bindings->SetConstants("PerDraw", &constants, sizeof(constants));
                if (!m_LoggedEnvironmentState) {
                    Logger::Info("[MainPass] Environment IBL state: cube=",
                                 (m_EnvironmentCubemap && m_EnvironmentCubemap->IsCube()) ? 1 : 0,
                                 " shView=", m_EnvironmentSH2Buffer ? 1 : 0,
                                 " iblEnabled=", iblEnabled > 0.5f ? 1 : 0);
                    m_LoggedEnvironmentState = true;
                }
                commands.SetBindGroup(0, bindings.get());
            }

            if (mesh->GetIndexBuffer()) {
                commands.BindIndexBuffer(mesh->GetIndexBuffer());
                commands.DrawIndexedInstanced(
                    subMesh->indexCount, static_cast<uint32_t>(instanceCount),
                    subMesh->indexOffset, static_cast<uint32_t>(subMesh->vertexOffset));
                ++m_LastStats.drawCalls;
            } else {
                commands.BindIndexBuffer(nullptr);
                commands.DrawInstanced(
                    subMesh->indexCount, static_cast<uint32_t>(instanceCount),
                    subMesh->vertexOffset);
                ++m_LastStats.drawCalls;
            }
        } else {
            LegacyPerDrawConstants constants{};
            std::memcpy(constants.mvp, mvp.Data(), sizeof(constants.mvp));
            FillColorConstants(constants.baseColor, *mat, "BaseColor", Vec3::One());
            EnsureNamedBindingDefaults();
            auto bindings = GetOrCreateMaterialBindGroup(
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
                ++m_LastStats.drawCalls;
            } else {
                commands.BindIndexBuffer(nullptr);
                commands.Draw(subMesh->indexCount, subMesh->vertexOffset);
                ++m_LastStats.drawCalls;
            }
        }
        itemIndex += instanceCount;
    }

    // EndFrame called by Renderer after PostProcessPass.
}
