#include "Renderer/RHIConformance.h"

#include "Renderer/IRenderContext.h"
#include "Renderer/ShaderCompilerSlang.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace {
std::shared_ptr<GpuShader> CreateVulkanConformanceShader(IRHIContext& context, const char* source,
                                                         const VertexElement* layout, uint32_t layoutCount,
                                                         std::string& error) {
    // Keep the probe self-contained rather than depending on project assets or ShaderManager singleton state.
    // Slang's public compiler API consumes files, so reserve a unique temporary directory and clean it on every exit.
    struct TemporaryDirectory {
        std::filesystem::path path;
        ~TemporaryDirectory() {
            std::error_code ignored;
            if (!path.empty())
                std::filesystem::remove_all(path, ignored);
        }
    } temporary;
    std::error_code filesystemError;
    const auto root = std::filesystem::temp_directory_path(filesystemError);
    if (filesystemError) {
        error = "temporary shader directory is unavailable: " + filesystemError.message();
        return nullptr;
    }
    static std::atomic<uint64_t> serial{0};
    for (uint32_t attempt = 0; attempt < 16 && temporary.path.empty(); ++attempt) {
        const auto candidate = root / ("myengine-rhi-conformance-" +
                                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                                       "-" + std::to_string(serial.fetch_add(1)));
        if (std::filesystem::create_directory(candidate, filesystemError))
            temporary.path = candidate;
        else if (filesystemError)
            break;
    }
    if (temporary.path.empty()) {
        error = "cannot create temporary shader directory: " + filesystemError.message();
        return nullptr;
    }
    const auto path = temporary.path / "Conformance.hlsl";
    {
        std::ofstream output(path, std::ios::binary);
        output << source;
        output.close();
        if (!output) {
            error = "cannot write temporary conformance shader";
            return nullptr;
        }
    }
    std::vector<uint8_t> vertexBytecode, pixelBytecode;
    CookedShaderStageReflection vertexReflection;
    if (!ShaderCompilerSlang::CompileStageFromFile(path, "VSMain", ShaderStage::Vertex, ShaderBackend::Vulkan,
                                                   vertexBytecode, {}, &error, &vertexReflection) ||
        !ShaderCompilerSlang::CompileStageFromFile(path, "PSMain", ShaderStage::Pixel, ShaderBackend::Vulkan,
                                                   pixelBytecode, {}, &error)) {
        return nullptr;
    }
    auto shader = context.CreateShaderFromBytecode(vertexBytecode.data(), vertexBytecode.size(), pixelBytecode.data(),
                                                   pixelBytecode.size(), layout, layoutCount);
    if (!shader) {
        error = "Vulkan rejected conformance SPIR-V shader bytecode";
    } else {
        for (const auto& input : vertexReflection.inputs)
            shader->reflection.inputs.push_back({input.semantic, input.semanticIndex, input.location});
    }
    return shader;
}
} // namespace

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
    const auto waitReadback = [](const std::shared_ptr<GpuReadbackTicket>& ticket) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (ticket && !ticket->IsReady() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return ticket && ticket->IsReady();
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
    colorDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource | RHIResourceUsage::CopySource;
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

    static const char* hlslShaderSource = R"(
struct VSInput { float3 position : POSITION; };
struct VSOutput { float4 position : SV_POSITION; };
VSOutput VSMain(VSInput input) {
    VSOutput output; output.position = float4(input.position, 1.0); return output;
}
float4 PSMain(VSOutput input) : SV_TARGET { return float4(0.2, 0.6, 0.9, 1.0); }
)";
    static const char* metalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;
