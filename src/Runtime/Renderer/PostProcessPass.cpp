#include "Renderer/PostProcessPass.h"

#include "Camera/Camera.h"
#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
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
};
struct SSAOConstants {
    float projection[16];
    float invProjection[16];
    float screenSize[4];
    float ssaoParams[4];
    float samples[64][4];
};
struct SSAOBlurConstants { float texelSize[4]; };

PostProcessConstants CollectPostProcessParams(const Scene& scene, uint32_t width, uint32_t height) {
    PostProcessConstants out{};
    out.params[0] = 1.0f; out.params[1] = 2.2f; out.params[2] = 1.0f;
    out.params2[0] = 1.0f; out.params2[1] = 1.0f;
    out.params2[2] = 1.0f; out.params2[3] = 1.0f;
    out.screenSize[0] = 1.0f / width; out.screenSize[1] = 1.0f / height;
    out.screenSize[2] = static_cast<float>(width); out.screenSize[3] = static_cast<float>(height);
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive()) return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled()) return;
        out.params[0] = post->GetExposure(); out.params[1] = post->GetGamma();
        out.params[2] = post->IsToneMappingEnabled() ? 1.0f : 0.0f;
        out.params[3] = post->GetVignette();
        out.params2[0] = post->GetSaturation(); out.params2[1] = post->GetContrast();
        out.params2[2] = post->GetAntiAliasingStrength() > 0.0f ? 1.0f : 0.0f;
        out.params2[3] = post->GetAntiAliasingStrength(); found = true;
    });
    return out;
}

SSAOConstants BuildSSAOConstants(const Scene& scene, const Camera& camera,
                                 uint32_t width, uint32_t height) {
    SSAOConstants out{};
    std::memcpy(out.projection, camera.GetProj().Data(), sizeof(out.projection));
    Mat4 inverse; Mat4Invert(camera.GetProj(), inverse);
    std::memcpy(out.invProjection, inverse.Data(), sizeof(out.invProjection));
    out.screenSize[0] = 1.0f / width; out.screenSize[1] = 1.0f / height;
    out.screenSize[2] = static_cast<float>(width); out.screenSize[3] = static_cast<float>(height);
    out.ssaoParams[0] = 1.2f; out.ssaoParams[1] = 0.025f; out.ssaoParams[2] = 1.5f;
    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive()) return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled()) return;
        out.ssaoParams[0] = post->GetSSAORadius(); out.ssaoParams[1] = post->GetSSAOBias();
        out.ssaoParams[2] = post->GetSSAOPower(); out.ssaoParams[3] = post->GetSSAOIntensity();
        found = true;
    });
    std::mt19937 rng(42); std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (uint32_t i = 0; i < 16; ++i) {
        const float u1 = dist(rng), u2 = dist(rng), r = std::sqrt(u1);
        const float theta = 6.2831853f * u2, scale = 0.1f + (i / 16.0f) * (i / 16.0f) * 0.9f;
        out.samples[i][0] = r * std::cos(theta) * scale;
        out.samples[i][1] = r * std::sin(theta) * scale;
        out.samples[i][2] = std::sqrt((std::max)(0.0f, 1.0f - u1)) * scale;
    }
    return out;
}
}

PostProcessPass::PostProcessPass(IRenderContext* context) : RenderPass(context) {}

void PostProcessPass::Resize(uint32_t width, uint32_t height) {
    width = (std::max)(1u, width); height = (std::max)(1u, height);
    if (width == m_Width && height == m_Height) return;
    m_Width = width; m_Height = height;
    m_SceneColor.reset(); m_SceneDepth.reset(); m_SSAO.reset();
    m_SSAOBlur.reset(); m_Composite.reset();
    m_SceneColorRtv.reset(); m_SceneColorSrv.reset();
    m_SceneDepthDsv.reset(); m_SceneDepthSrv.reset();
    m_SSAORtv.reset(); m_SSAOSrv.reset(); m_SSAOBlurRtv.reset(); m_SSAOBlurSrv.reset();
    m_CompositeRtv.reset(); m_CompositeSrv.reset();
    m_SceneColorState = m_SceneDepthState = m_SSAOState =
        m_SSAOBlurState = m_CompositeState = RHIResourceState::Undefined;
}

