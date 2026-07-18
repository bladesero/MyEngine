#include "Renderer/PostProcessPass.h"

#include "Camera/Camera.h"
#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace {
struct PostProcessConstants {
    float params[4];
    float params2[4];
    float screenSize[4];
    float params3[4];
};
struct SSAOConstants {
    float projection[16];
    float invProjection[16];
    float screenSize[4];
    float ssaoParams[4];
    float samples[64][4];
};
struct SSAOBlurConstants {
    float texelSize[4];
};

PostProcessConstants CollectPostProcessParams(const Scene& scene, uint32_t width, uint32_t height) {
    PostProcessConstants out{};
    out.params[0] = 1.0f;
    out.params[1] = 2.2f;
    out.params[2] = 1.0f;
    out.params2[0] = 1.0f;
    out.params2[1] = 1.0f;
    out.params2[2] = 1.0f;
    out.params2[3] = 1.0f;
    out.screenSize[0] = 1.0f / width;
    out.screenSize[1] = 1.0f / height;
    out.screenSize[2] = static_cast<float>(width);
    out.screenSize[3] = static_cast<float>(height);
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive())
            return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled())
            return;
        out.params[0] = post->GetExposure();
        out.params[1] = post->GetGamma();
        out.params[2] = post->IsToneMappingEnabled() ? 1.0f : 0.0f;
        out.params[3] = post->GetVignette();
        out.params2[0] = post->GetSaturation();
        out.params2[1] = post->GetContrast();
        out.params2[2] = post->GetAntiAliasingStrength() > 0.0f ? 1.0f : 0.0f;
        out.params2[3] = post->GetAntiAliasingStrength();
        found = true;
    });
    return out;
}

SSAOConstants BuildSSAOConstants(const Scene& scene, const Camera& camera, uint32_t width, uint32_t height) {
    SSAOConstants out{};
    std::memcpy(out.projection, camera.GetProj().Data(), sizeof(out.projection));
    Mat4 inverse;
    Mat4Invert(camera.GetProj(), inverse);
    std::memcpy(out.invProjection, inverse.Data(), sizeof(out.invProjection));
    out.screenSize[0] = 1.0f / width;
    out.screenSize[1] = 1.0f / height;
    out.screenSize[2] = static_cast<float>(width);
    out.screenSize[3] = static_cast<float>(height);
    out.ssaoParams[0] = 1.2f;
    out.ssaoParams[1] = 0.025f;
    out.ssaoParams[2] = 1.5f;
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive())
            return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled())
            return;
        out.ssaoParams[0] = post->GetSSAORadius();
        out.ssaoParams[1] = post->GetSSAOBias();
        out.ssaoParams[2] = post->GetSSAOPower();
        out.ssaoParams[3] = post->GetSSAOIntensity();
        found = true;
    });
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (uint32_t i = 0; i < 16; ++i) {
        const float u1 = dist(rng), u2 = dist(rng), r = std::sqrt(u1);
        const float theta = 6.2831853f * u2, scale = 0.1f + (i / 16.0f) * (i / 16.0f) * 0.9f;
        out.samples[i][0] = r * std::cos(theta) * scale;
        out.samples[i][1] = r * std::sin(theta) * scale;
        out.samples[i][2] = std::sqrt((std::max)(0.0f, 1.0f - u1)) * scale;
    }
    return out;
}
} // namespace

PostProcessPass::PostProcessPass(IRHIDevice* device) : RenderPass(device) {
}

