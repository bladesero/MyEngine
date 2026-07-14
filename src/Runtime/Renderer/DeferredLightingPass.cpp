#include "Renderer/DeferredLightingPass.h"

#include "Camera/Camera.h"
#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/ShaderManager.h"

#include <algorithm>
#include <cstring>

namespace {

struct DeferredLightingConstants {
    float viewProj[16];
    float invViewProj[16];
    float lightViewProj[16];
    float lightViewProjCascade[3][16];
    float cascadeSplits[4];
    float spotLightViewProj[16];
    float lightDirection[4];
    float lightColor[4];
    float cameraPosition[4];
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
    float iblInfo[4];
    float screenSize[4];
};

} // namespace

DeferredLightingPass::DeferredLightingPass(IRHIDevice* device) : RenderPass(device) {
}

void DeferredLightingPass::Resize(uint32_t width, uint32_t height) {
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    if (m_Width == width && m_Height == height)
        return;
    m_Width = width;
    m_Height = height;
    m_SceneColor.reset();
    m_SceneColorRtv.reset();
    m_SceneColorSrv.reset();
    m_SceneColorState = RHIResourceState::Undefined;
}

void DeferredLightingPass::SetGBufferInput(std::shared_ptr<GpuTextureView> albedo,
                                           std::shared_ptr<GpuTextureView> normal,
                                           std::shared_ptr<GpuTextureView> material,
                                           std::shared_ptr<GpuTextureView> emissive) {
    m_GBufferAlbedo = std::move(albedo);
    m_GBufferNormal = std::move(normal);
    m_GBufferMaterial = std::move(material);
    m_GBufferEmissive = std::move(emissive);
}

void DeferredLightingPass::SetDepthInput(std::shared_ptr<GpuTextureView> depth) {
    m_SceneDepth = std::move(depth);
}

void DeferredLightingPass::SetLightingInput(const SceneLightData& lights) {
    m_Lights = lights;
}

void DeferredLightingPass::SetShadowInput(const Mat4& lightViewProj, bool directionalShadowEnabled,
                                          GpuTexture* shadowMap, const Mat4& spotLightViewProj, int spotShadowIndex,
                                          GpuTexture* spotShadowMap, const Vec3& pointShadowPosition,
                                          float pointShadowRange, int pointShadowIndex, GpuTexture* pointShadowMap,
                                          const Mat4* cascadeViewProj, uint32_t cascadeCount,
                                          const float* cascadeSplits) {
    m_LightViewProj = lightViewProj;
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

void DeferredLightingPass::SetEnvironmentInput(GpuTexture* environmentCubemap,
                                               std::shared_ptr<GpuBufferView> sh2Buffer) {
    m_EnvironmentCubemap = environmentCubemap;
    m_EnvironmentSH2Buffer = std::move(sh2Buffer);
}

bool DeferredLightingPass::PrepareGraphResources() {
    return EnsureResources();
}

DeferredLightingPass::GraphResources DeferredLightingPass::GetGraphResources() const {
    GraphResources resources;
    resources.sceneColor = m_SceneColor;
    resources.sceneColorRtv = m_SceneColorRtv;
    resources.sceneColorSrv = m_SceneColorSrv;
    resources.initialState = m_SceneColorState;
    return resources;
}

void DeferredLightingPass::MarkGraphResourcesShaderResource() {
    m_SceneColorState = RHIResourceState::ShaderResource;
}

bool DeferredLightingPass::EnsureResources() {
    if (!Device())
        return false;
    if (!m_SceneColor) {
        RHITextureDesc desc;
        desc.width = m_Width;
        desc.height = m_Height;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.format = RHIFormat::RGBA16Float;
        desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
        desc.debugName = "DeferredSceneColor";
        m_SceneColor = Device()->CreateTexture(desc);
        if (!m_SceneColor) {
            Logger::Error("[DeferredLightingPass] Failed to create scene color");
            return false;
        }
        RHITextureViewDesc rtvDesc;
        rtvDesc.usage = RHIResourceUsage::RenderTarget;
        m_SceneColorRtv = Device()->CreateTextureView(m_SceneColor, rtvDesc);
        RHITextureViewDesc srvDesc;
        srvDesc.usage = RHIResourceUsage::ShaderResource;
        m_SceneColorSrv = Device()->CreateTextureView(m_SceneColor, srvDesc);
        if (!m_SceneColorRtv || !m_SceneColorSrv) {
            Logger::Error("[DeferredLightingPass] Failed to create scene color views");
            return false;
        }
    }

    if (!m_LinearSampler) {
        RHISamplerDesc linear;
        linear.addressU = linear.addressV = linear.addressW = RHIAddressMode::Clamp;
        m_LinearSampler = Device()->CreateSampler(linear);
        RHISamplerDesc point = linear;
        point.filter = RHIFilter::Point;
        m_PointSampler = Device()->CreateSampler(point);
        RHISamplerDesc shadow = linear;
        shadow.filter = RHIFilter::ComparisonLinear;
        m_ShadowSampler = Device()->CreateSampler(shadow);
        if (!m_LinearSampler || !m_PointSampler || !m_ShadowSampler) {
            Logger::Error("[DeferredLightingPass] Failed to create samplers");
            return false;
        }
    }
    return GetOrCreateShader() && GetOrCreatePipeline();
}

GpuShader* DeferredLightingPass::GetOrCreateShader() {
    if (!Device())
        return nullptr;
    if (!m_ShaderHandle) {
        m_ShaderHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kDeferredLighting, nullptr, 0);
    }
    if (!m_ShaderHandle || !m_ShaderHandle->shader)
        return nullptr;
    if (m_ShaderVersion != m_ShaderHandle->version) {
        m_ShaderVersion = m_ShaderHandle->version;
        m_Pipeline.reset();
    }
    return m_ShaderHandle->shader.get();
}