bool PostProcessPass::EnsureResources() {
    auto* device = Context(); if (!device) return false;
    if (m_SceneColor) {
        const bool changed = (m_FXAAHandle && m_FXAAHandle->version != m_FXAAVersion) ||
            (m_SSAOHandle && m_SSAOHandle->version != m_SSAOVersion) ||
            (m_BlurHandle && m_BlurHandle->version != m_BlurVersion);
        if (changed) {
            m_FXAAShader = m_FXAAHandle->shader; m_SSAOShader = m_SSAOHandle->shader;
            m_BlurShader = m_BlurHandle->shader;
            GraphicsPipelineDesc pipeline;
            pipeline.depthStencil.depthTestEnable = false;
            pipeline.depthStencil.depthWriteEnable = false;
            pipeline.rasterizer.cullMode = RHICullMode::None;
            pipeline.shader = m_FXAAShader; pipeline.colorFormats = {RHIFormat::RGBA8UNorm};
            m_FXAAOffscreenPipeline = device->CreateGraphicsPipeline(pipeline);
            pipeline.colorFormats = {RHIFormat::RGBA16Float};
            m_FXAAOffscreenPipeline = device->CreateGraphicsPipeline(pipeline);
            pipeline.shader = m_SSAOShader; pipeline.colorFormats = {RHIFormat::R8UNorm};
            pipeline.rasterizer.cullMode = RHICullMode::None;
            m_SSAOPipeline = device->CreateGraphicsPipeline(pipeline);
            pipeline.shader = m_BlurShader; m_BlurPipeline = device->CreateGraphicsPipeline(pipeline);
            m_FXAAVersion = m_FXAAHandle->version;
            m_SSAOVersion = m_SSAOHandle->version;
            m_BlurVersion = m_BlurHandle->version;
        }
        return m_FXAABackbufferPipeline && m_FXAAOffscreenPipeline && m_SSAOPipeline && m_BlurPipeline;
    }
    auto makeTexture = [&](const char* name, RHIFormat format, RHIResourceUsage usage,
                           std::shared_ptr<GpuTexture>& texture,
                           std::shared_ptr<GpuTextureView>& output,
                           std::shared_ptr<GpuTextureView>& input) {
        RHITextureDesc desc; desc.width = m_Width; desc.height = m_Height;
        desc.format = format; desc.usage = usage; desc.debugName = name;
        texture = device->CreateTexture(desc);
        RHITextureViewDesc outDesc;
        outDesc.usage = HasUsage(usage, RHIResourceUsage::DepthStencil)
            ? RHIResourceUsage::DepthStencil : RHIResourceUsage::RenderTarget;
        output = device->CreateTextureView(texture, outDesc);
        RHITextureViewDesc inDesc; inDesc.usage = RHIResourceUsage::ShaderResource;
        input = device->CreateTextureView(texture, inDesc);
        return texture && output && input;
    };
    if (!makeTexture("SceneColor", RHIFormat::RGBA16Float,
            RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource,
            m_SceneColor, m_SceneColorRtv, m_SceneColorSrv) ||
        !makeTexture("SceneDepth", RHIFormat::D24S8,
            RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource,
            m_SceneDepth, m_SceneDepthDsv, m_SceneDepthSrv) ||
        !makeTexture("SSAO", RHIFormat::R8UNorm,
            RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource,
            m_SSAO, m_SSAORtv, m_SSAOSrv) ||
        !makeTexture("SSAOBlur", RHIFormat::R8UNorm,
            RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource,
            m_SSAOBlur, m_SSAOBlurRtv, m_SSAOBlurSrv) ||
        !makeTexture("Composite", RHIFormat::RGBA16Float,
            RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource,
            m_Composite, m_CompositeRtv, m_CompositeSrv)) return false;

    RHISamplerDesc linear; linear.addressU = linear.addressV = linear.addressW = RHIAddressMode::Clamp;
    m_LinearClamp = device->CreateSampler(linear);
    RHISamplerDesc point = linear; point.filter = RHIFilter::Point;
    m_PointClamp = device->CreateSampler(point);
    RHISamplerDesc noise; noise.filter = RHIFilter::Point;
    noise.addressU = noise.addressV = noise.addressW = RHIAddressMode::Repeat;
    m_NoiseSampler = device->CreateSampler(noise);
    uint8_t pixels[4 * 4 * 4]; std::mt19937 rng(17); std::uniform_int_distribution<int> d(0, 255);
    for (size_t i = 0; i < sizeof(pixels); i += 4) {
        pixels[i] = static_cast<uint8_t>(d(rng)); pixels[i + 1] = static_cast<uint8_t>(d(rng));
        pixels[i + 2] = 128; pixels[i + 3] = 255;
    }
    m_Noise = device->UploadTexture2D(pixels, 4, 4);
    RHITextureViewDesc noiseView; noiseView.usage = RHIResourceUsage::ShaderResource;
    m_NoiseSrv = device->CreateTextureView(m_Noise, noiseView);

    m_FXAAHandle = ShaderManager::Get().GetOrCreate(
        "Content/Engine/Shaders/PostProcessFXAA.shader", nullptr, 0);
    m_SSAOHandle = ShaderManager::Get().GetOrCreate(
        "Content/Engine/Shaders/PostProcessSSAO.shader", nullptr, 0);
    m_BlurHandle = ShaderManager::Get().GetOrCreate(
        "Content/Engine/Shaders/PostProcessSSAOBlur.shader", nullptr, 0);
    m_FXAAShader = m_FXAAHandle ? m_FXAAHandle->shader : nullptr;
    m_SSAOShader = m_SSAOHandle ? m_SSAOHandle->shader : nullptr;
    m_BlurShader = m_BlurHandle ? m_BlurHandle->shader : nullptr;
    m_FXAAVersion = m_FXAAHandle ? m_FXAAHandle->version : 0;
    m_SSAOVersion = m_SSAOHandle ? m_SSAOHandle->version : 0;
    m_BlurVersion = m_BlurHandle ? m_BlurHandle->version : 0;
    GraphicsPipelineDesc pipeline;
    pipeline.depthStencil.depthTestEnable = false;
    pipeline.depthStencil.depthWriteEnable = false;
    pipeline.rasterizer.cullMode = RHICullMode::None;
    pipeline.shader = m_FXAAShader; pipeline.colorFormats = {RHIFormat::RGBA8UNorm};
    m_FXAABackbufferPipeline = device->CreateGraphicsPipeline(pipeline);
    pipeline.colorFormats = {RHIFormat::RGBA16Float};
    m_FXAAOffscreenPipeline = device->CreateGraphicsPipeline(pipeline);
    pipeline.shader = m_SSAOShader; pipeline.colorFormats = {RHIFormat::R8UNorm};
    pipeline.rasterizer.cullMode = RHICullMode::None;
    m_SSAOPipeline = device->CreateGraphicsPipeline(pipeline);
    pipeline.shader = m_BlurShader;
    pipeline.rasterizer.cullMode = RHICullMode::None;
    m_BlurPipeline = device->CreateGraphicsPipeline(pipeline);
    return m_LinearClamp && m_PointClamp && m_NoiseSampler && m_NoiseSrv &&
           m_FXAABackbufferPipeline && m_FXAAOffscreenPipeline && m_SSAOPipeline && m_BlurPipeline;
}