void PostProcessPass::Resize(uint32_t width, uint32_t height) {
    width = (std::max)(1u, width);
    height = (std::max)(1u, height);
    if (width == m_Width && height == m_Height)
        return;
    m_Width = width;
    m_Height = height;
    m_SSAOWidth = (std::max)(1u, static_cast<uint32_t>(m_Width * m_SSAOScale));
    m_SSAOHeight = (std::max)(1u, static_cast<uint32_t>(m_Height * m_SSAOScale));
    m_SceneColor.reset();
    m_SceneDepth.reset();
    m_SSAO.reset();
    m_SSAOBlur.reset();
    m_Composite.reset();
    m_SceneColorRtv.reset();
    m_SceneColorSrv.reset();
    m_SceneDepthDsv.reset();
    m_SceneDepthSrv.reset();
    m_SSAORtv.reset();
    m_SSAOSrv.reset();
    m_SSAOBlurRtv.reset();
    m_SSAOBlurSrv.reset();
    m_CompositeRtv.reset();
    m_CompositeSrv.reset();
    m_SceneColorState = m_SceneDepthState = m_SSAOState = m_SSAOBlurState = m_CompositeState =
        RHIResourceState::Undefined;
}

void PostProcessPass::SetSSAOEnabled(bool enabled) {
    if (m_SSAOEnabled == enabled)
        return;
    m_SSAOEnabled = enabled;
    m_SSAO.reset();
    m_SSAOBlur.reset();
    m_Noise.reset();
    m_SSAORtv.reset();
    m_SSAOSrv.reset();
    m_SSAOBlurRtv.reset();
    m_SSAOBlurSrv.reset();
    m_NoiseSrv.reset();
    m_SSAOState = RHIResourceState::Undefined;
    m_SSAOBlurState = RHIResourceState::Undefined;
}

void PostProcessPass::SetSSAOScale(float scale) {
    const float normalized = scale <= 0.75f ? 0.5f : 1.0f;
    if (std::abs(m_SSAOScale - normalized) < 0.001f)
        return;
    m_SSAOScale = normalized;
    m_SSAOWidth = (std::max)(1u, static_cast<uint32_t>(m_Width * m_SSAOScale));
    m_SSAOHeight = (std::max)(1u, static_cast<uint32_t>(m_Height * m_SSAOScale));
    m_SSAO.reset();
    m_SSAOBlur.reset();
    m_SSAORtv.reset();
    m_SSAOSrv.reset();
    m_SSAOBlurRtv.reset();
    m_SSAOBlurSrv.reset();
    m_SSAOState = RHIResourceState::Undefined;
    m_SSAOBlurState = RHIResourceState::Undefined;
}

