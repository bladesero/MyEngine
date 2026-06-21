#include "TestHarness.h"

#include "Assets/AssetManager.h"
#include "Camera/Camera.h"
#include "Renderer/GpuUploadQueue.h"
#include "Renderer/LightComponent.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/Renderer.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct MockBuffer final : GpuBuffer {};
struct MockBufferView final : GpuBufferView {};
struct MockShader final : GpuShader {};
struct MockTexture final : GpuTexture {};
struct MockTextureView final : GpuTextureView {};
struct MockSampler final : GpuSampler {};
class MockTimestampPool final : public GpuTimestampQueryPool {
public:
    uint32_t GetCount() const override { return 4; }
    uint64_t GetFrequency() const override { return 1000000; }
    bool ReadResults(uint32_t first, uint32_t count, std::vector<uint64_t>& ticks) override {
        if (first + count > 4) return false;
        ticks.resize(count); for (uint32_t i = 0; i < count; ++i) ticks[i] = first + i;
        return true;
    }
};

class MockReadbackTicket final : public GpuReadbackTicket {
public:
    explicit MockReadbackTicket(std::vector<uint8_t> bytes)
        : m_Bytes(std::move(bytes)) {}
    bool IsReady() const override { return ready; }
    bool Read(std::vector<uint8_t>& data) override {
        if (!ready) return false;
        data = m_Bytes;
        return true;
    }
    uint32_t GetSize() const override { return static_cast<uint32_t>(m_Bytes.size()); }
    bool ready = false;
private:
    std::vector<uint8_t> m_Bytes;
};

class MockCommandList final : public GpuCommandList {
public:
    void BindShader(GpuShader*) override { ++shaderBinds; }
    void BindVertexBuffer(GpuBuffer*) override { ++vertexBinds; }
    void BindIndexBuffer(GpuBuffer*) override {}
    void SetVSConstants(const void*, uint32_t) override { ++constantUpdates; }
    void Draw(uint32_t, uint32_t) override { ++drawCalls; }
    void DrawIndexed(uint32_t, uint32_t, uint32_t) override { ++drawCalls; }
    void DrawInstanced(uint32_t, uint32_t instanceCount, uint32_t) override {
        ++drawCalls;
        submittedInstances += instanceCount;
    }
    void DrawIndexedInstanced(uint32_t, uint32_t instanceCount,
                              uint32_t, uint32_t) override {
        ++drawCalls;
        submittedInstances += instanceCount;
    }
    void SetViewport(float, float, float, float) override {}
    void BindPSTexture(uint32_t, GpuTexture*) override {}
    void SetBlendMode(GpuBlendMode mode) override { blendModes.push_back(mode); }
    void Transition(GpuResource*, RHIResourceState before,
                    RHIResourceState after) override {
        transitions.emplace_back(before, after);
    }
    void TransitionTexture(GpuTexture*, const RHITextureViewDesc& range,
                           RHIResourceState before, RHIResourceState after) override {
        transitions.emplace_back(before, after);
        textureTransitions.push_back(range);
    }
    void BeginRendering(const RenderingInfo&) override { ++renderingScopes; ++renderingBeginCalls; }
    void EndRendering() override { --renderingScopes; }
    void SetGraphicsPipeline(GpuGraphicsPipeline* pipeline) override {
        ++pipelineBinds;
        pipelineBlendEnabled.push_back(pipeline && !pipeline->desc.blend.attachments.empty() &&
                                       pipeline->desc.blend.attachments[0].blendEnable);
    }
    void SetComputePipeline(GpuComputePipeline*) override { ++computePipelineBinds; }
    void SetBindGroup(uint32_t, GpuBindGroup*) override { ++bindGroupBinds; }
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        ++dispatches; dispatchGroups = {x, y, z};
    }
    void UAVBarrier(GpuResource*) override { ++uavBarriers; }
    void CopyTexture(GpuTexture*, const RHITextureRegion& dst,
                     GpuTexture*, const RHITextureRegion& src) override {
        copiedDst = dst; copiedSrc = src; ++textureRegionCopies;
    }
    void DrawIndirect(GpuBuffer*, uint64_t) override { ++indirectDraws; }
    void DrawIndexedIndirect(GpuBuffer*, uint64_t) override { ++indirectDraws; }
    void WriteTimestamp(GpuTimestampQueryPool*, uint32_t) override { ++timestamps; }
    void ResolveTimestamps(GpuTimestampQueryPool*, uint32_t, uint32_t) override { ++timestampResolves; }

    int shaderBinds = 0;
    int vertexBinds = 0;
    int constantUpdates = 0;
    int drawCalls = 0;
    int submittedInstances = 0;
    std::vector<GpuBlendMode> blendModes;
    std::vector<std::pair<RHIResourceState, RHIResourceState>> transitions;
    std::vector<RHITextureViewDesc> textureTransitions;
    int renderingScopes = 0;
    int renderingBeginCalls = 0;
    int pipelineBinds = 0;
    std::vector<bool> pipelineBlendEnabled;
    int computePipelineBinds = 0;
    int bindGroupBinds = 0;
    int dispatches = 0;
    int uavBarriers = 0;
    int textureRegionCopies = 0;
    int indirectDraws = 0;
    int timestamps = 0;
    int timestampResolves = 0;
    RHITextureRegion copiedDst{}, copiedSrc{};
    std::array<uint32_t, 3> dispatchGroups{};
};

class MockRenderContext final : public IRenderContext {
public:
    MockRenderContext() {
        backBufferTexture = std::make_shared<MockTexture>();
        backBufferTexture->desc.width = 128;
        backBufferTexture->desc.height = 72;
        backBufferTexture->desc.format = RHIFormat::RGBA8UNorm;
        backBufferTexture->desc.usage = RHIResourceUsage::RenderTarget;
        backBufferView = std::make_shared<MockTextureView>();
        backBufferView->texture = backBufferTexture;
        backBufferView->desc.usage = RHIResourceUsage::RenderTarget;
    }