void PostProcessPass::BeginOffscreen() {
    if (!EnsureResources()) {
        Logger::Error("[PostProcessPass] Failed to create backend-independent resources");
        return;
    }
    auto* commands = Context()->GetGraphicsCommandList(); if (!commands) return;
    commands->Transition(m_SceneColor.get(), m_SceneColorState, RHIResourceState::RenderTarget);
    commands->Transition(m_SceneDepth.get(), m_SceneDepthState, RHIResourceState::DepthWrite);
    m_SceneColorState = RHIResourceState::RenderTarget;
    m_SceneDepthState = RHIResourceState::DepthWrite;
    RenderingAttachment color; color.view = m_SceneColorRtv.get(); color.loadOp = RHILoadOp::Clear;
    RenderingAttachment depth; depth.view = m_SceneDepthDsv.get(); depth.loadOp = RHILoadOp::Clear;
    RenderingInfo info{&color, 1, &depth, m_Width, m_Height};
    commands->BeginRendering(info); m_SceneRendering = true;
}

void PostProcessPass::CloseSceneRendering() {
    if (!m_SceneRendering) return;
    auto* commands = Context()->GetGraphicsCommandList(); if (!commands) return;
    commands->EndRendering(); m_SceneRendering = false;
    commands->Transition(m_SceneColor.get(), m_SceneColorState, RHIResourceState::ShaderResource);
    commands->Transition(m_SceneDepth.get(), m_SceneDepthState, RHIResourceState::ShaderResource);
    m_SceneColorState = m_SceneDepthState = RHIResourceState::ShaderResource;
}