bool PostProcessPass::EnsureResources() {
    auto* device = Device();
    if (!device)
        return false;
    auto resetResources = [&]() {
        m_SceneColor.reset();
        m_SceneDepth.reset();
        m_SSAO.reset();
        m_SSAOBlur.reset();
        m_Composite.reset();
        m_Noise.reset();
        m_SceneColorRtv.reset();
        m_SceneColorSrv.reset();
        m_SceneDepthDsv.reset();
        m_SceneDepthSrv.reset();
        m_SSAORtv.reset();
        m_SSAOSrv.reset();
        m_SSAOBlurRtv.reset();
        m_SSAOBlurSrv.reset();
        m_CompositeRtv.reset();
        m_CompositeSrv.reset();
        m_LinearClamp.reset();
        m_PointClamp.reset();
        m_NoiseSampler.reset();
        m_NoiseSrv.reset();
        m_SceneColorState = m_SceneDepthState = m_SSAOState = m_SSAOBlurState = m_CompositeState =
            RHIResourceState::Undefined;
    };
    auto resourcesComplete = [&]() {
        const bool baseComplete = m_SceneColor && m_SceneColorRtv && m_SceneColorSrv && m_SceneDepth &&
                                  m_SceneDepthDsv && m_SceneDepthSrv && m_Composite && m_CompositeRtv &&
                                  m_CompositeSrv && m_LinearClamp && m_PointClamp && m_FXAABackbufferPipeline &&
                                  m_FXAAOffscreenPipeline;
        if (!baseComplete)
            return false;
        if (!m_SSAOEnabled)
            return true;
        return m_SSAO && m_SSAORtv && m_SSAOSrv && m_SSAOBlur && m_SSAOBlurRtv && m_SSAOBlurSrv && m_NoiseSampler &&
               m_Noise && m_NoiseSrv && m_SSAOPipeline && m_BlurPipeline;
    };
    if (m_SceneColor) {
        if (!resourcesComplete()) {
            resetResources();
        } else {
            const bool changed = (m_FXAAHandle && m_FXAAHandle->version != m_FXAAVersion) ||
                                 (m_SSAOEnabled && m_SSAOHandle && m_SSAOHandle->version != m_SSAOVersion) ||
                                 (m_SSAOEnabled && m_BlurHandle && m_BlurHandle->version != m_BlurVersion);
            if (changed) {
                m_FXAAShader = m_FXAAHandle->shader;
                if (m_SSAOEnabled) {
                    m_SSAOShader = m_SSAOHandle->shader;
                    m_BlurShader = m_BlurHandle->shader;
                }
                GraphicsPipelineDesc pipeline;
                pipeline.depthStencil.depthTestEnable = false;
                pipeline.depthStencil.depthWriteEnable = false;
                pipeline.rasterizer.cullMode = RHICullMode::None;
                const RHIBackend backend = device->GetBackend();
                const RHIFormat backbufferFormat =
                    backend == RHIBackend::Metal || backend == RHIBackend::Vulkan ? RHIFormat::BGRA8UNorm
                                                                                 : RHIFormat::RGBA8UNorm;
                pipeline.shader = m_FXAAShader;
                pipeline.colorFormats = {backbufferFormat};
                m_FXAABackbufferPipeline = device->CreateGraphicsPipeline(pipeline);
                pipeline.colorFormats = {RHIFormat::RGBA16Float};
                m_FXAAOffscreenPipeline = device->CreateGraphicsPipeline(pipeline);
                if (m_SSAOEnabled) {
                    pipeline.shader = m_SSAOShader;
                    pipeline.colorFormats = {RHIFormat::R8UNorm};
                    pipeline.rasterizer.cullMode = RHICullMode::None;
                    m_SSAOPipeline = device->CreateGraphicsPipeline(pipeline);
                    pipeline.shader = m_BlurShader;
                    m_BlurPipeline = device->CreateGraphicsPipeline(pipeline);
                    m_SSAOVersion = m_SSAOHandle->version;
                    m_BlurVersion = m_BlurHandle->version;
                }
                m_FXAAVersion = m_FXAAHandle->version;
                m_LoggedCompositeBindingFailure = false;
            }
            return resourcesComplete();
        }
    }
    auto makeTexture = [&](const char* name, RHIFormat format, RHIResourceUsage usage,
                           std::shared_ptr<GpuTexture>& texture, std::shared_ptr<GpuTextureView>& output,
                           std::shared_ptr<GpuTextureView>& input, uint32_t width = 0, uint32_t height = 0) {
        RHITextureDesc desc;
        desc.width = width ? width : m_Width;
        desc.height = height ? height : m_Height;
        desc.format = format;
        desc.usage = usage;
        desc.debugName = name;
        texture = device->CreateTexture(desc);
        if (!texture) {
            Logger::Error("[PostProcessPass] Failed to create texture: ", name);
            return false;
        }
        RHITextureViewDesc outDesc;
        outDesc.usage = HasUsage(usage, RHIResourceUsage::DepthStencil) ? RHIResourceUsage::DepthStencil
                                                                        : RHIResourceUsage::RenderTarget;
        output = device->CreateTextureView(texture, outDesc);
        if (!output) {
            Logger::Error("[PostProcessPass] Failed to create output view: ", name);
            return false;
        }
        RHITextureViewDesc inDesc;
        inDesc.usage = RHIResourceUsage::ShaderResource;
        input = device->CreateTextureView(texture, inDesc);
        if (!input) {
            Logger::Error("[PostProcessPass] Failed to create shader-resource view: ", name);
            return false;
        }
        return true;
    };
    if (!makeTexture("SceneColor", RHIFormat::RGBA16Float,
                     RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource, m_SceneColor, m_SceneColorRtv,
                     m_SceneColorSrv) ||
        !makeTexture("SceneDepth", RHIFormat::D24S8, RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource,
                     m_SceneDepth, m_SceneDepthDsv, m_SceneDepthSrv) ||
        !makeTexture("Composite", RHIFormat::RGBA16Float,
                     RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource, m_Composite, m_CompositeRtv,
                     m_CompositeSrv)) {
        resetResources();
        return false;
    }
    if (m_SSAOEnabled &&
        (!makeTexture("SSAO", RHIFormat::R8UNorm, RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource,
                      m_SSAO, m_SSAORtv, m_SSAOSrv, m_SSAOWidth, m_SSAOHeight) ||
         !makeTexture("SSAOBlur", RHIFormat::R8UNorm, RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource,
                      m_SSAOBlur, m_SSAOBlurRtv, m_SSAOBlurSrv, m_SSAOWidth, m_SSAOHeight))) {
        resetResources();
        return false;
    }

    RHISamplerDesc linear;
    linear.addressU = linear.addressV = linear.addressW = RHIAddressMode::Clamp;
    m_LinearClamp = device->CreateSampler(linear);
    RHISamplerDesc point = linear;
    point.filter = RHIFilter::Point;
    m_PointClamp = device->CreateSampler(point);
    if (m_SSAOEnabled) {
        RHISamplerDesc noise;
        noise.filter = RHIFilter::Point;
        noise.addressU = noise.addressV = noise.addressW = RHIAddressMode::Repeat;
        m_NoiseSampler = device->CreateSampler(noise);
    }
    if (!m_LinearClamp || !m_PointClamp || (m_SSAOEnabled && !m_NoiseSampler)) {
        Logger::Error("[PostProcessPass] Failed to create post-process samplers");
        resetResources();
        return false;
    }
    if (m_SSAOEnabled) {
        uint8_t pixels[4 * 4 * 4];
        std::mt19937 rng(17);
        std::uniform_int_distribution<int> d(0, 255);
        for (size_t i = 0; i < sizeof(pixels); i += 4) {
            pixels[i] = static_cast<uint8_t>(d(rng));
            pixels[i + 1] = static_cast<uint8_t>(d(rng));
            pixels[i + 2] = 128;
            pixels[i + 3] = 255;
        }
        m_Noise = device->UploadTexture2D(pixels, 4, 4);
        if (!m_Noise) {
            Logger::Error("[PostProcessPass] Failed to upload SSAO noise texture");
            resetResources();
            return false;
        }
        RHITextureViewDesc noiseView;
        noiseView.usage = RHIResourceUsage::ShaderResource;
        m_NoiseSrv = device->CreateTextureView(m_Noise, noiseView);
        if (!m_NoiseSrv) {
            Logger::Error("[PostProcessPass] Failed to create SSAO noise texture view");
            resetResources();
            return false;
        }
    }

    m_FXAAHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kPostProcessFXAA, nullptr, 0);
    m_FXAAShader = m_FXAAHandle ? m_FXAAHandle->shader : nullptr;
    if (m_SSAOEnabled) {
        m_SSAOHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kPostProcessSSAO, nullptr, 0);
        m_BlurHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kPostProcessSSAOBlur, nullptr, 0);
        m_SSAOShader = m_SSAOHandle ? m_SSAOHandle->shader : nullptr;
        m_BlurShader = m_BlurHandle ? m_BlurHandle->shader : nullptr;
    }
    if (!m_FXAAShader || (m_SSAOEnabled && (!m_SSAOShader || !m_BlurShader))) {
        Logger::Error("[PostProcessPass] Failed to load post-process shaders");
        resetResources();
        return false;
    }
    m_FXAAVersion = m_FXAAHandle ? m_FXAAHandle->version : 0;
    m_SSAOVersion = m_SSAOHandle ? m_SSAOHandle->version : 0;
    m_BlurVersion = m_BlurHandle ? m_BlurHandle->version : 0;
    GraphicsPipelineDesc pipeline;
    pipeline.depthStencil.depthTestEnable = false;
    pipeline.depthStencil.depthWriteEnable = false;
    pipeline.rasterizer.cullMode = RHICullMode::None;
    const RHIBackend backend = device->GetBackend();
    const RHIFormat backbufferFormat =
        backend == RHIBackend::Metal || backend == RHIBackend::Vulkan ? RHIFormat::BGRA8UNorm
                                                                     : RHIFormat::RGBA8UNorm;
    pipeline.shader = m_FXAAShader;
    pipeline.colorFormats = {backbufferFormat};
    m_FXAABackbufferPipeline = device->CreateGraphicsPipeline(pipeline);
    if (!m_FXAABackbufferPipeline)
        Logger::Error("[PostProcessPass] Failed to create FXAA backbuffer pipeline");
    pipeline.colorFormats = {RHIFormat::RGBA16Float};
    m_FXAAOffscreenPipeline = device->CreateGraphicsPipeline(pipeline);
    if (!m_FXAAOffscreenPipeline)
        Logger::Error("[PostProcessPass] Failed to create FXAA offscreen pipeline");
    if (m_SSAOEnabled) {
        pipeline.shader = m_SSAOShader;
        pipeline.colorFormats = {RHIFormat::R8UNorm};
        pipeline.rasterizer.cullMode = RHICullMode::None;
        m_SSAOPipeline = device->CreateGraphicsPipeline(pipeline);
        if (!m_SSAOPipeline)
            Logger::Error("[PostProcessPass] Failed to create SSAO pipeline");
        pipeline.shader = m_BlurShader;
        pipeline.rasterizer.cullMode = RHICullMode::None;
        m_BlurPipeline = device->CreateGraphicsPipeline(pipeline);
        if (!m_BlurPipeline)
            Logger::Error("[PostProcessPass] Failed to create SSAO blur pipeline");
    }
    if (!resourcesComplete()) {
        resetResources();
        return false;
    }
    return true;
}

