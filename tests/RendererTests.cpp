#include "TestHarness.h"

#include "Assets/AssetManager.h"
#include "Camera/Camera.h"
#include "Renderer/GpuUploadQueue.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/Renderer.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
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
    void BeginRendering(const RenderingInfo&) override { ++renderingScopes; }
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
    int renderingScopes = 0;
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
    bool Init(IWindow*) override { return true; }
    void Shutdown() override {}
    void BeginFrame(float, float, float, float) override { ++beginFrames; }
    void EndFrame() override { ++endFrames; }
    GpuCommandList* GetGraphicsCommandList() override { return &commands; }
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
        auto view = std::make_shared<MockTextureView>();
        view->texture = texture; view->desc = desc; return view;
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
    int beginFrames = 0;
    int endFrames = 0;
    int vertexUploads = 0;
    int indexUploads = 0;
    int shaderCreates = 0;
    int textureUploads = 0;
    int graphTextureCreates = 0;
    int bufferCreates = 0;
    int computeShaderCreates = 0;
    std::vector<uint8_t> bufferBytes;
    std::shared_ptr<MockReadbackTicket> lastReadback;
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
    if (!Check(graph.Execute(), "RenderGraph execute failed: " + graph.GetLastError())) return false;
    if (!Check(executed == std::vector<std::string>({"Main", "Composite"}) &&
               context.graphTextureCreates == 1 && context.commands.renderingScopes == 0,
               "RenderGraph did not execute through the RHI resource path")) return false;
    if (!Check(context.commands.transitions.size() == 2 &&
               context.commands.transitions[0].second == RHIResourceState::RenderTarget &&
               context.commands.transitions[1].second == RHIResourceState::ShaderResource,
               "RenderGraph state transitions mismatch")) return false;

    graph.Reset();
    const RGTextureHandle reused = graph.CreateTexture("SceneColor", desc);
    graph.AddPass("Rewrite", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(reused, RHILoadOp::Clear);
    }, {});
    if (!Check(graph.Execute() && context.graphTextureCreates == 1,
               "RenderGraph did not reuse a descriptor-compatible transient texture")) return false;

    RenderGraph invalid(context);
    const RGTextureHandle unread = invalid.CreateTexture("Unread", desc);
    invalid.AddPass("InvalidRead", [&](RenderGraphBuilder& builder) {
        builder.ReadTexture(unread);
    }, {});
    return Check(!invalid.Compile() &&
                 invalid.GetLastError().find("uninitialized") != std::string::npos,
                 "RenderGraph accepted an uninitialized texture read");
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
    graph.AddPass("RHIValidationPass", [&](RenderGraphBuilder& builder) {
        builder.WriteColor(target, RHILoadOp::Clear);
    }, [&](GpuCommandList& commands, const RenderGraphResources&) {
        commands.SetGraphicsPipeline(pipeline.get());
        commands.SetBindGroup(0, bindings.get());
        commands.Draw(3);
    });
    return Check(graph.Execute() && context.commands.pipelineBinds == 1 &&
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
    if (!Check(graph.Execute() &&
               executed == std::vector<std::string>({"ProjectSH", "ConsumeSH"}) &&
               context.bufferCreates == 1 && context.commands.dispatches == 1,
               "RenderGraph compute-buffer dependency execution failed")) return false;
    RenderGraph invalid(context);
    const auto unread = invalid.CreateBuffer("UnreadBuffer", desc);
    invalid.AddPass("InvalidBufferRead", [&](RenderGraphBuilder& builder) {
        builder.ReadBuffer(unread);
    }, {});
    return Check(!invalid.Compile() &&
                 invalid.GetLastError().find("uninitialized buffer") != std::string::npos,
                 "RenderGraph accepted an uninitialized buffer read");
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
    Renderer renderer(&context);
    int queuedUploadRuns = 0;
    GpuUploadQueue::Get().Enqueue([&queuedUploadRuns](IRenderContext& uploadContext) {
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

MYENGINE_REGISTER_TEST("Renderer", "TestExtendedRHIContracts", TestExtendedRHIContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphValidationAndExecution", TestRenderGraphValidationAndExecution);
MYENGINE_REGISTER_TEST("Renderer", "TestNamedShaderBindings", TestNamedShaderBindings);
MYENGINE_REGISTER_TEST("Renderer", "TestBackendIndependentPassRecording", TestBackendIndependentPassRecording);
MYENGINE_REGISTER_TEST("Renderer", "TestComputeStorageBufferAndAsyncReadback", TestComputeStorageBufferAndAsyncReadback);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphComputeBufferDependencies", TestRenderGraphComputeBufferDependencies);
MYENGINE_REGISTER_TEST("Renderer", "TestHeadlessRendering", TestHeadlessRendering);

} // namespace