void PostProcessPass::DrawFullscreen(GpuCommandList& commands, GpuGraphicsPipeline& pipeline,
                                     GpuBindGroup& bindings, GpuTextureView& target,
                                     RHIResourceState& targetState, const ClearColor& clear) {
    auto texture = target.texture;
    commands.Transition(texture.get(), targetState, RHIResourceState::RenderTarget);
    targetState = RHIResourceState::RenderTarget;
    RenderingAttachment color; color.view = &target; color.loadOp = RHILoadOp::Clear; color.clearColor = clear;
    RenderingInfo info{&color, 1, nullptr, m_Width, m_Height};
    commands.BeginRendering(info); commands.SetGraphicsPipeline(&pipeline);
    commands.SetBindGroup(0, &bindings); commands.Draw(3); commands.EndRendering();
    commands.Transition(texture.get(), targetState, RHIResourceState::ShaderResource);
    targetState = RHIResourceState::ShaderResource;
}

void PostProcessPass::RenderSSAO(const Scene& scene, const Camera& camera) {
    CloseSceneRendering(); if (!m_SSAOPipeline) return;
    auto* commands = Context()->GetGraphicsCommandList(); if (!commands) return;
    auto ssaoBindings = Context()->CreateBindGroup(m_SSAOShader);
    SSAOConstants constants = BuildSSAOConstants(scene, camera, m_Width, m_Height);
    ssaoBindings->SetConstants("SSAOParams", &constants, sizeof(constants));
    ssaoBindings->SetTexture("g_DepthTex", m_SceneDepthSrv);
    ssaoBindings->SetSampler("g_DepthSampler", m_PointClamp);
    ssaoBindings->SetTexture("g_NoiseTex", m_NoiseSrv);
    ssaoBindings->SetSampler("g_NoiseSampler", m_NoiseSampler);
    DrawFullscreen(*commands, *m_SSAOPipeline, *ssaoBindings, *m_SSAORtv,
                   m_SSAOState, {1, 1, 1, 1});
    SSAOBlurConstants blur{};
    blur.texelSize[0] = 1.0f / m_Width; blur.texelSize[2] = 1.0f / m_Height;
    auto horizontal = Context()->CreateBindGroup(m_BlurShader);
    horizontal->SetConstants("BlurParams", &blur, sizeof(blur));
    horizontal->SetTexture("g_SSAOInput", m_SSAOSrv);
    horizontal->SetSampler("g_SSAOSampler", m_PointClamp);
    DrawFullscreen(*commands, *m_BlurPipeline, *horizontal, *m_SSAOBlurRtv,
                   m_SSAOBlurState, {1, 1, 1, 1});
    blur.texelSize[1] = 1.0f;
    auto vertical = Context()->CreateBindGroup(m_BlurShader);
    vertical->SetConstants("BlurParams", &blur, sizeof(blur));
    vertical->SetTexture("g_SSAOInput", m_SSAOBlurSrv);
    vertical->SetSampler("g_SSAOSampler", m_PointClamp);
    DrawFullscreen(*commands, *m_BlurPipeline, *vertical, *m_SSAORtv,
                   m_SSAOState, {1, 1, 1, 1});
}

void PostProcessPass::RenderBloom(const Scene&) {}

void PostProcessPass::EndOffscreenAndComposite(const Scene& scene) {
    CloseSceneRendering(); if (!m_FXAAShader) return;
    auto* commands = Context()->GetGraphicsCommandList(); if (!commands) return;
    auto bindings = Context()->CreateBindGroup(m_FXAAShader);
    PostProcessConstants constants = CollectPostProcessParams(scene, m_Width, m_Height);
    bindings->SetConstants("PostProcessParams", &constants, sizeof(constants));
    bindings->SetTexture("g_SceneColor", m_SceneColorSrv);
    bindings->SetSampler("g_Sampler", m_LinearClamp);
    bindings->SetTexture("g_SSAOMap", m_SSAOSrv ? m_SSAOSrv : m_SceneColorSrv);
    bindings->SetSampler("g_SSAOSampler", m_PointClamp);
    if (m_CompositeToBackbuffer) {
        auto* target = Context()->GetCurrentBackBufferView();
        if (!target) return;
        RenderingAttachment color; color.view = target; color.loadOp = RHILoadOp::Clear;
        RenderingInfo info{&color, 1, nullptr, m_Width, m_Height};
        commands->BeginRendering(info);
        commands->SetGraphicsPipeline(m_FXAABackbufferPipeline.get());
        commands->SetBindGroup(0, bindings.get()); commands->Draw(3); commands->EndRendering();
    } else {
        DrawFullscreen(*commands, *m_FXAAOffscreenPipeline, *bindings, *m_CompositeRtv,
                       m_CompositeState, {0, 0, 0, 1});
    }
}

void PostProcessPass::Execute(const Scene&, const Camera&) {}