bool PostProcessPass::PrepareGraphResources() {
    if (!EnsureResources()) {
        Logger::Error("[PostProcessPass] Failed to create backend-independent resources");
        return false;
    }
    m_SceneRendering = false;
    return true;
}

PostProcessPass::GraphResources PostProcessPass::GetGraphResources() const {
    GraphResources out;
    out.sceneColor = m_SceneColor;
    out.sceneColorRtv = m_SceneColorRtv;
    out.sceneColorSrv = m_SceneColorSrv;
    out.sceneDepth = m_SceneDepth;
    out.sceneDepthDsv = m_SceneDepthDsv;
    out.sceneDepthSrv = m_SceneDepthSrv;
    out.ssao = m_SSAO;
    out.ssaoRtv = m_SSAORtv;
    out.ssaoBlur = m_SSAOBlur;
    out.ssaoBlurRtv = m_SSAOBlurRtv;
    out.composite = m_Composite;
    out.compositeRtv = m_CompositeRtv;
    out.sceneColorState = m_SceneColorState;
    out.sceneDepthState = m_SceneDepthState;
    out.ssaoState = m_SSAOState;
    out.ssaoBlurState = m_SSAOBlurState;
    out.compositeState = m_CompositeState;
    return out;
}

void PostProcessPass::MarkGraphResourcesShaderResource(bool compositeWritten) {
    m_SceneRendering = false;
    m_SceneColorState = RHIResourceState::ShaderResource;
    m_SceneDepthState = RHIResourceState::ShaderResource;
    if (m_SSAO)
        m_SSAOState = RHIResourceState::ShaderResource;
    if (m_SSAOBlur)
        m_SSAOBlurState = RHIResourceState::ShaderResource;
    if (compositeWritten) {
        m_CompositeState = RHIResourceState::ShaderResource;
    }
}