    bool Init(IWindow*) override { return true; }
    void Shutdown() override {}
    RHIBackend GetBackend() const override { return backend; }
    void BeginFrame(float, float, float, float) override { ++beginFrames; }
    void EndFrame() override { ++endFrames; }
    GpuCommandList* GetGraphicsCommandList() override { return &commands; }
    GpuTextureView* GetCurrentBackBufferView() override { return backBufferView.get(); }
    std::shared_ptr<GpuBuffer> CreateVertexBuffer(
        const void*, uint32_t, uint32_t) override {
        ++vertexUploads;
        return std::make_shared<MockBuffer>();
    }
    std::shared_ptr<GpuBuffer> CreateIndexBuffer(
        const void*, uint32_t) override {
        ++indexUploads;
        return std::make_shared<MockBuffer>();
    }
    std::shared_ptr<GpuShader> CreateShader(
        const std::string&, const std::string&, const std::string&,
        const VertexElement*, uint32_t) override {
        ++shaderCreates;
        return std::make_shared<MockShader>();
    }
    std::shared_ptr<GpuShader> CreateShaderFromBytecode(
        const void*, size_t, const void*, size_t,
        const VertexElement*, uint32_t) override {
        ++shaderCreates;
        return std::make_shared<MockShader>();
    }
    std::shared_ptr<GpuTexture> UploadTexture2D(
        const void*, int, int) override {
        ++textureUploads;
        return std::make_shared<MockTexture>();
    }
    bool UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer, uint64_t offset,
                      const void* data, uint64_t size) override {
        if (!buffer || !data || offset + size > bufferBytes.size()) return false;
        std::memcpy(bufferBytes.data() + offset, data, static_cast<size_t>(size)); return true;
    }
    std::shared_ptr<GpuTexture> UploadTexture(
        const RHITextureDesc& desc, const RHITextureSubresourceData*, uint32_t count) override {
        if (!count) return nullptr;
        auto texture = std::make_shared<MockTexture>(); texture->desc = desc; return texture;
    }
    RHIDeviceCapabilities GetCapabilities() const override {
        RHIDeviceCapabilities caps; caps.maxColorAttachments = 8;
        caps.timestampQueries = caps.indirectDraw = true; return caps;
    }
    std::shared_ptr<GpuTimestampQueryPool> CreateTimestampQueryPool(uint32_t count) override {
        return count <= 4 ? std::make_shared<MockTimestampPool>() : nullptr;
    }
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override {
        auto texture = std::make_shared<MockTexture>(); texture->desc = desc;
        ++graphTextureCreates; return texture;
    }
    std::shared_ptr<GpuTextureView> CreateTextureView(
        const std::shared_ptr<GpuTexture>& texture,
        const RHITextureViewDesc& desc) override {
        ++textureViewCreates;
        auto view = std::make_shared<MockTextureView>();
        view->texture = texture; view->desc = desc; return view;
    }
    std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc&) override {
        ++samplerCreates;
        return std::make_shared<MockSampler>();
    }
    std::shared_ptr<GpuBuffer> CreateBuffer(
        const RHIBufferDesc& desc, const void* initialData = nullptr) override {
        auto buffer = std::make_shared<MockBuffer>(); buffer->desc = desc;
        bufferBytes.resize(desc.size);
        if (initialData && desc.size) std::memcpy(bufferBytes.data(), initialData, desc.size);
        ++bufferCreates;
        return buffer;
    }
    std::shared_ptr<GpuBufferView> CreateBufferView(
        const std::shared_ptr<GpuBuffer>& buffer,
        const RHIBufferViewDesc& desc) override {
        if (!buffer || !desc.elementCount) return nullptr;
        auto view = std::make_shared<MockBufferView>();
        view->buffer = buffer; view->desc = desc;
        return view;
    }
    std::shared_ptr<GpuShader> CreateComputeShaderFromBytecode(
        const void*, size_t) override {
        auto shader = std::make_shared<MockShader>();
        ++computeShaderCreates;
        return shader;
    }
    std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(
        const std::shared_ptr<GpuBuffer>& buffer) override {
        if (!buffer) return nullptr;
        auto ticket = std::make_shared<MockReadbackTicket>(bufferBytes);
        lastReadback = ticket;
        return ticket;
    }

    MockCommandList commands;
    RHIBackend backend = RHIBackend::Unknown;
    int beginFrames = 0;
    int endFrames = 0;
    int vertexUploads = 0;
    int indexUploads = 0;
    int shaderCreates = 0;
    int textureUploads = 0;
    int graphTextureCreates = 0;
    int textureViewCreates = 0;
    int samplerCreates = 0;
    int bufferCreates = 0;
    int computeShaderCreates = 0;
    std::vector<uint8_t> bufferBytes;
    std::shared_ptr<MockReadbackTicket> lastReadback;
    std::shared_ptr<MockTexture> backBufferTexture;
    std::shared_ptr<MockTextureView> backBufferView;
};

bool TestExtendedRHIContracts() {
    MockRenderContext context;
    RHIBufferDesc bufferDesc; bufferDesc.size = 16;
    auto buffer = context.CreateBuffer(bufferDesc);
    const uint32_t value = 0x12345678u;
    if (!Check(context.UpdateBuffer(buffer, 4, &value, sizeof(value)) &&
               std::memcmp(context.bufferBytes.data() + 4, &value, sizeof(value)) == 0,
               "RHI partial buffer update failed")) return false;
    RHITextureDesc textureDesc; textureDesc.width = 8; textureDesc.height = 8;
    uint32_t pixels[64]{};
    RHITextureSubresourceData upload{pixels, 8u * 4u, 8u * 8u * 4u, 0, 0};
    auto source = context.UploadTexture(textureDesc, &upload, 1);
    auto destination = context.CreateTexture(textureDesc);
    RHITextureRegion srcRegion{1, 2, 0, 3, 4, 1, 0, 0};
    RHITextureRegion dstRegion{4, 1, 0, 3, 4, 1, 0, 0};
    context.commands.CopyTexture(destination.get(), dstRegion, source.get(), srcRegion);
    context.commands.DrawIndirect(buffer.get(), 0);
    context.commands.DrawIndexedIndirect(buffer.get(), 0);
    auto timestamps = context.CreateTimestampQueryPool(4);
    context.commands.WriteTimestamp(timestamps.get(), 0);
    context.commands.ResolveTimestamps(timestamps.get(), 0, 1);
    std::vector<uint64_t> ticks;
    const auto caps = context.GetCapabilities();
    return Check(source && context.commands.textureRegionCopies == 1 &&
                 context.commands.copiedSrc.x == 1 && context.commands.copiedDst.x == 4 &&
                 context.commands.indirectDraws == 2 && context.commands.timestamps == 1 &&
                 context.commands.timestampResolves == 1 && timestamps &&
                 timestamps->ReadResults(0, 1, ticks) && ticks.size() == 1 &&
                 caps.maxColorAttachments == 8 && caps.indirectDraw && caps.timestampQueries,
                 "extended RHI transfer/query/indirect contracts were not preserved");
}

