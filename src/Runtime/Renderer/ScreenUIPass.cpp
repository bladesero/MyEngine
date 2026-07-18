#include "Renderer/ScreenUIPass.h"

#include "Core/Logger.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/RHI/GpuPipeline.h"
#include "Renderer/RHI/VertexLayout.h"
#include "Renderer/ShaderManager.h"

#include <cstddef>
#include <cstdint>

namespace {

struct UIScreenConstants {
    float invSize[2];
    float translation[2];
};

const char* kUIShader = R"(
cbuffer UIScreenConstants : register(b0)
{
    float2 u_InvSize;
    float2 u_Padding;
};
Texture2D u_Texture : register(t0);
SamplerState u_Sampler : register(s0);

struct VSIn
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(VSIn input)
{
    VSOut output;
    float2 p = input.position + u_Padding;
    output.position = float4(p.x * u_InvSize.x * 2.0f - 1.0f,
                             1.0f - p.y * u_InvSize.y * 2.0f,
                             0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOut input) : SV_Target
{
    return u_Texture.Sample(u_Sampler, input.uv) * input.color;
}
)";

const char* kUIShaderMetal = R"(
#include <metal_stdlib>
using namespace metal;

struct UIScreenConstants {
    float2 u_InvSize;
    float2 u_Padding;
};

struct VSIn {
    float2 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
    float4 color    [[attribute(2)]];
};

struct VSOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex VSOut VSMain(VSIn input [[stage_in]],
                    constant UIScreenConstants& UIScreenConstants [[buffer(0)]])
{
    VSOut output;
    float2 p = input.position + UIScreenConstants.u_Padding;
    output.position = float4(p.x * UIScreenConstants.u_InvSize.x * 2.0 - 1.0,
                             1.0 - p.y * UIScreenConstants.u_InvSize.y * 2.0,
                             0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

fragment float4 PSMain(VSOut input [[stage_in]],
                       texture2d<float> u_Texture [[texture(0)]])
{
    constexpr sampler u_Sampler(filter::linear, address::clamp_to_edge);
    return u_Texture.sample(u_Sampler, input.uv) * input.color;
}
)";

} // namespace

ScreenUIPass::ScreenUIPass(IRHIDevice* device) : RenderPass(device) {
}

void ScreenUIPass::Resize(uint32_t width, uint32_t height) {
    m_Width = width > 0 ? width : 1;
    m_Height = height > 0 ? height : 1;
}

GpuShader* ScreenUIPass::GetOrCreateShader() {
    if (m_Shader || m_ShaderCreationAttempted || !Device())
        return m_Shader.get();
    m_ShaderCreationAttempted = true;
    const VertexElement layout[] = {
        {"POSITION", 0, VertexFormat::Float2, offsetof(UIVertex, x)},
        {"TEXCOORD", 0, VertexFormat::Float2, offsetof(UIVertex, u)},
        {"COLOR", 0, VertexFormat::Float4, offsetof(UIVertex, r)},
    };
    if (Device()->GetBackend() == RHIBackend::Vulkan) {
        m_ShaderHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kScreenUI, layout, 3);
        m_Shader = m_ShaderHandle ? m_ShaderHandle->shader : nullptr;
    } else {
        const char* source = Device()->GetBackend() == RHIBackend::Metal ? kUIShaderMetal : kUIShader;
        m_Shader = Device()->CreateShader(source, "VSMain", "PSMain", layout, 3);
    }
    if (!m_Shader)
        Logger::Warn("[ScreenUIPass] Failed to create UI shader");
    return m_Shader.get();
}

GpuGraphicsPipeline* ScreenUIPass::GetOrCreatePipeline(RHIFormat colorFormat) {
    const uint32_t key = static_cast<uint32_t>(colorFormat);
    if (const auto it = m_Pipelines.find(key); it != m_Pipelines.end())
        return it->second.get();
    GpuShader* shader = GetOrCreateShader();
    if (!shader || !Device())
        return nullptr;

    GraphicsPipelineDesc desc;
    desc.shader = m_Shader;
    desc.colorFormats = {colorFormat};
    desc.depthStencil.depthTestEnable = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.cullMode = RHICullMode::None;
    desc.blend.attachments[0].blendEnable = true;
    auto pipeline = Device()->CreateGraphicsPipeline(desc);
    if (!pipeline) {
        Logger::Warn("[ScreenUIPass] Failed to create UI pipeline");
        return nullptr;
    }
    GpuGraphicsPipeline* result = pipeline.get();
    m_Pipelines.emplace(key, std::move(pipeline));
    return result;
}