void PostProcessPass::BeginOffscreen(GpuCommandList& commands) {
    if (!EnsureResources()) {
        Logger::Error("[PostProcessPass] Failed to create backend-independent resources");
        return;
    }
    commands.Transition(m_SceneColor.get(), m_SceneColorState, RHIResourceState::RenderTarget);
    commands.Transition(m_SceneDepth.get(), m_SceneDepthState, RHIResourceState::DepthWrite);
    m_SceneColorState = RHIResourceState::RenderTarget;
    m_SceneDepthState = RHIResourceState::DepthWrite;
    RenderingAttachment color;
    color.view = m_SceneColorRtv.get();
    color.loadOp = RHILoadOp::Clear;
    RenderingAttachment depth;
    depth.view = m_SceneDepthDsv.get();
    depth.loadOp = RHILoadOp::Clear;
    RenderingInfo info{&color, 1, &depth, m_Width, m_Height};
    commands.BeginRendering(info);
    m_SceneRendering = true;
}

void PostProcessPass::CloseSceneRendering(GpuCommandList& commands) {
    if (!m_SceneRendering)
        return;
    commands.EndRendering();
    m_SceneRendering = false;
    commands.Transition(m_SceneColor.get(), m_SceneColorState, RHIResourceState::ShaderResource);
    commands.Transition(m_SceneDepth.get(), m_SceneDepthState, RHIResourceState::ShaderResource);
    m_SceneColorState = m_SceneDepthState = RHIResourceState::ShaderResource;
}