bool TestRenderGraphValidationAndExecution() {
    MockRenderContext context;
    using RGError = RenderGraph::ErrorCode;
    RenderGraph graph(context);
    RHITextureDesc desc;
    desc.width = 128; desc.height = 64;
    desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    const RGTextureHandle sceneColor = graph.CreateTexture("SceneColor", desc);
    std::vector<std::string> executed;
    graph.AddPass("Main", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(sceneColor, RHILoadOp::Clear, RHIStoreOp::Store,
                           {0.1f, 0.2f, 0.3f, 1.0f});
    }, [&](GpuCommandList&, const RenderGraphResources& resources) {
        if (resources.GetTexture(sceneColor)) executed.push_back("Main");
    });
    graph.AddPass("Composite", [&](RenderGraphBuilder& builder) {
        builder.ReadTexture(sceneColor);
    }, [&](GpuCommandList&, const RenderGraphResources& resources) {
        if (resources.GetView(sceneColor)) executed.push_back("Composite");
    });
    if (!Check(graph.Compile(), "RenderGraph compile failed: " + graph.GetLastError())) return false;
    if (!Check(graph.GetExecutionOrder() == std::vector<std::string>({"Main", "Composite"}),
               "RenderGraph execution order mismatch")) return false;
    if (!Check(graph.Execute(context.commands), "RenderGraph execute failed: " + graph.GetLastError())) return false;
    if (!Check(executed == std::vector<std::string>({"Main", "Composite"}) &&
               context.graphTextureCreates == 1 && context.commands.renderingScopes == 0,
               "RenderGraph did not execute through the RHI resource path")) return false;
    if (!Check(context.commands.transitions.size() == 2 &&
               context.commands.transitions[0].second == RHIResourceState::RenderTarget &&
               context.commands.transitions[1].second == RHIResourceState::ShaderResource,
               "RenderGraph state transitions mismatch")) return false;

    graph.Reset();
    const RGTextureHandle reused = graph.CreateTexture("SceneColor", desc);
    graph.SetFinalState(reused, RHIResourceState::ShaderResource);
    graph.AddPass("Rewrite", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(reused, RHILoadOp::Clear);
    }, {});
    if (!Check(graph.Execute(context.commands) && context.graphTextureCreates == 1,
               "RenderGraph did not reuse a descriptor-compatible transient texture")) return false;

    RenderGraph invalid(context);
    const RGTextureHandle unread = invalid.CreateTexture("Unread", desc);
    invalid.AddPass("InvalidRead", [&](RenderGraphBuilder& builder) {
        builder.ReadTexture(unread);
    }, {});
    if (!Check(!invalid.Compile() &&
               invalid.GetLastErrorCode() == RGError::UninitializedTextureRead,
               "RenderGraph accepted an uninitialized texture read")) return false;

    RenderGraph emptyPass(context);
    emptyPass.AddPass("ImplicitSideEffect", [](RenderGraphBuilder&) {}, {});
    if (!Check(!emptyPass.Compile() &&
               emptyPass.GetLastErrorCode() == RGError::MissingResourceAccess,
               "RenderGraph accepted an unmarked empty pass")) return false;
    emptyPass.Reset();
    bool sideEffectRan = false;
    emptyPass.AddPass("ExplicitSideEffect", [](RenderGraphBuilder&) {},
        [&](GpuCommandList&, const RenderGraphResources&) { sideEffectRan = true; },
        RenderGraph::PassFlags::AllowNoResourceAccess);
    if (!Check(emptyPass.Execute(context.commands) && sideEffectRan,
               "RenderGraph rejected an explicitly marked side-effect pass")) return false;

    RenderGraph invalidHandle(context);
    invalidHandle.AddPass("InvalidHandle", [&](RenderGraphBuilder& builder) {
        builder.ReadTexture({12345});
    }, {});
    if (!Check(!invalidHandle.Compile() &&
               invalidHandle.GetLastErrorCode() == RGError::InvalidTextureHandle,
               "RenderGraph accepted an invalid texture handle")) return false;

    RenderGraph duplicateAccess(context);
    const auto duplicateColor = duplicateAccess.CreateTexture("DuplicateColor", desc);
    duplicateAccess.AddPass("DuplicateAccess", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(duplicateColor);
        builder.ReadTexture(duplicateColor);
    }, {});
    if (!Check(!duplicateAccess.Compile() &&
               duplicateAccess.GetLastErrorCode() == RGError::DuplicateResourceAccess,
               "RenderGraph accepted duplicate texture access in one pass")) return false;

    RenderGraph usageMismatch(context);
    RHITextureDesc srvOnly = desc;
    srvOnly.usage = RHIResourceUsage::ShaderResource;
    const auto srvOnlyHandle = usageMismatch.CreateTexture("SrvOnly", srvOnly);
    usageMismatch.AddPass("BadColorWrite", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(srvOnlyHandle);
    }, {});
    if (!Check(!usageMismatch.Compile() &&
               usageMismatch.GetLastErrorCode() == RGError::TextureUsageMismatch,
               "RenderGraph accepted a color write to a non-render-target texture")) return false;

    RenderGraph attachmentMismatch(context);
    RHITextureDesc small = desc;
    small.width = 32;
    const auto colorA = attachmentMismatch.CreateTexture("ColorA", desc);
    const auto colorB = attachmentMismatch.CreateTexture("ColorB", small);
    attachmentMismatch.AddPass("MismatchedAttachments", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(colorA);
        builder.WriteColor(colorB);
    }, {});
    if (!Check(!attachmentMismatch.Compile() &&
               attachmentMismatch.GetLastErrorCode() == RGError::AttachmentSizeMismatch,
               "RenderGraph accepted mismatched attachment sizes")) return false;

    RenderGraph depthFormatMismatch(context);
    const auto nonDepth = depthFormatMismatch.CreateTexture("NonDepth", desc);
    depthFormatMismatch.AddPass("BadDepth", [&](RenderGraphBuilder& builder) {
        builder.WriteDepth(nonDepth);
    }, {});
    if (!Check(!depthFormatMismatch.Compile() &&
               depthFormatMismatch.GetLastErrorCode() == RGError::TextureUsageMismatch,
               "RenderGraph accepted a depth write to a non-depth texture")) return false;

    RenderGraph colorFormatMismatch(context);
    RHITextureDesc depthDesc = desc;
    depthDesc.format = RHIFormat::D24S8;
    depthDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::DepthStencil;
    const auto depthAsColor = colorFormatMismatch.CreateTexture("DepthAsColor", depthDesc);
    colorFormatMismatch.AddPass("DepthAsColor", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(depthAsColor);
    }, {});
    if (!Check(!colorFormatMismatch.Compile() &&
               colorFormatMismatch.GetLastErrorCode() == RGError::AttachmentFormatMismatch,
               "RenderGraph accepted a depth-format texture as a color attachment")) return false;

    MockRenderContext finalContext;
    RenderGraph importedFinal(finalContext);
    auto importedTexture = std::make_shared<MockTexture>();
    importedTexture->desc = desc;
    const auto importedHandle = importedFinal.ImportTexture(
        "ImportedColor", importedTexture, RHIResourceState::Undefined,
        RHIResourceState::ShaderResource);
    importedFinal.AddPass("WriteImported", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(importedHandle);
    }, {});
    if (!Check(importedFinal.Execute(finalContext.commands) &&
               finalContext.commands.transitions.size() == 2 &&
               finalContext.commands.transitions[0].second == RHIResourceState::RenderTarget &&
               finalContext.commands.transitions[1].second == RHIResourceState::ShaderResource,
               "RenderGraph did not transition imported texture to its final state")) return false;

    MockRenderContext finalBufferContext;
    RenderGraph importedBufferFinal(finalBufferContext);
    auto importedBuffer = std::make_shared<MockBuffer>();
    RHIBufferDesc storageDesc;
    storageDesc.size = 64;
    storageDesc.stride = 16;
    storageDesc.usage = RHIResourceUsage::UnorderedAccess | RHIResourceUsage::ShaderResource;
    importedBuffer->desc = storageDesc;
    const auto importedBufferHandle = importedBufferFinal.ImportBuffer(
        "ImportedStorage", importedBuffer, RHIResourceState::Undefined,
        RHIResourceState::ShaderResource);
    importedBufferFinal.AddPass("WriteImportedBuffer", [&](RenderGraphBuilder& builder) {
        builder.ReadWriteUAV(importedBufferHandle);
    }, {});
    if (!Check(importedBufferFinal.Execute(finalBufferContext.commands) &&
               finalBufferContext.commands.transitions.size() == 2 &&
               finalBufferContext.commands.transitions[0].second == RHIResourceState::UnorderedAccess &&
               finalBufferContext.commands.transitions[1].second == RHIResourceState::ShaderResource,
               "RenderGraph did not transition imported buffer to its final state")) return false;

    RenderGraph tooManyColors(context);
    std::vector<RGTextureHandle> colors;
    for (uint32_t i = 0; i < 9; ++i) {
        colors.push_back(tooManyColors.CreateTexture("Color" + std::to_string(i), desc));
    }
    tooManyColors.AddPass("TooManyColors", [&](RenderGraphBuilder& builder) {
        for (const auto& handle : colors) builder.WriteColor(handle);
    }, {});
    return Check(!tooManyColors.Compile() &&
                 tooManyColors.GetLastErrorCode() == RGError::TooManyColorAttachments,
                 "RenderGraph accepted more color attachments than the device supports");
}