struct VSInput { float3 position [[attribute(0)]]; };
struct VSOutput { float4 position [[position]]; };
vertex VSOutput VSMain(VSInput input [[stage_in]]) {
    VSOutput output; output.position = float4(input.position, 1.0); return output;
}
fragment float4 PSMain(VSOutput input [[stage_in]]) { return float4(0.2, 0.6, 0.9, 1.0); }
)";
    const VertexElement layout[] = {{"POSITION", 0, VertexFormat::Float3, 0}};
    const char* shaderSource = context.GetBackend() == RHIBackend::Metal ? metalShaderSource : hlslShaderSource;
    std::string shaderError;
    auto shader = context.GetBackend() == RHIBackend::Vulkan
                      ? CreateVulkanConformanceShader(context, hlslShaderSource, layout, 1, shaderError)
                      : context.CreateShader(shaderSource, "VSMain", "PSMain", layout, 1);
    if (!shader && !shaderError.empty()) {
        report.failure = "shader-compile: " + shaderError;
        return report;
    }
    GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.shader = shader;
    pipelineDesc.colorFormats = {RHIFormat::RGBA8UNorm};
    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;
    pipelineDesc.rasterizer.cullMode = RHICullMode::None;
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
    colorAttachment.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    RenderingInfo rendering;
    rendering.colors = &colorAttachment;
    rendering.colorCount = 1;
    rendering.width = rendering.height = 32;
    commands->Transition(color.get(), RHIResourceState::Undefined, RHIResourceState::RenderTarget);
    commands->BeginRendering(rendering);
    commands->SetGraphicsPipeline(pipeline.get());
    commands->SetVertexBuffer(vertexBuffer.get());
    commands->Draw(3);
    commands->EndRendering();
    auto renderedTriangle = context.GetBackend() == RHIBackend::Vulkan
                                ? context.ReadbackTextureAsync(color, {0, 0, 0, 32, 32, 1, 0, 0})
                                : nullptr;
    context.EndFrame();
    if (!require(!context.IsDeviceLost(), "pipeline-bind", "pipeline binding/draw transitioned to device-lost state"))
        return report;

    if (context.GetBackend() == RHIBackend::Vulkan) {
        std::vector<uint8_t> renderedBytes;
        const uint32_t pitch = renderedTriangle ? renderedTriangle->GetRowPitch() : 0;
        bool pixelsMatch = waitReadback(renderedTriangle) && renderedTriangle->Read(renderedBytes) && pitch >= 128 &&
                           renderedBytes.size() >= static_cast<uint64_t>(pitch) * 32;
        if (pixelsMatch) {
            const uint8_t* center = renderedBytes.data() + 16u * pitch + 16u * 4u;
            const uint8_t* corner = renderedBytes.data();
            pixelsMatch = center[0] >= 49 && center[0] <= 53 && center[1] >= 151 && center[1] <= 155 &&
                          center[2] >= 228 && center[2] <= 232 && center[3] == 255 && corner[0] == 0 &&
                          corner[1] == 0 && corner[2] == 0 && corner[3] == 255;
        }
        if (!require(pixelsMatch, "rendered-pixels", "SPIR-V triangle center/background pixels do not match"))
            return report;
    }

    if (context.GetBackend() == RHIBackend::Metal && capabilities.indirectDrawCount && capabilities.indirectDispatch &&
        capabilities.shaderDrawParameters) {
        static const char* indirectComputeSource = R"(
#include <metal_stdlib>
using namespace metal;
struct DrawArgs {
    uint objectIndex;
    uint indexCount;
    uint instanceCount;
    uint startIndex;
    int baseVertex;
    uint startInstance;
};
[[kernel]] void CSMain(device DrawArgs* g_Args [[buffer(0)]],
                       device uint* g_Count [[buffer(1)]],
                       uint threadIndex [[thread_position_in_grid]]) {
    if (threadIndex != 0)
        return;
    g_Args[0] = DrawArgs{0, 3, 1, 0, 0, 0};
    g_Args[1] = DrawArgs{1, 3, 1, 0, 0, 1};
    g_Count[0] = 3;
}
)";
        static const char* indirectGraphicsSource = R"(