void PostProcessPass::DrawFullscreen(GpuCommandList& commands, GpuGraphicsPipeline& pipeline, GpuBindGroup& bindings,
                                     GpuTextureView& target, RHIResourceState& targetState, const ClearColor& clear) {
    auto texture = target.texture;
    commands.Transition(texture.get(), targetState, RHIResourceState::RenderTarget);
    targetState = RHIResourceState::RenderTarget;
    RenderingAttachment color;
    color.view = &target;
    color.loadOp = RHILoadOp::Clear;
    color.clearColor = clear;
    RenderingInfo info{&color, 1, nullptr, m_Width, m_Height};
    commands.BeginRendering(info);
    commands.SetGraphicsPipeline(&pipeline);
    commands.SetBindGroup(0, &bindings);
    commands.Draw(3);
    commands.EndRendering();
    commands.Transition(texture.get(), targetState, RHIResourceState::ShaderResource);
    targetState = RHIResourceState::ShaderResource;
}

void PostProcessPass::RenderSSAO(GpuCommandList& commands, const Scene& scene, const Camera& camera) {
    CloseSceneRendering(commands);
    if (!m_SSAOPipeline)
        return;
    DrawSSAOOcclusion(commands, scene, camera);
    DrawSSAOBlurHorizontal(commands);
    DrawSSAOBlurVertical(commands);
}

void PostProcessPass::DrawSSAOOcclusion(GpuCommandList& commands, const Scene& scene, const Camera& camera) {
    if (!m_SSAOEnabled || !m_SSAOPipeline || !m_SSAOShader)
        return;
    auto ssaoBindings = Device()->CreateBindGroup(m_SSAOShader);
    if (!ssaoBindings)
        return;
    SSAOConstants constants = BuildSSAOConstants(scene, camera, m_SSAOWidth, m_SSAOHeight);
    ssaoBindings->SetConstants("SSAOParams", &constants, sizeof(constants));
    ssaoBindings->SetTexture("g_DepthTex", m_SceneDepthSrv);
    ssaoBindings->SetSampler("g_DepthSampler", m_PointClamp);
    ssaoBindings->SetTexture("g_NoiseTex", m_NoiseSrv);
    ssaoBindings->SetSampler("g_NoiseSampler", m_NoiseSampler);
    commands.SetGraphicsPipeline(m_SSAOPipeline.get());
    commands.SetBindGroup(0, ssaoBindings.get());
    commands.Draw(3);
}