bool TestNamedShaderBindings() {
    auto shader = std::make_shared<MockShader>();
    shader->reflection.bindings = {
        {"FrameConstants", ShaderBindingType::ConstantBuffer, 0, 1, 16, ShaderStageVertex},
        {"SceneColor", ShaderBindingType::Texture, 0, 1, 0, ShaderStagePixel},
        {"LinearClamp", ShaderBindingType::Sampler, 0, 1, 0, ShaderStagePixel}};
    GpuBindGroup bindings(shader);
    float constants[4] = {};
    if (!Check(!bindings.SetConstants("FrameConstants", constants, 12),
               "named binding accepted an invalid constant-buffer size")) return false;
    if (!Check(bindings.SetConstants("FrameConstants", constants, sizeof(constants)),
               "named constant binding failed")) return false;
    std::string error;
    if (!Check(!bindings.Validate(&error) && error.find("SceneColor") != std::string::npos,
               "bind group did not report a missing reflected binding")) return false;
    auto texture = std::make_shared<MockTexture>();
    auto view = std::make_shared<MockTextureView>(); view->texture = texture;
    auto sampler = std::make_shared<MockSampler>();
    return Check(bindings.SetTexture("SceneColor", view) &&
                 bindings.SetSampler("LinearClamp", sampler) && bindings.Validate(&error),
                 "complete named bind group failed validation: " + error);
}

