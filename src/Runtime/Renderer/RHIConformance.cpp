#include "Renderer/RHIConformance.h"

#include "Renderer/IRenderContext.h"

#include <array>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

std::string RHIConformanceReport::Summary() const {
    std::ostringstream stream;
    stream << (passed ? "PASS" : "FAIL") << " stages=" << completedStages.size();
    for (const auto& stage : completedStages)
        stream << " " << stage;
    if (!failure.empty())
        stream << " failure=" << failure;
    return stream.str();
}

RHIConformanceReport RunRHIConformance(IRHIContext& context) {
    RHIConformanceReport report;
    auto require = [&](bool condition, const char* stage, const char* failure) {
        if (!condition) {
            report.failure = std::string(stage) + ": " + failure;
            return false;
        }
        report.completedStages.emplace_back(stage);
        return true;
    };

    const RHIDeviceCapabilities capabilities = context.GetCapabilities();
    if (!require(context.GetBackend() != RHIBackend::Unknown && capabilities.maxTextureDimension2D >= 64 &&
                     capabilities.maxColorAttachments >= 1,
                 "capabilities", "invalid backend capability baseline"))
        return report;

    std::array<uint32_t, 16> initial{};
    for (uint32_t i = 0; i < initial.size(); ++i)
        initial[i] = i;
    RHIBufferDesc bufferDesc;
    bufferDesc.size = static_cast<uint32_t>(sizeof(initial));
    bufferDesc.stride = sizeof(uint32_t) * 4;
    bufferDesc.usage =
        RHIResourceUsage::ShaderResource | RHIResourceUsage::CopySource | RHIResourceUsage::CopyDestination;
    bufferDesc.debugName = "RHIConformanceBuffer";
    auto buffer = context.CreateBuffer(bufferDesc, initial.data());
    RHIBufferViewDesc bufferViewDesc;
    bufferViewDesc.elementCount = 4;
    auto bufferView = context.CreateBufferView(buffer, bufferViewDesc);
    const uint32_t replacement = 0x1234abcd;
    if (!require(buffer && bufferView &&
                     context.UpdateBuffer(buffer, sizeof(uint32_t), &replacement, sizeof(replacement)),
                 "buffer", "create, structured view, or partial update failed"))
        return report;

    const std::array<uint32_t, 16> pixels = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff, 0xff202020, 0xff404040,
                                             0xff606060, 0xff808080, 0xff112233, 0xff223344, 0xff334455, 0xff445566,
                                             0xffabcdef, 0xff123456, 0xff654321, 0xfffedcba};
    RHITextureDesc uploadDesc;
    uploadDesc.width = uploadDesc.height = 4;
    uploadDesc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopySource;
    RHITextureSubresourceData upload{pixels.data(), 16, 64, 0, 0};
    auto uploaded = context.UploadTexture(uploadDesc, &upload, 1);
    RHITextureViewDesc shaderViewDesc;
    auto uploadedView = context.CreateTextureView(uploaded, shaderViewDesc);
    if (!require(uploaded && uploadedView, "upload", "texture upload or SRV failed"))
        return report;

    RHITextureDesc colorDesc;
    colorDesc.width = colorDesc.height = 32;
    colorDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    colorDesc.debugName = "RHIConformanceColor";
    auto color = context.CreateTexture(colorDesc);
    RHITextureViewDesc colorViewDesc;
    colorViewDesc.usage = RHIResourceUsage::RenderTarget;
    auto colorView = context.CreateTextureView(color, colorViewDesc);
    RHITextureDesc depthDesc = colorDesc;
    depthDesc.format = RHIFormat::D24S8;
    depthDesc.usage = RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource;
    depthDesc.debugName = "RHIConformanceDepth";
    auto depth = context.CreateTexture(depthDesc);
    RHITextureViewDesc depthViewDesc;
    depthViewDesc.usage = RHIResourceUsage::DepthStencil;
    auto depthView = context.CreateTextureView(depth, depthViewDesc);
    if (!require(color && colorView && depth && depthView, "render-targets", "color/depth texture or view failed"))
        return report;

    static const char* shaderSource = R"(