GpuGraphicsPipeline* DeferredLightingPass::GetOrCreatePipeline() {
    if (!Device() || !m_ShaderHandle || !m_ShaderHandle->shader)
        return nullptr;
    if (!m_Pipeline) {
        GraphicsPipelineDesc desc;
        desc.shader = m_ShaderHandle->shader;
        desc.colorFormats = {RHIFormat::RGBA16Float};
        desc.depthStencil.depthTestEnable = false;
        desc.depthStencil.depthWriteEnable = false;
        desc.rasterizer.cullMode = RHICullMode::None;
        m_Pipeline = Device()->CreateGraphicsPipeline(desc);
        if (!m_Pipeline) {
            Logger::Error("[DeferredLightingPass] Failed to create graphics pipeline");
        }
    }
    return m_Pipeline.get();
}

std::shared_ptr<GpuTextureView> DeferredLightingPass::GetTextureView(GpuTexture* texture) {
    if (!Device() || !texture)
        return nullptr;
    auto found = m_TextureViews.find(texture);
    if (found != m_TextureViews.end())
        return found->second;
    RHITextureViewDesc desc;
    desc.mipCount = texture->desc.mipLevels;
    desc.layerCount = texture->desc.arrayLayers;
    desc.usage = RHIResourceUsage::ShaderResource;
    auto view = Device()->CreateTextureView(std::shared_ptr<GpuTexture>(texture, [](GpuTexture*) {}), desc);
    if (view)
        m_TextureViews[texture] = view;
    return view;
}