bool TestBackendIndependentPassRecording() {
    MockRenderContext context;
    auto shader = std::make_shared<MockShader>();
    GraphicsPipelineDesc pipelineDesc; pipelineDesc.shader = shader;
    pipelineDesc.topology = RHIPrimitiveTopology::TriangleStrip;
    pipelineDesc.depthStencil.depthCompareOp = RHICompareOp::GreaterEqual;
    pipelineDesc.depthStencil.stencilEnable = true;
    pipelineDesc.depthStencil.frontFace.passOp = RHIStencilOp::Replace;
    pipelineDesc.depthStencil.stencilReference = 7;
    pipelineDesc.rasterizer.cullMode = RHICullMode::Front;
    pipelineDesc.rasterizer.frontFace = RHIFrontFace::CounterClockwise;
    pipelineDesc.rasterizer.depthBias = 8;
    pipelineDesc.blend.attachments[0].blendEnable = true;
    pipelineDesc.blend.attachments[0].srcColorFactor = RHIBlendFactor::One;
    pipelineDesc.blend.attachments[0].dstColorFactor = RHIBlendFactor::OneMinusSrcAlpha;
    pipelineDesc.multisample.sampleCount = 4;
    auto pipeline = context.CreateGraphicsPipeline(pipelineDesc);
    if (!Check(pipeline &&
               pipeline->desc.topology == RHIPrimitiveTopology::TriangleStrip &&
               pipeline->desc.depthStencil.depthCompareOp == RHICompareOp::GreaterEqual &&
               pipeline->desc.depthStencil.frontFace.passOp == RHIStencilOp::Replace &&
               pipeline->desc.depthStencil.stencilReference == 7 &&
               pipeline->desc.rasterizer.cullMode == RHICullMode::Front &&
               pipeline->desc.rasterizer.depthBias == 8 &&
               pipeline->desc.blend.attachments[0].blendEnable &&
               pipeline->desc.multisample.sampleCount == 4,
               "graphics pipeline state was not preserved by the RHI")) return false;
    auto bindings = context.CreateBindGroup(shader);
    RHITextureDesc targetDesc;
    targetDesc.width = 32; targetDesc.height = 32;
    targetDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    RenderGraph graph(context);
    const auto target = graph.CreateTexture("ValidationTarget", targetDesc);
    graph.SetFinalState(target, RHIResourceState::ShaderResource);
    graph.AddPass("RHIValidationPass", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(target, RHILoadOp::Clear);
    }, [&](GpuCommandList& commands, const RenderGraphResources&) {
        commands.SetGraphicsPipeline(pipeline.get());
        commands.SetBindGroup(0, bindings.get());
        commands.Draw(3);
    });
    return Check(graph.Execute(context.commands) && context.commands.pipelineBinds == 1 &&
                 context.commands.bindGroupBinds == 1 && context.commands.drawCalls == 1,
                 "backend-independent validation pass did not record the expected RHI commands");
}

bool TestComputeStorageBufferAndAsyncReadback() {
    MockRenderContext context;
    const std::array<uint32_t, 4> initial = {1, 2, 3, 4};
    RHIBufferDesc bufferDesc;
    bufferDesc.size = sizeof(initial); bufferDesc.stride = sizeof(uint32_t);
    bufferDesc.usage = RHIResourceUsage::UnorderedAccess |
                       RHIResourceUsage::ShaderResource |
                       RHIResourceUsage::CopySource;
    auto buffer = context.CreateBuffer(bufferDesc, initial.data());
    RHIBufferViewDesc viewDesc;
    viewDesc.elementCount = static_cast<uint32_t>(initial.size());
    viewDesc.usage = RHIResourceUsage::UnorderedAccess;
    auto view = context.CreateBufferView(buffer, viewDesc);
    auto shader = std::make_shared<MockShader>();
    shader->reflection.bindings = {
        {"SHOutput", ShaderBindingType::StorageBuffer, 0, 1, 0, ShaderStageCompute}};
    auto bindings = context.CreateBindGroup(shader);
    if (!Check(view && bindings->SetStorageBuffer("SHOutput", view),
               "compute storage-buffer named binding failed")) return false;
    std::string error;
    if (!Check(bindings->Validate(&error), "compute bind group validation failed: " + error)) return false;
    ComputePipelineDesc pipelineDesc; pipelineDesc.shader = shader;
    auto pipeline = context.CreateComputePipeline(pipelineDesc);
    auto* commands = context.GetGraphicsCommandList();
    commands->SetComputePipeline(pipeline.get());
    commands->SetBindGroup(0, bindings.get());
    commands->Dispatch(2, 3, 4);
    commands->UAVBarrier(buffer.get());
    if (!Check(context.commands.computePipelineBinds == 1 &&
               context.commands.dispatches == 1 && context.commands.uavBarriers == 1 &&
               context.commands.dispatchGroups == std::array<uint32_t, 3>{2, 3, 4},
               "compute pass did not record the expected RHI commands")) return false;
    auto ticket = context.ReadbackBufferAsync(buffer);
    std::vector<uint8_t> bytes;
    if (!Check(ticket && !ticket->IsReady() && !ticket->Read(bytes),
               "async readback completed synchronously")) return false;
    context.lastReadback->ready = true;
    return Check(ticket->IsReady() && ticket->Read(bytes) && bytes.size() == sizeof(initial) &&
                 std::memcmp(bytes.data(), initial.data(), sizeof(initial)) == 0,
                 "async readback returned incorrect buffer contents");
}

bool TestRenderGraphComputeBufferDependencies() {
    MockRenderContext context;
    using RGError = RenderGraph::ErrorCode;
    RenderGraph graph(context);
    RHIBufferDesc desc;
    desc.size = 9 * 16; desc.stride = 16;
    desc.usage = RHIResourceUsage::UnorderedAccess |
                 RHIResourceUsage::ShaderResource |
                 RHIResourceUsage::CopySource;
    const auto output = graph.CreateBuffer("SHOutput", desc);
    std::vector<std::string> executed;
    graph.AddPass("ProjectSH", [&](RenderGraphBuilder& builder) {
        builder.ReadWriteUAV(output);
    }, [&](GpuCommandList& commands, const RenderGraphResources& resources) {
        if (resources.GetBuffer(output) && resources.GetBufferView(output)) {
            commands.Dispatch(1, 1, 1); executed.push_back("ProjectSH");
        }
    });
    graph.AddPass("ConsumeSH", [&](RenderGraphBuilder& builder) {
        builder.ReadBuffer(output);
    }, [&](GpuCommandList&, const RenderGraphResources& resources) {
        if (resources.GetBuffer(output)) executed.push_back("ConsumeSH");
    });
    if (!Check(graph.Execute(context.commands) &&
               executed == std::vector<std::string>({"ProjectSH", "ConsumeSH"}) &&
               context.bufferCreates == 1 && context.commands.dispatches == 1,
               "RenderGraph compute-buffer dependency execution failed")) return false;
    RenderGraph invalid(context);
    const auto unread = invalid.CreateBuffer("UnreadBuffer", desc);
    invalid.AddPass("InvalidBufferRead", [&](RenderGraphBuilder& builder) {
        builder.ReadBuffer(unread);
    }, {});
    return Check(!invalid.Compile() &&
                 invalid.GetLastErrorCode() == RGError::UninitializedBufferRead,
                 "RenderGraph accepted an uninitialized buffer read");
}