void PostProcessPass::DrawSSAOBlurHorizontal(GpuCommandList& commands) {
    if (!m_SSAOEnabled || !m_BlurPipeline || !m_BlurShader)
        return;
    SSAOBlurConstants blur{};
    blur.texelSize[0] = 1.0f / m_SSAOWidth;
    blur.texelSize[2] = 1.0f / m_SSAOHeight;
    auto horizontal = Device()->CreateBindGroup(m_BlurShader);
    if (!horizontal)
        return;
    horizontal->SetConstants("BlurParams", &blur, sizeof(blur));
    horizontal->SetTexture("g_SSAOInput", m_SSAOSrv);
    horizontal->SetSampler("g_SSAOSampler", m_PointClamp);
    commands.SetGraphicsPipeline(m_BlurPipeline.get());
    commands.SetBindGroup(0, horizontal.get());
    commands.Draw(3);
}

void PostProcessPass::DrawSSAOBlurVertical(GpuCommandList& commands) {
    if (!m_SSAOEnabled || !m_BlurPipeline || !m_BlurShader)
        return;
    SSAOBlurConstants blur{};
    blur.texelSize[0] = 1.0f / m_SSAOWidth;
    blur.texelSize[2] = 1.0f / m_SSAOHeight;
    blur.texelSize[1] = 1.0f;
    auto vertical = Device()->CreateBindGroup(m_BlurShader);
    if (!vertical)
        return;
    vertical->SetConstants("BlurParams", &blur, sizeof(blur));
    vertical->SetTexture("g_SSAOInput", m_SSAOBlurSrv);
    vertical->SetSampler("g_SSAOSampler", m_PointClamp);
    commands.SetGraphicsPipeline(m_BlurPipeline.get());
    commands.SetBindGroup(0, vertical.get());
    commands.Draw(3);
}

void PostProcessPass::RenderBloom(GpuCommandList&, const Scene&) {
}

void PostProcessPass::EndOffscreenAndComposite(GpuCommandList& commands, const Scene& scene,
                                               GpuTextureView* backBufferView) {
    CloseSceneRendering(commands);
    if (!m_FXAAShader)
        return;
    if (m_CompositeToBackbuffer) {
        DrawCompositeToBackbuffer(commands, scene, backBufferView);
    } else {
        auto bindings = Device()->CreateBindGroup(m_FXAAShader);
        PostProcessConstants constants = CollectPostProcessParams(scene, m_Width, m_Height);
        constants.params3[0] = m_InputPreprocessed ? 1.0f : 0.0f;
        if (!bindings || !bindings->SetConstants("PostProcessParams", &constants, sizeof(constants))) {
            if (!m_LoggedCompositeBindingFailure) {
                Logger::Error("[PostProcessPass] PostProcessParams does not match shader reflection");
                m_LoggedCompositeBindingFailure = true;
            }
            return;
        }
        bindings->SetTexture("g_SceneColor", m_SceneColorSrv);
        bindings->SetSampler("g_Sampler", m_LinearClamp);
        bindings->SetTexture("g_SSAOMap", m_SSAOSrv ? m_SSAOSrv : m_SceneColorSrv);
        bindings->SetSampler("g_SSAOSampler", m_PointClamp);
        DrawFullscreen(commands, *m_FXAAOffscreenPipeline, *bindings, *m_CompositeRtv, m_CompositeState, {0, 0, 0, 1});
    }
}

void PostProcessPass::DrawCompositeOffscreen(GpuCommandList& commands, const Scene& scene) {
    DrawCompositeOffscreen(commands, scene, m_SceneColorSrv.get());
}