void DeferredLightingPass::Execute(GpuCommandList& commands, const Scene&, const Camera& camera) {
    if (!EnsureResources() || !m_Pipeline || !m_ShaderHandle || !m_ShaderHandle->shader)
        return;
    if (!m_GBufferAlbedo || !m_GBufferNormal || !m_GBufferMaterial || !m_GBufferEmissive || !m_SceneDepth) {
        Logger::Error("[DeferredLightingPass] Missing GBuffer or depth inputs");
        return;
    }

    DeferredLightingConstants constants{};
    const Mat4 viewProj = camera.GetViewProj();
    Mat4 invViewProj = Mat4::Identity();
    Mat4Invert(viewProj, invViewProj);
    std::memcpy(constants.viewProj, viewProj.Data(), sizeof(constants.viewProj));
    std::memcpy(constants.invViewProj, invViewProj.Data(), sizeof(constants.invViewProj));
    std::memcpy(constants.lightViewProj, m_LightViewProj.Data(), sizeof(constants.lightViewProj));
    for (uint32_t i = 0; i < 3; ++i) {
        std::memcpy(constants.lightViewProjCascade[i], m_LightViewProjCascade[i].Data(),
                    sizeof(constants.lightViewProjCascade[i]));
    }
    std::memcpy(constants.cascadeSplits, m_CascadeSplits, sizeof(constants.cascadeSplits));
    std::memcpy(constants.spotLightViewProj, m_SpotLightViewProj.Data(), sizeof(constants.spotLightViewProj));

    constants.lightDirection[0] = m_Lights.direction.x;
    constants.lightDirection[1] = m_Lights.direction.y;
    constants.lightDirection[2] = m_Lights.direction.z;
    constants.lightDirection[3] = m_Lights.directionalIntensity;
    constants.lightColor[0] = m_Lights.color.x;
    constants.lightColor[1] = m_Lights.color.y;
    constants.lightColor[2] = m_Lights.color.z;
    constants.lightColor[3] = 1.0f;
    const Vec3 cameraPosition = camera.GetPosition();
    constants.cameraPosition[0] = cameraPosition.x;
    constants.cameraPosition[1] = cameraPosition.y;
    constants.cameraPosition[2] = cameraPosition.z;
    constants.cameraPosition[3] = 1.0f;

    const size_t pointCount = (std::min)(m_Lights.pointLights.size(), size_t{4});
    for (size_t i = 0; i < pointCount; ++i) {
        const ScenePointLight& point = m_Lights.pointLights[i];
        constants.pointLightPositions[i][0] = point.position.x;
        constants.pointLightPositions[i][1] = point.position.y;
        constants.pointLightPositions[i][2] = point.position.z;
        constants.pointLightPositions[i][3] = point.range;
        constants.pointLightColors[i][0] = point.color.x;
        constants.pointLightColors[i][1] = point.color.y;
        constants.pointLightColors[i][2] = point.color.z;
        constants.pointLightColors[i][3] = point.intensity;
    }
    const size_t spotCount = (std::min)(m_Lights.spotLights.size(), size_t{4});
    for (size_t i = 0; i < spotCount; ++i) {
        const SceneSpotLight& spot = m_Lights.spotLights[i];
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
    constants.lightInfo[1] = m_Lights.ambientIntensity;
    constants.lightInfo[2] = static_cast<float>(spotCount);
    constants.pointShadowPosition[0] = m_PointShadowPosition.x;
    constants.pointShadowPosition[1] = m_PointShadowPosition.y;
    constants.pointShadowPosition[2] = m_PointShadowPosition.z;
    constants.pointShadowPosition[3] = m_PointShadowRange;
    constants.shadowInfo[0] = (m_DirectionalShadowEnabled && m_ShadowMap) ? 1.0f : 0.0f;
    constants.shadowInfo[1] = static_cast<float>(m_SpotShadowIndex);
    constants.shadowInfo[2] = static_cast<float>(m_PointShadowIndex);
    constants.shadowInfo[3] = 0.05f;
    constants.shadowIntensity[0] = m_Lights.directionalShadowIntensity;
    constants.shadowIntensity[1] = (m_SpotShadowIndex >= 0 && static_cast<size_t>(m_SpotShadowIndex) < spotCount)
                                       ? m_Lights.spotLights[static_cast<size_t>(m_SpotShadowIndex)].shadowIntensity
                                       : 1.0f;
    constants.shadowIntensity[2] = (m_PointShadowIndex >= 0 && static_cast<size_t>(m_PointShadowIndex) < pointCount)
                                       ? m_Lights.pointLights[static_cast<size_t>(m_PointShadowIndex)].shadowIntensity
                                       : 1.0f;
    constants.shadowIntensity[3] = 1.0f;
    constants.iblInfo[0] = (m_EnvironmentCubemap && m_EnvironmentCubemap->IsCube()) ? 1.0f : 0.0f;
    constants.iblInfo[1] = 1.0f;
    constants.screenSize[0] = 1.0f / static_cast<float>(m_Width);
    constants.screenSize[1] = 1.0f / static_cast<float>(m_Height);
    constants.screenSize[2] = static_cast<float>(m_Width);
    constants.screenSize[3] = static_cast<float>(m_Height);

    auto bindings = Device()->CreateBindGroup(m_ShaderHandle->shader);
    if (!bindings)
        return;
    bindings->SetConstants("DeferredLightingParams", &constants, sizeof(constants));
    bindings->SetTexture("g_GBufferAlbedo", m_GBufferAlbedo);
    bindings->SetTexture("g_GBufferNormal", m_GBufferNormal);
    bindings->SetTexture("g_GBufferMaterial", m_GBufferMaterial);
    bindings->SetTexture("g_GBufferEmissive", m_GBufferEmissive);
    bindings->SetTexture("g_SceneDepth", m_SceneDepth);
    bindings->SetTexture("g_ShadowMap", GetTextureView(m_ShadowMap));
    bindings->SetTexture("g_SpotShadowMap", GetTextureView(m_SpotShadowMap));
    bindings->SetTexture("g_PointShadowMap", GetTextureView(m_PointShadowMap));
    bindings->SetTexture("g_IBLCubemap", GetTextureView(m_EnvironmentCubemap));
    bindings->SetSampler("g_LinearSampler", m_LinearSampler);
    bindings->SetSampler("g_PointSampler", m_PointSampler);
    bindings->SetSampler("g_ShadowSampler", m_ShadowSampler);
    if (!bindings->SetStorageBuffer("g_EnvironmentSH2", m_EnvironmentSH2Buffer) && !m_LoggedMissingEnvironmentSH) {
        Logger::Error("[DeferredLightingPass] Failed to bind g_EnvironmentSH2");
        m_LoggedMissingEnvironmentSH = true;
    }

    commands.SetGraphicsPipeline(m_Pipeline.get());
    commands.SetBindGroup(0, bindings.get());
    commands.Draw(3);
}