bool TestRenderGraphTextureSubresourceAccess() {
    MockRenderContext context;
    using RGError = RenderGraph::ErrorCode;
    RenderGraph graph(context);
    RHITextureDesc desc;
    desc.width = 64;
    desc.height = 64;
    desc.mipLevels = 2;
    desc.arrayLayers = 1;
    desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    auto imported = std::make_shared<MockTexture>();
    imported->desc = desc;
    const auto texture = graph.ImportTexture("MipChain", imported, RHIResourceState::ShaderResource);
    bool foundMip1View = false;
    graph.AddPass("ReadDisjointMips", [texture](RenderGraphBuilder& builder) {
        builder.ReadTexture(texture, RGTextureSubresource{0, 1, 0, 1});
        builder.ReadTexture(texture, RGTextureSubresource{1, 1, 0, 1});
    }, [&](GpuCommandList&, const RenderGraphResources& resources) {
        foundMip1View = resources.GetView(texture, RGTextureSubresource{1, 1, 0, 1}) != nullptr;
    });
    if (!Check(graph.Execute(context.commands),
               "RenderGraph rejected disjoint subresource reads: " + graph.GetLastError())) {
        return false;
    }
    if (!Check(foundMip1View, "RenderGraph did not expose a subresource view")) return false;

    MockRenderContext transitionContext;
    RenderGraph transitions(transitionContext);
    const auto transitionTexture = transitions.CreateTexture("MipChainTransitions", desc);
    transitions.SetFinalState(transitionTexture, RHIResourceState::ShaderResource);
    transitions.AddPass("WriteMip0", [transitionTexture](RenderGraphBuilder& builder) {
        builder.WriteColor(transitionTexture, RGTextureSubresource{0, 1, 0, 1}, RHILoadOp::Clear);
    }, {});
    transitions.AddPass("WriteMip1", [transitionTexture](RenderGraphBuilder& builder) {
        builder.WriteColor(transitionTexture, RGTextureSubresource{1, 1, 0, 1}, RHILoadOp::Clear);
    }, {});
    if (!Check(transitions.Execute(transitionContext.commands),
               "RenderGraph failed subresource transitions: " + transitions.GetLastError())) {
        return false;
    }
    if (!Check(transitionContext.commands.textureTransitions.size() >= 2 &&
               transitionContext.commands.textureTransitions[0].firstMip == 0 &&
               transitionContext.commands.textureTransitions[1].firstMip == 1,
               "RenderGraph did not transition individual texture subresources")) return false;
    if (!Check(transitionContext.textureViewCreates >= 2,
               "RenderGraph did not create access-local subresource views")) return false;

    RenderGraph overlap(context);
    const auto overlapTexture = overlap.CreateTexture("Overlap", desc);
    overlap.AddPass("OverlapMips", [overlapTexture](RenderGraphBuilder& builder) {
        builder.WriteColor(overlapTexture, RGTextureSubresource{0, 2, 0, 1});
        builder.ReadTexture(overlapTexture, RGTextureSubresource{1, 1, 0, 1});
    }, {});
    if (!Check(!overlap.Compile() &&
               overlap.GetLastErrorCode() == RGError::DuplicateResourceAccess,
               "RenderGraph accepted overlapping texture subresource access")) return false;

    RenderGraph invalidRange(context);
    const auto invalidTexture = invalidRange.CreateTexture("InvalidRange", desc);
    invalidRange.AddPass("InvalidRange", [invalidTexture](RenderGraphBuilder& builder) {
        builder.ReadTexture(invalidTexture, RGTextureSubresource{2, 1, 0, 1});
    }, {});
    return Check(!invalidRange.Compile() &&
                 invalidRange.GetLastErrorCode() == RGError::InvalidTextureHandle,
                 "RenderGraph accepted an out-of-range texture subresource");
}

bool TestRenderGraphPassCullingAndLifetime() {
    MockRenderContext context;
    RenderGraph graph(context);
    RHITextureDesc desc;
    desc.width = 32;
    desc.height = 32;
    desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    const auto used = graph.CreateTexture("Used", desc);
    const auto unused = graph.CreateTexture("Unused", desc);
    std::vector<std::string> executed;
    graph.AddPass("ProduceUsed", [used](RenderGraphBuilder& builder) {
        builder.WriteColor(used, RHILoadOp::Clear);
    }, [&](GpuCommandList&, const RenderGraphResources&) {
        executed.push_back("ProduceUsed");
    });
    graph.AddPass("ProduceUnused", [unused](RenderGraphBuilder& builder) {
        builder.WriteColor(unused, RHILoadOp::Clear);
    }, [&](GpuCommandList&, const RenderGraphResources&) {
        executed.push_back("ProduceUnused");
    });
    graph.AddPass("ConsumeUsed", [used](RenderGraphBuilder& builder) {
        builder.ReadTexture(used);
    }, [&](GpuCommandList&, const RenderGraphResources&) {
        executed.push_back("ConsumeUsed");
    });
    if (!Check(graph.Compile(), "RenderGraph culling compile failed: " + graph.GetLastError()))
        return false;
    if (!Check(graph.GetExecutionOrder() ==
               std::vector<std::string>({"ProduceUsed", "ConsumeUsed"}),
               "RenderGraph culling execution order mismatch")) return false;
    if (!Check(graph.GetCulledPasses() == std::vector<std::string>({"ProduceUnused"}),
               "RenderGraph did not report the culled pass")) return false;
    if (!Check(graph.Execute(context.commands) &&
               executed == std::vector<std::string>({"ProduceUsed", "ConsumeUsed"}),
               "RenderGraph executed a culled pass")) return false;
    if (!Check(context.graphTextureCreates == 1,
               "RenderGraph created a culled transient texture")) return false;

    RenderGraph finalOutput(context);
    const auto output = finalOutput.CreateTexture("FinalOutput", desc);
    finalOutput.SetFinalState(output, RHIResourceState::ShaderResource);
    bool finalRan = false;
    finalOutput.AddPass("ProduceFinal", [output](RenderGraphBuilder& builder) {
        builder.WriteColor(output);
    }, [&](GpuCommandList&, const RenderGraphResources&) {
        finalRan = true;
    });
    return Check(finalOutput.Execute(context.commands) && finalRan &&
                 finalOutput.GetCulledPasses().empty(),
                 "RenderGraph culled a pass writing a final-state resource");
}