std::shared_ptr<GpuTexture> ScreenUIPass::GetOrCreateWhiteTexture() {
    if (m_WhiteTexture || !Device())
        return m_WhiteTexture;
    const uint8_t pixel[4] = {255, 255, 255, 255};
    m_WhiteTexture = Device()->UploadTexture2D(pixel, 1, 1);
    return m_WhiteTexture;
}

std::shared_ptr<GpuTextureView> ScreenUIPass::GetOrCreateTextureView(const std::shared_ptr<GpuTexture>& texture) {
    if (!Device() || !texture)
        return nullptr;
    if (const auto it = m_TextureViews.find(texture.get()); it != m_TextureViews.end())
        return it->second;
    RHITextureViewDesc desc;
    desc.usage = RHIResourceUsage::ShaderResource;
    auto view = Device()->CreateTextureView(texture, desc);
    if (view)
        m_TextureViews.emplace(texture.get(), view);
    return view;
}

std::shared_ptr<GpuSampler> ScreenUIPass::GetOrCreateSampler() {
    if (m_LinearSampler || !Device())
        return m_LinearSampler;
    RHISamplerDesc desc;
    desc.filter = RHIFilter::Linear;
    desc.addressU = desc.addressV = desc.addressW = RHIAddressMode::Clamp;
    m_LinearSampler = Device()->CreateSampler(desc);
    return m_LinearSampler;
}

void ScreenUIPass::Execute(GpuCommandList&, const Scene&, const Camera&) {
}

bool ScreenUIPass::Prepare(const UIDrawList& drawList, RHIFormat colorFormat) {
    if (!GetOrCreatePipeline(colorFormat))
        return false;
    if (Device()->GetBackend() != RHIBackend::Vulkan)
        return true;

    // Vulkan texture uploads and layout transitions must happen outside the dynamic-rendering scope opened by
    // RenderGraph. Build every resource variant used by this immutable draw list before graph execution begins.
    const auto whiteTexture = GetOrCreateWhiteTexture();
    if (!whiteTexture || !GetOrCreateSampler())
        return false;
    for (const UIDrawCommand& command : drawList.GetCommands()) {
        const auto& texture = command.texture ? command.texture : whiteTexture;
        if (!GetOrCreateTextureView(texture))
            return false;
    }
    return true;
}

void ScreenUIPass::Execute(GpuCommandList& commands, const UIDrawList& drawList, RHIFormat colorFormat) {
    GpuGraphicsPipeline* pipeline = GetOrCreatePipeline(colorFormat);
    if (!pipeline || drawList.Empty())
        return;

    commands.SetGraphicsPipeline(pipeline);
    for (const UIDrawCommand& command : drawList.GetCommands()) {
        if (!command.vertexBuffer || !command.indexBuffer || command.indexCount == 0)
            continue;
        UIScreenConstants constants{};
        constants.invSize[0] = 1.0f / static_cast<float>(m_Width);
        constants.invSize[1] = 1.0f / static_cast<float>(m_Height);
        constants.translation[0] = command.translateX;
        constants.translation[1] = command.translateY;
        if (Device()->GetBackend() == RHIBackend::Vulkan) {
            const auto texture = command.texture ? command.texture : m_WhiteTexture;
            auto bindings = Device()->CreateBindGroup(m_Shader);
            const auto viewIt = texture ? m_TextureViews.find(texture.get()) : m_TextureViews.end();
            const auto textureView = viewIt != m_TextureViews.end() ? viewIt->second : nullptr;
            const auto sampler = m_LinearSampler;
            std::string error;
            const bool complete = bindings &&
                                  bindings->SetConstants("UIScreenConstants", &constants, sizeof(constants)) &&
                                  bindings->SetTexture("u_Texture", textureView) &&
                                  bindings->SetSampler("u_Sampler", sampler) && bindings->Validate(&error);
            if (!complete) {
                if (!m_LoggedBindingFailure) {
                    Logger::Warn("[ScreenUIPass] Failed to bind Vulkan UI resources",
                                 error.empty() ? std::string{} : ": ", error);
                    m_LoggedBindingFailure = true;
                }
                continue;
            }
            commands.SetBindGroup(0, bindings.get());
        } else {
            commands.SetVSConstants(&constants, sizeof(constants));
        }
        if (command.scissor.enabled) {
            commands.SetScissor(command.scissor.x, command.scissor.y, static_cast<uint32_t>(command.scissor.width),
                                static_cast<uint32_t>(command.scissor.height));
        }
        commands.BindVertexBuffer(command.vertexBuffer.get());
        commands.BindIndexBuffer(command.indexBuffer.get());
        if (Device()->GetBackend() != RHIBackend::Vulkan)
            commands.BindPSTexture(0, command.texture ? command.texture.get() : GetOrCreateWhiteTexture().get());
        commands.DrawIndexed(command.indexCount, command.startIndex, command.baseVertex);
    }
}