struct VSInput { float3 position : POSITION; };
struct VSOutput { float4 position : SV_POSITION; };
VSOutput VSMain(VSInput input) {
    VSOutput output; output.position = float4(input.position, 1.0); return output;
}
float4 PSMain(VSOutput input) : SV_TARGET { return float4(0.2, 0.6, 0.9, 1.0); }
)";
    const VertexElement layout[] = {{"POSITION", 0, VertexFormat::Float3, 0}};
    auto shader = context.CreateShader(shaderSource, "VSMain", "PSMain", layout, 1);
    GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.shader = shader;
    pipelineDesc.colorFormats = {RHIFormat::RGBA8UNorm};
    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;
    auto pipeline = context.CreateGraphicsPipeline(pipelineDesc);
    const float vertices[] = {-0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, -0.5f, 0.0f};
    auto vertexBuffer = context.CreateVertexBuffer(vertices, sizeof(vertices), sizeof(float) * 3);
    context.BeginFrame(0.02f, 0.03f, 0.04f, 1.0f);
    GpuCommandList* commands = context.GetGraphicsCommandList();
    if (!shader || !pipeline || !vertexBuffer || !commands) {
        std::ostringstream failure;
        failure << "shader=" << static_cast<bool>(shader) << " pipeline=" << static_cast<bool>(pipeline)
                << " vertexBuffer=" << static_cast<bool>(vertexBuffer) << " commands=" << static_cast<bool>(commands);
        report.failure = "pipeline-create: " + failure.str();
        context.EndFrame();
        return report;
    }
    report.completedStages.emplace_back("pipeline-create");
    RenderingAttachment colorAttachment;
    colorAttachment.view = colorView.get();
    colorAttachment.loadOp = RHILoadOp::Clear;
    colorAttachment.storeOp = RHIStoreOp::Store;
    RenderingInfo rendering;
    rendering.colors = &colorAttachment;
    rendering.colorCount = 1;
    rendering.width = rendering.height = 32;
    commands->BeginRendering(rendering);
    commands->SetGraphicsPipeline(pipeline.get());
    commands->SetVertexBuffer(vertexBuffer.get());
    commands->Draw(3);
    commands->EndRendering();
    context.EndFrame();
    if (!require(!context.IsDeviceLost(), "pipeline-bind", "pipeline binding/draw transitioned to device-lost state"))
        return report;

    RHISamplerDesc samplerDesc;
    samplerDesc.filter = RHIFilter::Point;
    samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = RHIAddressMode::Clamp;
    if (!require(context.CreateSampler(samplerDesc) != nullptr, "sampler", "sampler creation failed"))
        return report;

    GpuSwapChain* swapChain = context.GetSwapChain();
    const uint32_t originalWidth = swapChain ? swapChain->GetWidth() : 0;
    const uint32_t originalHeight = swapChain ? swapChain->GetHeight() : 0;
    if (!require(swapChain && originalWidth > 0 && originalHeight > 0 && !swapChain->Resize(0, 0) &&
                     swapChain->GetWidth() == originalWidth && swapChain->GetHeight() == originalHeight &&
                     swapChain->Resize(64, 64) && swapChain->Resize(originalWidth, originalHeight),
                 "swapchain", "minimize rejection or resize/restore failed"))
        return report;

    context.BeginFrame(0.02f, 0.03f, 0.04f, 1.0f);
    context.EndFrame();
    const RHIDeviceLossInfo loss = context.GetDeviceLossInfo();
    if (!require(!context.IsDeviceLost() && loss.reason == RHIDeviceLossReason::None, "frame",
                 "frame/present transitioned to device-lost state"))
        return report;

    auto waitReadback = [](const std::shared_ptr<GpuReadbackTicket>& ticket) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (ticket && !ticket->IsReady() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return ticket && ticket->IsReady();
    };
    // D3D12 records readback copies into the active frame command list while
    // immediate backends execute the same public calls directly.
    context.BeginFrame(0.02f, 0.03f, 0.04f, 1.0f);
    auto bufferReadback = context.ReadbackBufferAsync(buffer);
    RHITextureRegion textureRegion{0, 0, 0, 4, 4, 1, 0, 0};
    auto textureReadback = context.ReadbackTextureAsync(uploaded, textureRegion);
    // Publish copy/query work at the frame boundary.
    context.EndFrame();
    std::vector<uint8_t> bufferBytes;
    const bool bufferReadbackOk =
        waitReadback(bufferReadback) && bufferReadback->Read(bufferBytes) && bufferBytes.size() >= sizeof(initial) &&
        std::memcmp(bufferBytes.data() + sizeof(uint32_t), &replacement, sizeof(replacement)) == 0;
    std::vector<uint8_t> textureBytes;
    const bool textureReadbackOk =
        waitReadback(textureReadback) && textureReadback->Read(textureBytes) && textureBytes.size() >= 64;
    if (!bufferReadbackOk || !textureReadbackOk) {
        std::ostringstream failure;
        uint32_t actualBufferValue = 0;
        if (bufferBytes.size() >= sizeof(uint32_t) * 2)
            std::memcpy(&actualBufferValue, bufferBytes.data() + sizeof(uint32_t), sizeof(actualBufferValue));
        failure << "bufferOk=" << bufferReadbackOk << " bufferBytes=" << bufferBytes.size()
                << " actualBufferValue=" << actualBufferValue << " textureOk=" << textureReadbackOk
                << " textureTicket=" << static_cast<bool>(textureReadback)
                << " textureReady=" << (textureReadback && textureReadback->IsReady())
                << " textureBytes=" << textureBytes.size();
        report.failure = "readback: " + failure.str();
        return report;
    }
    report.completedStages.emplace_back("readback");

    std::vector<std::shared_ptr<GpuBufferView>> descriptorPressure;
    descriptorPressure.reserve(2048);
    for (uint32_t index = 0; index < 2048; ++index) {
        auto view = context.CreateBufferView(buffer, bufferViewDesc);
        if (!view)
            break;
        descriptorPressure.push_back(std::move(view));
    }
    const bool exhaustedGracefully = !context.IsDeviceLost();
    descriptorPressure.clear();
    context.BeginFrame(0.02f, 0.03f, 0.04f, 1.0f);
    context.EndFrame();
    if (!require(exhaustedGracefully && context.CreateBufferView(buffer, bufferViewDesc), "descriptor-pressure",
                 "descriptor pressure caused device loss or leases did not recycle"))
        return report;

    report.passed = true;
    return report;
}