bool TestRenderGraphDescriptorKeyedPooling() {
    MockRenderContext context;
    RenderGraph graph(context);
    RHITextureDesc textureDesc;
    textureDesc.width = 48;
    textureDesc.height = 48;
    textureDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;

    auto first = graph.CreateTexture("FrameAColor", textureDesc);
    graph.SetFinalState(first, RHIResourceState::ShaderResource);
    graph.AddPass("WriteA", [first](RenderGraphBuilder& builder) {
        builder.WriteColor(first, RHILoadOp::Clear);
    }, {});
    if (!Check(graph.Execute(context.commands) && context.graphTextureCreates == 1,
               "RenderGraph initial descriptor-keyed texture allocation failed")) return false;

    graph.Reset();
    auto second = graph.CreateTexture("FrameBColorDifferentName", textureDesc);
    graph.SetFinalState(second, RHIResourceState::ShaderResource);
    graph.AddPass("WriteB", [second](RenderGraphBuilder& builder) {
        builder.WriteColor(second, RHILoadOp::Clear);
    }, {});
    if (!Check(graph.Execute(context.commands) && context.graphTextureCreates == 1,
               "RenderGraph did not reuse same-desc texture with a different name")) return false;

    RHIBufferDesc bufferDesc;
    bufferDesc.size = 128;
    bufferDesc.stride = 16;
    bufferDesc.usage = RHIResourceUsage::UnorderedAccess | RHIResourceUsage::ShaderResource;
    graph.Reset();
    auto firstBuffer = graph.CreateBuffer("FrameABuffer", bufferDesc);
    graph.SetFinalState(firstBuffer, RHIResourceState::ShaderResource);
    graph.AddPass("WriteBufferA", [firstBuffer](RenderGraphBuilder& builder) {
        builder.ReadWriteUAV(firstBuffer);
    }, {});
    if (!Check(graph.Execute(context.commands) && context.bufferCreates == 1,
               "RenderGraph initial descriptor-keyed buffer allocation failed")) return false;

    graph.Reset();
    auto secondBuffer = graph.CreateBuffer("FrameBBufferDifferentName", bufferDesc);
    graph.SetFinalState(secondBuffer, RHIResourceState::ShaderResource);
    graph.AddPass("WriteBufferB", [secondBuffer](RenderGraphBuilder& builder) {
        builder.ReadWriteUAV(secondBuffer);
    }, {});
    if (!Check(graph.Execute(context.commands) && context.bufferCreates == 1,
               "RenderGraph did not reuse same-desc buffer with a different name")) return false;

    MockRenderContext culledContext;
    RenderGraph culledGraph(culledContext);
    auto culled = culledGraph.CreateTexture("CulledTransient", textureDesc);
    culledGraph.AddPass("WriteCulled", [culled](RenderGraphBuilder& builder) {
        builder.WriteColor(culled);
    }, {});
    if (!Check(culledGraph.Execute(culledContext.commands) &&
               culledContext.graphTextureCreates == 0,
               "RenderGraph created a culled transient before pooling check")) return false;
    culledGraph.Reset();
    auto afterCulled = culledGraph.CreateTexture("AfterCulled", textureDesc);
    culledGraph.SetFinalState(afterCulled, RHIResourceState::ShaderResource);
    culledGraph.AddPass("WriteAfterCulled", [afterCulled](RenderGraphBuilder& builder) {
        builder.WriteColor(afterCulled);
    }, {});
    return Check(culledGraph.Execute(culledContext.commands) &&
                 culledContext.graphTextureCreates == 1,
                 "RenderGraph let a culled transient pollute the descriptor pool");
}