#include <metal_stdlib>
using namespace metal;
struct VSInput { float3 position [[attribute(0)]]; };
struct VSOutput {
    float4 position [[position]];
    uint drawIndex [[flat]];
};
vertex VSOutput VSMain(VSInput input [[stage_in]], uint drawIndex [[instance_id]]) {
    VSOutput output;
    const float xOffset = drawIndex == 0 ? -0.45 : 0.45;
    output.position = float4(input.position.x + xOffset, input.position.yz, 1.0);
    output.drawIndex = drawIndex;
    return output;
}
fragment float4 PSMain(VSOutput input [[stage_in]]) {
    return input.drawIndex == 0 ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
}
)";
        auto computeShader =
            context.CreateComputeShaderFromBytecode(indirectComputeSource, std::strlen(indirectComputeSource));
        ComputePipelineDesc computePipelineDesc;
        computePipelineDesc.shader = computeShader;
        auto computePipeline = context.CreateComputePipeline(computePipelineDesc);

        RHIBufferDesc argsDesc;
        argsDesc.size = 2u * sizeof(RHIObjectDrawIndexedIndirectArgs);
        argsDesc.stride = sizeof(RHIObjectDrawIndexedIndirectArgs);
        argsDesc.usage =
            RHIResourceUsage::UnorderedAccess | RHIResourceUsage::ShaderResource | RHIResourceUsage::IndirectArguments;
        argsDesc.debugName = "RHIConformanceMetalIndirectArgs";
        auto indirectArgs = context.CreateBuffer(argsDesc);
        RHIBufferViewDesc argsViewDesc;
        argsViewDesc.elementCount = 2;
        argsViewDesc.usage = RHIResourceUsage::UnorderedAccess;
        auto indirectArgsView = context.CreateBufferView(indirectArgs, argsViewDesc);

        RHIBufferDesc countDesc;
        countDesc.size = sizeof(uint32_t);
        countDesc.stride = sizeof(uint32_t);
        countDesc.usage =
            RHIResourceUsage::UnorderedAccess | RHIResourceUsage::ShaderResource | RHIResourceUsage::IndirectArguments;
        countDesc.debugName = "RHIConformanceMetalIndirectCount";
        auto indirectCount = context.CreateBuffer(countDesc);
        RHIBufferViewDesc countViewDesc;
        countViewDesc.elementCount = 1;
        countViewDesc.usage = RHIResourceUsage::UnorderedAccess;
        auto indirectCountView = context.CreateBufferView(indirectCount, countViewDesc);
        auto indirectBindings = context.CreateBindGroup(computeShader);

        auto indirectShader = context.CreateShader(indirectGraphicsSource, "VSMain", "PSMain", layout, 1);
        GraphicsPipelineDesc indirectPipelineDesc;
        indirectPipelineDesc.shader = indirectShader;
        indirectPipelineDesc.colorFormats = {RHIFormat::RGBA8UNorm};
        indirectPipelineDesc.depthStencil.depthTestEnable = false;
        indirectPipelineDesc.depthStencil.depthWriteEnable = false;
        indirectPipelineDesc.rasterizer.cullMode = RHICullMode::None;
        auto indirectPipeline = context.CreateGraphicsPipeline(indirectPipelineDesc);
        const float indirectVertices[] = {-0.35f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.35f, -0.5f, 0.0f};
        const uint32_t indirectIndices[] = {0, 1, 2};
        auto indirectVertexBuffer =
            context.CreateVertexBuffer(indirectVertices, sizeof(indirectVertices), sizeof(float) * 3);
        auto indirectIndexBuffer = context.CreateIndexBuffer(indirectIndices, sizeof(indirectIndices));
        auto indirectStream = context.CreateIndexedIndirectCommandStream(2);

        RHITextureDesc indirectColorDesc;
        indirectColorDesc.width = indirectColorDesc.height = 32;
        indirectColorDesc.format = RHIFormat::RGBA8UNorm;
        indirectColorDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::CopySource;
        indirectColorDesc.debugName = "RHIConformanceMetalIndirectColor";
        auto indirectColor = context.CreateTexture(indirectColorDesc);
        RHITextureViewDesc indirectColorViewDesc;
        indirectColorViewDesc.usage = RHIResourceUsage::RenderTarget;
        auto indirectColorView = context.CreateTextureView(indirectColor, indirectColorViewDesc);
        indirectColorDesc.debugName = "RHIConformanceMetalEmptyIndirectColor";
        auto emptyIndirectColor = context.CreateTexture(indirectColorDesc);
        auto emptyIndirectColorView = context.CreateTextureView(emptyIndirectColor, indirectColorViewDesc);

        std::string bindingError;
        const bool indirectResourcesReady =
            computeShader && computePipeline && indirectArgs && indirectArgsView && indirectCount &&
            indirectCountView && indirectBindings && indirectBindings->SetStorageBuffer("g_Args", indirectArgsView) &&
            indirectBindings->SetStorageBuffer("g_Count", indirectCountView) &&
            indirectBindings->Validate(&bindingError) && indirectShader && indirectPipeline && indirectVertexBuffer &&
            indirectIndexBuffer && indirectStream && indirectColor && indirectColorView && emptyIndirectColor &&
            emptyIndirectColorView;
        if (!indirectResourcesReady) {
            report.failure =
                "metal-indirect-create: " +
                (bindingError.empty() ? std::string("resource or pipeline creation failed") : bindingError);
            return report;
        }

        context.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        commands = context.GetGraphicsCommandList();
        commands->SetComputePipeline(computePipeline.get());
        commands->SetBindGroup(0, indirectBindings.get());
        commands->Dispatch(1, 1, 1);
        commands->UAVBarrier(indirectArgs.get());
        commands->BuildIndexedIndirectCommandStream(indirectStream.get(), indirectArgs.get(), 0, indirectCount.get(), 0,
                                                    indirectIndexBuffer.get(), 2,
                                                    sizeof(RHIObjectDrawIndexedIndirectArgs));
        RenderingAttachment indirectColorAttachment;
        indirectColorAttachment.view = indirectColorView.get();
        indirectColorAttachment.loadOp = RHILoadOp::Clear;
        indirectColorAttachment.storeOp = RHIStoreOp::Store;
        indirectColorAttachment.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
        RenderingInfo indirectRendering;
        indirectRendering.colors = &indirectColorAttachment;
        indirectRendering.colorCount = 1;
        indirectRendering.width = indirectRendering.height = 32;
        commands->BeginRendering(indirectRendering);
        commands->SetGraphicsPipeline(indirectPipeline.get());
        commands->SetVertexBuffer(indirectVertexBuffer.get());
        commands->SetIndexBuffer(indirectIndexBuffer.get());
        commands->ExecuteIndexedIndirectCommandStream(indirectStream.get());
        commands->EndRendering();
        commands->ClearStorageBuffer(indirectCountView.get(), 0);
        commands->BuildIndexedIndirectCommandStream(indirectStream.get(), indirectArgs.get(), 0, indirectCount.get(), 0,
                                                    indirectIndexBuffer.get(), 2,
                                                    sizeof(RHIObjectDrawIndexedIndirectArgs));
        RenderingAttachment emptyIndirectColorAttachment;
        emptyIndirectColorAttachment.view = emptyIndirectColorView.get();
        emptyIndirectColorAttachment.loadOp = RHILoadOp::Clear;
        emptyIndirectColorAttachment.storeOp = RHIStoreOp::Store;
        emptyIndirectColorAttachment.clearColor = {0.0f, 0.0f, 1.0f, 1.0f};
        RenderingInfo emptyIndirectRendering;
        emptyIndirectRendering.colors = &emptyIndirectColorAttachment;
        emptyIndirectRendering.colorCount = 1;
        emptyIndirectRendering.width = emptyIndirectRendering.height = 32;
        commands->BeginRendering(emptyIndirectRendering);
        commands->SetGraphicsPipeline(indirectPipeline.get());
        commands->SetVertexBuffer(indirectVertexBuffer.get());
        commands->SetIndexBuffer(indirectIndexBuffer.get());
        commands->ExecuteIndexedIndirectCommandStream(indirectStream.get());
        commands->EndRendering();
        RHITextureRegion indirectReadbackRegion{0, 0, 0, 32, 32, 1, 0, 0};
        auto indirectReadback = context.ReadbackTextureAsync(indirectColor, indirectReadbackRegion);
        auto emptyIndirectReadback = context.ReadbackTextureAsync(emptyIndirectColor, indirectReadbackRegion);
        context.EndFrame();

        std::vector<uint8_t> indirectPixels;
        std::vector<uint8_t> emptyIndirectPixels;
        bool sawObjectZero = false;
        bool sawObjectOne = false;
        if (waitReadback(indirectReadback) && indirectReadback->Read(indirectPixels)) {
            const uint32_t rowPitch = indirectReadback->GetRowPitch();
            for (uint32_t y = 0; y < indirectReadback->GetHeight(); ++y) {
                for (uint32_t x = 0; x < indirectReadback->GetWidth(); ++x) {
                    const uint8_t* pixel = indirectPixels.data() + y * rowPitch + x * 4u;
                    sawObjectZero = sawObjectZero || (pixel[0] > 200 && pixel[1] < 32);
                    sawObjectOne = sawObjectOne || (pixel[1] > 200 && pixel[0] < 32);
                }
            }
        }
        const bool emptyDrawStayedClear = waitReadback(emptyIndirectReadback) &&
                                          emptyIndirectReadback->Read(emptyIndirectPixels) &&
                                          emptyIndirectPixels.size() >= 4 && emptyIndirectPixels[0] < 32 &&
                                          emptyIndirectPixels[1] < 32 && emptyIndirectPixels[2] > 200;
        if (!require(sawObjectZero && sawObjectOne && emptyDrawStayedClear && !context.IsDeviceLost(), "metal-indirect",
                     "compute args/count -> ICB -> baseInstance indexed render/readback or zero-draw reuse failed")) {
            return report;
        }
    }

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
    const uint32_t texturePitch = textureReadback ? textureReadback->GetRowPitch() : 0;
    bool textureReadbackOk = waitReadback(textureReadback) && textureReadback->Read(textureBytes) &&
                             textureReadback->GetWidth() == 4 && textureReadback->GetHeight() == 4 &&
                             texturePitch >= 16 && textureBytes.size() >= static_cast<uint64_t>(texturePitch) * 3 + 16;
    for (uint32_t row = 0; textureReadbackOk && row < 4; ++row) {
        textureReadbackOk = std::memcmp(textureBytes.data() + row * texturePitch, pixels.data() + row * 4, 16) == 0;
    }
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