void PostProcessPass::DrawCompositeOffscreen(GpuCommandList& commands, const Scene& scene,
                                             GpuTextureView* sceneColorView) {
    if (!m_FXAAShader || !m_FXAAOffscreenPipeline)
        return;
    auto bindings = Device()->CreateBindGroup(m_FXAAShader);
    if (!bindings || !sceneColorView)
        return;
    PostProcessConstants constants = CollectPostProcessParams(scene, m_Width, m_Height);
    constants.params3[0] = m_InputPreprocessed ? 1.0f : 0.0f;
    if (!bindings->SetConstants("PostProcessParams", &constants, sizeof(constants))) {
        if (!m_LoggedCompositeBindingFailure) {
            Logger::Error("[PostProcessPass] PostProcessParams does not match shader reflection");
            m_LoggedCompositeBindingFailure = true;
        }
        return;
    }
    bindings->SetTexture("g_SceneColor", std::shared_ptr<GpuTextureView>(sceneColorView, [](GpuTextureView*) {}));
    bindings->SetSampler("g_Sampler", m_LinearClamp);
    bindings->SetTexture(
        "g_SSAOMap", m_SSAOSrv ? m_SSAOSrv : std::shared_ptr<GpuTextureView>(sceneColorView, [](GpuTextureView*) {}));
    bindings->SetSampler("g_SSAOSampler", m_PointClamp);
    commands.SetGraphicsPipeline(m_FXAAOffscreenPipeline.get());
    commands.SetBindGroup(0, bindings.get());
    commands.Draw(3);
}

void PostProcessPass::DrawCompositeToBackbuffer(GpuCommandList& commands, const Scene& scene,
                                                GpuTextureView* backBufferView) {
    if (!m_FXAAShader || !m_FXAABackbufferPipeline || !backBufferView)
        return;
    RenderingAttachment color;
    color.view = backBufferView;
    color.loadOp = RHILoadOp::Clear;
    RenderingInfo info{&color, 1, nullptr, m_Width, m_Height};
    commands.BeginRendering(info);
    DrawCompositeToCurrentTarget(commands, scene);
    commands.EndRendering();
}

void PostProcessPass::DrawCompositeToCurrentTarget(GpuCommandList& commands, const Scene& scene) {
    DrawCompositeToCurrentTarget(commands, scene, m_SceneColorSrv.get());
}

void PostProcessPass::DrawCompositeToCurrentTarget(GpuCommandList& commands, const Scene& scene,
                                                   GpuTextureView* sceneColorView) {
    if (!m_FXAAShader || !m_FXAABackbufferPipeline)
        return;
    auto bindings = Device()->CreateBindGroup(m_FXAAShader);
    if (!bindings || !sceneColorView)
        return;
    PostProcessConstants constants = CollectPostProcessParams(scene, m_Width, m_Height);
    constants.params3[0] = m_InputPreprocessed ? 1.0f : 0.0f;
    if (!bindings->SetConstants("PostProcessParams", &constants, sizeof(constants))) {
        if (!m_LoggedCompositeBindingFailure) {
            Logger::Error("[PostProcessPass] PostProcessParams does not match shader reflection");
            m_LoggedCompositeBindingFailure = true;
        }
        return;
    }
    bindings->SetTexture("g_SceneColor", std::shared_ptr<GpuTextureView>(sceneColorView, [](GpuTextureView*) {}));
    bindings->SetSampler("g_Sampler", m_LinearClamp);
    bindings->SetTexture(
        "g_SSAOMap", m_SSAOSrv ? m_SSAOSrv : std::shared_ptr<GpuTextureView>(sceneColorView, [](GpuTextureView*) {}));
    bindings->SetSampler("g_SSAOSampler", m_PointClamp);
    commands.SetGraphicsPipeline(m_FXAABackbufferPipeline.get());
    commands.SetBindGroup(0, bindings.get());
    commands.Draw(3);
}

void PostProcessPass::Execute(GpuCommandList&, const Scene&, const Camera&) {
}