bool TestHeadlessRendering() {
    AssetManager::Get().Clear();
    Scene scene("HeadlessRender");
    Actor* actor = scene.CreateActor("Cube");
    auto* meshRenderer = actor->AddComponent<MeshRendererComponent>();
    meshRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    meshRenderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    Actor* culledActor = scene.CreateActor("CulledCube");
    culledActor->GetTransform().position = { 10000.0f, 0.0f, 0.0f };
    auto* culledRenderer = culledActor->AddComponent<MeshRendererComponent>();
    culledRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    culledRenderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    Actor* transparentActor = scene.CreateActor("TransparentCube");
    transparentActor->GetTransform().position = { 0.0f, 0.0f, 1.0f };
    auto* transparentRenderer = transparentActor->AddComponent<MeshRendererComponent>();
    transparentRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    auto transparentMaterial = MaterialAsset::CreateDefault("TransparentTest");
    transparentMaterial->SetBlendMode(BlendMode::Transparent);
    transparentRenderer->SetMaterial(AssetManager::Get().Register(transparentMaterial));

    Camera camera;
    camera.LookAt({ 0.0f, 0.0f, -4.0f }, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    Renderer renderer(&context, &context, &context);
    int queuedUploadRuns = 0;
    GpuUploadQueue::Get().Enqueue([&queuedUploadRuns](IRHIDevice& uploadContext) {
        ++queuedUploadRuns;
        const uint8_t pixel[4] = { 255, 255, 255, 255 };
        uploadContext.UploadTexture2D(pixel, 1, 1);
    });
    renderer.RenderScene(scene, camera, true);

    if (!Check(queuedUploadRuns == 1 && GpuUploadQueue::Get().PendingCount() == 0,
               "GPU upload queue was not consumed on the render thread")) return false;
    if (!Check(context.beginFrames == 1 && context.endFrames == 1,
               "headless renderer frame lifecycle mismatch")) return false;
    if (!Check(context.shaderCreates >= 1, "headless shader was not created")) return false;
    if (!Check(context.vertexUploads == 1 && context.indexUploads == 1,
               "headless mesh upload mismatch")) return false;
    if (!Check(context.textureUploads == 3,
               "headless texture uploads missing material or named-binding fallback")) return false;
    if (!Check(context.commands.drawCalls == 3,
               "frustum culling emitted an unexpected draw count")) return false;
    const auto transparentPipeline = std::find(
        context.commands.pipelineBlendEnabled.begin(),
        context.commands.pipelineBlendEnabled.end(), true);
    return Check(transparentPipeline != context.commands.pipelineBlendEnabled.end() &&
                 transparentPipeline != context.commands.pipelineBlendEnabled.begin() &&
                 std::count(context.commands.pipelineBlendEnabled.begin(),
                            context.commands.pipelineBlendEnabled.end(), true) == 1,
                 "opaque/transparent render ordering or blend state mismatch");
}

bool TestRendererOffscreenGraphPostProcessPath() {
    AssetManager::Get().Clear();
    Scene scene;
    Actor* lightActor = scene.CreateActor("ShadowLight");
    auto* light = lightActor->AddComponent<LightComponent>();
    light->SetLightType(LightType::Directional);
    light->SetCastShadows(true);
    Camera camera;
    camera.LookAt({ 0.0f, 0.0f, -4.0f }, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    context.backend = RHIBackend::D3D11;
    Renderer renderer(&context, &context, &context);
    renderer.Resize(128, 72);
    renderer.SetOutputOffscreen(true);
    renderer.RenderScene(scene, camera, true);

    const auto hasTransitionTo = [&](RHIResourceState state) {
        return std::find_if(context.commands.transitions.begin(),
                            context.commands.transitions.end(),
                            [state](const auto& transition) {
                                return transition.second == state;
                            }) != context.commands.transitions.end();
    };
    const auto countTransitionsTo = [&](RHIResourceState state) {
        return std::count_if(context.commands.transitions.begin(),
                             context.commands.transitions.end(),
                             [state](const auto& transition) {
                                 return transition.second == state;
                             });
    };
    if (!Check(context.beginFrames == 1 && context.endFrames == 1,
               "offscreen graph renderer frame lifecycle mismatch")) return false;
    if (!Check(context.graphTextureCreates >= 5 && context.samplerCreates >= 3,
               "offscreen graph post-process resources were not prepared")) return false;
    if (!Check(context.commands.renderingScopes == 0 &&
               context.commands.renderingBeginCalls >= 5,
               "offscreen graph did not manage expected render scopes")) return false;
    if (!Check(context.commands.dispatches >= 1,
               "offscreen graph did not execute environment SH projection")) return false;
    if (!Check(hasTransitionTo(RHIResourceState::RenderTarget) &&
               hasTransitionTo(RHIResourceState::DepthWrite) &&
               hasTransitionTo(RHIResourceState::UnorderedAccess) &&
               hasTransitionTo(RHIResourceState::ShaderResource),
               "offscreen graph did not transition render graph resources")) return false;
    if (!Check(countTransitionsTo(RHIResourceState::DepthWrite) >= 2,
                "offscreen graph did not transition shadow and scene depth resources")) return false;
    return Check(renderer.GetSceneColorView() != nullptr,
                 "offscreen graph renderer did not expose a scene color view");
}

bool TestRendererBackbufferCompositeGraphTarget() {
    AssetManager::Get().Clear();
    Scene scene;
    Camera camera;
    camera.LookAt({ 0.0f, 0.0f, -4.0f }, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    context.backend = RHIBackend::D3D11;
    Renderer renderer(&context, &context, &context);
    renderer.Resize(128, 72);
    renderer.RenderScene(scene, camera, true);

    if (!Check(context.beginFrames == 1 && context.endFrames == 1,
               "backbuffer graph renderer frame lifecycle mismatch")) return false;
    if (!Check(context.commands.renderingScopes == 0 &&
               context.commands.renderingBeginCalls >= 5,
               "backbuffer graph composite did not use graph-managed render scopes")) return false;
    if (!Check(context.commands.pipelineBinds >= 5 && context.commands.bindGroupBinds >= 4,
               "backbuffer graph composite did not draw post-process passes")) return false;
    return Check(context.commands.drawCalls >= 5,
                 "backbuffer graph composite emitted too few draw calls");
}

bool TestRendererGraphHasNoEmptyCompatibilityPasses() {
    const char* candidates[] = {
        "src/Runtime/Renderer/Renderer.cpp",
        "../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../../src/Runtime/Renderer/Renderer.cpp",
    };
    std::string source;
    for (const char* path : candidates) {
        std::ifstream file(path, std::ios::binary);
        if (!file) continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        break;
    }
    return Check(!source.empty() &&
                 source.find("AllowNoResourceAccess") == std::string::npos &&
                 source.find("\"PrepareMain\"") == std::string::npos,
                 "Renderer should not register CPU-only RenderGraph compatibility passes");
}

MYENGINE_REGISTER_TEST("Renderer", "TestExtendedRHIContracts", TestExtendedRHIContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphValidationAndExecution", TestRenderGraphValidationAndExecution);
MYENGINE_REGISTER_TEST("Renderer", "TestNamedShaderBindings", TestNamedShaderBindings);
MYENGINE_REGISTER_TEST("Renderer", "TestBackendIndependentPassRecording", TestBackendIndependentPassRecording);
MYENGINE_REGISTER_TEST("Renderer", "TestComputeStorageBufferAndAsyncReadback", TestComputeStorageBufferAndAsyncReadback);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphComputeBufferDependencies", TestRenderGraphComputeBufferDependencies);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphTextureSubresourceAccess", TestRenderGraphTextureSubresourceAccess);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphPassCullingAndLifetime", TestRenderGraphPassCullingAndLifetime);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphDescriptorKeyedPooling", TestRenderGraphDescriptorKeyedPooling);
MYENGINE_REGISTER_TEST("Renderer", "TestHeadlessRendering", TestHeadlessRendering);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererOffscreenGraphPostProcessPath", TestRendererOffscreenGraphPostProcessPath);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererBackbufferCompositeGraphTarget", TestRendererBackbufferCompositeGraphTarget);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererGraphHasNoEmptyCompatibilityPasses", TestRendererGraphHasNoEmptyCompatibilityPasses);

} // namespace
