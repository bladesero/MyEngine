#include "TestHarness.h"

#include "Animation/SkinnedMeshRendererComponent.h"
#include "Assets/AssetManager.h"
#include "Assets/LightingProbeAsset.h"
#include "Camera/Camera.h"
#include "Core/FrameStats.h"
#include "Core/RuntimeQualityDegradation.h"
#include "Game/SceneRenderLayer.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/EnvironmentPass.h"
#include "Renderer/DeferredLightingPass.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/GBufferPass.h"
#include "Renderer/GpuUploadQueue.h"
#include "Renderer/GpuSceneDatabase.h"
#include "Renderer/LightComponent.h"
#include "Renderer/MaterialResourceCache.h"
#include "Renderer/ModernDeferredPipeline.h"
#include "Renderer/ParticleSystemComponent.h"
#include "Renderer/ProbeBakeRenderer.h"
#include "Renderer/ProbeComponents.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/RHI/RHIResourceStats.h"
#include "Renderer/RenderGraph.h"
#include "Renderer/Renderer.h"
#include "Renderer/ShaderCompilerSlang.h"
#include "Renderer/ShaderCacheService.h"
#include "Renderer/ShaderGraphCompiler.h"
#include "Renderer/ShaderManager.h"
#include "Scene/MeshRendererComponent.h"
#include "Scene/Scene.h"
#include "UI/Render/UIDrawList.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

std::filesystem::path FindRepositoryFile(const std::array<const char*, 4>& candidates) {
    for (const char* path : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
            return path;
    }
    return {};
}

std::string ReadRepositoryTextFile(const std::array<const char*, 4>& candidates) {
    const auto path = FindRepositoryFile(candidates);
    if (path.empty())
        return {};
    std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::string CompactSource(const std::string& source) {
    std::string compact;
    compact.reserve(source.size());
    std::copy_if(source.begin(), source.end(), std::back_inserter(compact),
                 [](unsigned char character) { return std::isspace(character) == 0; });
    return compact;
}

size_t CountOccurrences(const std::string& text, const std::string& needle) {
    if (needle.empty())
        return 0;
    size_t count = 0;
    for (size_t offset = 0; (offset = text.find(needle, offset)) != std::string::npos; offset += needle.size())
        ++count;
    return count;
}

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
        if (first + count > 4)
            return false;
        ticks.resize(count);
        for (uint32_t i = 0; i < count; ++i)
            ticks[i] = first + i;
        return true;
    }
};

class MockReadbackTicket final : public GpuReadbackTicket {
public:
    explicit MockReadbackTicket(std::vector<uint8_t> bytes) : m_Bytes(std::move(bytes)) {}
    bool IsReady() const override { return ready; }
    bool Read(std::vector<uint8_t>& data) override {
        if (!ready)
            return false;
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
    void DrawIndexedInstanced(uint32_t, uint32_t instanceCount, uint32_t, uint32_t) override {
        ++drawCalls;
        submittedInstances += instanceCount;
    }
    void SetViewport(float, float, float, float) override {}
    void BindPSTexture(uint32_t, GpuTexture*) override {}
    void SetBlendMode(GpuBlendMode mode) override { blendModes.push_back(mode); }
    void Transition(GpuResource* resource, RHIResourceState before, RHIResourceState after) override {
        transitions.emplace_back(before, after);
        transitionResources.push_back(resource);
    }
    void TransitionTexture(GpuTexture*, const RHITextureViewDesc& range, RHIResourceState before,
                           RHIResourceState after) override {
        transitions.emplace_back(before, after);
        textureTransitions.push_back(range);
    }
    void BeginRendering(const RenderingInfo&) override {
        ++renderingScopes;
        ++renderingBeginCalls;
    }
    void EndRendering() override { --renderingScopes; }
    void SetGraphicsPipeline(GpuGraphicsPipeline* pipeline) override {
        ++pipelineBinds;
        pipelineBlendEnabled.push_back(pipeline && !pipeline->desc.blend.attachments.empty() &&
                                       pipeline->desc.blend.attachments[0].blendEnable);
        pipelineDepthWriteEnabled.push_back(pipeline && pipeline->desc.depthStencil.depthWriteEnable);
    }
    void SetComputePipeline(GpuComputePipeline*) override { ++computePipelineBinds; }
    void SetBindGroup(uint32_t, GpuBindGroup* bindings) override {
        ++bindGroupBinds;
        if (captureBindGroupConstants && bindings)
            bindGroupConstants.push_back(bindings->GetConstants());
    }
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        ++dispatches;
        dispatchGroups = {x, y, z};
    }
    void DispatchIndirect(GpuBuffer*, uint64_t) override { ++indirectDispatches; }
    void UAVBarrier(GpuResource*) override { ++uavBarriers; }
    void CopyTexture(GpuTexture*, const RHITextureRegion& dst, GpuTexture*, const RHITextureRegion& src) override {
        copiedDst = dst;
        copiedSrc = src;
        ++textureRegionCopies;
    }
    void DrawIndirect(GpuBuffer*, uint64_t) override { ++indirectDraws; }
    void DrawIndexedIndirect(GpuBuffer*, uint64_t) override { ++indirectDraws; }
    void DrawIndexedIndirectCount(GpuBuffer*, uint64_t, GpuBuffer*, uint64_t, uint32_t, uint32_t) override {
        ++indirectCountDraws;
    }
    void WriteTimestamp(GpuTimestampQueryPool*, uint32_t) override { ++timestamps; }
    void ResolveTimestamps(GpuTimestampQueryPool*, uint32_t, uint32_t) override { ++timestampResolves; }

    int shaderBinds = 0;
    int vertexBinds = 0;
    int constantUpdates = 0;
    int drawCalls = 0;
    int submittedInstances = 0;
    std::vector<GpuBlendMode> blendModes;
    std::vector<std::pair<RHIResourceState, RHIResourceState>> transitions;
    std::vector<GpuResource*> transitionResources;
    std::vector<RHITextureViewDesc> textureTransitions;
    int renderingScopes = 0;
    int renderingBeginCalls = 0;
    int pipelineBinds = 0;
    std::vector<bool> pipelineBlendEnabled;
    std::vector<bool> pipelineDepthWriteEnabled;
    int computePipelineBinds = 0;
    int bindGroupBinds = 0;
    bool captureBindGroupConstants = false;
    std::vector<std::unordered_map<std::string, std::vector<uint8_t>>> bindGroupConstants;
    int dispatches = 0;
    int indirectDispatches = 0;
    int uavBarriers = 0;
    int textureRegionCopies = 0;
    int indirectDraws = 0;
    int indirectCountDraws = 0;
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
    bool IsDeviceLost() const override { return deviceLoss.reason != RHIDeviceLossReason::None; }
    const std::string& GetLastDeviceError() const override { return deviceLoss.diagnostic; }
    RHIDeviceLossInfo GetDeviceLossInfo() const override { return deviceLoss; }
    GpuCommandList* GetGraphicsCommandList() override { return &commands; }
    GpuTextureView* GetCurrentBackBufferView() override { return backBufferView.get(); }
    std::shared_ptr<GpuBuffer> CreateVertexBuffer(const void*, uint32_t bytes, uint32_t stride) override {
        ++vertexUploads;
        auto buffer = std::make_shared<MockBuffer>();
        buffer->desc.size = bytes;
        buffer->desc.stride = stride;
        CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(buffer));
        return buffer;
    }
    std::shared_ptr<GpuBuffer> CreateIndexBuffer(const void*, uint32_t bytes) override {
        ++indexUploads;
        auto buffer = std::make_shared<MockBuffer>();
        buffer->desc.size = bytes;
        CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(buffer));
        return buffer;
    }
    std::shared_ptr<GpuShader> CreateShader(const std::string&, const std::string&, const std::string&,
                                            const VertexElement*, uint32_t) override {
        ++shaderCreates;
        return std::make_shared<MockShader>();
    }
    std::shared_ptr<GpuShader> CreateShaderFromBytecode(const void*, size_t, const void*, size_t, const VertexElement*,
                                                        uint32_t) override {
        ++shaderCreates;
        return std::make_shared<MockShader>();
    }
    std::shared_ptr<GpuTexture> UploadTexture2D(const void*, int, int) override {
        ++textureUploads;
        return std::make_shared<MockTexture>();
    }
    bool UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer, uint64_t offset, const void* data,
                      uint64_t size) override {
        if (!buffer || !data || offset + size > bufferBytes.size())
            return false;
        std::memcpy(bufferBytes.data() + offset, data, static_cast<size_t>(size));
        return true;
    }
    std::shared_ptr<GpuTexture> UploadTexture(const RHITextureDesc& desc, const RHITextureSubresourceData* data,
                                              uint32_t count) override {
        if (!count)
            return nullptr;
        ++textureUploads;
        uploadedTextureDescs.push_back(desc);
        uploadedSubresourceCounts.push_back(count);
        for (uint32_t i = 0; data && i < count; ++i) {
            uploadedSubresources.push_back(data[i]);
        }
        auto texture = std::make_shared<MockTexture>();
        texture->desc = desc;
        CommitRHIResourceAccounting(std::static_pointer_cast<GpuTexture>(texture));
        return texture;
    }
    RHIDeviceCapabilities GetCapabilities() const override {
        RHIDeviceCapabilities caps;
        caps.maxColorAttachments = 8;
        caps.timestampQueries = caps.indirectDraw = true;
        if (modernCapabilities) {
            caps.computeShaders = true;
            caps.storageTextures = true;
            caps.indirectDrawCount = true;
            caps.indirectDispatch = true;
            caps.bindlessResources = true;
            caps.shaderDrawParameters = true;
            caps.modernDeferredFormats = true;
            caps.maxBindlessResources = 4096;
        }
        return caps;
    }
    bool IsFormatSupported(RHIFormat format, RHIResourceUsage usage) const override {
        return HasUsage(usage, RHIResourceUsage::ShaderResource) &&
               ((supportBc1 && format == RHIFormat::BC1UNorm) || (supportBc3 && format == RHIFormat::BC3UNorm));
    }
    std::shared_ptr<GpuTimestampQueryPool> CreateTimestampQueryPool(uint32_t count) override {
        return count <= 4 ? std::make_shared<MockTimestampPool>() : nullptr;
    }
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override {
        auto texture = std::make_shared<MockTexture>();
        texture->desc = desc;
        ++graphTextureCreates;
        return texture;
    }
    std::shared_ptr<GpuTextureView> CreateTextureView(const std::shared_ptr<GpuTexture>& texture,
                                                      const RHITextureViewDesc& desc) override {
        ++textureViewCreates;
        textureViewDescs.push_back(desc);
        auto view = std::make_shared<MockTextureView>();
        view->texture = texture;
        view->desc = desc;
        if (assignBindlessIndices)
            view->bindlessIndex = nextBindlessIndex++;
        return view;
    }
    std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc& desc) override {
        ++samplerCreates;
        samplerDescs.push_back(desc);
        auto sampler = std::make_shared<MockSampler>();
        sampler->desc = desc;
        return sampler;
    }
    std::shared_ptr<GpuBuffer> CreateBuffer(const RHIBufferDesc& desc, const void* initialData = nullptr) override {
        auto buffer = std::make_shared<MockBuffer>();
        buffer->desc = desc;
        bufferBytes.resize(desc.size);
        if (initialData && desc.size)
            std::memcpy(bufferBytes.data(), initialData, desc.size);
        ++bufferCreates;
        return buffer;
    }
    std::shared_ptr<GpuBufferView> CreateBufferView(const std::shared_ptr<GpuBuffer>& buffer,
                                                    const RHIBufferViewDesc& desc) override {
        if (!buffer || !desc.elementCount)
            return nullptr;
        auto view = std::make_shared<MockBufferView>();
        view->buffer = buffer;
        view->desc = desc;
        return view;
    }
    std::shared_ptr<GpuShader> CreateComputeShaderFromBytecode(const void*, size_t) override {
        auto shader = std::make_shared<MockShader>();
        ++computeShaderCreates;
        return shader;
    }
    std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(const std::shared_ptr<GpuBuffer>& buffer) override {
        if (!buffer)
            return nullptr;
        auto ticket = std::make_shared<MockReadbackTicket>(bufferBytes);
        lastReadback = ticket;
        return ticket;
    }
    std::shared_ptr<GpuBindGroup> CreateBindGroup(const std::shared_ptr<GpuShader>& shader) override {
        ++bindGroupCreates;
        return shader ? std::make_shared<GpuBindGroup>(shader) : nullptr;
    }

    MockCommandList commands;
    RHIBackend backend = RHIBackend::Unknown;
    bool supportBc1 = false;
    bool supportBc3 = false;
    bool modernCapabilities = false;
    bool assignBindlessIndices = false;
    uint32_t nextBindlessIndex = 0;
    int beginFrames = 0;
    int endFrames = 0;
    int vertexUploads = 0;
    int indexUploads = 0;
    int shaderCreates = 0;
    int textureUploads = 0;
    std::vector<RHITextureDesc> uploadedTextureDescs;
    std::vector<uint32_t> uploadedSubresourceCounts;
    std::vector<RHITextureSubresourceData> uploadedSubresources;
    int graphTextureCreates = 0;
    int textureViewCreates = 0;
    std::vector<RHITextureViewDesc> textureViewDescs;
    int samplerCreates = 0;
    std::vector<RHISamplerDesc> samplerDescs;
    int bufferCreates = 0;
    int computeShaderCreates = 0;
    int bindGroupCreates = 0;
    std::vector<uint8_t> bufferBytes;
    std::shared_ptr<MockReadbackTicket> lastReadback;
    std::shared_ptr<MockTexture> backBufferTexture;
    std::shared_ptr<MockTextureView> backBufferView;
    RHIDeviceLossInfo deviceLoss;
};

std::shared_ptr<TextureAsset> CreateCompressedTextureForUploadTest(const std::string& path) {
    TextureDesc desc;
    desc.width = 4;
    desc.height = 4;
    desc.generateCompressedMips = true;
    std::vector<uint8_t> pixels(static_cast<size_t>(desc.width * desc.height * 4), 255);
    auto texture = std::make_shared<TextureAsset>(path);
    texture->SetName("CompressedUpload");
    texture->SetPixelData(std::move(pixels), desc);
    return texture;
}

bool TestMaterialResourceCacheUploadsBc3WhenSupported() {
    MockRenderContext context;
    context.backend = RHIBackend::D3D11;
    context.supportBc1 = true;
    context.supportBc3 = true;

    auto texture = CreateCompressedTextureForUploadTest("__test__/CompressedUploadBC3");
    MaterialResourceCache cache(&context);
    cache.EnsureTextureUploaded(texture.get());

    if (!Check(context.textureUploads == 1, "BC3-capable device did not upload the texture"))
        return false;
    if (!Check(!context.uploadedTextureDescs.empty() &&
                   context.uploadedTextureDescs.front().format == RHIFormat::BC3UNorm,
               "BC3-capable device did not receive a BC3 texture upload"))
        return false;
    if (!Check(context.uploadedSubresourceCounts.size() == 1 && context.uploadedSubresourceCounts.front() == 3,
               "BC3 upload did not include all mip levels"))
        return false;
    const bool hasBc3MipPitch =
        std::find_if(context.uploadedSubresources.begin(), context.uploadedSubresources.end(),
                     [](const RHITextureSubresourceData& data) {
                         return data.mipLevel == 0 && data.rowPitch == 16 && data.slicePitch == 16;
                     }) != context.uploadedSubresources.end();
    return Check(hasBc3MipPitch, "BC3 upload did not use block-compressed row or slice pitch");
}

bool TestMaterialResourceCacheFallsBackToRgbaWhenBc1Unsupported() {
    MockRenderContext context;
    context.backend = RHIBackend::Vulkan;
    context.supportBc1 = false;

    auto texture = CreateCompressedTextureForUploadTest("__test__/CompressedUploadRGBA");
    MaterialResourceCache cache(&context);
    cache.EnsureTextureUploaded(texture.get());

    if (!Check(context.textureUploads == 1, "RGBA fallback device did not upload the texture"))
        return false;
    if (!Check(!context.uploadedTextureDescs.empty() &&
                   context.uploadedTextureDescs.front().format == RHIFormat::RGBA8UNorm,
               "BC1-unsupported device did not fall back to RGBA8"))
        return false;
    const bool hasRgbaMipPitch =
        std::find_if(context.uploadedSubresources.begin(), context.uploadedSubresources.end(),
                     [](const RHITextureSubresourceData& data) {
                         return data.mipLevel == 0 && data.rowPitch == 16 && data.slicePitch == 64;
                     }) != context.uploadedSubresources.end();
    return Check(hasRgbaMipPitch, "RGBA fallback upload did not use linear RGBA row or slice pitch");
}

bool TestMaterialResourceCacheGpuResidencyEvictionAndReupload() {
    MockRenderContext context;
    context.backend = RHIBackend::D3D11;
    auto referenced = CreateCompressedTextureForUploadTest("__test__/GpuReferenced");
    auto pinned = CreateCompressedTextureForUploadTest("__test__/GpuPinned");
    const uint64_t baseline = RHIResourceStatsProvider::GetStats().liveResourceBytes;
    MaterialResourceCache cache(&context);
    cache.EnsureTextureUploaded(referenced.get());
    auto heldView = cache.GetTextureView(static_cast<GpuTexture*>(referenced->GetGpuHandle()));
    cache.EnsureTextureUploaded(pinned.get());
    AssetManager::Get().Pin(pinned->GetPath());
    const auto blocked = MaterialResourceCache::CollectGlobalTextureGarbage(baseline + 1, 1.0f, 4);
    AssetManager::Get().Unpin(pinned->GetPath());
    if (!Check(blocked.pressureDetected && !blocked.targetReached && blocked.evictions.empty() &&
                   blocked.blockers.size() == 2 && referenced->HasGpuHandle() && pinned->HasGpuHandle(),
               "GPU texture eviction did not report referenced and pinned blockers"))
        return false;

    heldView.reset();
    const auto evicted = MaterialResourceCache::CollectGlobalTextureGarbage(baseline + 100, 1.0f, 1);
    if (!Check(evicted.evictions.size() == 1 && evicted.targetReached &&
                   (!referenced->HasGpuHandle() || !pinned->HasGpuHandle()),
               "GPU texture LRU did not release one eligible texture to its low watermark"))
        return false;
    TextureAsset* released = !referenced->HasGpuHandle() ? referenced.get() : pinned.get();
    const int uploadsBefore = context.textureUploads;
    cache.EnsureTextureUploaded(released);
    if (!Check(released->HasGpuHandle() && context.textureUploads == uploadsBefore + 1,
               "evicted GPU texture was not rebuilt on demand"))
        return false;

    auto expired = CreateCompressedTextureForUploadTest("__test__/GpuExpiredAsset");
    cache.EnsureTextureUploaded(expired.get());
    expired.reset();
    const auto expiredCollected = MaterialResourceCache::CollectGlobalTextureGarbage(baseline + 1, 1.0f, 8);
    const bool foundExpired =
        std::any_of(expiredCollected.evictions.begin(), expiredCollected.evictions.end(),
                    [](const GpuTextureEvictionRecord& item) { return item.path == "__test__/GpuExpiredAsset"; });
    return Check(foundExpired, "GPU cache did not safely collect an entry after its CPU asset expired");
}

bool TestMaterialResourceCacheMeshResidencyAndQualityDegradation() {
    RuntimeQualityDegradation::Reset();
    MockRenderContext context;
    const uint64_t baseline = RHIResourceStatsProvider::GetStats().liveResourceBytes;
    auto mesh = MeshAsset::CreateCube("GpuMeshLRU");
    MaterialResourceCache cache(&context);
    cache.EnsureMeshUploaded(mesh.get());
    if (!Check(mesh->IsUploaded() && mesh->GetGpuBufferBytes() > 0, "mesh residency test failed to upload buffers"))
        return false;
    for (int collection = 0; collection < 2; ++collection) {
        const auto active = MaterialResourceCache::CollectGlobalMeshGarbage(baseline + 1, 1.0f, 4, 2);
        if (!Check(active.evictions.empty() && !active.blockers.empty() && mesh->IsUploaded(),
                   "active mesh grace window did not block eviction"))
            return false;
    }
    const auto evicted = MaterialResourceCache::CollectGlobalMeshGarbage(baseline + 1, 1.0f, 4, 2);
    if (!Check(evicted.evictions.size() == 1 && !mesh->IsUploaded(),
               "inactive mesh was not evicted after its grace window"))
        return false;
    cache.EnsureMeshUploaded(mesh.get());
    if (!Check(mesh->IsUploaded() && context.vertexUploads >= 2, "evicted mesh was not rebuilt on demand"))
        return false;

    RuntimeQualityDegradation::SetLevel(1);
    auto texture = CreateCompressedTextureForUploadTest("__test__/QualityMipBias");
    cache.EnsureTextureUploaded(texture.get());
    const auto& uploaded = context.uploadedTextureDescs.back();
    if (!Check(uploaded.width == 2 && uploaded.height == 2 && uploaded.mipLevels == 2,
               "quality degradation did not bias uploaded texture mips"))
        return false;
    ParticleSystemComponent particles;
    particles.GetSettings().maxParticles = 8;
    RuntimeQualityDegradation::SetLevel(2);
    particles.Emit(8);
    const bool particleCap = particles.GetAliveCount() == 2;
    RuntimeQualityDegradation::Reset();
    return Check(particleCap, "quality degradation did not bound particle count");
}

bool TestExtendedRHIContracts() {
    MockRenderContext context;
    RHIBufferDesc bufferDesc;
    bufferDesc.size = 16;
    auto buffer = context.CreateBuffer(bufferDesc);
    const uint32_t value = 0x12345678u;
    if (!Check(context.UpdateBuffer(buffer, 4, &value, sizeof(value)) &&
                   std::memcmp(context.bufferBytes.data() + 4, &value, sizeof(value)) == 0,
               "RHI partial buffer update failed"))
        return false;
    RHITextureDesc textureDesc;
    textureDesc.width = 8;
    textureDesc.height = 8;
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
    return Check(source && context.commands.textureRegionCopies == 1 && context.commands.copiedSrc.x == 1 &&
                     context.commands.copiedDst.x == 4 && context.commands.indirectDraws == 2 &&
                     context.commands.timestamps == 1 && context.commands.timestampResolves == 1 && timestamps &&
                     timestamps->ReadResults(0, 1, ticks) && ticks.size() == 1 && caps.maxColorAttachments == 8 &&
                     caps.indirectDraw && caps.timestampQueries,
                 "extended RHI transfer/query/indirect contracts were not preserved");
}

bool TestStableRHIDeviceLossContract() {
    MockRenderContext context;
    if (!Check(!context.IsDeviceLost() && context.GetDeviceLossInfo().reason == RHIDeviceLossReason::None,
               "healthy RHI context reported device loss"))
        return false;
    context.deviceLoss = {RHIDeviceLossReason::Hung, -2005270522, 7, "present detected a hung device"};
    const RHIDeviceLossInfo info = context.GetDeviceLossInfo();
    return Check(context.IsDeviceLost() && info.reason == RHIDeviceLossReason::Hung &&
                     std::string(RHIDeviceLossReasonName(info.reason)) == "hung" && info.nativeCode == -2005270522 &&
                     info.deviceGeneration == 7 && info.diagnostic == context.GetLastDeviceError() &&
                     std::string(RHIDeviceLossReasonName(RHIDeviceLossReason::DriverInternalError)) ==
                         "driver_internal_error",
                 "stable RHI device-loss reason, generation, or diagnostic was lost");
}

bool TestRenderGraphValidationAndExecution() {
    MockRenderContext context;
    using RGError = RenderGraph::ErrorCode;
    RenderGraph graph(context);
    RHITextureDesc desc;
    desc.width = 128;
    desc.height = 64;
    desc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    const RGTextureHandle sceneColor = graph.CreateTexture("SceneColor", desc);
    std::vector<std::string> executed;
    graph.AddPass(
        "Main",
        [&](RenderGraphBuilder& builder) {
            builder.WriteColor(sceneColor, RHILoadOp::Clear, RHIStoreOp::Store, {0.1f, 0.2f, 0.3f, 1.0f});
        },
        [&](GpuCommandList&, const RenderGraphResources& resources) {
            if (resources.GetTexture(sceneColor))
                executed.push_back("Main");
        });
    graph.AddPass(
        "Composite", [&](RenderGraphBuilder& builder) { builder.ReadTexture(sceneColor); },
        [&](GpuCommandList&, const RenderGraphResources& resources) {
            if (resources.GetView(sceneColor))
                executed.push_back("Composite");
        });
    if (!Check(graph.Compile(), "RenderGraph compile failed: " + graph.GetLastError()))
        return false;
    if (!Check(graph.GetExecutionOrder() == std::vector<std::string>({"Main", "Composite"}),
               "RenderGraph execution order mismatch"))
        return false;
    if (!Check(graph.Execute(context.commands), "RenderGraph execute failed: " + graph.GetLastError()))
        return false;
    if (!Check(executed == std::vector<std::string>({"Main", "Composite"}) && context.graphTextureCreates == 1 &&
                   context.commands.renderingScopes == 0,
               "RenderGraph did not execute through the RHI resource path"))
        return false;
    if (!Check(context.commands.transitions.size() == 2 &&
                   context.commands.transitions[0].second == RHIResourceState::RenderTarget &&
                   context.commands.transitions[1].second == RHIResourceState::ShaderResource,
               "RenderGraph state transitions mismatch"))
        return false;

    graph.Reset();
    const RGTextureHandle reused = graph.CreateTexture("SceneColor", desc);
    graph.SetFinalState(reused, RHIResourceState::ShaderResource);
    graph.AddPass("Rewrite", [&](RenderGraphBuilder& builder) { builder.WriteColor(reused, RHILoadOp::Clear); }, {});
    if (!Check(graph.Execute(context.commands) && context.graphTextureCreates == 1,
               "RenderGraph did not reuse a descriptor-compatible transient texture"))
        return false;

    RenderGraph invalid(context);
    const RGTextureHandle unread = invalid.CreateTexture("Unread", desc);
    invalid.AddPass("InvalidRead", [&](RenderGraphBuilder& builder) { builder.ReadTexture(unread); }, {});
    if (!Check(!invalid.Compile() && invalid.GetLastErrorCode() == RGError::UninitializedTextureRead,
               "RenderGraph accepted an uninitialized texture read"))
        return false;

    RenderGraph emptyPass(context);
    emptyPass.AddPass("ImplicitSideEffect", [](RenderGraphBuilder&) {}, {});
    if (!Check(!emptyPass.Compile() && emptyPass.GetLastErrorCode() == RGError::MissingResourceAccess,
               "RenderGraph accepted an unmarked empty pass"))
        return false;
    emptyPass.Reset();
    bool sideEffectRan = false;
    emptyPass.AddPass(
        "ExplicitSideEffect", [](RenderGraphBuilder&) {},
        [&](GpuCommandList&, const RenderGraphResources&) { sideEffectRan = true; },
        RenderGraph::PassFlags::AllowNoResourceAccess);
    if (!Check(emptyPass.Execute(context.commands) && sideEffectRan,
               "RenderGraph rejected an explicitly marked side-effect pass"))
        return false;

    RenderGraph invalidHandle(context);
    invalidHandle.AddPass("InvalidHandle", [&](RenderGraphBuilder& builder) { builder.ReadTexture({12345}); }, {});
    if (!Check(!invalidHandle.Compile() && invalidHandle.GetLastErrorCode() == RGError::InvalidTextureHandle,
               "RenderGraph accepted an invalid texture handle"))
        return false;

    RenderGraph duplicateAccess(context);
    const auto duplicateColor = duplicateAccess.CreateTexture("DuplicateColor", desc);
    duplicateAccess.AddPass("DuplicateAccess",
                            [&](RenderGraphBuilder& builder) {
                                builder.WriteColor(duplicateColor);
                                builder.ReadTexture(duplicateColor);
                            },
                            {});
    if (!Check(!duplicateAccess.Compile() && duplicateAccess.GetLastErrorCode() == RGError::DuplicateResourceAccess,
               "RenderGraph accepted duplicate texture access in one pass"))
        return false;

    RenderGraph usageMismatch(context);
    RHITextureDesc srvOnly = desc;
    srvOnly.usage = RHIResourceUsage::ShaderResource;
    const auto srvOnlyHandle = usageMismatch.CreateTexture("SrvOnly", srvOnly);
    usageMismatch.AddPass("BadColorWrite", [&](RenderGraphBuilder& builder) { builder.WriteColor(srvOnlyHandle); }, {});
    if (!Check(!usageMismatch.Compile() && usageMismatch.GetLastErrorCode() == RGError::TextureUsageMismatch,
               "RenderGraph accepted a color write to a non-render-target texture"))
        return false;

    RenderGraph attachmentMismatch(context);
    RHITextureDesc small = desc;
    small.width = 32;
    const auto colorA = attachmentMismatch.CreateTexture("ColorA", desc);
    const auto colorB = attachmentMismatch.CreateTexture("ColorB", small);
    attachmentMismatch.AddPass("MismatchedAttachments",
                               [&](RenderGraphBuilder& builder) {
                                   builder.WriteColor(colorA);
                                   builder.WriteColor(colorB);
                               },
                               {});
    if (!Check(!attachmentMismatch.Compile() &&
                   attachmentMismatch.GetLastErrorCode() == RGError::AttachmentSizeMismatch,
               "RenderGraph accepted mismatched attachment sizes"))
        return false;

    RenderGraph depthFormatMismatch(context);
    const auto nonDepth = depthFormatMismatch.CreateTexture("NonDepth", desc);
    depthFormatMismatch.AddPass("BadDepth", [&](RenderGraphBuilder& builder) { builder.WriteDepth(nonDepth); }, {});
    if (!Check(!depthFormatMismatch.Compile() &&
                   depthFormatMismatch.GetLastErrorCode() == RGError::TextureUsageMismatch,
               "RenderGraph accepted a depth write to a non-depth texture"))
        return false;

    RenderGraph colorFormatMismatch(context);
    RHITextureDesc depthDesc = desc;
    depthDesc.format = RHIFormat::D24S8;
    depthDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::DepthStencil;
    const auto depthAsColor = colorFormatMismatch.CreateTexture("DepthAsColor", depthDesc);
    colorFormatMismatch.AddPass("DepthAsColor", [&](RenderGraphBuilder& builder) { builder.WriteColor(depthAsColor); },
                                {});
    if (!Check(!colorFormatMismatch.Compile() &&
                   colorFormatMismatch.GetLastErrorCode() == RGError::AttachmentFormatMismatch,
               "RenderGraph accepted a depth-format texture as a color attachment"))
        return false;

    MockRenderContext finalContext;
    RenderGraph importedFinal(finalContext);
    auto importedTexture = std::make_shared<MockTexture>();
    importedTexture->desc = desc;
    const auto importedHandle = importedFinal.ImportTexture(
        "ImportedColor", importedTexture, RHIResourceState::Undefined, RHIResourceState::ShaderResource);
    importedFinal.AddPass("WriteImported", [&](RenderGraphBuilder& builder) { builder.WriteColor(importedHandle); },
                          {});
    if (!Check(importedFinal.Execute(finalContext.commands) && finalContext.commands.transitions.size() == 2 &&
                   finalContext.commands.transitions[0].second == RHIResourceState::RenderTarget &&
                   finalContext.commands.transitions[1].second == RHIResourceState::ShaderResource,
               "RenderGraph did not transition imported texture to its final state"))
        return false;

    MockRenderContext finalBufferContext;
    RenderGraph importedBufferFinal(finalBufferContext);
    auto importedBuffer = std::make_shared<MockBuffer>();
    RHIBufferDesc storageDesc;
    storageDesc.size = 64;
    storageDesc.stride = 16;
    storageDesc.usage = RHIResourceUsage::UnorderedAccess | RHIResourceUsage::ShaderResource;
    importedBuffer->desc = storageDesc;
    const auto importedBufferHandle = importedBufferFinal.ImportBuffer(
        "ImportedStorage", importedBuffer, RHIResourceState::Undefined, RHIResourceState::ShaderResource);
    importedBufferFinal.AddPass("WriteImportedBuffer",
                                [&](RenderGraphBuilder& builder) { builder.ReadWriteUAV(importedBufferHandle); }, {});
    if (!Check(importedBufferFinal.Execute(finalBufferContext.commands) &&
                   finalBufferContext.commands.transitions.size() == 2 &&
                   finalBufferContext.commands.transitions[0].second == RHIResourceState::UnorderedAccess &&
                   finalBufferContext.commands.transitions[1].second == RHIResourceState::ShaderResource,
               "RenderGraph did not transition imported buffer to its final state"))
        return false;

    RenderGraph tooManyColors(context);
    std::vector<RGTextureHandle> colors;
    for (uint32_t i = 0; i < 9; ++i) {
        colors.push_back(tooManyColors.CreateTexture("Color" + std::to_string(i), desc));
    }
    tooManyColors.AddPass("TooManyColors",
                          [&](RenderGraphBuilder& builder) {
                              for (const auto& handle : colors)
                                  builder.WriteColor(handle);
                          },
                          {});
    return Check(!tooManyColors.Compile() && tooManyColors.GetLastErrorCode() == RGError::TooManyColorAttachments,
                 "RenderGraph accepted more color attachments than the device supports");
}

bool TestNamedShaderBindings() {
    auto shader = std::make_shared<MockShader>();
    shader->reflection.bindings = {
        {"FrameConstants", ShaderBindingType::ConstantBuffer, 0, 1, 16, ShaderStageVertex},
        {"SceneColor", ShaderBindingType::Texture, 0, 1, 0, ShaderStagePixel},
        {"LinearClamp", ShaderBindingType::Sampler, 0, 1, 0, ShaderStagePixel},
        {"EnvironmentSH", ShaderBindingType::StructuredBuffer, 1, 1, 0, ShaderStagePixel},
        {"BindlessTextures", ShaderBindingType::Texture, 0, UINT32_MAX, 0, ShaderStagePixel, 1}};
    GpuBindGroup bindings(shader);
    float constants[4] = {};
    if (!Check(!bindings.SetConstants("FrameConstants", constants, 12),
               "named binding accepted an invalid constant-buffer size"))
        return false;
    if (!Check(bindings.SetConstants("FrameConstants", constants, sizeof(constants)), "named constant binding failed"))
        return false;
    std::string error;
    if (!Check(!bindings.Validate(&error) && error.find("SceneColor") != std::string::npos,
               "bind group did not report a missing reflected binding"))
        return false;
    auto texture = std::make_shared<MockTexture>();
    auto view = std::make_shared<MockTextureView>();
    view->texture = texture;
    auto sampler = std::make_shared<MockSampler>();
    auto buffer = std::make_shared<MockBufferView>();
    return Check(bindings.SetTexture("SceneColor", view) && bindings.SetSampler("LinearClamp", sampler) &&
                     !bindings.SetStorageBuffer("EnvironmentSH", buffer) &&
                     bindings.SetBuffer("EnvironmentSH", buffer) && bindings.Validate(&error),
                 "complete named bind group or implicit bindless table failed validation: " + error);
}

bool TestRenderGraphComputePassTypeAndUavBarriers() {
    MockRenderContext context;
    auto storage = std::make_shared<MockBuffer>();
    storage->desc.size = 64;
    storage->desc.stride = 16;
    storage->desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess;

    RenderGraph graph(context);
    const auto handle = graph.ImportBuffer("PersistentHistory", storage, RHIResourceState::ShaderResource,
                                           RHIResourceState::ShaderResource);
    graph.AddComputePass(
        "TemporalAccumulate", [&](RenderGraphBuilder& builder) { builder.ReadWriteUAV(handle); },
        [](GpuCommandList& commands, const RenderGraphResources&) { commands.Dispatch(1); });
    graph.AddComputePass(
        "SpatialFilter", [&](RenderGraphBuilder& builder) { builder.ReadWriteUAV(handle); },
        [](GpuCommandList& commands, const RenderGraphResources&) { commands.Dispatch(1); });
    if (!Check(graph.Compile(), "compute RenderGraph compile failed: " + graph.GetLastError()))
        return false;
    if (!Check(graph.GetExecutionPassTypes() ==
                   std::vector<RenderGraph::PassType>({RenderGraph::PassType::Compute, RenderGraph::PassType::Compute}),
               "RenderGraph lost compute pass classification"))
        return false;
    if (!Check(graph.Execute(context.commands) && context.commands.dispatches == 2 && context.commands.uavBarriers == 1,
               "RenderGraph did not insert a UAV barrier between compute writers"))
        return false;

    RenderGraph invalid(context);
    RHITextureDesc color;
    color.width = color.height = 16;
    color.usage = RHIResourceUsage::RenderTarget;
    const auto colorHandle = invalid.CreateTexture("RasterOnly", color);
    invalid.AddComputePass("InvalidComputeAttachment",
                           [&](RenderGraphBuilder& builder) { builder.WriteColor(colorHandle); }, {});
    return Check(!invalid.Compile() && invalid.GetLastErrorCode() == RenderGraph::ErrorCode::ComputeAttachmentAccess,
                 "RenderGraph accepted a raster attachment in a compute pass");
}

bool TestGpuSceneDatabaseDirtyUploadAndGeometryArena() {
    AssetManager::Get().Clear();
    MockRenderContext context;
    Scene scene("GpuScene");
    auto redMaterial = MaterialAsset::CreateDefault("GpuSceneRed");
    redMaterial->SetParam("BaseColor", MaterialParam::FromVec4(0.8f, 0.1f, 0.2f, 1.0f));
    redMaterial->SetParam("Metallic", MaterialParam::FromFloat(0.75f));
    redMaterial->SetParam("Roughness", MaterialParam::FromFloat(0.2f));
    auto blueMaterial = MaterialAsset::CreateDefault("GpuSceneBlue");
    blueMaterial->SetParam("BaseColor", MaterialParam::FromVec4(0.1f, 0.2f, 0.9f, 1.0f));
    blueMaterial->SetParam("Metallic", MaterialParam::FromFloat(0.05f));
    blueMaterial->SetParam("Roughness", MaterialParam::FromFloat(0.8f));
    const auto redHandle = AssetManager::Get().Register(redMaterial);
    const auto blueHandle = AssetManager::Get().Register(blueMaterial);

    Actor* meshActor = scene.CreateActor("RedMesh");
    meshActor->GetTransform().position = {-2.0f, 0.5f, 1.0f};
    auto* renderer = meshActor->AddComponent<MeshRendererComponent>();
    renderer->SetMesh(AssetManager::Get().GetCubeMesh());
    renderer->SetMaterial(redHandle);
    Actor* secondMeshActor = scene.CreateActor("BlueMesh");
    secondMeshActor->GetTransform().position = {3.0f, -1.0f, 2.0f};
    secondMeshActor->GetTransform().scale = {2.0f, 0.5f, 1.5f};
    auto* secondRenderer = secondMeshActor->AddComponent<MeshRendererComponent>();
    secondRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    secondRenderer->SetMaterial(blueHandle);
    Actor* lightActor = scene.CreateActor("PointLight");
    auto* light = lightActor->AddComponent<LightComponent>();
    light->SetLightType(LightType::Point);
    light->SetRange(12.0f);

    GpuSceneDatabase database(&context);
    if (!Check(database.Update(scene, 1), "GPU Scene initial extraction failed"))
        return false;
    const auto first = database.GetStats();
    if (!Check(first.candidateObjects == 2 && first.localLights == 1 && first.uploadBytes > 0 &&
                   first.geometryUploadBytes > 0 && database.GetObjectBuffer() && database.GetLightBuffer() &&
                   database.GetMaterialBuffer() && database.GetMaterials().size() == 2 && first.materialResolves == 0 &&
                   database.GetGeometryArena().GetVertexBuffer() && database.GetGeometryArena().GetIndexBuffer(),
               "GPU Scene did not create the expected object/light/material/geometry resources"))
        return false;
    const auto matrixEquals = [](const Mat4& lhs, const Mat4& rhs) {
        for (uint32_t index = 0; index < 16; ++index)
            if (!NearlyEqual(lhs.Data()[index], rhs.Data()[index]))
                return false;
        return true;
    };
    const auto& initialObjects = database.GetObjects();
    const auto& initialMaterials = database.GetMaterials();
    Mat4 expectedSecondNormal = Mat4::Identity();
    if (!Check(Mat4Invert(secondMeshActor->GetWorldMatrix(), expectedSecondNormal),
               "non-uniform GPU Scene object transform was unexpectedly singular")) {
        return false;
    }
    expectedSecondNormal = expectedSecondNormal.Transposed();
    if (!Check(initialObjects[0].materialId != initialObjects[1].materialId &&
                   matrixEquals(initialObjects[0].world, meshActor->GetWorldMatrix()) &&
                   matrixEquals(initialObjects[1].world, secondMeshActor->GetWorldMatrix()) &&
                   matrixEquals(initialObjects[1].normalMatrix, expectedSecondNormal) &&
                   database.GetObjectBuffer()->desc.stride == sizeof(GpuSceneObjectData) &&
                   database.GetObjectBuffer()->desc.stride == 256u &&
                   NearlyEqual(initialMaterials[initialObjects[0].materialId].baseColor.x, 0.8f) &&
                   NearlyEqual(initialMaterials[initialObjects[0].materialId].material.x, 0.75f) &&
                   NearlyEqual(initialMaterials[initialObjects[0].materialId].material.y, 0.2f) &&
                   NearlyEqual(initialMaterials[initialObjects[1].materialId].baseColor.z, 0.9f) &&
                   NearlyEqual(initialMaterials[initialObjects[1].materialId].material.x, 0.05f) &&
                   NearlyEqual(initialMaterials[initialObjects[1].materialId].material.y, 0.8f),
               "GPU Scene collapsed distinct actor transforms or material records onto object zero"))
        return false;
    if (!Check(context.vertexUploads == 1 && context.indexUploads == 1 &&
                   !HasUsage(database.GetObjectBuffer()->desc.usage, RHIResourceUsage::UnorderedAccess) &&
                   !HasUsage(database.GetLightBuffer()->desc.usage, RHIResourceUsage::UnorderedAccess) &&
                   !HasUsage(database.GetMaterialBuffer()->desc.usage, RHIResourceUsage::UnorderedAccess),
               "GPU Scene did not use typed geometry buffers and read-only scene buffers"))
        return false;

    meshActor->GetTransform().position.x = 3.0f;
    if (!Check(database.Update(scene, 2), "GPU Scene incremental extraction failed"))
        return false;
    const auto second = database.GetStats();
    const auto& objects = database.GetObjects();
    const auto& materials = database.GetMaterials();
    if (!Check(second.dirtyObjectRanges == 1 && second.geometryUploadBytes == 0 && second.materialResolves == 0 &&
                   second.materialCacheHits >= 2 &&
                   std::memcmp(objects[0].world.Data(), objects[0].previousWorld.Data(), sizeof(float) * 16) != 0 &&
                   matrixEquals(objects[1].world, objects[1].previousWorld) &&
                   matrixEquals(objects[1].normalMatrix, expectedSecondNormal) &&
                   objects[0].materialId != objects[1].materialId &&
                   NearlyEqual(materials[objects[0].materialId].baseColor.x, 0.8f) &&
                   NearlyEqual(materials[objects[1].materialId].baseColor.z, 0.9f),
               "GPU Scene did not preserve per-object transforms, material IDs, or dirty ranges"))
        return false;

    redMaterial->SetParam("BaseColor", MaterialParam::FromVec4(0.25f, 0.5f, 0.75f, 1.0f));
    if (!Check(database.Update(scene, 2), "GPU Scene same-frame extraction reuse failed") ||
        !Check(database.GetStats().extractionReused && database.GetStats().uploadBytes == 0 &&
                   NearlyEqual(database.GetMaterials()[database.GetObjects()[0].materialId].baseColor.x, 0.8f),
               "GPU Scene rebuilt or mutated an immutable same-frame extract"))
        return false;
    if (!Check(database.Update(scene, 3), "GPU Scene material-version invalidation failed"))
        return false;
    const auto third = database.GetStats();
    const auto& updatedMaterials = database.GetMaterials();
    if (!Check(third.dirtyMaterialRanges == 1 &&
                   NearlyEqual(updatedMaterials[database.GetObjects()[0].materialId].baseColor.x, 0.25f),
               "GPU Scene reused stale cached material data after a material edit")) {
        return false;
    }

    secondMeshActor->GetTransform().scale.y = 0.0f;
    if (!Check(database.Update(scene, 4), "GPU Scene singular normal-matrix fallback extraction failed"))
        return false;
    return Check(database.GetStats().dirtyObjectRanges == 1 &&
                     matrixEquals(database.GetObjects()[1].normalMatrix, Mat4::Identity()),
                 "GPU Scene did not fall back to an identity normal matrix for a singular world transform");
}

bool TestGpuSceneMaterialBindlessSamplerSelection() {
    AssetManager& assets = AssetManager::Get();
    assets.Clear();
    MockRenderContext context;
    context.assignBindlessIndices = true;

    auto material = MaterialAsset::CreateDefault("__test__/ModernSamplerMaterial");
    const std::array<const char*, 5> slots = {"BaseColorMap", "NormalMap", "MetallicRoughnessMap", "OcclusionMap",
                                              "EmissiveMap"};
    const std::array<TextureFilter, 5> filters = {TextureFilter::Linear, TextureFilter::Nearest, TextureFilter::Linear,
                                                  TextureFilter::Nearest, TextureFilter::Anisotropic};
    const std::array<TextureWrap, 5> wrapU = {TextureWrap::Repeat, TextureWrap::Repeat, TextureWrap::Clamp,
                                              TextureWrap::Mirror, TextureWrap::Clamp};
    const std::array<TextureWrap, 5> wrapV = {TextureWrap::Repeat, TextureWrap::Repeat, TextureWrap::Repeat,
                                              TextureWrap::Clamp, TextureWrap::Clamp};
    std::array<TextureHandle, 5> textures;
    for (uint32_t index = 0; index < textures.size(); ++index) {
        auto texture = TextureAsset::CreateSolid("ModernSampler" + std::to_string(index), 255, 255, 255, 255);
        texture->SetSampler(filters[index], wrapU[index], wrapV[index]);
        textures[index] = assets.Register(texture);
        material->SetTexture(slots[index], textures[index]);
    }

    Scene scene("ModernSamplerScene");
    Actor* actor = scene.CreateActor("SamplerCube");
    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    renderer->SetMesh(assets.GetCubeMesh());
    renderer->SetMaterial(assets.Register(material));

    GpuSceneDatabase database(&context);
    if (!Check(database.Update(scene, 1) && database.GetObjects().size() == 1 && database.GetMaterials().size() == 1,
               "GPU Scene failed to extract a textured material for sampler selection")) {
        return false;
    }
    const auto& gpu = database.GetMaterials()[database.GetObjects()[0].materialId];
    const std::array<uint32_t, 5> expected = {
        0u,
        kGpuSceneMaterialSamplerPointBit,
        kGpuSceneMaterialSamplerClampUBit,
        kGpuSceneMaterialSamplerPointBit | kGpuSceneMaterialSamplerClampVBit,
        kGpuSceneMaterialSamplerClampUBit | kGpuSceneMaterialSamplerClampVBit,
    };
    for (uint32_t index = 0; index < expected.size(); ++index) {
        if (!Check(gpu.GetSamplerIndex(index) == expected[index] && (gpu.flags & (1u << index)) != 0,
                   "GPU Scene material sampler index does not match the TextureAsset filter/wrap state")) {
            return false;
        }
    }

    // TextureAsset::SetSampler intentionally changes only sampler metadata. The material cache must still notice it
    // even when neither the asset pointer, texture version, nor GPU image changes.
    textures[2]->SetSampler(TextureFilter::Nearest, TextureWrap::Clamp, TextureWrap::Repeat);
    if (!Check(database.Update(scene, 2), "GPU Scene sampler-only cache invalidation failed"))
        return false;
    const auto& updated = database.GetMaterials()[database.GetObjects()[0].materialId];
    return Check(updated.GetSamplerIndex(2) == (kGpuSceneMaterialSamplerPointBit | kGpuSceneMaterialSamplerClampUBit) &&
                     database.GetStats().dirtyMaterialRanges == 1,
                 "GPU Scene reused a stale sampler index after TextureAsset sampler metadata changed");
}

bool TestModernBindlessSamplerShaderContract() {
#ifndef MYENGINE_PLATFORM_WINDOWS
    return true;
#else
    const std::array<const char*, kGpuSceneMaterialSamplerCount> samplerNames = {
        "g_LinearRepeatSampler",       "g_PointRepeatSampler",         "g_LinearClampURepeatVSampler",
        "g_PointClampURepeatVSampler", "g_LinearRepeatUClampVSampler", "g_PointRepeatUClampVSampler",
        "g_LinearClampSampler",        "g_PointClampSampler",
    };
    const std::array<std::array<const char*, 4>, 2> candidates = {{
        {"EngineContent/Shaders/ModernDepth.hlsl", "../../../EngineContent/Shaders/ModernDepth.hlsl",
         "../../../../EngineContent/Shaders/ModernDepth.hlsl", "../../../../../EngineContent/Shaders/ModernDepth.hlsl"},
        {"EngineContent/Shaders/ModernGBuffer.hlsl", "../../../EngineContent/Shaders/ModernGBuffer.hlsl",
         "../../../../EngineContent/Shaders/ModernGBuffer.hlsl",
         "../../../../../EngineContent/Shaders/ModernGBuffer.hlsl"},
    }};
    const std::array<ShaderBackend, 2> backends = {ShaderBackend::D3D12, ShaderBackend::Vulkan};
    for (const auto& shaderCandidates : candidates) {
        const auto shaderPath = FindRepositoryFile(shaderCandidates);
        if (!Check(!shaderPath.empty(), "Modern material shader source was not found"))
            return false;
        const std::string compactSource = CompactSource(ReadRepositoryTextFile(shaderCandidates));
        if (!Check(compactSource.find("uint4textureIndices0;uint4samplerIndices0;uinttextureIndex4;"
                                      "uintsamplerIndex4;uintflags;uintpadding;") != std::string::npos,
                   "Modern material shader does not match the 96-byte GpuSceneMaterial ABI")) {
            return false;
        }
        for (ShaderBackend backend : backends) {
            std::vector<uint8_t> bytecode;
            CookedShaderStageReflection reflection;
            std::string error;
            if (!Check(ShaderCompilerSlang::CompileStageFromFile(shaderPath, "PSMain", ShaderStage::Pixel, backend,
                                                                 bytecode, {}, &error, &reflection),
                       "Modern material sampler shader compile failed: " + error)) {
                return false;
            }
            for (const char* samplerName : samplerNames) {
                const auto found = std::find_if(reflection.bindings.begin(), reflection.bindings.end(),
                                                [&](const auto& binding) { return binding.name == samplerName; });
                if (!Check(found != reflection.bindings.end() && found->type == CookedShaderBindingType::Sampler,
                           std::string("Modern shader reflection lost sampler '") + samplerName + "'")) {
                    return false;
                }
            }
        }
    }

    const std::array<const char*, 4> pipelineCandidates = {
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    };
    const std::string pipeline = ReadRepositoryTextFile(pipelineCandidates);
    if (!Check(!pipeline.empty() && CountOccurrences(pipeline, "SetMaterialSamplerTable(") >= 4,
               "Modern depth, shadow, and GBuffer passes do not bind the fixed material sampler table")) {
        return false;
    }
    const std::array<const char*, 4> cookerCandidates = {
        "src/Runtime/Renderer/ShaderCooker.cpp",
        "../../../src/Runtime/Renderer/ShaderCooker.cpp",
        "../../../../src/Runtime/Renderer/ShaderCooker.cpp",
        "../../../../../src/Runtime/Renderer/ShaderCooker.cpp",
    };
    const std::string cooker = ReadRepositoryTextFile(cookerCandidates);
    return Check(cooker.find("shader-cooker-v5-stablepublish1-objectdraw2-materialsampler1") != std::string::npos,
                 "Modern material sampler ABI did not invalidate stale cooked shader artifacts");
#endif
}

bool TestModernGpuSceneNormalAndReflectionContracts() {
    const std::array<std::array<const char*, 4>, 3> objectShaderCandidates = {{
        {"EngineContent/Shaders/ModernCulling.hlsl", "../../../EngineContent/Shaders/ModernCulling.hlsl",
         "../../../../EngineContent/Shaders/ModernCulling.hlsl",
         "../../../../../EngineContent/Shaders/ModernCulling.hlsl"},
        {"EngineContent/Shaders/ModernDepth.hlsl", "../../../EngineContent/Shaders/ModernDepth.hlsl",
         "../../../../EngineContent/Shaders/ModernDepth.hlsl", "../../../../../EngineContent/Shaders/ModernDepth.hlsl"},
        {"EngineContent/Shaders/ModernGBuffer.hlsl", "../../../EngineContent/Shaders/ModernGBuffer.hlsl",
         "../../../../EngineContent/Shaders/ModernGBuffer.hlsl",
         "../../../../../EngineContent/Shaders/ModernGBuffer.hlsl"},
    }};
    const std::string objectPrefix =
        "row_majorfloat4x4world;row_majorfloat4x4previousWorld;row_majorfloat4x4normalMatrix;float4boundsMin;";
    for (const auto& candidates : objectShaderCandidates) {
        const std::string shader = CompactSource(ReadRepositoryTextFile(candidates));
        if (!Check(!shader.empty() && shader.find(objectPrefix) != std::string::npos,
                   "Modern culling/depth/GBuffer shader lost the 256-byte GPU Scene object ABI")) {
            return false;
        }
    }

    const std::string gbuffer = CompactSource(ReadRepositoryTextFile(objectShaderCandidates[2]));
    if (!Check(gbuffer.find("output.normalW=normalize(mul(float4(input.normal,0.0f),object.normalMatrix).xyz)") !=
                       std::string::npos &&
                   gbuffer.find("tangentW-=output.normalW*dot(tangentW,output.normalW)") != std::string::npos &&
                   gbuffer.find("tangentW=cross(fallbackAxis,output.normalW)") != std::string::npos &&
                   gbuffer.find("output.normalW=normalize(cross(output.tangentW,bitangentW))") == std::string::npos,
               "Modern GBuffer does not use inverse-transpose normals with a stable orthogonal tangent")) {
        return false;
    }

    const std::array<const char*, 4> reflectionCandidates = {
        "EngineContent/Shaders/ModernReflection.hlsli",
        "../../../EngineContent/Shaders/ModernReflection.hlsli",
        "../../../../EngineContent/Shaders/ModernReflection.hlsli",
        "../../../../../EngineContent/Shaders/ModernReflection.hlsli",
    };
    const std::string reflection = CompactSource(ReadRepositoryTextFile(reflectionCandidates));
    const std::string clustered = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/ClusteredDeferred.hlsl",
        "../../../EngineContent/Shaders/ClusteredDeferred.hlsl",
        "../../../../EngineContent/Shaders/ClusteredDeferred.hlsl",
        "../../../../../EngineContent/Shaders/ClusteredDeferred.hlsl",
    }));
    const std::string screenSpace = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
    }));
    if (!Check(
            reflection.find("incidentDirection=normalize(worldPosition-cameraPosition)") != std::string::npos &&
                reflection.find("normal=normalize(worldNormal)") != std::string::npos &&
                reflection.find("returnnormalize(reflect(incidentDirection,normal))") != std::string::npos &&
                clustered.find("ModernWorldReflectionDirection(worldPosition,g_CameraPosition,normal)") !=
                    std::string::npos &&
                screenSpace.find("ModernWorldReflectionDirection(worldPosition,g_CameraPositionAmbient.xyz,normal)") !=
                    std::string::npos,
            "Modern cubemap sampling points do not share the normalized world-space reflection convention")) {
        return false;
    }

    if (!Check(ShaderCompilerSlang::IsAvailable(),
               "Slang compiler is unavailable; Modern GBuffer DXIL/SPIR-V cannot be validated")) {
        return false;
    }
    const auto gbufferPath = FindRepositoryFile(objectShaderCandidates[2]);
    const std::array<ShaderBackend, 2> backends = {ShaderBackend::D3D12, ShaderBackend::Vulkan};
    for (ShaderBackend backend : backends) {
        for (const auto& stage : {std::pair{"VSMain", ShaderStage::Vertex}, std::pair{"PSMain", ShaderStage::Pixel}}) {
            std::vector<uint8_t> bytecode;
            std::string error;
            if (!Check(ShaderCompilerSlang::CompileStageFromFile(gbufferPath, stage.first, stage.second, backend,
                                                                 bytecode, {}, &error),
                       "Modern GBuffer normal-matrix shader compile failed: " + error)) {
                return false;
            }
            if (!Check(!bytecode.empty(), "Modern GBuffer normal-matrix shader produced empty bytecode"))
                return false;
        }
    }
    return true;
}

bool TestModernEnvironmentLightingMatchesClassicContract() {
    const std::array<const char*, 4> shaderCandidates = {
        "EngineContent/Shaders/ClusteredDeferred.hlsl",
        "../../../EngineContent/Shaders/ClusteredDeferred.hlsl",
        "../../../../EngineContent/Shaders/ClusteredDeferred.hlsl",
        "../../../../../EngineContent/Shaders/ClusteredDeferred.hlsl",
    };
    const auto shaderPath = FindRepositoryFile(shaderCandidates);
    if (!Check(!shaderPath.empty(), "ClusteredDeferred shader source was not found"))
        return false;
    const std::string shader = CompactSource(ReadRepositoryTextFile(shaderCandidates));
    if (!Check(shader.find("StructuredBuffer<float4>g_EnvironmentSH2:register(t11)") != std::string::npos &&
                   shader.find("float3globalEnvironmentDiffuse=EvaluateEnvironmentSH2(normal)") != std::string::npos &&
                   shader.find("EvaluateLocalSHVolumes") != std::string::npos &&
                   shader.find("g_IBLCubemap.SampleLevel(g_LinearSampler,normal,6.0f)") == std::string::npos,
               "Modern Deferred diffuse IBL does not use the same convolved SH2 contract as Classic Deferred")) {
        return false;
    }
    const std::array<const char*, 5> shBasis = {"0.282095f", "0.488603f", "1.092548f", "0.315392f", "0.546274f"};
    if (!Check(std::all_of(shBasis.begin(), shBasis.end(),
                           [&](const char* coefficient) { return shader.find(coefficient) != std::string::npos; }),
               "Modern Deferred SH2 evaluation is missing the normalized Classic Deferred basis")) {
        return false;
    }

    const std::string pipeline = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    }));
    const size_t clustered = pipeline.find("RGTextureHandleModernDeferredPipeline::AddClusteredLightingPasses(");
    const size_t screenSpace =
        pipeline.find("RGTextureHandleModernDeferredPipeline::AddScreenSpaceEffects(", clustered);
    if (!Check(clustered != std::string::npos && screenSpace != std::string::npos,
               "Modern clustered lighting implementation was not found")) {
        return false;
    }
    const std::string lighting = pipeline.substr(clustered, screenSpace - clustered);
    if (!Check(lighting.find("builder.ReadBuffer(lightingEnvironmentSH)") != std::string::npos &&
                   lighting.find("bindings->SetBuffer(\"g_EnvironmentSH2\",lightingEnvironmentSHSrv)") !=
                       std::string::npos &&
                   lighting.find("bindings->SetSampler(\"g_LinearSampler\",m_LinearClampSampler)") != std::string::npos,
               "Modern clustered lighting does not declare/bind SH2 with the fixed clamp sampler")) {
        return false;
    }

    const std::string d3d12 = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/D3D12Context.cpp",
        "../../../src/Runtime/Renderer/D3D12Context.cpp",
        "../../../../src/Runtime/Renderer/D3D12Context.cpp",
        "../../../../../src/Runtime/Renderer/D3D12Context.cpp",
    }));
    const size_t uploadTexture = d3d12.find("std::shared_ptr<GpuTexture>D3D12Context::UploadTexture(");
    const size_t capabilities = d3d12.find("RHIDeviceCapabilitiesD3D12Context::GetCapabilities()", uploadTexture);
    if (!Check(uploadTexture != std::string::npos && capabilities != std::string::npos &&
                   d3d12.substr(uploadTexture, capabilities - uploadTexture)
                           .find("ToD3D12State(RHIResourceState::ShaderResource)") != std::string::npos,
               "D3D12 uploaded shader resources are not transitioned for compute shader reads")) {
        return false;
    }

    if (!Check(ShaderCompilerSlang::IsAvailable(),
               "Slang compiler is unavailable; Modern environment DXIL/SPIR-V cannot be validated")) {
        return false;
    }
    const std::array<ShaderBackend, 2> backends = {ShaderBackend::D3D12, ShaderBackend::Vulkan};
    for (ShaderBackend backend : backends) {
        std::vector<uint8_t> bytecode;
        CookedShaderStageReflection reflection;
        std::string error;
        if (!Check(ShaderCompilerSlang::CompileStageFromFile(shaderPath, "CSDeferredLighting", ShaderStage::Compute,
                                                             backend, bytecode, {}, &error, &reflection),
                   "ClusteredDeferred environment compile failed: " + error)) {
            return false;
        }
        const uint32_t textureShift = backend == ShaderBackend::Vulkan ? 16u : 0u;
        const uint32_t samplerShift = backend == ShaderBackend::Vulkan ? 64u : 0u;
        const auto hasBinding = [&](const char* name, CookedShaderBindingType type, uint32_t bindPoint) {
            return std::find_if(reflection.bindings.begin(), reflection.bindings.end(), [&](const auto& binding) {
                       return binding.name == name && binding.type == type && binding.bindPoint == bindPoint &&
                              binding.bindSpace == 0;
                   }) != reflection.bindings.end();
        };
        if (!Check(!bytecode.empty() &&
                       hasBinding("g_IBLCubemap", CookedShaderBindingType::Texture, textureShift + 9u) &&
                       hasBinding("g_EnvironmentSH2", CookedShaderBindingType::StructuredBuffer, textureShift + 11u) &&
                       hasBinding("g_LinearSampler", CookedShaderBindingType::Sampler, samplerShift),
                   "ClusteredDeferred reflection lost a required environment lighting binding")) {
            return false;
        }
    }
    return true;
}

bool TestModernClusterBuffersStartInNativeUavState() {
#ifndef MYENGINE_PLATFORM_WINDOWS
    return true;
#else
    MockRenderContext context;
    context.backend = RHIBackend::D3D12;
    ShaderManager::Get().Clear();
    ShaderManager::Get().SetDevice(&context);
    struct ShaderManagerReset {
        ~ShaderManagerReset() {
            ShaderManager::Get().Clear();
            ShaderManager::Get().SetDevice(nullptr);
        }
    } shaderManagerReset;

    ModernDeferredPipeline pipeline(&context);
    if (!Check(pipeline.IsReady(),
               "Modern Deferred test pipeline is unavailable: " + pipeline.GetInitializationError()))
        return false;
    for (uint32_t index = 0; index < kGpuSceneMaterialSamplerCount; ++index) {
        const RHIFilter filter = (index & kGpuSceneMaterialSamplerPointBit) != 0 ? RHIFilter::Point : RHIFilter::Linear;
        const RHIAddressMode addressU =
            (index & kGpuSceneMaterialSamplerClampUBit) != 0 ? RHIAddressMode::Clamp : RHIAddressMode::Repeat;
        const RHIAddressMode addressV =
            (index & kGpuSceneMaterialSamplerClampVBit) != 0 ? RHIAddressMode::Clamp : RHIAddressMode::Repeat;
        const auto found =
            std::find_if(context.samplerDescs.begin(), context.samplerDescs.end(), [&](const RHISamplerDesc& desc) {
                return desc.filter == filter && desc.addressU == addressU && desc.addressV == addressV &&
                       desc.addressW == RHIAddressMode::Repeat;
            });
        if (!Check(found != context.samplerDescs.end(),
                   "Modern pipeline did not create every fixed material sampler-table entry")) {
            return false;
        }
    }
    pipeline.Resize(64, 64);
    Scene scene("ModernClusterInitialState");
    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 1.0f);
    if (!Check(pipeline.Prepare(scene, camera, 1),
               "Modern Deferred test pipeline preparation failed: " + pipeline.GetInitializationError()))
        return false;

    RenderGraph graph(context);
    const auto createInput = [&](const char* name, RHIFormat format) {
        RHITextureDesc desc;
        desc.width = 64;
        desc.height = 64;
        desc.format = format;
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = name;
        auto texture = context.CreateTexture(desc);
        RHITextureViewDesc viewDesc;
        viewDesc.usage = RHIResourceUsage::ShaderResource;
        auto view = context.CreateTextureView(texture, viewDesc);
        return std::pair<std::shared_ptr<GpuTexture>, std::shared_ptr<GpuTextureView>>{std::move(texture),
                                                                                       std::move(view)};
    };
    auto albedo = createInput("ClusterTestAlbedo", RHIFormat::RGBA8UNorm);
    auto normal = createInput("ClusterTestNormal", RHIFormat::RGBA16Float);
    auto material = createInput("ClusterTestMaterial", RHIFormat::RGBA8UNorm);
    auto emissive = createInput("ClusterTestEmissive", RHIFormat::RGBA16Float);
    auto depth = createInput("ClusterTestDepth", RHIFormat::D24S8);
    const auto importInput = [&](const char* name, const auto& resource) {
        return graph.ImportTexture(name, resource.first, resource.second, RHIResourceState::ShaderResource,
                                   RHIResourceState::ShaderResource);
    };
    const auto albedoHandle = importInput("ClusterTestAlbedo", albedo);
    const auto normalHandle = importInput("ClusterTestNormal", normal);
    const auto materialHandle = importInput("ClusterTestMaterial", material);
    const auto emissiveHandle = importInput("ClusterTestEmissive", emissive);
    const auto depthHandle = importInput("ClusterTestDepth", depth);
    RHITextureDesc environmentDesc;
    environmentDesc.width = 4;
    environmentDesc.height = 4;
    environmentDesc.arrayLayers = 6;
    environmentDesc.cube = true;
    environmentDesc.format = RHIFormat::RGBA16Float;
    environmentDesc.usage = RHIResourceUsage::ShaderResource;
    environmentDesc.debugName = "ClusterTestEnvironment";
    auto environment = context.CreateTexture(environmentDesc);
    RHITextureViewDesc environmentViewDesc;
    environmentViewDesc.layerCount = 6;
    environmentViewDesc.usage = RHIResourceUsage::ShaderResource;
    auto environmentView = context.CreateTextureView(environment, environmentViewDesc);
    const auto environmentHandle =
        graph.ImportTexture("ClusterTestEnvironment", environment, environmentView, RHIResourceState::ShaderResource,
                            RHIResourceState::ShaderResource);
    pipeline.AddClusteredLightingPasses(graph, camera, albedoHandle, albedo.second, normalHandle, normal.second,
                                        materialHandle, material.second, emissiveHandle, emissive.second, depthHandle,
                                        depth.second, environmentHandle, environmentView, {}, nullptr, {});
    if (!Check(graph.Execute(context.commands), "Modern cluster graph execution failed: " + graph.GetLastError()))
        return false;

    const std::array<std::string, 3> clusterBuffers = {"ClusterCounts", "ClusterOffsets", "ClusterLightIndices"};
    std::array<bool, 3> sawUavToSrv{};
    bool sawUndefinedToUav = false;
    for (size_t index = 0; index < context.commands.transitions.size(); ++index) {
        auto* buffer = index < context.commands.transitionResources.size()
                           ? dynamic_cast<GpuBuffer*>(context.commands.transitionResources[index])
                           : nullptr;
        if (!buffer)
            continue;
        for (size_t clusterIndex = 0; clusterIndex < clusterBuffers.size(); ++clusterIndex) {
            if (buffer->desc.debugName != clusterBuffers[clusterIndex])
                continue;
            const auto [before, after] = context.commands.transitions[index];
            sawUndefinedToUav |= before == RHIResourceState::Undefined && after == RHIResourceState::UnorderedAccess;
            sawUavToSrv[clusterIndex] |=
                before == RHIResourceState::UnorderedAccess && after == RHIResourceState::ShaderResource;
        }
    }
    return Check(!sawUndefinedToUav &&
                     std::all_of(sawUavToSrv.begin(), sawUavToSrv.end(), [](bool value) { return value; }),
                 "Modern cluster buffers were not imported from their native first-frame UAV state");
#endif
}

bool TestPersistentNativePipelineCacheContracts() {
#ifndef MYENGINE_PLATFORM_WINDOWS
    return true;
#else
    const std::string d3d12 = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/D3D12Context.cpp",
        "../../../src/Runtime/Renderer/D3D12Context.cpp",
        "../../../../src/Runtime/Renderer/D3D12Context.cpp",
        "../../../../../src/Runtime/Renderer/D3D12Context.cpp",
    }));
    if (!Check(d3d12.find("sh->rootSignature=m_IndirectObjectRootSignature") != std::string::npos &&
                   d3d12.find("shader->computeRootSignature=m_ComputeRootSignature") != std::string::npos,
               "D3D12 shaders do not reuse the device-level fixed root signatures")) {
        return false;
    }
    if (!Check(d3d12.find("CreateComputePipelineStateCached") != std::string::npos &&
                   d3d12.find("desc.CachedPSO={cachedBlob.data(),cachedBlob.size()}") != std::string::npos &&
                   d3d12.find("pipelineState->GetCachedBlob") != std::string::npos &&
                   d3d12.find("WritePipelineBlobAtomic") != std::string::npos &&
                   d3d12.find("MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH") != std::string::npos &&
                   d3d12.find("if(cacheRejected)") != std::string::npos,
               "D3D12 compute PSOs lack persistent, atomic, corruption-tolerant cached-blob reuse")) {
        return false;
    }

    const std::string vulkan = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/VulkanContext.cpp",
        "../../../src/Runtime/Renderer/VulkanContext.cpp",
        "../../../../src/Runtime/Renderer/VulkanContext.cpp",
        "../../../../../src/Runtime/Renderer/VulkanContext.cpp",
    }));
    return Check(
        vulkan.find("vkCreatePipelineCache") != std::string::npos &&
            vulkan.find("vkGetPipelineCacheData") != std::string::npos &&
            vulkan.find("vkCreateGraphicsPipelines(m_Impl->device,m_Impl->pipelineCache") != std::string::npos &&
            vulkan.find("vkCreateComputePipelines(m_Impl->device,m_Impl->pipelineCache") != std::string::npos &&
            vulkan.find("WriteVulkanPipelineCache") != std::string::npos,
        "Vulkan graphics/compute pipelines do not share a persisted VkPipelineCache");
#endif
}

bool TestD3D12DebugEventUsesAnsiMetadata() {
#ifndef MYENGINE_PLATFORM_WINDOWS
    return true;
#else
    const std::string d3d12 = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/D3D12Context.cpp",
        "../../../src/Runtime/Renderer/D3D12Context.cpp",
        "../../../../src/Runtime/Renderer/D3D12Context.cpp",
        "../../../../../src/Runtime/Renderer/D3D12Context.cpp",
    }));
    return Check(d3d12.find("constexprUINTkPixEventAnsiVersion=1") != std::string::npos &&
                     d3d12.find("BeginEvent(kPixEventAnsiVersion,name,") != std::string::npos &&
                     d3d12.find("(std::strlen(name)+1)*sizeof(name[0])") != std::string::npos &&
                     d3d12.find("BeginEvent(0,name,") == std::string::npos,
                 "D3D12 debug events must identify narrow marker text as ANSI and include its terminator");
#endif
}

bool TestBackendIndependentPassRecording() {
    MockRenderContext context;
    auto shader = std::make_shared<MockShader>();
    GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.shader = shader;
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
    if (!Check(pipeline && pipeline->desc.topology == RHIPrimitiveTopology::TriangleStrip &&
                   pipeline->desc.depthStencil.depthCompareOp == RHICompareOp::GreaterEqual &&
                   pipeline->desc.depthStencil.frontFace.passOp == RHIStencilOp::Replace &&
                   pipeline->desc.depthStencil.stencilReference == 7 &&
                   pipeline->desc.rasterizer.cullMode == RHICullMode::Front &&
                   pipeline->desc.rasterizer.depthBias == 8 && pipeline->desc.blend.attachments[0].blendEnable &&
                   pipeline->desc.multisample.sampleCount == 4,
               "graphics pipeline state was not preserved by the RHI"))
        return false;
    auto bindings = context.CreateBindGroup(shader);
    RHITextureDesc targetDesc;
    targetDesc.width = 32;
    targetDesc.height = 32;
    targetDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    RenderGraph graph(context);
    const auto target = graph.CreateTexture("ValidationTarget", targetDesc);
    graph.SetFinalState(target, RHIResourceState::ShaderResource);
    graph.AddPass(
        "RHIValidationPass", [&](RenderGraphBuilder& builder) { builder.WriteColor(target, RHILoadOp::Clear); },
        [&](GpuCommandList& commands, const RenderGraphResources&) {
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
    bufferDesc.size = sizeof(initial);
    bufferDesc.stride = sizeof(uint32_t);
    bufferDesc.usage =
        RHIResourceUsage::UnorderedAccess | RHIResourceUsage::ShaderResource | RHIResourceUsage::CopySource;
    auto buffer = context.CreateBuffer(bufferDesc, initial.data());
    RHIBufferViewDesc viewDesc;
    viewDesc.elementCount = static_cast<uint32_t>(initial.size());
    viewDesc.usage = RHIResourceUsage::UnorderedAccess;
    auto view = context.CreateBufferView(buffer, viewDesc);
    auto shader = std::make_shared<MockShader>();
    shader->reflection.bindings = {{"SHOutput", ShaderBindingType::StorageBuffer, 0, 1, 0, ShaderStageCompute}};
    auto bindings = context.CreateBindGroup(shader);
    if (!Check(view && bindings->SetStorageBuffer("SHOutput", view), "compute storage-buffer named binding failed"))
        return false;
    std::string error;
    if (!Check(bindings->Validate(&error), "compute bind group validation failed: " + error))
        return false;
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.shader = shader;
    auto pipeline = context.CreateComputePipeline(pipelineDesc);
    auto* commands = context.GetGraphicsCommandList();
    commands->SetComputePipeline(pipeline.get());
    commands->SetBindGroup(0, bindings.get());
    commands->Dispatch(2, 3, 4);
    commands->UAVBarrier(buffer.get());
    if (!Check(context.commands.computePipelineBinds == 1 && context.commands.dispatches == 1 &&
                   context.commands.uavBarriers == 1 &&
                   context.commands.dispatchGroups == std::array<uint32_t, 3>{2, 3, 4},
               "compute pass did not record the expected RHI commands"))
        return false;
    auto ticket = context.ReadbackBufferAsync(buffer);
    std::vector<uint8_t> bytes;
    if (!Check(ticket && !ticket->IsReady() && !ticket->Read(bytes), "async readback completed synchronously"))
        return false;
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
    desc.size = 9 * 16;
    desc.stride = 16;
    desc.usage = RHIResourceUsage::UnorderedAccess | RHIResourceUsage::ShaderResource | RHIResourceUsage::CopySource;
    const auto output = graph.CreateBuffer("SHOutput", desc);
    std::vector<std::string> executed;
    graph.AddPass(
        "ProjectSH", [&](RenderGraphBuilder& builder) { builder.ReadWriteUAV(output); },
        [&](GpuCommandList& commands, const RenderGraphResources& resources) {
            if (resources.GetBuffer(output) && resources.GetBufferView(output)) {
                commands.Dispatch(1, 1, 1);
                executed.push_back("ProjectSH");
            }
        });
    graph.AddPass(
        "ConsumeSH", [&](RenderGraphBuilder& builder) { builder.ReadBuffer(output); },
        [&](GpuCommandList&, const RenderGraphResources& resources) {
            if (resources.GetBuffer(output))
                executed.push_back("ConsumeSH");
        });
    if (!Check(graph.Execute(context.commands) && executed == std::vector<std::string>({"ProjectSH", "ConsumeSH"}) &&
                   context.bufferCreates == 1 && context.commands.dispatches == 1,
               "RenderGraph compute-buffer dependency execution failed"))
        return false;
    RenderGraph invalid(context);
    const auto unread = invalid.CreateBuffer("UnreadBuffer", desc);
    invalid.AddPass("InvalidBufferRead", [&](RenderGraphBuilder& builder) { builder.ReadBuffer(unread); }, {});
    return Check(!invalid.Compile() && invalid.GetLastErrorCode() == RGError::UninitializedBufferRead,
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
    graph.AddPass(
        "ReadDisjointMips",
        [texture](RenderGraphBuilder& builder) {
            builder.ReadTexture(texture, RGTextureSubresource{0, 1, 0, 1});
            builder.ReadTexture(texture, RGTextureSubresource{1, 1, 0, 1});
        },
        [&](GpuCommandList&, const RenderGraphResources& resources) {
            foundMip1View = resources.GetView(texture, RGTextureSubresource{1, 1, 0, 1}) != nullptr;
        });
    if (!Check(graph.Execute(context.commands),
               "RenderGraph rejected disjoint subresource reads: " + graph.GetLastError())) {
        return false;
    }
    if (!Check(foundMip1View, "RenderGraph did not expose a subresource view"))
        return false;

    MockRenderContext transitionContext;
    RenderGraph transitions(transitionContext);
    const auto transitionTexture = transitions.CreateTexture("MipChainTransitions", desc);
    transitions.SetFinalState(transitionTexture, RHIResourceState::ShaderResource);
    transitions.AddPass("WriteMip0",
                        [transitionTexture](RenderGraphBuilder& builder) {
                            builder.WriteColor(transitionTexture, RGTextureSubresource{0, 1, 0, 1}, RHILoadOp::Clear);
                        },
                        {});
    transitions.AddPass("WriteMip1",
                        [transitionTexture](RenderGraphBuilder& builder) {
                            builder.WriteColor(transitionTexture, RGTextureSubresource{1, 1, 0, 1}, RHILoadOp::Clear);
                        },
                        {});
    if (!Check(transitions.Execute(transitionContext.commands),
               "RenderGraph failed subresource transitions: " + transitions.GetLastError())) {
        return false;
    }
    if (!Check(transitionContext.commands.textureTransitions.size() >= 2 &&
                   transitionContext.commands.textureTransitions[0].firstMip == 0 &&
                   transitionContext.commands.textureTransitions[1].firstMip == 1,
               "RenderGraph did not transition individual texture subresources"))
        return false;
    if (!Check(transitionContext.textureViewCreates >= 2, "RenderGraph did not create access-local subresource views"))
        return false;

    MockRenderContext mixedStateContext;
    RenderGraph mixedState(mixedStateContext);
    RHITextureDesc uavDesc = desc;
    uavDesc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess;
    const auto mixedTexture = mixedState.CreateTexture("MixedMipStates", uavDesc);
    mixedState.AddComputePass("WriteMip0UAV",
                              [mixedTexture](RenderGraphBuilder& builder) {
                                  builder.ReadWriteUAV(mixedTexture, RGTextureSubresource{0, 1, 0, 1});
                              },
                              {});
    mixedState.AddComputePass("WriteMip1UAV",
                              [mixedTexture](RenderGraphBuilder& builder) {
                                  builder.ReadWriteUAV(mixedTexture, RGTextureSubresource{1, 1, 0, 1});
                              },
                              {});
    mixedState.AddComputePass("ReadFullMipChain",
                              [mixedTexture](RenderGraphBuilder& builder) { builder.ReadTexture(mixedTexture); }, {});
    if (!Check(mixedState.Execute(mixedStateContext.commands),
               "RenderGraph failed mixed subresource-to-full transition: " + mixedState.GetLastError()))
        return false;
    const bool transitionedMip0ToSrv =
        std::find_if(mixedStateContext.commands.textureTransitions.begin(),
                     mixedStateContext.commands.textureTransitions.end(), [](const RHITextureViewDesc& range) {
                         return range.firstMip == 0 && range.mipCount == 1;
                     }) != mixedStateContext.commands.textureTransitions.end();
    const bool transitionedMip1ToSrv =
        std::find_if(mixedStateContext.commands.textureTransitions.begin(),
                     mixedStateContext.commands.textureTransitions.end(), [](const RHITextureViewDesc& range) {
                         return range.firstMip == 1 && range.mipCount == 1;
                     }) != mixedStateContext.commands.textureTransitions.end();
    if (!Check(transitionedMip0ToSrv && transitionedMip1ToSrv,
               "RenderGraph did not expand mixed mip states before full-texture SRV access"))
        return false;

    RenderGraph overlap(context);
    const auto overlapTexture = overlap.CreateTexture("Overlap", desc);
    overlap.AddPass("OverlapMips",
                    [overlapTexture](RenderGraphBuilder& builder) {
                        builder.WriteColor(overlapTexture, RGTextureSubresource{0, 2, 0, 1});
                        builder.ReadTexture(overlapTexture, RGTextureSubresource{1, 1, 0, 1});
                    },
                    {});
    if (!Check(!overlap.Compile() && overlap.GetLastErrorCode() == RGError::DuplicateResourceAccess,
               "RenderGraph accepted overlapping texture subresource access"))
        return false;

    RenderGraph invalidRange(context);
    const auto invalidTexture = invalidRange.CreateTexture("InvalidRange", desc);
    invalidRange.AddPass("InvalidRange",
                         [invalidTexture](RenderGraphBuilder& builder) {
                             builder.ReadTexture(invalidTexture, RGTextureSubresource{2, 1, 0, 1});
                         },
                         {});
    return Check(!invalidRange.Compile() && invalidRange.GetLastErrorCode() == RGError::InvalidTextureHandle,
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
    graph.AddPass(
        "ProduceUsed", [used](RenderGraphBuilder& builder) { builder.WriteColor(used, RHILoadOp::Clear); },
        [&](GpuCommandList&, const RenderGraphResources&) { executed.push_back("ProduceUsed"); });
    graph.AddPass(
        "ProduceUnused", [unused](RenderGraphBuilder& builder) { builder.WriteColor(unused, RHILoadOp::Clear); },
        [&](GpuCommandList&, const RenderGraphResources&) { executed.push_back("ProduceUnused"); });
    graph.AddPass(
        "ConsumeUsed", [used](RenderGraphBuilder& builder) { builder.ReadTexture(used); },
        [&](GpuCommandList&, const RenderGraphResources&) { executed.push_back("ConsumeUsed"); });
    if (!Check(graph.Compile(), "RenderGraph culling compile failed: " + graph.GetLastError()))
        return false;
    if (!Check(graph.GetExecutionOrder() == std::vector<std::string>({"ProduceUsed", "ConsumeUsed"}),
               "RenderGraph culling execution order mismatch"))
        return false;
    if (!Check(graph.GetCulledPasses() == std::vector<std::string>({"ProduceUnused"}),
               "RenderGraph did not report the culled pass"))
        return false;
    if (!Check(graph.Execute(context.commands) && executed == std::vector<std::string>({"ProduceUsed", "ConsumeUsed"}),
               "RenderGraph executed a culled pass"))
        return false;
    if (!Check(context.graphTextureCreates == 1, "RenderGraph created a culled transient texture"))
        return false;

    RenderGraph finalOutput(context);
    const auto output = finalOutput.CreateTexture("FinalOutput", desc);
    finalOutput.SetFinalState(output, RHIResourceState::ShaderResource);
    bool finalRan = false;
    finalOutput.AddPass(
        "ProduceFinal", [output](RenderGraphBuilder& builder) { builder.WriteColor(output); },
        [&](GpuCommandList&, const RenderGraphResources&) { finalRan = true; });
    return Check(finalOutput.Execute(context.commands) && finalRan && finalOutput.GetCulledPasses().empty(),
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
    graph.AddPass("WriteA", [first](RenderGraphBuilder& builder) { builder.WriteColor(first, RHILoadOp::Clear); }, {});
    if (!Check(graph.Execute(context.commands) && context.graphTextureCreates == 1,
               "RenderGraph initial descriptor-keyed texture allocation failed"))
        return false;

    graph.Reset();
    auto second = graph.CreateTexture("FrameBColorDifferentName", textureDesc);
    graph.SetFinalState(second, RHIResourceState::ShaderResource);
    graph.AddPass("WriteB", [second](RenderGraphBuilder& builder) { builder.WriteColor(second, RHILoadOp::Clear); },
                  {});
    if (!Check(graph.Execute(context.commands) && context.graphTextureCreates == 1,
               "RenderGraph did not reuse same-desc texture with a different name"))
        return false;

    RHIBufferDesc bufferDesc;
    bufferDesc.size = 128;
    bufferDesc.stride = 16;
    bufferDesc.usage = RHIResourceUsage::UnorderedAccess | RHIResourceUsage::ShaderResource;
    graph.Reset();
    auto firstBuffer = graph.CreateBuffer("FrameABuffer", bufferDesc);
    graph.SetFinalState(firstBuffer, RHIResourceState::ShaderResource);
    graph.AddPass("WriteBufferA", [firstBuffer](RenderGraphBuilder& builder) { builder.ReadWriteUAV(firstBuffer); },
                  {});
    if (!Check(graph.Execute(context.commands) && context.bufferCreates == 1,
               "RenderGraph initial descriptor-keyed buffer allocation failed"))
        return false;

    graph.Reset();
    auto secondBuffer = graph.CreateBuffer("FrameBBufferDifferentName", bufferDesc);
    graph.SetFinalState(secondBuffer, RHIResourceState::ShaderResource);
    graph.AddPass("WriteBufferB", [secondBuffer](RenderGraphBuilder& builder) { builder.ReadWriteUAV(secondBuffer); },
                  {});
    if (!Check(graph.Execute(context.commands) && context.bufferCreates == 1,
               "RenderGraph did not reuse same-desc buffer with a different name"))
        return false;

    MockRenderContext culledContext;
    RenderGraph culledGraph(culledContext);
    auto culled = culledGraph.CreateTexture("CulledTransient", textureDesc);
    culledGraph.AddPass("WriteCulled", [culled](RenderGraphBuilder& builder) { builder.WriteColor(culled); }, {});
    if (!Check(culledGraph.Execute(culledContext.commands) && culledContext.graphTextureCreates == 0,
               "RenderGraph created a culled transient before pooling check"))
        return false;
    culledGraph.Reset();
    auto afterCulled = culledGraph.CreateTexture("AfterCulled", textureDesc);
    culledGraph.SetFinalState(afterCulled, RHIResourceState::ShaderResource);
    culledGraph.AddPass("WriteAfterCulled",
                        [afterCulled](RenderGraphBuilder& builder) { builder.WriteColor(afterCulled); }, {});
    return Check(culledGraph.Execute(culledContext.commands) && culledContext.graphTextureCreates == 1,
                 "RenderGraph let a culled transient pollute the descriptor pool");
}

bool TestEnvironmentRadianceHorizonContract() {
    const char* candidates[] = {
        "EngineContent/Shaders/EnvironmentRadiance.hlsli",
        "../../../EngineContent/Shaders/EnvironmentRadiance.hlsli",
        "../../../../EngineContent/Shaders/EnvironmentRadiance.hlsli",
        "../../../../../EngineContent/Shaders/EnvironmentRadiance.hlsli",
    };
    std::string source;
    for (const char* path : candidates) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        break;
    }
    if (!Check(!source.empty(), "EnvironmentRadiance shader source was not found")) {
        return false;
    }
    if (!Check(source.find("float3 EnvironmentRadiance(float3 direction, float3 sunDirection)") != std::string::npos,
               "environment radiance does not accept runtime sun direction")) {
        return false;
    }
    if (!Check(source.find("float3 sunDirection = normalize(float3(") == std::string::npos,
               "environment radiance still hard-codes sun direction")) {
        return false;
    }
    if (!Check(source.find("float skyMask = smoothstep") != std::string::npos,
               "environment radiance does not mask sky scattering at the horizon")) {
        return false;
    }
    if (!Check(source.find("max(direction.y, 0.0f)") != std::string::npos,
               "environment radiance still allows negative-Y air mass")) {
        return false;
    }
    if (!Check(source.find("float3 groundRadiance") != std::string::npos &&
                   source.find("float3 skyRadiance") != std::string::npos,
               "environment radiance does not separate ground and sky hemispheres")) {
        return false;
    }
    return Check(source.find("lerp(groundRadiance + horizonHaze, skyRadiance + horizonHaze, skyMask)") !=
                     std::string::npos,
                 "environment radiance does not blend ground and sky by skyMask");
}

bool TestEnvironmentPassSunDirectionDirtyState() {
    const char* candidates[] = {
        "src/Runtime/Renderer/EnvironmentPass.cpp",
        "../../../src/Runtime/Renderer/EnvironmentPass.cpp",
        "../../../../src/Runtime/Renderer/EnvironmentPass.cpp",
        "../../../../../src/Runtime/Renderer/EnvironmentPass.cpp",
    };
    std::string source;
    for (const char* path : candidates) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        break;
    }
    if (!Check(!source.empty(), "EnvironmentPass source was not found"))
        return false;
    if (!Check(source.find("void EnvironmentPass::MarkDirty()") != std::string::npos &&
                   source.find("m_Generated = false;") != std::string::npos &&
                   source.find("m_EnvironmentInShaderState = false;") != std::string::npos &&
                   source.find("m_SHBufferInShaderState = false;") != std::string::npos,
               "environment dirty path does not reset generated shader state")) {
        return false;
    }
    if (!Check(source.find("void EnvironmentPass::SetSunDirection(const Vec3& direction)") != std::string::npos &&
                   source.find("direction.Normalized()") != std::string::npos &&
                   source.find("DefaultSunDirection()") != std::string::npos,
               "environment pass does not normalize or default sun direction")) {
        return false;
    }
    if (!Check(source.find("referenceDirection") != std::string::npos &&
                   source.find("LengthSq() < 1e-6f") != std::string::npos &&
                   source.find("return;") != std::string::npos,
               "environment pass does not preserve generated state for unchanged sun direction")) {
        return false;
    }
    return Check(source.find("if (m_Generated)") != std::string::npos &&
                     source.find("MarkDirty();") != std::string::npos &&
                     source.find("m_GeneratedSunDirection = m_SunDirection;") != std::string::npos,
                 "environment pass does not dirty generated resources after sun direction changes");
}

bool TestRendererSynchronizesEnvironmentSunBeforePrepare() {
    const char* candidates[] = {
        "src/Runtime/Renderer/Renderer.cpp",
        "../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../../src/Runtime/Renderer/Renderer.cpp",
    };
    std::string source;
    for (const char* path : candidates) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        break;
    }
    if (!Check(!source.empty(), "Renderer source was not found"))
        return false;
    const size_t collect = source.find("CollectEnvironmentSunDirection(scene)");
    const size_t environmentSet = source.find("m_EnvironmentPass->SetSunDirection(environmentSunDirection)");
    const size_t mainSet = source.find("m_MainPass->SetSunDirection(environmentSunDirection)");
    const size_t prepare = source.find("m_EnvironmentPass->PrepareGraphResources()");
    return Check(collect != std::string::npos && environmentSet != std::string::npos && mainSet != std::string::npos &&
                     prepare != std::string::npos && collect < environmentSet && environmentSet < prepare &&
                     mainSet < prepare,
                 "renderer does not synchronize sun direction before preparing environment graph resources");
}

bool TestShadowedMainPassDirectShadowVisibilityContract() {
    const char* candidates[] = {
        "EngineContent/Shaders/ShadowedMainPass.hlsl",
        "../../../EngineContent/Shaders/ShadowedMainPass.hlsl",
        "../../../../EngineContent/Shaders/ShadowedMainPass.hlsl",
        "../../../../../EngineContent/Shaders/ShadowedMainPass.hlsl",
    };
    std::string source;
    for (const char* path : candidates) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        break;
    }
    if (!Check(!source.empty(), "ShadowedMainPass shader source was not found")) {
        return false;
    }
    if (!Check(source.find("DIRECT_SHADOW_MIN_VISIBILITY = 0.03f") != std::string::npos,
               "direct shadow minimum visibility should stay near fully occluded")) {
        return false;
    }
    if (!Check(source.find("? 1.0f : 0.35f") == std::string::npos &&
                   source.find("lerp(0.35f, 1.0f") == std::string::npos,
               "direct shadows still preserve 35 percent direct lighting")) {
        return false;
    }
    if (!Check(source.find("float4 g_ShadowIntensity;") != std::string::npos &&
                   source.find("saturate(g_ShadowIntensity.x)") != std::string::npos &&
                   source.find("saturate(g_ShadowIntensity.y)") != std::string::npos &&
                   source.find("saturate(g_ShadowIntensity.z)") != std::string::npos,
               "shadowed shader does not expose per-light shadow intensity")) {
        return false;
    }
    return Check(source.find("g_LightColor.rgb * max(g_LightDirection.w, 0.0f) * shadow") != std::string::npos,
                 "directional direct light is not multiplied by shadow visibility");
}

bool TestShadowDepthAlphaTestIncludesVertexAlpha() {
    const std::array<const char*, 4> candidates = {
        "EngineContent/Shaders/ShadowDepth.hlsl",
        "../../../EngineContent/Shaders/ShadowDepth.hlsl",
        "../../../../EngineContent/Shaders/ShadowDepth.hlsl",
        "../../../../../EngineContent/Shaders/ShadowDepth.hlsl",
    };
    const std::string source = ReadRepositoryTextFile(candidates);
    if (!Check(!source.empty(), "ShadowDepth shader source was not found"))
        return false;
    if (!Check(source.find("float4 color : COLOR0;") != std::string::npos &&
                   source.find("float alpha : COLOR0;") != std::string::npos &&
                   source.find("o.alpha = v.color.a;") != std::string::npos,
               "shadow depth vertex stage does not carry mesh vertex alpha")) {
        return false;
    }
    if (!Check(source.find("g_BaseColorMap.Sample(g_BaseColorSampler, input.uv).a") != std::string::npos &&
                   source.find("g_AlphaTest.z * input.alpha") != std::string::npos,
               "shadow alpha test does not multiply texture, material, and vertex alpha")) {
        return false;
    }
    return Check(source.find("clip(alpha - g_AlphaTest.x);") != std::string::npos,
                 "shadow alpha test does not clip against the material threshold");
}

bool TestShaderGraphMaskedAlphaTestIncludesVertexAlpha() {
    ShaderGraph graph;
    uint64_t nextPinId = 1;
    graph.nodes.push_back(CreateShaderGraphNode("SurfaceOutputLit", 1, nextPinId));
    const std::vector<ShaderPropertyDesc> properties;
    const auto compile = [&](ShaderPass pass) {
        const ShaderGraphCompileRequest request{&graph, &properties, ShaderShadingModel::Lit, ShaderSurfaceType::Masked,
                                                pass};
        return ShaderGraphCompiler::Compile(request);
    };

    const auto gbuffer = compile(ShaderPass::GBuffer);
    const auto forward = compile(ShaderPass::Forward);
    const auto shadow = compile(ShaderPass::Shadow);
    if (!Check(gbuffer.succeeded && forward.succeeded && shadow.succeeded,
               "masked Shader Graph passes failed source generation")) {
        return false;
    }

    const std::string compactGBuffer = CompactSource(gbuffer.hlsl);
    const std::string compactForward = CompactSource(forward.hlsl);
    const std::string compactShadow = CompactSource(shadow.hlsl);
    const auto clipsVertexAlpha = [](const std::string& source) {
        return source.find("floatsurfaceOpacity=s.opacity*p.color.a;") != std::string::npos &&
               source.find("if(surfaceOpacity<s.alphaClip)discard;") != std::string::npos &&
               source.find("if(s.opacity<s.alphaClip)discard;") == std::string::npos;
    };
    if (!Check(clipsVertexAlpha(compactGBuffer) && clipsVertexAlpha(compactForward),
               "Shader Graph GBuffer/Forward alpha test ignores mesh vertex alpha")) {
        return false;
    }
    if (!Check(compactGBuffer.find(",surfaceOpacity);o.normal=") != std::string::npos &&
                   compactForward.find(",surfaceOpacity);}") != std::string::npos,
               "Shader Graph color passes do not reuse the clipped opacity in their output")) {
        return false;
    }
    return Check(compactShadow.find("float4color:COLOR0;") != std::string::npos &&
                     compactShadow.find("o.color=v.color;") != std::string::npos && clipsVertexAlpha(compactShadow),
                 "Shader Graph shadow pass does not preserve the visible masked silhouette");
}

bool TestSkinnedGBufferMotionUsesPreviousBonePalette() {
    AssetManager::Get().Clear();
    std::vector<MeshVertex> vertices(3);
    vertices[0].position = {-0.5f, -0.5f, 0.0f};
    vertices[1].position = {0.0f, 0.5f, 0.0f};
    vertices[2].position = {0.5f, -0.5f, 0.0f};
    auto mesh = std::make_shared<MeshAsset>("__test__/SkinnedMotionTriangle");
    mesh->SetGeometry(std::move(vertices), {0, 1, 2, 0, 1, 2}, {{0, 3, 0, 0, "First"}, {3, 3, 0, 0, "Second"}});
    const MeshHandle meshHandle = AssetManager::Get().Register(std::move(mesh));

    Scene scene("SkinnedMotion");
    Actor* actor = scene.CreateActor("Skinned");
    auto* skin = actor->AddComponent<SkinnedMeshRendererComponent>();
    skin->SetSourceMesh(meshHandle);
    skin->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    skin->SetSkeleton(std::vector<Bone>(1), std::vector<SkinWeight>(3));
    AnimationClip clip;
    clip.duration = 1.0f;
    clip.looping = false;
    BoneTrack track;
    track.boneIndex = 0;
    track.keys = {{0.0f, Vec3::Zero(), Quat::Identity(), Vec3::One()},
                  {1.0f, {2.0f, 0.0f, 0.0f}, Quat::Identity(), Vec3::One()}};
    clip.tracks.push_back(std::move(track));
    skin->SetAnimation(std::move(clip));

    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 1.0f);
    MockRenderContext context;
    context.commands.captureBindGroupConstants = true;
    ShaderManager::Get().Clear();
    ShaderManager::Get().SetDevice(&context);
    struct ShaderManagerReset {
        ~ShaderManagerReset() {
            ShaderManager::Get().Clear();
            ShaderManager::Get().SetDevice(nullptr);
        }
    } shaderManagerReset;

    GBufferPass pass(&context);
    skin->SetAnimationTime(0.0f);
    pass.Execute(context.commands, scene, camera);
    skin->SetAnimationTime(1.0f);
    pass.Execute(context.commands, scene, camera);

    struct PerDrawConstants {
        float viewProj[16];
        float world[16];
        float previousViewProj[16];
        float previousWorld[16];
        float baseColor[4];
        float material[4];
        float emissive[4];
        float mapFlags[4];
        float boneMatrices[128][16];
        float previousBoneMatrices[128][16];
        float skinInfo[4];
        float normalMatrix[16];
    };
    std::vector<PerDrawConstants> draws;
    for (const auto& snapshot : context.commands.bindGroupConstants) {
        const auto constants = snapshot.find("PerDraw");
        if (constants == snapshot.end() || constants->second.size() != sizeof(PerDrawConstants))
            continue;
        PerDrawConstants captured{};
        std::memcpy(&captured, constants->second.data(), sizeof(captured));
        draws.push_back(captured);
    }
    if (!Check(draws.size() == 4, "skinned GBuffer test did not capture both submeshes across two frames"))
        return false;
    for (size_t draw = 2; draw < draws.size(); ++draw) {
        if (!Check(NearlyEqual(draws[draw].skinInfo[0], 1.0f) && NearlyEqual(draws[draw].boneMatrices[0][12], 2.0f) &&
                       NearlyEqual(draws[draw].previousBoneMatrices[0][12], 0.0f),
                   "skinned GBuffer motion did not preserve the last rendered pose for every submesh")) {
            return false;
        }
    }

    const std::string builtin = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/GBufferPass.hlsl",
        "../../../EngineContent/Shaders/GBufferPass.hlsl",
        "../../../../EngineContent/Shaders/GBufferPass.hlsl",
        "../../../../../EngineContent/Shaders/GBufferPass.hlsl",
    }));
    if (!Check(
            builtin.find("g_PreviousBoneMatrices[128]") != std::string::npos &&
                builtin.find("previousLocalPosition=mul(previousLocalPosition,previousSkin);") != std::string::npos &&
                builtin.find("mul(mul(previousLocalPosition,g_PreviousWorld),g_PreviousViewProj)") != std::string::npos,
            "built-in GBuffer shader does not use the previous bone palette for velocity")) {
        return false;
    }

    ShaderGraph graph;
    uint64_t nextPinId = 1;
    graph.nodes.push_back(CreateShaderGraphNode("SurfaceOutputLit", 1, nextPinId));
    const std::vector<ShaderPropertyDesc> properties;
    const ShaderGraphCompileRequest request{&graph, &properties, ShaderShadingModel::Lit, ShaderSurfaceType::Opaque,
                                            ShaderPass::GBuffer};
    const ShaderGraphCompileResult generated = ShaderGraphCompiler::Compile(request);
    const std::string graphSource = CompactSource(generated.hlsl);
    const std::string canonical =
        ShaderGraphCompiler::BuildCanonicalKey(graph, properties, ShaderShadingModel::Lit, ShaderSurfaceType::Opaque);
    return Check(generated.succeeded && graphSource.find("g_PreviousBoneMatrices[128]") != std::string::npos &&
                     graphSource.find("previousLocalPosition=mul(previousLocalPosition,previousSkin);") !=
                         std::string::npos &&
                     canonical.find("\"compiler\":3") != std::string::npos,
                 "Shader Graph GBuffer motion ABI or cache version omitted previous bone poses");
}

bool TestSlangReflectionPreservesSamplerStateBindings() {
    const std::array<const char*, 4> shaderCandidates = {
        "EngineContent/Shaders/ShadowDepth.hlsl",
        "../../../EngineContent/Shaders/ShadowDepth.hlsl",
        "../../../../EngineContent/Shaders/ShadowDepth.hlsl",
        "../../../../../EngineContent/Shaders/ShadowDepth.hlsl",
    };
    const auto shaderPath = FindRepositoryFile(shaderCandidates);
    if (!Check(!shaderPath.empty(), "ShadowDepth shader source was not found"))
        return false;

    if (!Check(ShaderCompilerSlang::IsAvailable(),
               "Slang compiler is unavailable; sampler reflection ABI cannot be validated")) {
        return false;
    }

    std::vector<uint8_t> bytecode;
    CookedShaderStageReflection reflection;
    std::string error;
    if (!Check(ShaderCompilerSlang::CompileStageFromFile(shaderPath, "PSMain", ShaderStage::Pixel, ShaderBackend::D3D12,
                                                         bytecode, {}, &error, &reflection),
               "ShadowDepth Slang reflection compile failed: " + error)) {
        return false;
    }
    const auto isBaseColorSampler = [](const auto& binding) { return binding.name == "g_BaseColorSampler"; };
    const auto sampler = std::find_if(reflection.bindings.begin(), reflection.bindings.end(), isBaseColorSampler);
    const size_t samplerCount =
        static_cast<size_t>(std::count_if(reflection.bindings.begin(), reflection.bindings.end(), isBaseColorSampler));
    return Check(sampler != reflection.bindings.end() && samplerCount == 1 &&
                     sampler->type == CookedShaderBindingType::Sampler && sampler->bindPoint == 0 &&
                     sampler->bindSpace == 0,
                 "Slang reflection dropped or corrupted the ShadowDepth base-color sampler binding");
}

bool TestVulkanStructuredBufferAndScreenUIBindingContracts() {
    const std::array<const char*, 4> vulkanCandidates = {
        "src/Runtime/Renderer/VulkanContext.cpp",
        "../../../src/Runtime/Renderer/VulkanContext.cpp",
        "../../../../src/Runtime/Renderer/VulkanContext.cpp",
        "../../../../../src/Runtime/Renderer/VulkanContext.cpp",
    };
    const std::string vulkan = ReadRepositoryTextFile(vulkanCandidates);
    if (!Check(!vulkan.empty() &&
                   vulkan.find("binding.bindPoint >= 128 ? ShaderBindingType::StorageBuffer") != std::string::npos &&
                   vulkan.find(": ShaderBindingType::StructuredBuffer") != std::string::npos &&
                   vulkan.find("binding.name == \"g_EnvironmentSH2\"") == std::string::npos,
               "Vulkan SPIR-V reflection does not preserve read-only t-register structured buffers")) {
        return false;
    }

    const std::array<const char*, 4> passCandidates = {
        "src/Runtime/Renderer/ScreenUIPass.cpp",
        "../../../src/Runtime/Renderer/ScreenUIPass.cpp",
        "../../../../src/Runtime/Renderer/ScreenUIPass.cpp",
        "../../../../../src/Runtime/Renderer/ScreenUIPass.cpp",
    };
    const std::string pass = ReadRepositoryTextFile(passCandidates);
    if (!Check(!pass.empty() && pass.find("EngineShaders::kScreenUI") != std::string::npos &&
                   pass.find("CreateBindGroup") != std::string::npos &&
                   pass.find("SetConstants(\"UIScreenConstants\"") != std::string::npos &&
                   pass.find("SetTexture(\"u_Texture\"") != std::string::npos &&
                   pass.find("SetSampler(\"u_Sampler\"") != std::string::npos &&
                   pass.find("SetBindGroup(0, bindings.get())") != std::string::npos,
               "Vulkan ScreenUI does not use the cooked shader named-binding path")) {
        return false;
    }

    const std::array<const char*, 4> shaderCandidates = {
        "EngineContent/Shaders/ScreenUI.hlsl",
        "../../../EngineContent/Shaders/ScreenUI.hlsl",
        "../../../../EngineContent/Shaders/ScreenUI.hlsl",
        "../../../../../EngineContent/Shaders/ScreenUI.hlsl",
    };
    const auto shaderPath = FindRepositoryFile(shaderCandidates);
    if (!Check(!shaderPath.empty(), "ScreenUI shader source was not found"))
        return false;
    if (!Check(ShaderCompilerSlang::IsAvailable(), "Slang compiler is unavailable; Vulkan ScreenUI cannot be cooked"))
        return false;

    std::vector<uint8_t> vertexBytecode;
    std::vector<uint8_t> pixelBytecode;
    CookedShaderStageReflection vertexReflection;
    CookedShaderStageReflection pixelReflection;
    std::string error;
    if (!Check(ShaderCompilerSlang::CompileStageFromFile(shaderPath, "VSMain", ShaderStage::Vertex,
                                                         ShaderBackend::Vulkan, vertexBytecode, {}, &error,
                                                         &vertexReflection) &&
                   ShaderCompilerSlang::CompileStageFromFile(shaderPath, "PSMain", ShaderStage::Pixel,
                                                             ShaderBackend::Vulkan, pixelBytecode, {}, &error,
                                                             &pixelReflection),
               "ScreenUI Vulkan Slang compile failed: " + error)) {
        return false;
    }
    const auto hasBinding = [](const CookedShaderStageReflection& reflection, const std::string& name,
                               CookedShaderBindingType type) {
        return std::find_if(reflection.bindings.begin(), reflection.bindings.end(), [&](const auto& binding) {
                   return binding.name == name && binding.type == type;
               }) != reflection.bindings.end();
    };
    return Check(!vertexBytecode.empty() && !pixelBytecode.empty() &&
                     hasBinding(vertexReflection, "UIScreenConstants", CookedShaderBindingType::ConstantBuffer) &&
                     hasBinding(pixelReflection, "u_Texture", CookedShaderBindingType::Texture) &&
                     hasBinding(pixelReflection, "u_Sampler", CookedShaderBindingType::Sampler),
                 "ScreenUI Vulkan shader reflection is missing a required named binding");
}

bool TestGpuDrivenShadowSetupFailureFallsBackBeforeGraphMutation() {
    const std::array<const char*, 4> pipelineHeaderCandidates = {
        "src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
    };
    const std::array<const char*, 4> pipelineSourceCandidates = {
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    };
    const std::array<const char*, 4> rendererSourceCandidates = {
        "src/Runtime/Renderer/Renderer.cpp",
        "../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../../src/Runtime/Renderer/Renderer.cpp",
    };
    const std::string header = CompactSource(ReadRepositoryTextFile(pipelineHeaderCandidates));
    const std::string pipeline = CompactSource(ReadRepositoryTextFile(pipelineSourceCandidates));
    const std::string renderer = CompactSource(ReadRepositoryTextFile(rendererSourceCandidates));
    if (!Check(!header.empty() && !pipeline.empty() && !renderer.empty(),
               "GPU shadow fallback source files were not found")) {
        return false;
    }
    if (!Check(header.find("boolAddGpuDrivenShadowView(") != std::string::npos &&
                   header.find("voidAbortGpuDrivenShadowFrame();") != std::string::npos,
               "GPU shadow setup does not expose an explicit success/failure contract")) {
        return false;
    }

    const size_t addView = pipeline.find("boolModernDeferredPipeline::AddGpuDrivenShadowView(");
    const size_t depthValidation = pipeline.find("stream->depthBindings->Validate(&validationError)", addView);
    const size_t graphImport = pipeline.find("graph.ImportBuffer(name+\"Objects\"", addView);
    if (!Check(addView != std::string::npos && depthValidation != std::string::npos &&
                   graphImport != std::string::npos && depthValidation < graphImport,
               "GPU shadow bindings are not fully validated before mutating the RenderGraph")) {
        return false;
    }
    if (!Check(pipeline.find("m_ShadowSubmissionSerial+kShadowStreamRetireSubmissions") != std::string::npos &&
                   pipeline.find("++m_ShadowSubmissionSerial;") != std::string::npos &&
                   pipeline.find("retired.releaseSubmission<=m_ShadowSubmissionSerial") != std::string::npos,
               "replaced GPU shadow streams are not retained by successful submission count")) {
        return false;
    }

    const size_t checkedAdd = renderer.find("if(!m_ModernDeferredPipeline->AddGpuDrivenShadowView(");
    const size_t abortShadow = renderer.find("m_ModernDeferredPipeline->AbortGpuDrivenShadowFrame();", checkedAdd);
    const size_t resetGraph = renderer.find("m_RenderGraph->Reset();", abortShadow);
    const size_t cpuFallback = renderer.find("addCpuShadowPass(\"ShadowFallback\");", resetGraph);
    return Check(checkedAdd != std::string::npos && abortShadow != std::string::npos &&
                     resetGraph != std::string::npos && cpuFallback != std::string::npos && checkedAdd < abortShadow &&
                     abortShadow < resetGraph && resetGraph < cpuFallback,
                 "Renderer does not rebuild failed GPU shadows with the CPU fallback pass");
}

bool TestGpuDrivenShadowSetupFailureLeavesGraphUntouched() {
    MockRenderContext context;
    RenderGraph graph(context);
    RHITextureDesc shadowDesc;
    shadowDesc.width = 64;
    shadowDesc.height = 64;
    shadowDesc.format = RHIFormat::D24S8;
    shadowDesc.usage = RHIResourceUsage::DepthStencil;
    auto shadow = context.CreateTexture(shadowDesc);
    RHITextureViewDesc shadowViewDesc;
    shadowViewDesc.usage = RHIResourceUsage::DepthStencil;
    auto shadowView = context.CreateTextureView(shadow, shadowViewDesc);
    const auto shadowTarget = graph.ImportTexture("ShadowSetupFailureTarget", shadow, shadowView,
                                                  RHIResourceState::DepthWrite, RHIResourceState::DepthWrite);

    ModernDeferredPipeline unavailablePipeline(nullptr);
    if (!Check(!unavailablePipeline.AddGpuDrivenShadowView(graph, "UnavailableGpuShadow", shadowTarget, {},
                                                           Mat4::Identity()),
               "GPU shadow setup unexpectedly succeeded without a device or pipelines")) {
        return false;
    }
    if (!Check(unavailablePipeline.GetStats().gpuShadowSetupFailures == 1 &&
                   !unavailablePipeline.GetLastShadowSetupError().empty(),
               "GPU shadow setup failure did not publish diagnostics")) {
        return false;
    }
    return Check(graph.Compile() && graph.GetExecutionOrder().empty(),
                 "failed GPU shadow setup registered a clear or draw pass in the RenderGraph");
}

bool TestModernTemporalReprojectionShaderContract() {
    const std::array<const char*, 4> candidates = {
        "EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
    };
    const std::string source = ReadRepositoryTextFile(candidates);
    if (!Check(!source.empty(), "ModernScreenSpace shader source was not found"))
        return false;
    if (!Check(source.find("row_major float4x4 g_PreviousInverseViewProjection;") != std::string::npos &&
                   source.find("row_major float4x4 g_PreviousViewProjection;") != std::string::npos &&
                   source.find("float4 g_PreviousCameraPosition;") != std::string::npos &&
                   CountOccurrences(source, "ReconstructWorldPositionUv(previousGeometryUv, previousDepth") >= 1 &&
                   CountOccurrences(source, "g_PreviousInverseViewProjection") >= 2,
               "SSGI/SSR temporal paths do not reproject previous depth in previous-world space")) {
        return false;
    }
    if (!Check(CountOccurrences(source, "SampleLevel(g_PointSampler, previousGeometryUv, 0.0f)") >= 2,
               "temporal rejection does not point-sample full-resolution previous depth and normal histories")) {
        return false;
    }
    const std::string compact = CompactSource(source);
    return Check(
        compact.find("currentEffectUv=(float2(pixel)+0.5f)*g_EffectTexelSize") != std::string::npos &&
            compact.find("historyUv=StableHistoryUv(currentEffectUv,velocity)") != std::string::npos &&
            compact.find("previousGeometryUv=currentGeometryUv-velocity") != std::string::npos &&
            compact.find("g_History.SampleLevel(g_LinearSampler,historyUv,0.0f)") != std::string::npos &&
            compact.find("g_PreviousDepth.SampleLevel(g_PointSampler,previousGeometryUv,0.0f)") != std::string::npos &&
            compact.find("g_PreviousNormal.SampleLevel(g_PointSampler,previousGeometryUv,0.0f)") != std::string::npos,
        "half-resolution history UVs are not separated from full-resolution depth/normal reprojection");
}

bool TestModernTaaCameraJitterStabilityContract() {
    const std::string shader = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/ModernTAA.hlsl",
        "../../../EngineContent/Shaders/ModernTAA.hlsl",
        "../../../../EngineContent/Shaders/ModernTAA.hlsl",
        "../../../../../EngineContent/Shaders/ModernTAA.hlsl",
    }));
    const std::string pipelineHeader = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
    }));
    const std::string pipelineSource = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    }));
    const std::string forwardSource = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ForwardRenderPasses.cpp",
        "../../../src/Runtime/Renderer/ForwardRenderPasses.cpp",
        "../../../../src/Runtime/Renderer/ForwardRenderPasses.cpp",
        "../../../../../src/Runtime/Renderer/ForwardRenderPasses.cpp",
    }));
    const std::string mainPassSource = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/MainPass.cpp",
        "../../../src/Runtime/Renderer/MainPass.cpp",
        "../../../../src/Runtime/Renderer/MainPass.cpp",
        "../../../../../src/Runtime/Renderer/MainPass.cpp",
    }));
    const std::string rendererSource = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/Renderer.cpp",
        "../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../../src/Runtime/Renderer/Renderer.cpp",
    }));
    const std::string postCompositeShader = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/PostProcessFXAA.hlsl",
        "../../../EngineContent/Shaders/PostProcessFXAA.hlsl",
        "../../../../EngineContent/Shaders/PostProcessFXAA.hlsl",
        "../../../../../EngineContent/Shaders/PostProcessFXAA.hlsl",
    }));
    if (!Check(!shader.empty() && !pipelineHeader.empty() && !pipelineSource.empty() && !forwardSource.empty() &&
                   !mainPassSource.empty() && !rendererSource.empty() && !postCompositeShader.empty(),
               "Modern TAA reference-rewrite contract sources were not found")) {
        return false;
    }
    if (!Check(shader.find("cbufferTemporalAAConstants:register(b0)") != std::string::npos &&
                   shader.find("row_majorfloat4x4g_InverseJitteredViewProjection") != std::string::npos &&
                   shader.find("row_majorfloat4x4g_PreviousUnjitteredViewProjection") != std::string::npos &&
                   shader.find("unjitteredUv=currentUv+g_CurrentJitterUv") != std::string::npos &&
                   shader.find("mul(float4(currentNdc,depth,1.0f),g_InverseJitteredViewProjection)") !=
                       std::string::npos &&
                   shader.find("mul(float4(worldPosition,1.0f),g_PreviousUnjitteredViewProjection)") !=
                       std::string::npos &&
                   shader.find("previousUv=float2(previousNdc.x*0.5f+0.5f,0.5f-previousNdc.y*0.5f)") !=
                       std::string::npos &&
                   shader.find("currentSamplePixel=min(uint2(clampedUnjitteredUv*float2(g_RenderSize)),"
                               "g_RenderSize-1u)") != std::string::npos &&
                   shader.find("ReprojectToPreviousFrame(clampedUnjitteredUv,currentDepth,previousUv)") !=
                       std::string::npos,
               "TAA does not follow the WebGPU unjitter and depth-reprojection data flow")) {
        return false;
    }
    if (!Check(shader.find("CurrentNeighborhoodMoments(clampedUnjitteredUv,neighborhoodMean,"
                           "neighborhoodVariance)") != std::string::npos &&
                   shader.find("mean/=9.0f") != std::string::npos &&
                   shader.find("variance=max(secondMoment-mean*mean,0.0f.xxx)") != std::string::npos &&
                   shader.find("clipSigma=1.5f*(1.0f+max(g_HistoryClipExpansion,0.0f))") != std::string::npos &&
                   shader.find("historyYCoCg=clamp(historyYCoCg,neighborhoodMean-clipSigma*standardDeviation,"
                               "neighborhoodMean+clipSigma*standardDeviation)") != std::string::npos &&
                   shader.find("historyWeight=historyValid?saturate(g_HistoryWeight):0.0f") != std::string::npos &&
                   shader.find("resolved=lerp(current.rgb,history.rgb,historyWeight)") != std::string::npos,
               "TAA YCoCg 3x3 variance clipping or reference blend contract regressed")) {
        return false;
    }
    if (!Check(shader.find("g_HistoryValid!=0u&&reprojectionValid") != std::string::npos &&
                   shader.find("nextHistoryAge=historyValid?min(max(history.a,0.0f)+1.0f,127.0f):1.0f") !=
                       std::string::npos &&
                   shader.find("g_DebugMode==16u") != std::string::npos &&
                   shader.find("g_DebugMode==32u") != std::string::npos &&
                   shader.find("g_Velocity") == std::string::npos &&
                   shader.find("g_PreviousDepth") == std::string::npos &&
                   shader.find("g_PreviousNormal") == std::string::npos,
               "TAA history validity, diagnostics, or isolated resource contract regressed")) {
        return false;
    }
    if (!Check(pipelineHeader.find("structTemporalAAConstants") != std::string::npos &&
                   pipelineHeader.find("sizeof(TemporalAAConstants)==176") != std::string::npos &&
                   pipelineHeader.find("floattaaHistoryWeight=0.8f") != std::string::npos &&
                   pipelineHeader.find("Mat4m_PreviousUnjitteredViewProjection=Mat4::Identity()") !=
                       std::string::npos &&
                   pipelineSource.find("m_PreviousUnjitteredViewProjection=m_PendingUnjitteredViewProjection") !=
                       std::string::npos &&
                   pipelineSource.find("m_PendingUnjitteredViewProjection=unjitteredViewProjection") !=
                       std::string::npos &&
                   pipelineSource.find("SetConstants(\"TemporalAAConstants\",&taaConstants,"
                                       "sizeof(taaConstants))") != std::string::npos,
               "Modern Deferred does not commit or bind the independent TAA reference constants")) {
        return false;
    }
    return Check(
        forwardSource.find("context.viewProjection?*context.viewProjection:camera.GetViewProj()") !=
                std::string::npos &&
            mainPassSource.find("forwardContext.viewProjection=viewProjection") != std::string::npos &&
            rendererSource.find("modernFrameReady?&m_ModernDeferredPipeline->GetCurrentViewProjection():nullptr") !=
                std::string::npos &&
            rendererSource.find("SetInputPreprocessed(modernFrameReady)") != std::string::npos &&
            postCompositeShader.find("if(g_Params3.x>0.5f){") != std::string::npos &&
            postCompositeShader.find("g_SceneColor.Load(int3(pixel,0))") != std::string::npos,
        "Modern transparent rendering or final composite is not preserving the TAA projection phase");
}

bool TestModernScreenSpaceCompositeShaderContract() {
    const std::array<const char*, 4> candidates = {
        "EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
    };
    const std::string source = ReadRepositoryTextFile(candidates);
    if (!Check(!source.empty(), "ModernScreenSpace shader source was not found"))
        return false;
    if (!Check(source.find("float4 BilateralUpsample(Texture2D<float4> source, uint2 pixel)") != std::string::npos &&
                   source.find("BilateralUpsample(g_SSGI, pixel)") != std::string::npos &&
                   source.find("BilateralUpsample(g_SSR, pixel)") != std::string::npos &&
                   source.find("dot(centerNormal, sampleNormal)") != std::string::npos,
               "half-resolution SSGI/SSR are not depth/normal-aware when composited at full resolution")) {
        return false;
    }
    const size_t ssrBegin = source.find("void CSSSRTrace");
    const size_t ssrEnd = source.find("void CSTemporal", ssrBegin);
    if (!Check(ssrBegin != std::string::npos && ssrEnd != std::string::npos && ssrBegin < ssrEnd,
               "SSR trace shader function was not found")) {
        return false;
    }
    const std::string ssrTrace = source.substr(ssrBegin, ssrEnd - ssrBegin);
    if (!Check(ssrTrace.find("g_Output[pixel] = float4(reflection, confidence);") != std::string::npos &&
                   ssrTrace.find("metallic") == std::string::npos,
               "SSR trace confidence is still coupled to material metallic/F0")) {
        return false;
    }
    if (!Check(source.find("g_Environment.SampleLevel(g_LinearSampler, reflectionDirection") != std::string::npos &&
                   source.find("specularCorrection = (reflection.rgb - environmentRadiance)") != std::string::npos,
               "SSR composite does not replace the clustered environment reflection")) {
        return false;
    }
    const std::string compact = CompactSource(source);
    if (!Check(compact.find("gi=BilateralUpsample(g_SSGI,pixel).rgb*diffuseResponse*max(g_SSGIIntensity,0.0f)") !=
                   std::string::npos,
               "SSGI intensity is not applied where indirect radiance reaches the final material composite")) {
        return false;
    }
    return Check(source.find("hdr + gi + specularCorrection") != std::string::npos &&
                     source.find("hdr + gi + reflection") == std::string::npos,
                 "SSR composite still adds a second specular lobe on top of environment lighting");
}

bool TestModernSsaoCompositeContract() {
    const std::string screenSpace = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
    }));
    const std::string pipelineHeader = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
    }));
    const std::string pipelineSource = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    }));
    const std::string renderer = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/Renderer.cpp",
        "../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../../src/Runtime/Renderer/Renderer.cpp",
    }));
    const std::string postProcess = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/PostProcessPass.cpp",
        "../../../src/Runtime/Renderer/PostProcessPass.cpp",
        "../../../../src/Runtime/Renderer/PostProcessPass.cpp",
        "../../../../../src/Runtime/Renderer/PostProcessPass.cpp",
    }));
    const std::string finalComposite = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/PostProcessFXAA.hlsl",
        "../../../EngineContent/Shaders/PostProcessFXAA.hlsl",
        "../../../../EngineContent/Shaders/PostProcessFXAA.hlsl",
        "../../../../../EngineContent/Shaders/PostProcessFXAA.hlsl",
    }));
    if (!Check(!screenSpace.empty() && !pipelineHeader.empty() && !pipelineSource.empty() && !renderer.empty() &&
                   !postProcess.empty() && !finalComposite.empty(),
               "Modern SSAO contract sources were not found")) {
        return false;
    }
    if (!Check(screenSpace.find("Texture2D<float>g_SSAO:register(t13)") != std::string::npos &&
                   screenSpace.find("(g_EffectMode&16u)!=0?") != std::string::npos &&
                   screenSpace.find("(hdr+gi+specularCorrection)*screenSpaceAO") != std::string::npos,
               "Modern HDR effects composite does not apply the SSAO visibility texture")) {
        return false;
    }
    if (!Check(pipelineHeader.find("GetCurrentProjection()const{returnm_ScreenSpaceConstants.projection;}") !=
                       std::string::npos &&
                   pipelineSource.find("&&!ssaoEnabled)returnhdr") != std::string::npos &&
                   pipelineSource.find("(ssaoEnabled?16u:0u)") != std::string::npos &&
                   pipelineSource.find("builder.ReadTexture(ssao)") != std::string::npos &&
                   pipelineSource.find("SetTexture(\"g_SSAO\",ssaoEnabled?ssaoSrv:hdrSrv)") != std::string::npos,
               "Modern screen-space pass does not declare, route, and bind SSAO independently of SSGI/SSR")) {
        return false;
    }
    if (!Check(renderer.find("constboolframeSsaoEnabled=ssaoEnabled") != std::string::npos &&
                   renderer.find("if(modernFrameReady&&frameSsaoEnabled)addSsaoPasses()") != std::string::npos &&
                   renderer.find("modernHiZ,ssao,postResources.ssaoSrv,frameSsaoEnabled") != std::string::npos &&
                   renderer.find("modernFrameReady?&m_ModernDeferredPipeline->GetCurrentProjection():nullptr") !=
                       std::string::npos,
               "Renderer still suppresses Modern SSAO or does not use its jittered projection")) {
        return false;
    }
    return Check(postProcess.find("constants.params3[1]=m_SSAOEnabled&&!m_InputPreprocessed?1.0f:0.0f") !=
                         std::string::npos &&
                     finalComposite.find("if(g_Params3.y>0.5f)color*=g_SSAOMap.Sample") != std::string::npos,
                 "final composite does not explicitly gate Classic SSAO and can apply an invalid fallback texture");
}

bool TestModernScreenSpaceSamplingAndConfidenceContracts() {
    const std::array<const char*, 4> candidates = {
        "EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
    };
    const std::string source = ReadRepositoryTextFile(candidates);
    if (!Check(!source.empty(), "ModernScreenSpace shader source was not found"))
        return false;
    const std::string compact = CompactSource(source);
    if (!Check(compact.find("EffectPixelToFullPixel(uint2pixel)") != std::string::npos &&
                   compact.find("float2(g_FullSize)/float2(g_EffectSize)") != std::string::npos,
               "half-resolution effects do not use render-size-aware representative pixels")) {
        return false;
    }
    if (!Check(compact.find("g_HiZ.GetDimensions(0,baseWidth,baseHeight,levelCount)") != std::string::npos &&
                   compact.find("resolvedMip=min(requestedMip,max(levelCount,1u)-1u)") != std::string::npos &&
                   compact.find("rayDepth>=minMaxDepth.x-deviceThickness") != std::string::npos &&
                   compact.find("rayDepth<=minMaxDepth.y+deviceThickness") != std::string::npos,
               "SSGI/SSR HiZ tracing does not clamp mip access and test the conservative min/max interval")) {
        return false;
    }
    if (!Check(compact.find("roughnessConfidence=saturate((g_SSRMaxRoughness-roughness)/roughnessFadeWidth)") !=
                       std::string::npos &&
                   compact.find("NeighborhoodMaxAlpha(g_Current,pixel,g_EffectSize)") != std::string::npos &&
                   compact.find("history.a=min(saturate(history.a)") != std::string::npos &&
                   compact.find("confidenceWeightedColor+=sampleValue.rgb*(weight*confidence)") != std::string::npos &&
                   compact.find("filteredColor=confidenceSum>1e-5f?confidenceWeightedColor/confidenceSum:0.0f") !=
                       std::string::npos,
               "SSR roughness fade or temporal confidence rejection is missing")) {
        return false;
    }
    if (!Check(compact.find("diffuseResponse=(1.0f-fresnel)*(1.0f-metallic)*albedo*ao") != std::string::npos,
               "SSGI composite does not apply the receiver diffuse material response")) {
        return false;
    }
    if (!Check(compact.find("RefineRayDepthCrossing(float3rayOrigin") != std::string::npos &&
                   CountOccurrences(compact, "RefineRayDepthCrossing(rayOrigin,direction,previousDistance,distance") ==
                       2 &&
                   CountOccurrences(compact, "previousViewDelta<-0.002f&&viewDelta>=-0.002f") == 2 &&
                   compact.find("hitWeight=saturate(1.0f-hitDistance/") != std::string::npos &&
                   compact.find("hitWeight=hitFacing*") == std::string::npos,
               "SSGI/SSR still accept self-overlaps, skip thin depth crossings, or double-attenuate cosine samples")) {
        return false;
    }
    const size_t ssgiTraceBegin = compact.find("voidCSSSGITrace");
    const size_t ssgiTraceEnd = compact.find("voidCSSSRTrace", ssgiTraceBegin);
    if (!Check(ssgiTraceBegin != std::string::npos && ssgiTraceEnd != std::string::npos &&
                   compact.substr(ssgiTraceBegin, ssgiTraceEnd - ssgiTraceBegin).find("g_SSGIIntensity") ==
                       std::string::npos,
               "SSGI trace history is still intensity-scaled before final composition")) {
        return false;
    }
    const size_t ssgiBegin = compact.find("float4AccumulateSSGIHistory(");
    const size_t ssrBegin = compact.find("float4AccumulateSSRHistory(", ssgiBegin);
    const size_t traceBegin = compact.find("voidCSSSGITrace", ssrBegin);
    if (!Check(ssgiBegin != std::string::npos && ssrBegin != std::string::npos && traceBegin != std::string::npos,
               "separate SSGI and SSR temporal accumulation functions were not found")) {
        return false;
    }
    const std::string ssgiTemporal = compact.substr(ssgiBegin, ssrBegin - ssgiBegin);
    const std::string ssrTemporal = compact.substr(ssrBegin, traceBegin - ssrBegin);
    if (!Check(ssgiTemporal.find("SSGINeighborhoodStatistics(g_Current,pixel,g_EffectSize") != std::string::npos &&
                   ssgiTemporal.find("temporalVariance=lerp(currentVariance,historyVariance,weight)+") !=
                       std::string::npos &&
                   ssgiTemporal.find("weight=valid?saturate(g_SSGIHistoryWeight):0.0f") != std::string::npos &&
                   ssgiTemporal.find("accumulated=lerp(current,history,weight)") != std::string::npos &&
                   ssgiTemporal.find("accumulated.a=lerp(currentSecondMoment,history.a,weight)") != std::string::npos &&
                   ssgiTemporal.find("RelativeLuminanceDifference") == std::string::npos,
               "SSGI temporal accumulation still rejects valid history by instantaneous luminance or mismatches "
               "radiance/moment weights")) {
        return false;
    }
    if (!Check(ssrTemporal.find("luminanceDelta=RelativeLuminanceDifference(current.rgb,history.rgb)") !=
                       std::string::npos &&
                   ssrTemporal.find("weight=valid?saturate(g_SSRHistoryWeight)*saturate(1.0f-luminanceDelta):0.0f") !=
                       std::string::npos &&
                   ssrTemporal.find("history.a=min(saturate(history.a)") != std::string::npos,
               "SSR temporal confidence and luminance rejection strategy changed unexpectedly")) {
        return false;
    }
    if (!Check(compact.find("(g_EffectMode&4u)!=0") != std::string::npos &&
                   compact.find("1.0f-exp(-radiance*8.0f)") != std::string::npos &&
                   compact.find("(g_EffectMode&8u)!=0") != std::string::npos &&
                   compact.find("BilateralUpsample(g_SSR,pixel).a") != std::string::npos &&
                   compact.find("float4(confidence.xxx,1.0f)") != std::string::npos,
               "SSGI and SSR confidence debug modes do not visualize the final half-resolution data explicitly")) {
        return false;
    }
    return Check(compact.find("voidCSTAA") == std::string::npos &&
                     compact.find("g_TAADebugOutput") == std::string::npos,
                 "ModernScreenSpace still embeds the independently compiled TAA resolve");
}

bool TestModernScreenSpacePostProcessTuningContract() {
    PostProcessComponent post;
    if (!Check(NearlyEqual(post.GetSSGIHistoryWeight(), 0.9f) && post.GetSSGIStepCount() == 32 &&
                   post.GetSSGIFilterRounds() == 3 && NearlyEqual(post.GetSSRMaxDistance(), 10.0f) &&
                   NearlyEqual(post.GetSSRHistoryWeight(), 0.9f) && post.GetSSRStepCount() == 48 &&
                   post.GetSSRFilterRounds() == 2 && post.IsTAAEnabled() &&
                   NearlyEqual(post.GetTAAHistoryWeight(), 0.8f) && NearlyEqual(post.GetTAAJitterSpread(), 1.0f) &&
                   NearlyEqual(post.GetTAAHistoryClipExpansion(), 0.0f),
               "PostProcess Modern SSGI/SSR/TAA tuning defaults changed unexpectedly")) {
        return false;
    }
    post.SetSSGIHistoryWeight(2.0f);
    post.SetSSGIStepCount(0);
    post.SetSSGIFilterRounds(9);
    post.SetSSRMaxDistance(0.0f);
    post.SetSSRHistoryWeight(-1.0f);
    post.SetSSRStepCount(256);
    post.SetSSRFilterRounds(9);
    post.SetTAAHistoryWeight(2.0f);
    post.SetTAAJitterSpread(3.0f);
    post.SetTAAHistoryClipExpansion(9.0f);
    if (!Check(NearlyEqual(post.GetSSGIHistoryWeight(), 0.99f) && post.GetSSGIStepCount() == 1 &&
                   post.GetSSGIFilterRounds() == 4 && NearlyEqual(post.GetSSRMaxDistance(), 0.1f) &&
                   NearlyEqual(post.GetSSRHistoryWeight(), 0.0f) && post.GetSSRStepCount() == 128 &&
                   post.GetSSRFilterRounds() == 4 && NearlyEqual(post.GetTAAHistoryWeight(), 0.99f) &&
                   NearlyEqual(post.GetTAAJitterSpread(), 2.0f) && NearlyEqual(post.GetTAAHistoryClipExpansion(), 4.0f),
               "PostProcess Modern SSGI/SSR/TAA tuning ranges are not bounded")) {
        return false;
    }

    const std::string shader = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
    }));
    const std::string taaShader = CompactSource(ReadRepositoryTextFile({
        "EngineContent/Shaders/ModernTAA.hlsl",
        "../../../EngineContent/Shaders/ModernTAA.hlsl",
        "../../../../EngineContent/Shaders/ModernTAA.hlsl",
        "../../../../../EngineContent/Shaders/ModernTAA.hlsl",
    }));
    const std::string pipeline = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    }));
    const std::string renderer = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/Renderer.cpp",
        "../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../../src/Runtime/Renderer/Renderer.cpp",
    }));
    if (!Check(!shader.empty() && !taaShader.empty() && !pipeline.empty() && !renderer.empty(),
               "Modern post-process tuning contract sources were not found")) {
        return false;
    }
    if (!Check(shader.find("g_SSGIHistoryWeight") != std::string::npos &&
                   shader.find("g_SSRHistoryWeight") != std::string::npos &&
                   shader.find("max(g_SSGIMaxDistance,0.1f)") != std::string::npos &&
                   shader.find("max(g_SSRMaxDistance,0.1f)") != std::string::npos &&
                   shader.find("g_MaxDistance") == std::string::npos &&
                   shader.find("g_HistoryWeight") == std::string::npos &&
                   shader.find("g_TAAHistoryWeight") == std::string::npos &&
                   taaShader.find("g_HistoryWeight") != std::string::npos &&
                   taaShader.find("g_HistoryClipExpansion") != std::string::npos &&
                   taaShader.find("clipSigma=1.5f*(1.0f+max(g_HistoryClipExpansion,0.0f))") != std::string::npos &&
                   taaShader.find("g_SSGIHistoryWeight") == std::string::npos &&
                   taaShader.find("g_SSRHistoryWeight") == std::string::npos,
               "TAA tuning is not isolated from the SSGI/SSR screen-space constant contract")) {
        return false;
    }
    if (!Check(
            pipeline.find("m_ScreenSpaceConstants.ssgiHistoryWeight=settings.ssgiHistoryWeight") != std::string::npos &&
                pipeline.find("m_ScreenSpaceConstants.ssrHistoryWeight=settings.ssrHistoryWeight") !=
                    std::string::npos &&
                pipeline.find("m_TAAConstants.historyWeight=settings.taaHistoryWeight") != std::string::npos &&
                pipeline.find("m_TAAConstants.historyClipExpansion=settings.taaHistoryClipExpansion") !=
                    std::string::npos &&
                pipeline.find("*jitterSpread") != std::string::npos &&
                pipeline.find("settings.taaJitterSpread,m_PreviousPostSettings.taaJitterSpread") != std::string::npos &&
                pipeline.find("m_PostSettings.ssgiFilterRounds") != std::string::npos &&
                pipeline.find("FloatsNearlyEqual(settings.ssgiIntensity") == std::string::npos &&
                pipeline.find("m_PostSettings.ssrFilterRounds") != std::string::npos,
            "Modern pipeline does not consume independent SSGI/SSR trace, temporal, and filter settings")) {
        return false;
    }
    return Check(
        renderer.find("options.modern.ssgiHistoryWeight=post->GetSSGIHistoryWeight()") != std::string::npos &&
            renderer.find("options.modern.ssgiStepCount=post->GetSSGIStepCount()") != std::string::npos &&
            renderer.find("options.modern.ssgiFilterRounds=post->GetSSGIFilterRounds()") != std::string::npos &&
            renderer.find("options.modern.ssrMaxDistance=post->GetSSRMaxDistance()") != std::string::npos &&
            renderer.find("options.modern.ssrHistoryWeight=post->GetSSRHistoryWeight()") != std::string::npos &&
            renderer.find("options.modern.ssrStepCount=post->GetSSRStepCount()") != std::string::npos &&
            renderer.find("options.modern.ssrFilterRounds=post->GetSSRFilterRounds()") != std::string::npos &&
            renderer.find("options.modern.taaEnabled=post->IsTAAEnabled()") != std::string::npos &&
            renderer.find("options.modern.taaHistoryWeight=post->GetTAAHistoryWeight()") != std::string::npos &&
            renderer.find("options.modern.taaJitterSpread=post->GetTAAJitterSpread()") != std::string::npos &&
            renderer.find("options.modern.taaHistoryClipExpansion=post->GetTAAHistoryClipExpansion()") !=
                std::string::npos,
        "Renderer does not route PostProcess SSGI/SSR/TAA tuning into Modern Deferred");
}

bool TestModernScreenSpaceDebugRoutingContract() {
    const std::array<const char*, 4> headerCandidates = {
        "src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
    };
    const std::array<const char*, 4> pipelineCandidates = {
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    };
    const std::array<const char*, 4> rendererCandidates = {
        "src/Runtime/Renderer/Renderer.cpp",
        "../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../../src/Runtime/Renderer/Renderer.cpp",
    };
    const std::array<const char*, 4> viewportCandidates = {
        "src/Editor/Panels/ViewportPanel.cpp",
        "../../../src/Editor/Panels/ViewportPanel.cpp",
        "../../../../src/Editor/Panels/ViewportPanel.cpp",
        "../../../../../src/Editor/Panels/ViewportPanel.cpp",
    };
    const std::string header = CompactSource(ReadRepositoryTextFile(headerCandidates));
    const std::string pipeline = CompactSource(ReadRepositoryTextFile(pipelineCandidates));
    const std::string renderer = CompactSource(ReadRepositoryTextFile(rendererCandidates));
    const std::string viewport = CompactSource(ReadRepositoryTextFile(viewportCandidates));
    if (!Check(!header.empty() && !pipeline.empty() && !renderer.empty() && !viewport.empty(),
               "Modern screen-space debug routing source was not found")) {
        return false;
    }
    if (!Check(header.find("GetSSGIDebugSrv()const{returnm_SSGIDebugOutputSrv;}") != std::string::npos &&
                   header.find("GetSSRDebugSrv()const{returnm_SSRDebugOutputSrv;}") != std::string::npos &&
                   header.find("GetTAAHistoryAgeDebugSrv()const{returnm_TAAHistoryAgeDebugOutputSrv;}") !=
                       std::string::npos &&
                   header.find("GetTAARejectReasonDebugSrv()const{returnm_TAARejectReasonDebugOutputSrv;}") !=
                       std::string::npos &&
                   header.find("GetSSGIDebugSrv()const{returnm_SSGIFilter[1].srv;}") == std::string::npos,
               "Modern debug getters do not expose the explicit full-resolution visualization outputs")) {
        return false;
    }
    if (!Check(pipeline.find("debugConstants.effectMode=debugSSGI?4u:8u") != std::string::npos &&
                   pipeline.find("debugSSGI?\"VisualizeSSGI\":\"VisualizeSSRConfidence\"") != std::string::npos &&
                   pipeline.find("m_SSGIDebugOutputSrv=m_ScreenSpaceDebug.srv") != std::string::npos &&
                   pipeline.find("m_SSRDebugOutputSrv=m_ScreenSpaceDebug.srv") != std::string::npos &&
                   pipeline.find("taaConstants.debugMode=16u") != std::string::npos &&
                   pipeline.find("taaConstants.debugMode=32u") != std::string::npos &&
                   pipeline.find("SetStorageTexture(\"g_TAADebugOutput\",m_ScreenSpaceDebug.uav)") !=
                       std::string::npos &&
                   pipeline.find("builder.ReadWriteUAV(m_FrameScreenSpaceDebug)") != std::string::npos,
               "Modern debug graph does not create SSGI/SSR/TAA full-resolution visualization output")) {
        return false;
    }
    return Check(renderer.find("caseRendererDebugView::SSGI:") != std::string::npos &&
                     renderer.find("caseRendererDebugView::SSRConfidence:") != std::string::npos &&
                     renderer.find("caseRendererDebugView::TAAHistoryAge:") != std::string::npos &&
                     renderer.find("caseRendererDebugView::TAARejectReason:") != std::string::npos &&
                     renderer.find("GetTAAHistoryAgeDebugSrv()") != std::string::npos &&
                     renderer.find("GetTAARejectReasonDebugSrv()") != std::string::npos &&
                     viewport.find("\"TAAHistoryAge\"") != std::string::npos &&
                     viewport.find("\"TAARejectReason\"") != std::string::npos,
                 "Renderer and Scene View do not route the TAA diagnostic views into Modern Deferred");
}

bool TestModernScreenSpaceSlangCompileContracts() {
    const std::array<const char*, 4> candidates = {
        "EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
        "../../../../../EngineContent/Shaders/ModernScreenSpace.hlsl",
    };
    const auto shaderPath = FindRepositoryFile(candidates);
    const std::array<const char*, 4> taaCandidates = {
        "EngineContent/Shaders/ModernTAA.hlsl",
        "../../../EngineContent/Shaders/ModernTAA.hlsl",
        "../../../../EngineContent/Shaders/ModernTAA.hlsl",
        "../../../../../EngineContent/Shaders/ModernTAA.hlsl",
    };
    const auto taaShaderPath = FindRepositoryFile(taaCandidates);
    if (!Check(!shaderPath.empty() && !taaShaderPath.empty(), "Modern screen-space or TAA shader source was not found"))
        return false;
    if (!Check(ShaderCompilerSlang::IsAvailable(),
               "Slang compiler is unavailable; Modern screen-space DXIL/SPIR-V cannot be validated")) {
        return false;
    }

    const std::array<const char*, 6> entries = {
        "CSSSGITrace", "CSSSRTrace", "CSTemporal", "CSAtrous", "CSEffectsComposite", "CSBloomTone",
    };
    const std::array<ShaderBackend, 2> backends = {ShaderBackend::D3D12, ShaderBackend::Vulkan};
    for (ShaderBackend backend : backends) {
        for (const char* entry : entries) {
            std::vector<uint8_t> bytecode;
            CookedShaderStageReflection reflection;
            std::string error;
            const std::string backendName = backend == ShaderBackend::D3D12 ? "D3D12" : "Vulkan";
            if (!Check(ShaderCompilerSlang::CompileStageFromFile(shaderPath, entry, ShaderStage::Compute, backend,
                                                                 bytecode, {}, &error, &reflection),
                       "ModernScreenSpace " + backendName + " compile failed for " + entry + ": " + error)) {
                return false;
            }
            if (!Check(!bytecode.empty() && reflection.threadGroupSize[0] == 8 && reflection.threadGroupSize[1] == 8 &&
                           reflection.threadGroupSize[2] == 1,
                       "ModernScreenSpace reflection has an invalid thread-group ABI for " + backendName + " " +
                           entry)) {
                return false;
            }
        }
        std::vector<uint8_t> taaBytecode;
        CookedShaderStageReflection taaReflection;
        std::string taaError;
        const std::string backendName = backend == ShaderBackend::D3D12 ? "D3D12" : "Vulkan";
        if (!Check(ShaderCompilerSlang::CompileStageFromFile(taaShaderPath, "CSTAA", ShaderStage::Compute, backend,
                                                             taaBytecode, {}, &taaError, &taaReflection),
                   "ModernTAA " + backendName + " compile failed: " + taaError)) {
            return false;
        }
        if (!Check(!taaBytecode.empty() && taaReflection.threadGroupSize[0] == 8 &&
                       taaReflection.threadGroupSize[1] == 8 && taaReflection.threadGroupSize[2] == 1,
                   "ModernTAA reflection has an invalid thread-group ABI for " + backendName)) {
            return false;
        }
    }
    return true;
}

bool TestModernHiZOddDimensionReductionContract() {
    const std::array<const char*, 4> candidates = {
        "EngineContent/Shaders/ModernHiZ.hlsl",
        "../../../EngineContent/Shaders/ModernHiZ.hlsl",
        "../../../../EngineContent/Shaders/ModernHiZ.hlsl",
        "../../../../../EngineContent/Shaders/ModernHiZ.hlsl",
    };
    const auto shaderPath = FindRepositoryFile(candidates);
    if (!Check(!shaderPath.empty(), "ModernHiZ shader source was not found"))
        return false;
    const std::string compact = CompactSource(ReadRepositoryTextFile(candidates));
    if (!Check(compact.find("g_SourceSize.x>g_DestinationSize.x*2u") != std::string::npos &&
                   compact.find("g_SourceSize.y>g_DestinationSize.y*2u") != std::string::npos &&
                   compact.find("ExpandRange(range,g_SourceHiZ.Load(int3(g_SourceSize-1u,0)))") != std::string::npos,
               "HiZ reduction drops the final source row or column for odd-sized viewports")) {
        return false;
    }
    if (!Check(ShaderCompilerSlang::IsAvailable(),
               "Slang compiler is unavailable; Modern HiZ DXIL/SPIR-V cannot be validated")) {
        return false;
    }
    const std::array<const char*, 2> entries = {"CSInit", "CSReduce"};
    const std::array<ShaderBackend, 2> backends = {ShaderBackend::D3D12, ShaderBackend::Vulkan};
    for (ShaderBackend backend : backends) {
        for (const char* entry : entries) {
            std::vector<uint8_t> bytecode;
            CookedShaderStageReflection reflection;
            std::string error;
            if (!Check(ShaderCompilerSlang::CompileStageFromFile(shaderPath, entry, ShaderStage::Compute, backend,
                                                                 bytecode, {}, &error, &reflection),
                       "ModernHiZ Slang compile failed for " + std::string(entry) + ": " + error)) {
                return false;
            }
            if (!Check(!bytecode.empty() && reflection.threadGroupSize[0] == 8 && reflection.threadGroupSize[1] == 8 &&
                           reflection.threadGroupSize[2] == 1,
                       "ModernHiZ reflection has an invalid thread-group ABI")) {
                return false;
            }
        }
    }
    return true;
}

bool TestModernCompatibilityGBufferPrecedesHiZContract() {
    const std::string renderer = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/Renderer.cpp",
        "../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../src/Runtime/Renderer/Renderer.cpp",
        "../../../../../src/Runtime/Renderer/Renderer.cpp",
    }));
    const std::string pipeline = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    }));
    if (!Check(!renderer.empty() && !pipeline.empty(), "Modern Deferred renderer sources were not found"))
        return false;

    const size_t compatibility = renderer.find("m_RenderGraph->AddPass(\"GBufferCompatibility\"");
    const size_t hiz = renderer.find("m_ModernDeferredPipeline->AddHiZPasses", compatibility);
    const size_t occlusion = renderer.find("m_ModernDeferredPipeline->AddHiZOcclusionCulling", hiz);
    const size_t indirect = renderer.find("m_ModernDeferredPipeline->AddGBufferPass", occlusion);
    if (!Check(compatibility != std::string::npos && hiz != std::string::npos && occlusion != std::string::npos &&
                   indirect != std::string::npos && compatibility < hiz && hiz < occlusion && occlusion < indirect,
               "compatibility/skinned GBuffer does not precede HiZ, occlusion culling, and indirect GBuffer")) {
        return false;
    }

    const size_t addGBuffer = pipeline.find("voidModernDeferredPipeline::AddGBufferPass(");
    const size_t clustered =
        pipeline.find("RGTextureHandleModernDeferredPipeline::AddClusteredLightingPasses(", addGBuffer);
    if (!Check(addGBuffer != std::string::npos && clustered != std::string::npos && addGBuffer < clustered,
               "Modern indirect GBuffer implementation was not found")) {
        return false;
    }
    const std::string gbuffer = pipeline.substr(addGBuffer, clustered - addGBuffer);
    return Check(CountOccurrences(gbuffer, "builder.WriteColor(") == 5 &&
                     CountOccurrences(gbuffer, "RHILoadOp::Load") >= 6 &&
                     gbuffer.find("builder.WriteDepth(sceneDepth,RHILoadOp::Load") != std::string::npos,
                 "indirect GBuffer does not load and preserve compatibility color/depth attachments");
}

bool TestModernDiagnosticsReadbackIsThrottled() {
    const std::string header = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.h",
    }));
    const std::string source = CompactSource(ReadRepositoryTextFile({
        "src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
        "../../../../../src/Runtime/Renderer/ModernDeferredPipeline.cpp",
    }));
    if (!Check(!header.empty() && !source.empty(), "Modern Deferred diagnostics sources were not found"))
        return false;
    return Check(header.find("kDiagnosticsReadbackInterval=30") != std::string::npos &&
                     source.find("frameNumber>=m_LastDiagnosticsReadbackFrame+kDiagnosticsReadbackInterval") !=
                         std::string::npos &&
                     CountOccurrences(source, "if(m_DiagnosticsReadbackThisFrame)") == 4,
                 "Modern diagnostics create GPU readback resources every frame instead of using a sampled cadence");
}

bool TestModernTemporalHistoryCommitAndAbort() {
#ifndef MYENGINE_PLATFORM_WINDOWS
    return true;
#else
    MockRenderContext context;
    context.backend = RHIBackend::D3D12;
    ShaderManager::Get().Clear();
    ShaderManager::Get().SetDevice(&context);
    struct ShaderManagerReset {
        ~ShaderManagerReset() {
            ShaderManager::Get().Clear();
            ShaderManager::Get().SetDevice(nullptr);
        }
    } shaderManagerReset;

    ModernDeferredPipeline pipeline(&context);
    if (!Check(pipeline.IsReady(),
               "Modern Deferred test pipeline is unavailable: " + pipeline.GetInitializationError())) {
        return false;
    }
    pipeline.Resize(64, 64);
    Scene scene("ModernTemporalCommitAbort");
    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 1.0f);

    const auto createInput = [&](const char* name, RHIFormat format) {
        RHITextureDesc desc;
        desc.width = 64;
        desc.height = 64;
        desc.format = format;
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = name;
        auto texture = context.CreateTexture(desc);
        RHITextureViewDesc viewDesc;
        viewDesc.usage = RHIResourceUsage::ShaderResource;
        auto view = context.CreateTextureView(texture, viewDesc);
        return std::pair<std::shared_ptr<GpuTexture>, std::shared_ptr<GpuTextureView>>{std::move(texture),
                                                                                       std::move(view)};
    };
    auto hdr = createInput("TemporalTestHDR", RHIFormat::RGBA16Float);
    auto depth = createInput("TemporalTestDepth", RHIFormat::D24S8);
    auto normal = createInput("TemporalTestNormal", RHIFormat::RGBA16Float);
    auto velocity = createInput("TemporalTestVelocity", RHIFormat::RG16Float);
    const auto stageTemporalFrame = [&]() {
        RenderGraph graph(context);
        const auto importInput = [&](const char* name, const auto& resource) {
            return graph.ImportTexture(name, resource.first, resource.second, RHIResourceState::ShaderResource,
                                       RHIResourceState::ShaderResource);
        };
        const auto hdrHandle = importInput("TemporalInputHDR", hdr);
        const auto depthHandle = importInput("TemporalInputDepth", depth);
        const auto normalHandle = importInput("TemporalInputNormal", normal);
        const auto velocityHandle = importInput("TemporalInputVelocity", velocity);
        pipeline.AddTemporalPostProcess(graph, hdrHandle, hdr.second, depthHandle, depth.second, normalHandle,
                                        normal.second, velocityHandle, velocity.second);
    };
    const auto matricesEqual = [](const Mat4& left, const Mat4& right) {
        for (uint32_t index = 0; index < 16; ++index) {
            if (!NearlyEqual(left.Data()[index], right.Data()[index], 1e-5f))
                return false;
        }
        return true;
    };

    if (!Check(pipeline.Prepare(scene, camera, 1), "Modern temporal frame 1 preparation failed"))
        return false;
    const Mat4 committedViewProjection = pipeline.GetCurrentViewProjection();
    const Vec4 projectionProbe{0.25f, -0.15f, 2.0f, 1.0f};
    const Vec4 stableClip = camera.GetViewProj().Transform(projectionProbe);
    const Vec4 jitteredClip = committedViewProjection.Transform(projectionProbe);
    const float stableUvY = 0.5f - 0.5f * stableClip.y / stableClip.w;
    const float jitteredUvY = 0.5f - 0.5f * jitteredClip.y / jitteredClip.w;
    if (!Check(NearlyEqual(jitteredClip.x / jitteredClip.w, stableClip.x / stableClip.w, 1e-6f) &&
                   NearlyEqual(jitteredUvY - stableUvY, (-1.0f / 6.0f) / 64.0f, 1e-6f),
               "perspective TAA jitter is not an exact sub-pixel clip-space translation")) {
        return false;
    }
    stageTemporalFrame();
    pipeline.CommitTemporalFrame();

    if (!Check(pipeline.Prepare(scene, camera, 2), "Modern temporal frame 2 preparation failed"))
        return false;
    if (!Check(matricesEqual(pipeline.GetPreviousViewProjection(), committedViewProjection),
               "temporal commit did not expose the prior successful frame to reprojection")) {
        return false;
    }
    const Mat4 abortedViewProjection = pipeline.GetCurrentViewProjection();
    if (!Check(!matricesEqual(abortedViewProjection, committedViewProjection),
               "successful temporal commit did not advance the viewport-local Halton phase")) {
        return false;
    }
    stageTemporalFrame();
    pipeline.AbortTemporalFrame("unit-test graph abort");
    if (!Check(pipeline.GetHistoryResetReason() == "unit-test graph abort",
               "temporal abort did not invalidate history with the supplied diagnostic")) {
        return false;
    }

    if (!Check(pipeline.Prepare(scene, camera, 2), "Modern temporal retry preparation failed"))
        return false;
    if (!Check(matricesEqual(pipeline.GetPreviousViewProjection(), committedViewProjection) &&
                   matricesEqual(pipeline.GetCurrentViewProjection(), abortedViewProjection),
               "an aborted temporal frame advanced its previous matrix or Halton phase")) {
        return false;
    }
    stageTemporalFrame();
    pipeline.CommitTemporalFrame();

    if (!Check(pipeline.Prepare(scene, camera, 3), "Modern temporal frame 3 preparation failed"))
        return false;
    if (!Check(matricesEqual(pipeline.GetPreviousViewProjection(), abortedViewProjection) &&
                   !matricesEqual(pipeline.GetCurrentViewProjection(), abortedViewProjection),
               "a successful temporal retry did not commit its matrix and advance exactly one Halton phase")) {
        return false;
    }

    Camera orthographicCamera;
    orthographicCamera.LookAt(Vec3::Zero(), {0.0f, 0.0f, 1.0f});
    orthographicCamera.SetOrtho(10.0f, 10.0f, 0.1f, 100.0f);
    if (!Check(pipeline.Prepare(scene, orthographicCamera, 4), "Modern orthographic temporal preparation failed"))
        return false;
    const Mat4 orthographicBase = orthographicCamera.GetViewProj();
    const Mat4 orthographicJittered = pipeline.GetCurrentViewProjection();
    return Check(NearlyEqual(orthographicJittered.m[2][0], orthographicBase.m[2][0]) &&
                     NearlyEqual(orthographicJittered.m[2][1], orthographicBase.m[2][1]) &&
                     !NearlyEqual(orthographicJittered.m[3][1], orthographicBase.m[3][1]),
                 "orthographic TAA jitter is depth-dependent instead of using clip-space translation");
#endif
}

bool TestHeadlessRendering() {
    AssetManager::Get().Clear();
    Scene scene("HeadlessRender");
    Actor* actor = scene.CreateActor("Cube");
    auto* meshRenderer = actor->AddComponent<MeshRendererComponent>();
    meshRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    meshRenderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    Actor* culledActor = scene.CreateActor("CulledCube");
    culledActor->GetTransform().position = {10000.0f, 0.0f, 0.0f};
    auto* culledRenderer = culledActor->AddComponent<MeshRendererComponent>();
    culledRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    culledRenderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    Actor* transparentActor = scene.CreateActor("TransparentCube");
    transparentActor->GetTransform().position = {0.0f, 0.0f, 1.0f};
    auto* transparentRenderer = transparentActor->AddComponent<MeshRendererComponent>();
    transparentRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    auto transparentMaterial = MaterialAsset::CreateDefault("TransparentTest");
    transparentMaterial->SetBlendMode(BlendMode::Transparent);
    transparentRenderer->SetMaterial(AssetManager::Get().Register(transparentMaterial));

    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    Renderer renderer(&context, &context, &context);
    int queuedUploadRuns = 0;
    const GpuUploadFence uploadFence = GpuUploadQueue::Get().Enqueue([&queuedUploadRuns](IRHIDevice& uploadContext) {
        ++queuedUploadRuns;
        const uint8_t pixel[4] = {255, 255, 255, 255};
        uploadContext.UploadTexture2D(pixel, 1, 1);
    });
    if (!Check(!GpuUploadQueue::Get().IsFenceComplete(uploadFence),
               "GPU upload fence completed before render-thread execution"))
        return false;
    renderer.RenderScene(scene, camera, true);

    if (!Check(queuedUploadRuns == 1 && GpuUploadQueue::Get().PendingCount() == 0 &&
                   GpuUploadQueue::Get().IsFenceComplete(uploadFence),
               "GPU upload queue was not consumed on the render thread"))
        return false;
    if (!Check(context.beginFrames == 1 && context.endFrames == 1, "headless renderer frame lifecycle mismatch"))
        return false;
    if (!Check(context.shaderCreates >= 1, "headless shader was not created"))
        return false;
    if (!Check(context.vertexUploads == 1 && context.indexUploads == 1, "headless mesh upload mismatch"))
        return false;
    if (!Check(context.textureUploads == 4,
               "headless texture uploads missing material, probe, or named-binding fallback"))
        return false;
    if (!Check(context.commands.drawCalls == 3, "frustum culling emitted an unexpected draw count"))
        return false;
    const auto hasSampler = [&](RHIFilter filter, RHIAddressMode addressMode) {
        return std::find_if(context.samplerDescs.begin(), context.samplerDescs.end(),
                            [filter, addressMode](const RHISamplerDesc& desc) {
                                return desc.filter == filter && desc.addressU == addressMode &&
                                       desc.addressV == addressMode && desc.addressW == addressMode;
                            }) != context.samplerDescs.end();
    };
    if (!Check(hasSampler(RHIFilter::Linear, RHIAddressMode::Repeat),
               "main material texture sampler did not default to repeat wrap mode"))
        return false;
    if (!Check(hasSampler(RHIFilter::ComparisonLinear, RHIAddressMode::Clamp),
               "shadow comparison sampler should remain clamp wrap mode"))
        return false;
    const auto transparentPipeline =
        std::find(context.commands.pipelineBlendEnabled.begin(), context.commands.pipelineBlendEnabled.end(), true);
    if (!Check(transparentPipeline != context.commands.pipelineBlendEnabled.end() &&
                   transparentPipeline != context.commands.pipelineBlendEnabled.begin() &&
                   std::count(context.commands.pipelineBlendEnabled.begin(),
                              context.commands.pipelineBlendEnabled.end(), true) == 1,
               "opaque/transparent render ordering or blend state mismatch")) {
        return false;
    }
    const size_t transparentPipelineIndex =
        static_cast<size_t>(std::distance(context.commands.pipelineBlendEnabled.begin(), transparentPipeline));
    return Check(transparentPipelineIndex < context.commands.pipelineDepthWriteEnabled.size() &&
                     !context.commands.pipelineDepthWriteEnabled[transparentPipelineIndex],
                 "transparent forward pipeline writes scene depth and invalidates temporal geometry history");
}

bool TestMeshRendererSubMeshMaterialSlotDraws() {
    AssetManager& assets = AssetManager::Get();
    assets.Clear();

    std::vector<MeshVertex> vertices(4);
    vertices[0].position = {-1.0f, -1.0f, 0.0f};
    vertices[1].position = {1.0f, -1.0f, 0.0f};
    vertices[2].position = {-1.0f, 1.0f, 0.0f};
    vertices[3].position = {1.0f, 1.0f, 0.0f};
    std::vector<uint32_t> indices{0, 1, 2, 1, 3, 2, 0, 2, 3};
    std::vector<SubMesh> subMeshes{
        SubMesh{0, 3, 0, 0, "OpaqueTri"},
        SubMesh{3, 3, 0, 1, "TransparentTri"},
        SubMesh{6, 3, 0, 0, "OpaqueTriReuseMaterial"},
    };
    auto mesh = std::make_shared<MeshAsset>("__test__/TwoSubMeshes");
    mesh->SetGeometry(std::move(vertices), std::move(indices), std::move(subMeshes));
    MeshHandle meshHandle = assets.Register(std::move(mesh));

    auto opaqueMaterial = MaterialAsset::CreateDefault("OpaqueSlot");
    auto clampTexture = std::make_shared<TextureAsset>("__test__/ClampTexture");
    TextureDesc textureDesc;
    textureDesc.width = 4;
    textureDesc.height = 4;
    std::vector<uint8_t> clampPixels(4 * 4 * 4, 255);
    clampTexture->SetPixelData(std::move(clampPixels), textureDesc);
    clampTexture->SetSampler(TextureFilter::Nearest, TextureWrap::Clamp, TextureWrap::Clamp);
    TextureHandle clampTextureHandle = assets.Register(std::move(clampTexture));
    opaqueMaterial->SetTexture("BaseColorMap", clampTextureHandle);
    MaterialHandle opaqueHandle = assets.Register(opaqueMaterial);
    auto transparentMaterial = MaterialAsset::CreateDefault("TransparentSlot");
    transparentMaterial->SetBlendMode(BlendMode::Transparent);
    MaterialHandle transparentHandle = assets.Register(transparentMaterial);

    Scene scene("SubMeshSlots");
    Actor* actor = scene.CreateActor("TwoSlotMesh");
    auto* renderer = actor->AddComponent<MeshRendererComponent>();
    renderer->SetMesh(meshHandle);
    renderer->SetMaterials({opaqueHandle, transparentHandle});

    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    Renderer rendererSystem(&context, &context, &context);
    rendererSystem.RenderScene(scene, camera, true);

    if (!Check(context.vertexUploads == 1 && context.indexUploads == 1, "submesh slot mesh was not uploaded once"))
        return false;
    if (!Check(context.commands.drawCalls == 4, "submesh slot renderer did not emit sky plus one draw per submesh"))
        return false;
    if (!Check(context.bindGroupCreates == 3,
               "main pass did not reuse cached material bind groups across same-material submeshes"))
        return false;
    const bool uploadedClampTextureMips =
        std::find_if(context.uploadedTextureDescs.begin(), context.uploadedTextureDescs.end(),
                     [](const RHITextureDesc& desc) {
                         return desc.width == 4 && desc.height == 4 && desc.mipLevels == 3;
                     }) != context.uploadedTextureDescs.end();
    if (!Check(uploadedClampTextureMips, "texture upload did not include the generated mip chain"))
        return false;
    const bool uploadedThreeSubresources =
        std::find(context.uploadedSubresourceCounts.begin(), context.uploadedSubresourceCounts.end(), 3u) !=
        context.uploadedSubresourceCounts.end();
    if (!Check(uploadedThreeSubresources, "texture upload subresource count did not match mip count"))
        return false;
    const bool hasExpectedMipPitches =
        std::find_if(context.uploadedSubresources.begin(), context.uploadedSubresources.end(),
                     [](const RHITextureSubresourceData& data) {
                         return data.mipLevel == 0 && data.rowPitch == 16 && data.slicePitch == 64;
                     }) != context.uploadedSubresources.end() &&
        std::find_if(context.uploadedSubresources.begin(), context.uploadedSubresources.end(),
                     [](const RHITextureSubresourceData& data) {
                         return data.mipLevel == 1 && data.rowPitch == 8 && data.slicePitch == 16;
                     }) != context.uploadedSubresources.end() &&
        std::find_if(context.uploadedSubresources.begin(), context.uploadedSubresources.end(),
                     [](const RHITextureSubresourceData& data) {
                         return data.mipLevel == 2 && data.rowPitch == 4 && data.slicePitch == 4;
                     }) != context.uploadedSubresources.end();
    if (!Check(hasExpectedMipPitches, "texture mip subresources used unexpected row or slice pitches"))
        return false;
    const bool hasMipView = std::find_if(context.textureViewDescs.begin(), context.textureViewDescs.end(),
                                         [](const RHITextureViewDesc& desc) { return desc.mipCount == 3; }) !=
                            context.textureViewDescs.end();
    if (!Check(hasMipView, "texture view did not expose the full mip chain"))
        return false;
    const bool hasNearestClampSampler =
        std::find_if(context.samplerDescs.begin(), context.samplerDescs.end(), [](const RHISamplerDesc& desc) {
            return desc.filter == RHIFilter::Point && desc.addressU == RHIAddressMode::Clamp &&
                   desc.addressV == RHIAddressMode::Clamp && desc.addressW == RHIAddressMode::Repeat;
        }) != context.samplerDescs.end();
    if (!Check(hasNearestClampSampler, "texture-specific sampler settings were not bound for material texture"))
        return false;
    return Check(std::count(context.commands.pipelineBlendEnabled.begin(), context.commands.pipelineBlendEnabled.end(),
                            true) == 1,
                 "submesh material slots did not classify transparent material independently");
}

bool TestMainPassSamplerCacheDeduplicatesTextureSamplerStates() {
    AssetManager& assets = AssetManager::Get();
    assets.Clear();

    std::vector<MeshVertex> vertices(3);
    vertices[0].position = {-1.0f, -1.0f, 0.0f};
    vertices[1].position = {1.0f, -1.0f, 0.0f};
    vertices[2].position = {0.0f, 1.0f, 0.0f};
    std::vector<uint32_t> indices{0, 1, 2};
    std::vector<SubMesh> subMeshes;
    constexpr uint32_t kMaterialCount = 48;
    subMeshes.reserve(kMaterialCount);
    for (uint32_t i = 0; i < kMaterialCount; ++i) {
        subMeshes.push_back(SubMesh{0, 3, 0, static_cast<int>(i), "SamplerCacheSubMesh"});
    }
    auto mesh = std::make_shared<MeshAsset>("__test__/SamplerCacheMesh");
    mesh->SetGeometry(std::move(vertices), std::move(indices), std::move(subMeshes));
    MeshHandle meshHandle = assets.Register(std::move(mesh));

    std::vector<MaterialHandle> materials;
    materials.reserve(kMaterialCount);
    for (uint32_t i = 0; i < kMaterialCount; ++i) {
        auto texture = std::make_shared<TextureAsset>("__test__/SamplerCacheTexture" + std::to_string(i));
        TextureDesc textureDesc;
        textureDesc.width = 1;
        textureDesc.height = 1;
        std::vector<uint8_t> pixels{255, 255, 255, 255};
        texture->SetPixelData(std::move(pixels), textureDesc);
        TextureHandle textureHandle = assets.Register(std::move(texture));

        auto material = MaterialAsset::CreateDefault("__test__/SamplerCacheMaterial" + std::to_string(i));
        material->SetTexture("BaseColorMap", textureHandle);
        materials.push_back(assets.Register(std::move(material)));
    }

    Scene scene("SamplerCacheScene");
    Actor* actor = scene.CreateActor("SamplerCacheMesh");
    auto* meshRenderer = actor->AddComponent<MeshRendererComponent>();
    meshRenderer->SetMesh(meshHandle);
    meshRenderer->SetMaterials(materials);

    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    Renderer renderer(&context, &context, &context);
    renderer.RenderScene(scene, camera, true);

    const int linearRepeatSamplers = static_cast<int>(
        std::count_if(context.samplerDescs.begin(), context.samplerDescs.end(), [](const RHISamplerDesc& desc) {
            return desc.filter == RHIFilter::Linear && desc.addressU == RHIAddressMode::Repeat &&
                   desc.addressV == RHIAddressMode::Repeat && desc.addressW == RHIAddressMode::Repeat;
        }));
    if (!Check(linearRepeatSamplers == 1, "main pass created duplicate Linear/Repeat samplers for texture assets")) {
        return false;
    }
    return Check(context.textureUploads == static_cast<int>(kMaterialCount) + 2,
                 "sampler cache test did not upload every unique material texture plus probe fallbacks");
}

bool TestDeferredPassResourceContracts() {
    MockRenderContext context;

    GBufferPass gbuffer(&context);
    gbuffer.Resize(320, 180);
    if (!Check(gbuffer.PrepareGraphResources(), "GBufferPass failed to prepare graph resources"))
        return false;
    const auto gbufferResources = gbuffer.GetGraphResources();
    if (!Check(gbufferResources.albedo && gbufferResources.normal && gbufferResources.material &&
                   gbufferResources.emissive && gbufferResources.velocity,
               "GBufferPass did not create all GBuffer textures"))
        return false;
    if (!Check(gbufferResources.albedoSrv && gbufferResources.normalSrv && gbufferResources.materialSrv &&
                   gbufferResources.emissiveSrv && gbufferResources.velocitySrv,
               "GBufferPass did not create all shader-resource views"))
        return false;
    if (!Check(gbufferResources.albedo->desc.format == RHIFormat::RGBA8UNorm &&
                   gbufferResources.normal->desc.format == RHIFormat::RGBA16Float &&
                   gbufferResources.material->desc.format == RHIFormat::RGBA8UNorm &&
                   gbufferResources.emissive->desc.format == RHIFormat::RGBA16Float &&
                   gbufferResources.velocity->desc.format == RHIFormat::RG16Float,
               "GBufferPass resource formats do not match the deferred contract"))
        return false;

    AssetManager::Get().Clear();
    Scene scene("GBufferContract");
    Actor* opaqueActor = scene.CreateActor("Opaque");
    auto* opaqueRenderer = opaqueActor->AddComponent<MeshRendererComponent>();
    opaqueRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    auto opaqueMaterial = MaterialAsset::CreateDefault("GBufferOpaque");
    opaqueRenderer->SetMaterial(AssetManager::Get().Register(opaqueMaterial));

    Actor* transparentActor = scene.CreateActor("Transparent");
    transparentActor->GetTransform().position = {0.0f, 0.0f, 1.0f};
    auto* transparentRenderer = transparentActor->AddComponent<MeshRendererComponent>();
    transparentRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    auto transparentMaterial = MaterialAsset::CreateDefault("GBufferTransparent");
    transparentMaterial->SetBlendMode(BlendMode::Transparent);
    transparentRenderer->SetMaterial(AssetManager::Get().Register(transparentMaterial));

    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext drawContext;
    ShaderManager::Get().Clear();
    ShaderManager::Get().SetDevice(&drawContext);
    GBufferPass drawableGBuffer(&drawContext);
    drawableGBuffer.Execute(drawContext.commands, scene, camera);
    if (!Check(drawContext.shaderCreates >= 1 && drawContext.commands.pipelineBinds == 1,
               "GBufferPass did not create a shader and graphics pipeline"))
        return false;
    if (!Check(drawContext.commands.drawCalls == 1,
               "GBufferPass should draw opaque/alpha-test geometry and skip transparent"))
        return false;
    if (!Check(!drawContext.commands.pipelineBlendEnabled.empty() && !drawContext.commands.pipelineBlendEnabled.front(),
               "GBufferPass pipeline should write material data without alpha blending"))
        return false;

    DeferredLightingPass lighting(&context);
    lighting.Resize(320, 180);
    ShaderManager::Get().Clear();
    ShaderManager::Get().SetDevice(&context);
    if (!Check(lighting.PrepareGraphResources(), "DeferredLightingPass failed to prepare graph resources"))
        return false;
    const auto lightingResources = lighting.GetGraphResources();
    if (!Check(lightingResources.sceneColor && lightingResources.sceneColorRtv && lightingResources.sceneColorSrv &&
                   lightingResources.sceneColor->desc.format == RHIFormat::RGBA16Float,
               "DeferredLightingPass scene color contract is invalid"))
        return false;

    return Check(context.graphTextureCreates == 6 && context.textureViewCreates == 12 && context.samplerCreates >= 3 &&
                     context.shaderCreates >= 1,
                 "deferred resource passes created an unexpected number of resources");
}

bool TestDeferredLightingShaderSourceContract() {
    std::ifstream sourceFile("EngineContent/Shaders/DeferredLightingPass.hlsl");
    std::stringstream buffer;
    buffer << sourceFile.rdbuf();
    const std::string source = buffer.str();
    if (!Check(!source.empty(), "DeferredLightingPass shader source is missing"))
        return false;
    if (!Check(source.find("WorldPosFromDepth") != std::string::npos &&
                   source.find("g_InvViewProj") != std::string::npos,
               "DeferredLightingPass does not reconstruct world position from depth"))
        return false;
    if (!Check(source.find("g_EnvironmentSH2") != std::string::npos &&
                   source.find("PbrEnvironmentLighting") != std::string::npos,
               "DeferredLightingPass does not consume environment SH/PBR lighting"))
        return false;
    if (!Check(source.find("SampleDirectionalShadow") != std::string::npos &&
                   source.find("g_ShadowMap.SampleCmpLevelZero") != std::string::npos,
               "DeferredLightingPass does not sample directional shadows"))
        return false;
    return Check(source.find("EnvironmentRadiance(viewDir") != std::string::npos,
                 "DeferredLightingPass does not include sky fallback for far depth");
}

bool TestRendererDeferredPathSubmitsGBufferLightingTransparentAndComposite() {
    AssetManager::Get().Clear();
    Scene scene("DeferredPath");
    Actor* opaqueActor = scene.CreateActor("Opaque");
    auto* opaqueRenderer = opaqueActor->AddComponent<MeshRendererComponent>();
    opaqueRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    opaqueRenderer->SetMaterial(AssetManager::Get().GetDefaultMaterial());

    Actor* transparentActor = scene.CreateActor("Transparent");
    transparentActor->GetTransform().position = {0.0f, 0.0f, 1.0f};
    auto* transparentRenderer = transparentActor->AddComponent<MeshRendererComponent>();
    transparentRenderer->SetMesh(AssetManager::Get().GetCubeMesh());
    auto transparentMaterial = MaterialAsset::CreateDefault("DeferredTransparent");
    transparentMaterial->SetBlendMode(BlendMode::Transparent);
    transparentRenderer->SetMaterial(AssetManager::Get().Register(transparentMaterial));

    Actor* lightActor = scene.CreateActor("Sun");
    auto* light = lightActor->AddComponent<LightComponent>();
    light->SetLightType(LightType::Directional);
    light->SetCastShadows(true);

    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    context.backend = RHIBackend::D3D11;
    Renderer renderer(&context, &context, &context);
    renderer.Resize(128, 72);
    renderer.SetRenderPath(RenderPath::Deferred);
    renderer.SetOutputOffscreen(true);
    renderer.RenderScene(scene, camera, true);

    if (!Check(context.beginFrames == 1 && context.endFrames == 1, "deferred renderer frame lifecycle mismatch"))
        return false;
    if (!Check(context.commands.renderingScopes == 0 && context.commands.renderingBeginCalls >= 7,
               "deferred graph did not submit expected render scopes"))
        return false;
    if (!Check(context.commands.pipelineBinds >= 7 && context.commands.bindGroupBinds >= 5,
               "deferred graph did not bind GBuffer, lighting, transparent, and composite passes"))
        return false;
    return Check(context.commands.drawCalls >= 5 && renderer.GetSceneColorView() != nullptr,
                 "deferred graph did not submit expected draw calls or output view");
}

bool TestRendererRenderPathDefaultsToForward() {
    MockRenderContext context;
    Renderer renderer(&context, &context, &context);
    if (!Check(renderer.GetRenderPath() == RenderPath::Forward, "Renderer did not default to the forward render path"))
        return false;
    renderer.SetRenderPath(RenderPath::Deferred);
    return Check(renderer.GetRenderPath() == RenderPath::Deferred,
                 "Renderer render path setter did not persist deferred mode");
}

bool TestRenderPipelineDeviceProfileResolution() {
    RHIDeviceCapabilities modern;
    modern.maxBindlessResources = 8192;
    modern.computeShaders = true;
    modern.storageTextures = true;
    modern.indirectDraw = true;
    modern.indirectDrawCount = true;
    modern.indirectDispatch = true;
    modern.bindlessResources = true;
    modern.shaderDrawParameters = true;
    modern.modernDeferredFormats = true;

    const auto desktop =
        ResolveRenderPipeline(RenderPath::Deferred, GraphicsDeviceProfile::Desktop, RHIBackend::D3D12, modern, true);
    const auto console =
        ResolveRenderPipeline(RenderPath::Deferred, GraphicsDeviceProfile::Console, RHIBackend::Vulkan, modern, true);
    const auto mobile =
        ResolveRenderPipeline(RenderPath::Deferred, GraphicsDeviceProfile::Mobile, RHIBackend::D3D12, modern, true);
    modern.storageTextures = false;
    const auto fallback =
        ResolveRenderPipeline(RenderPath::Deferred, GraphicsDeviceProfile::Console, RHIBackend::D3D12, modern, true);
    const auto forward =
        ResolveRenderPipeline(RenderPath::Forward, GraphicsDeviceProfile::Console, RHIBackend::D3D11, modern, true);
    return Check(desktop.resolvedPipeline == ResolvedRenderPipeline::ModernDeferred && desktop.modernSupported &&
                     console.resolvedPipeline == ResolvedRenderPipeline::ModernDeferred &&
                     mobile.resolvedPipeline == ResolvedRenderPipeline::ClassicDeferred &&
                     fallback.resolvedPipeline == ResolvedRenderPipeline::ClassicDeferred && fallback.usedFallback &&
                     !fallback.fallbackReason.empty() && forward.resolvedPipeline == ResolvedRenderPipeline::Forward,
                 "render pipeline device-profile resolution matrix mismatch");
}

bool TestRendererDefersModernInitializationUntilFirstRender() {
    AssetManager::Get().Clear();
    ShaderManager::Get().Clear();
    MockRenderContext context;
    context.backend = RHIBackend::D3D12;
    context.modernCapabilities = true;
    uint32_t cacheRequests = 0;
    ShaderCacheService::Get().SetResolver([&](const ShaderCacheRequest&) {
        ++cacheRequests;
        return ShaderCacheResult{};
    });

    bool valid = false;
    {
        Renderer sceneRenderer(&context, &context, &context);
        Renderer gameRenderer(&context, &context, &context);
        Renderer previewRenderer(&context, &context, &context);
        const bool constructorsWereLazy = cacheRequests == 0 && context.samplerCreates == 0;

        sceneRenderer.SetRenderPath(RenderPath::Deferred);
        const bool desktopResolvesBeforeCreation =
            sceneRenderer.GetPipelineDiagnostics().resolvedPipeline == ResolvedRenderPipeline::ModernDeferred &&
            cacheRequests == 0 && context.samplerCreates == 0;

        previewRenderer.SetRenderPath(RenderPath::Deferred);
        previewRenderer.SetDeviceProfile(GraphicsDeviceProfile::Mobile);
        const bool mobileStaysClassicWithoutCreation =
            previewRenderer.GetPipelineDiagnostics().resolvedPipeline == ResolvedRenderPipeline::ClassicDeferred &&
            cacheRequests == 0 && context.samplerCreates == 0;
        valid = constructorsWereLazy && desktopResolvesBeforeCreation && mobileStaysClassicWithoutCreation;
    }

    ShaderCacheService::Get().ClearResolver();
    ShaderManager::Get().Clear();
    AssetManager::Get().Clear();
    return Check(valid, "Renderer constructed or compiled the Modern pipeline before its first requested frame");
}

bool TestRendererStartsShaderPrewarmOffRenderThread() {
    AssetManager::Get().Clear();
    ShaderManager::Get().Clear();
    MockRenderContext context;
    context.backend = RHIBackend::D3D12;
    context.modernCapabilities = true;

    const std::filesystem::path projectRoot =
        std::filesystem::temp_directory_path() / "myengine_renderer_scene_shader_prewarm";
    std::error_code projectError;
    std::filesystem::remove_all(projectRoot, projectError);
    std::filesystem::create_directories(projectRoot / "Content" / "Shaders", projectError);
    std::filesystem::create_directories(projectRoot / ".myengine", projectError);
    const std::filesystem::path sceneShaderSource = projectRoot / "Content" / "Shaders" / "SceneStartup.shader";
    std::ofstream(sceneShaderSource)
        << R"({"type":"Shader","version":1,"stages":{"vertex":{"source":"SceneStartup.hlsl","entry":"VSMain"},"pixel":{"source":"SceneStartup.hlsl","entry":"PSMain"}},"defines":[]})";

    const std::string sceneShaderUuid = "renderer-scene-startup-shader";
    const std::filesystem::path sceneShaderArtifact =
        projectRoot / "Library" / "windows-x64" / sceneShaderUuid / "cooked.shader";
    std::filesystem::create_directories(sceneShaderArtifact.parent_path(), projectError);
    std::array<std::array<std::vector<uint8_t>, kShaderStageCount>, kShaderBackendCount> bytecode{};
    bytecode[static_cast<size_t>(ShaderBackend::D3D12)][static_cast<size_t>(ShaderStage::Vertex)] = {1};
    bytecode[static_cast<size_t>(ShaderBackend::D3D12)][static_cast<size_t>(ShaderStage::Pixel)] = {2};
    ShaderAsset cookedShader(sceneShaderArtifact.string());
    cookedShader.SetCooked(ShaderAsset::kVertexMask | ShaderAsset::kPixelMask, 1, std::move(bytecode));
    std::string cookedError;
    const bool cookedSaved = SaveCookedShaderAsset(cookedShader, sceneShaderArtifact, &cookedError);
    const nlohmann::json database = {
        {"version", 1},
        {"assets", nlohmann::json::array({{{"uuid", sceneShaderUuid},
                                           {"sourcePath", sceneShaderSource.generic_string()},
                                           {"artifactPath", sceneShaderArtifact.generic_string()},
                                           {"type", "shader"},
                                           {"importer", "shader"},
                                           {"importerVersion", 1},
                                           {"sourceHash", "test"},
                                           {"artifactHash", ""},
                                           {"settings", "{}"},
                                           {"dependencies", nlohmann::json::array()},
                                           {"state", "ready"},
                                           {"diagnostics", nlohmann::json::array()},
                                           {"alwaysCook", false}}})}};
    std::ofstream(projectRoot / ".myengine" / "AssetDatabase.json") << database.dump(2);
    AssetManager::Get().SetProjectRoot(projectRoot);

    std::promise<void> releaseWorkers;
    const std::shared_future<void> workerGate = releaseWorkers.get_future().share();
    std::atomic_uint32_t batchEntries{0};
    std::atomic_uint32_t batchArtifacts{0};
    std::atomic_bool sawSceneSource{false};
    std::atomic_bool sawLibraryPath{false};
    ShaderCacheService::Get().SetResolver([](const ShaderCacheRequest&) { return ShaderCacheResult{}; },
                                          [workerGate, &batchEntries, &batchArtifacts, &sawSceneSource, &sawLibraryPath,
                                           sceneShaderSource](const std::vector<ShaderCacheRequest>& requests) {
                                              batchArtifacts = static_cast<uint32_t>(requests.size());
                                              for (const ShaderCacheRequest& request : requests) {
                                                  const std::filesystem::path normalized =
                                                      request.sourcePath.lexically_normal();
                                                  if (normalized == sceneShaderSource.lexically_normal())
                                                      sawSceneSource.store(true);
                                                  const std::string path = normalized.generic_string();
                                                  if (path.find("/Library/") != std::string::npos)
                                                      sawLibraryPath.store(true);
                                              }
                                              ++batchEntries;
                                              workerGate.wait();
                                              return std::vector<ShaderCacheResult>(requests.size());
                                          });

    const ShaderAssetHandle sceneShader = AssetManager::Get().Load<ShaderAsset>("Content/Shaders/SceneStartup.shader");
    const std::shared_ptr<MaterialAsset> sceneMaterial = MaterialAsset::CreateDefault("SceneStartupMaterial");
    sceneMaterial->SetShaderAsset(sceneShader);

    Scene scene("AsyncShaderPrewarm");
    Actor* sceneActor = scene.CreateActor("CustomShaderActor");
    sceneActor->AddComponent<MeshRendererComponent>()->SetMaterial(MaterialHandle(sceneMaterial));
    Camera camera;
    const auto start = std::chrono::steady_clock::now();
    {
        Renderer renderer(&context, &context, &context);
        renderer.SetRenderPath(RenderPath::Deferred);
        renderer.RenderScene(scene, camera, true);
    }
    const double renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (batchEntries.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool valid = cookedSaved && sceneShader.IsValid() && sceneShader->IsCooked() && renderMs < 100.0 &&
                       batchEntries.load() == 1 && batchArtifacts.load() == 32 && sawSceneSource.load() &&
                       !sawLibraryPath.load();
    releaseWorkers.set_value();
    ShaderManager::Get().Clear();
    ShaderCacheService::Get().ClearResolver();
    AssetManager::Get().Clear();
    AssetManager::Get().SetProjectRoot({});
    std::filesystem::remove_all(projectRoot, projectError);
    std::ostringstream failure;
    failure << "first RenderScene blocked on asynchronous shader cache preparation"
            << " cookedSaved=" << cookedSaved << " validShader=" << sceneShader.IsValid()
            << " cookedShader=" << (sceneShader.IsValid() && sceneShader->IsCooked()) << " renderMs=" << renderMs
            << " batches=" << batchEntries.load() << " artifacts=" << batchArtifacts.load()
            << " sawSource=" << sawSceneSource.load() << " sawLibrary=" << sawLibraryPath.load()
            << " cookError=" << cookedError;
    return Check(valid, failure.str());
}

bool TestShaderPrewarmAsyncContinuesWithChangedRequestSet() {
    AssetManager::Get().Clear();
    ShaderManager::Get().Clear();
    MockRenderContext context;
    context.backend = RHIBackend::D3D12;
    std::atomic_uint32_t batchEntries{0};
    std::atomic_uint32_t requestedArtifacts{0};
    ShaderCacheService::Get().SetResolver(
        [](const ShaderCacheRequest&) { return ShaderCacheResult{}; },
        [&batchEntries, &requestedArtifacts](const std::vector<ShaderCacheRequest>& requests) {
            ++batchEntries;
            requestedArtifacts += static_cast<uint32_t>(requests.size());
            std::vector<ShaderCacheResult> results(requests.size());
            for (ShaderCacheResult& result : results)
                result.succeeded = true;
            return results;
        });
    ShaderManager::Get().SetDevice(&context);

    const ShaderPrewarmStatus initial =
        ShaderManager::Get().PrewarmCacheArtifactsAsync({EngineShaders::kPostProcessFXAA});
    ShaderPrewarmStatus changed = ShaderPrewarmStatus::Pending;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (changed == ShaderPrewarmStatus::Pending && std::chrono::steady_clock::now() < deadline) {
        changed = ShaderManager::Get().PrewarmCacheArtifactsAsync({EngineShaders::kProceduralSky});
        std::this_thread::yield();
    }
    const bool valid = initial == ShaderPrewarmStatus::Pending && changed == ShaderPrewarmStatus::Ready &&
                       batchEntries.load() == 2 && requestedArtifacts.load() == 2;

    ShaderManager::Get().Clear();
    ShaderCacheService::Get().ClearResolver();
    AssetManager::Get().Clear();
    return Check(valid, "asynchronous shader prewarm marked a changed request set ready before preparing it");
}

bool TestShaderManagerClearCancelsPendingPrewarm() {
    AssetManager::Get().Clear();
    ShaderManager::Get().Clear();
    MockRenderContext context;
    context.backend = RHIBackend::D3D12;
    std::atomic_bool workerEntered{false};
    std::atomic_bool cancellationObserved{false};
    ShaderCacheService::Get().SetResolverWithCancellation(
        [](const ShaderCacheRequest&) { return ShaderCacheResult{}; },
        [&workerEntered, &cancellationObserved](const std::vector<ShaderCacheRequest>& requests,
                                                const std::shared_ptr<ShaderCacheBatchCancellation>& cancellation) {
            workerEntered.store(true);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (cancellation && !cancellation->IsCancellationRequested() &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            cancellationObserved.store(cancellation && cancellation->IsCancellationRequested());
            std::vector<ShaderCacheResult> results(requests.size());
            for (ShaderCacheResult& result : results) {
                result.diagnostic = "prewarm cancelled by ShaderManager::Clear";
                result.failureKind = ShaderCacheFailureKind::Cancelled;
            }
            return results;
        });
    ShaderManager::Get().SetDevice(&context);

    const ShaderPrewarmStatus status =
        ShaderManager::Get().PrewarmCacheArtifactsAsync({EngineShaders::kPostProcessFXAA});
    const auto workerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!workerEntered.load() && std::chrono::steady_clock::now() < workerDeadline)
        std::this_thread::yield();
    const auto clearStart = std::chrono::steady_clock::now();
    ShaderManager::Get().Clear();
    const double clearElapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clearStart).count();
    const bool valid = status == ShaderPrewarmStatus::Pending && workerEntered.load() && cancellationObserved.load() &&
                       clearElapsedMs < 500.0;

    ShaderCacheService::Get().ClearResolver();
    AssetManager::Get().Clear();
    return Check(valid, "ShaderManager::Clear did not cancel a pending shader prewarm batch promptly");
}

bool TestShaderPrewarmFailureIsCachedWithoutRenderThreadRetry() {
    AssetManager::Get().Clear();
    ShaderManager::Get().Clear();
    MockRenderContext context;
    context.backend = RHIBackend::D3D12;
    std::atomic_uint32_t singleEntries{0};
    std::atomic_uint32_t batchEntries{0};
    ShaderCacheService::Get().SetResolver(
        [&singleEntries](const ShaderCacheRequest&) {
            ++singleEntries;
            return ShaderCacheResult{};
        },
        [&batchEntries](const std::vector<ShaderCacheRequest>& requests) {
            ++batchEntries;
            std::vector<ShaderCacheResult> results(requests.size());
            for (ShaderCacheResult& result : results)
                result.diagnostic = "intentional prewarm failure";
            return results;
        });
    ShaderManager::Get().SetDevice(&context);

    ShaderPrewarmStatus status = ShaderManager::Get().PrewarmCacheArtifactsAsync({EngineShaders::kPostProcessFXAA});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (status == ShaderPrewarmStatus::Pending && std::chrono::steady_clock::now() < deadline) {
        status = ShaderManager::Get().PrewarmCacheArtifactsAsync({EngineShaders::kPostProcessFXAA});
        std::this_thread::yield();
    }
    const ShaderPrewarmStatus repeated =
        ShaderManager::Get().PrewarmCacheArtifactsAsync({EngineShaders::kPostProcessFXAA});
    ShaderManager::Get().Recompile("EngineContent/Shaders/PostProcessFXAA.shader");
    ShaderPrewarmStatus afterEngineAliasReload =
        ShaderManager::Get().PrewarmCacheArtifactsAsync({EngineShaders::kPostProcessFXAA});
    const auto reloadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (afterEngineAliasReload == ShaderPrewarmStatus::Pending &&
           std::chrono::steady_clock::now() < reloadDeadline) {
        afterEngineAliasReload = ShaderManager::Get().PrewarmCacheArtifactsAsync({EngineShaders::kPostProcessFXAA});
        std::this_thread::yield();
    }
    const std::shared_ptr<ShaderHandle> handle =
        ShaderManager::Get().GetOrCreate(EngineShaders::kPostProcessFXAA, nullptr, 0);
    const bool valid = status == ShaderPrewarmStatus::Failed && repeated == ShaderPrewarmStatus::Failed &&
                       afterEngineAliasReload == ShaderPrewarmStatus::Failed && batchEntries.load() == 2 &&
                       singleEntries.load() == 0 && handle && !handle->shader;

    ShaderManager::Get().Clear();
    ShaderCacheService::Get().ClearResolver();
    AssetManager::Get().Clear();
    return Check(valid, "failed shader prewarm retried cooking or fell through to render-thread compilation");
}

bool TestRendererFeatureMaskSkipsOptionalPasses() {
    AssetManager::Get().Clear();
    Scene scene("RendererFeatureMask");
    Actor* meshActor = scene.CreateActor("Cube");
    auto* mesh = meshActor->AddComponent<MeshRendererComponent>();
    mesh->SetMesh(AssetManager::Get().GetCubeMesh());
    mesh->SetMaterial(AssetManager::Get().GetDefaultMaterial());
    Actor* lightActor = scene.CreateActor("Sun");
    auto* light = lightActor->AddComponent<LightComponent>();
    light->SetLightType(LightType::Directional);
    light->SetCastShadows(true);
    Actor* postActor = scene.CreateActor("PostProcess");
    postActor->AddComponent<PostProcessComponent>()->SetSSAOIntensity(1.0f);

    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);
    UIDrawList ui;
    ui.Add({});

    struct Result {
        int textureCreates = 0;
        int renderingBegins = 0;
        RendererFrameStats stats;
        bool hasOutput = false;
    };
    const auto renderWith = [&](RendererFeatureMask mask) {
        ShaderManager::Get().Clear();
        MockRenderContext context;
        context.backend = RHIBackend::D3D11;
        Renderer renderer(&context, &context, &context);
        renderer.Resize(128, 72);
        renderer.SetOutputOffscreen(true);
        renderer.SetFeatureMask(mask);
        renderer.SetUIDrawList(&ui);
        renderer.RenderScene(scene, camera, true);
        return Result{context.graphTextureCreates, context.commands.renderingBeginCalls,
                      FrameStatsProvider::GetRendererStats(), renderer.GetSceneColorView() != nullptr};
    };

    const Result all = renderWith(RendererFeatureMask::All);
    const Result noShadows = renderWith(RendererFeatureMask::All & ~RendererFeatureMask::Shadows);
    const Result noSsao = renderWith(RendererFeatureMask::All & ~RendererFeatureMask::SSAO);
    const Result noUi = renderWith(RendererFeatureMask::All & ~RendererFeatureMask::ScreenUI);
    const Result none = renderWith(RendererFeatureMask::None);

    if (!Check(all.hasOutput && noShadows.hasOutput && noSsao.hasOutput && noUi.hasOutput && none.hasOutput,
               "feature mask prevented the main offscreen output"))
        return false;
    if (!Check(all.stats.shadowDrawCalls > 0 && noShadows.stats.shadowDrawCalls == 0 &&
                   all.textureCreates >= noShadows.textureCreates + 3,
               "Shadows feature did not remove shadow resources and draws"))
        return false;
    if (!Check(all.textureCreates >= noSsao.textureCreates + 2 &&
                   all.stats.fullscreenDrawCalls >= noSsao.stats.fullscreenDrawCalls + 3,
               "SSAO feature did not remove occlusion resources and passes"))
        return false;
    if (!Check(all.renderingBegins >= noUi.renderingBegins + 1, "ScreenUI feature did not remove the UI graph pass"))
        return false;
    if (!Check(none.stats.shadowDrawCalls == 0 && none.textureCreates < all.textureCreates,
               "RendererFeatureMask::None did not disable optional resources"))
        return false;

    ShaderManager::Get().Clear();
    MockRenderContext dynamicContext;
    dynamicContext.backend = RHIBackend::D3D11;
    Renderer dynamicRenderer(&dynamicContext, &dynamicContext, &dynamicContext);
    if (!Check(dynamicRenderer.GetFeatureMask() == RendererFeatureMask::All,
               "Renderer feature mask did not default to All"))
        return false;
    dynamicRenderer.Resize(128, 72);
    dynamicRenderer.SetOutputOffscreen(true);
    dynamicRenderer.RenderScene(scene, camera, true);
    dynamicRenderer.SetFeatureMask(RendererFeatureMask::None);
    dynamicRenderer.RenderScene(scene, camera, true);
    return Check(FrameStatsProvider::GetRendererStats().shadowDrawCalls == 0 &&
                     dynamicRenderer.GetSceneColorView() != nullptr,
                 "dynamic feature disable retained shadow submission or lost the main output");
}

bool TestMaterialPreviewDirtyDrivenScheduling() {
    AssetManager::Get().Clear();
    ShaderManager::Get().Clear();
    MockRenderContext context;
    context.backend = RHIBackend::D3D11;
    SceneRenderLayer layer(&context, 320, 180);
    layer.SetPresentEnabled(false);
    if (!Check(!layer.IsSceneViewportActive() && !layer.IsGameViewportActive() &&
                   !layer.GetSceneViewport()->IsInputEnabled(),
               "editor viewports did not start inactive"))
        return false;

    layer.ConfigureMaterialPreview("EngineContent/Shaders/Mesh.shader", false);
    layer.OnRender();
    const int firstRenderCount = context.beginFrames;
    layer.OnRender();
    if (!Check(firstRenderCount == 1 && context.beginFrames == firstRenderCount,
               "static material preview rendered without a dirty request"))
        return false;

    layer.InvalidateMaterialPreview();
    layer.OnRender();
    if (!Check(context.beginFrames == firstRenderCount + 1, "material preview invalidation did not submit one frame"))
        return false;

    layer.SetMaterialPreviewRealtime(true);
    layer.OnRender();
    layer.OnRender();
    if (!Check(context.beginFrames == firstRenderCount + 3, "realtime material preview did not render while active"))
        return false;

    layer.SetMaterialPreviewActive(false);
    layer.OnRender();
    return Check(context.beginFrames == firstRenderCount + 3, "hidden realtime material preview continued rendering");
}

bool TestViewportActivityCommitPreservesContinuousInput() {
    MockRenderContext context;
    SceneRenderLayer layer(&context, 320, 180);
    layer.SetPresentEnabled(false);

    SceneViewport* viewport = layer.GetSceneViewport();
    if (!Check(viewport && !viewport->IsInputEnabled(), "editor scene input did not start disabled"))
        return false;

    layer.SetSceneViewportActive(true);
    viewport->SetInputEnabled(true);
    layer.BeginViewportActivityFrame();
    if (!Check(viewport->IsInputEnabled(), "visibility candidate reset transiently disabled scene input"))
        return false;

    layer.SetSceneViewportActive(true);
    layer.CommitViewportActivityFrame();
    if (!Check(layer.IsSceneViewportActive() && viewport->IsInputEnabled(),
               "visible viewport commit did not preserve continuous input"))
        return false;

    layer.BeginViewportActivityFrame();
    layer.CommitViewportActivityFrame();
    return Check(!layer.IsSceneViewportActive() && !viewport->IsInputEnabled(),
                 "hidden viewport commit retained input capture");
}

bool TestRendererOffscreenGraphPostProcessPath() {
    AssetManager::Get().Clear();
    Scene scene;
    Actor* lightActor = scene.CreateActor("ShadowLight");
    auto* light = lightActor->AddComponent<LightComponent>();
    light->SetLightType(LightType::Directional);
    light->SetCastShadows(true);
    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    context.backend = RHIBackend::D3D11;
    Renderer renderer(&context, &context, &context);
    renderer.Resize(128, 72);
    renderer.SetOutputOffscreen(true);
    renderer.RenderScene(scene, camera, true);

    const auto hasTransitionTo = [&](RHIResourceState state) {
        return std::find_if(context.commands.transitions.begin(), context.commands.transitions.end(),
                            [state](const auto& transition) { return transition.second == state; }) !=
               context.commands.transitions.end();
    };
    const auto countTransitionsTo = [&](RHIResourceState state) {
        return std::count_if(context.commands.transitions.begin(), context.commands.transitions.end(),
                             [state](const auto& transition) { return transition.second == state; });
    };
    if (!Check(context.beginFrames == 1 && context.endFrames == 1, "offscreen graph renderer frame lifecycle mismatch"))
        return false;
    if (!Check(context.graphTextureCreates >= 5 && context.samplerCreates >= 3,
               "offscreen graph post-process resources were not prepared"))
        return false;
    if (!Check(context.commands.renderingScopes == 0 && context.commands.renderingBeginCalls >= 5,
               "offscreen graph did not manage expected render scopes"))
        return false;
    if (!Check(context.commands.dispatches >= 1, "offscreen graph did not execute environment SH projection"))
        return false;
    if (!Check(hasTransitionTo(RHIResourceState::RenderTarget) && hasTransitionTo(RHIResourceState::DepthWrite) &&
                   hasTransitionTo(RHIResourceState::UnorderedAccess) &&
                   hasTransitionTo(RHIResourceState::ShaderResource),
               "offscreen graph did not transition render graph resources"))
        return false;
    if (!Check(countTransitionsTo(RHIResourceState::DepthWrite) >= 2,
               "offscreen graph did not transition shadow and scene depth resources"))
        return false;
    return Check(renderer.GetSceneColorView() != nullptr, "offscreen graph renderer did not expose a scene color view");
}

bool TestRendererBackbufferCompositeGraphTarget() {
    AssetManager::Get().Clear();
    Scene scene;
    Camera camera;
    camera.LookAt({0.0f, 0.0f, -4.0f}, Vec3::Zero());
    camera.SetPerspective(60.0f, 16.0f / 9.0f);

    MockRenderContext context;
    context.backend = RHIBackend::D3D11;
    Renderer renderer(&context, &context, &context);
    renderer.Resize(128, 72);
    renderer.RenderScene(scene, camera, true);

    if (!Check(context.beginFrames == 1 && context.endFrames == 1,
               "backbuffer graph renderer frame lifecycle mismatch"))
        return false;
    if (!Check(context.commands.renderingScopes == 0 && context.commands.renderingBeginCalls >= 5,
               "backbuffer graph composite did not use graph-managed render scopes"))
        return false;
    if (!Check(context.commands.pipelineBinds >= 5 && context.commands.bindGroupBinds >= 4,
               "backbuffer graph composite did not draw post-process passes"))
        return false;
    return Check(context.commands.drawCalls >= 5, "backbuffer graph composite emitted too few draw calls");
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
        if (!file)
            continue;
        std::ostringstream contents;
        contents << file.rdbuf();
        source = contents.str();
        break;
    }
    return Check(!source.empty() && source.find("AllowNoResourceAccess") == std::string::npos &&
                     source.find("\"PrepareMain\"") == std::string::npos,
                 "Renderer should not register CPU-only RenderGraph compatibility passes");
}

bool TestLightingProbeComponentAndAssetRoundTrip() {
    namespace fs = std::filesystem;
    Scene scene("ProbeRoundTrip");
    Actor* reflectionActor = scene.CreateActor("Reflection");
    auto* reflection = reflectionActor->AddComponent<ReflectionProbeComponent>();
    reflection->SetBoxExtents({3.0f, 4.0f, 5.0f});
    reflection->SetBlendDistance(0.75f);
    reflection->SetPriority(4);
    const std::string reflectionId = reflection->GetProbeId();
    Actor* volumeActor = scene.CreateActor("SHVolume");
    auto* volume = volumeActor->AddComponent<SHProbeVolumeComponent>();
    volume->SetGridSpacing(1.5f);
    const std::string volumeId = volume->GetProbeId();
    scene.SetLightingProbeAssetPath("Content/Lighting/ProbeRoundTrip.lightprobes");
    scene.SetLightingProbeBakeSettings({64, 32.0f});
    const std::string json = SceneSerializer::SaveToString(scene);
    Scene loaded;
    if (!SceneSerializer::LoadFromString(loaded, json))
        return Check(false, "lighting probe scene round trip failed to load");
    Actor* loadedReflectionActor = loaded.FindByName("Reflection");
    Actor* loadedVolumeActor = loaded.FindByName("SHVolume");
    const auto* loadedReflection =
        loadedReflectionActor ? loadedReflectionActor->GetComponent<ReflectionProbeComponent>() : nullptr;
    const auto* loadedVolume = loadedVolumeActor ? loadedVolumeActor->GetComponent<SHProbeVolumeComponent>() : nullptr;

    LightingProbeAsset asset("ProbeRoundTrip.lightprobes");
    asset.SetSceneGuid("probe-scene");
    asset.SetDependencyHash(42);
    asset.SetReflectionResolution(64);
    asset.ReflectionProbes().push_back({reflectionId, Vec3::Zero(), {3, 4, 5}, 8.0f, 0});
    asset.ReflectionPixels().resize(asset.GetBytesPerReflectionProbe(), 127);
    asset.SHVolumes().push_back({volumeId, Vec3::Zero(), {5, 5, 5}, 2, 2, 2, 0});
    asset.SHCoefficients().resize(8 * LightingProbeAsset::SHCoefficientCount);
    for (size_t i = 0; i < asset.SHCoefficients().size(); ++i)
        asset.SHCoefficients()[i] = {static_cast<float>(i) * 0.01f, 0.25f, 0.5f, 0.0f};
    asset.MarkReady();
    const fs::path root = fs::temp_directory_path() / "myengine_lighting_probe_asset_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const fs::path path = root / "ProbeRoundTrip.lightprobes";
    std::string error;
    const bool saved = SaveLightingProbeAssetToFile(asset, path.string(), &error);
    const auto reloaded = saved ? LoadLightingProbeAssetFromFile(path.string()) : nullptr;
    const bool halfRoundTrip =
        std::abs(LightingProbeHalfToFloat(LightingProbeFloatToHalf(3.14159f)) - 3.14159f) < 0.003f;
    const bool valid = loadedReflection && loadedVolume && loadedReflection->GetProbeId() == reflectionId &&
                       loadedVolume->GetProbeId() == volumeId &&
                       loaded.GetLightingProbeAssetPath() == scene.GetLightingProbeAssetPath() &&
                       loaded.GetLightingProbeBakeSettings().reflectionResolution == 64 && saved && reloaded &&
                       reloaded->GetReflectionProbes().size() == 1 && reloaded->GetSHVolumes().size() == 1 &&
                       reloaded->GetReflectionPixels() == asset.GetReflectionPixels() && halfRoundTrip;
    fs::remove_all(root, ec);
    return Check(valid, "lighting probe component or binary asset round trip failed: " + error);
}

bool TestLightingProbeBakeAndShaderContracts() {
    Scene scene("ProbeBake");
    scene.SetLightingProbeBakeSettings({64, 16.0f});
    Actor* reflectionActor = scene.CreateActor("Reflection");
    reflectionActor->AddComponent<ReflectionProbeComponent>()->SetBoxExtents({2.0f, 2.0f, 2.0f});
    Actor* volumeActor = scene.CreateActor("SHVolume");
    auto* volume = volumeActor->AddComponent<SHProbeVolumeComponent>();
    volume->SetBoxExtents({0.2f, 0.2f, 0.2f});
    volume->SetGridSpacing(2.0f);
    LightingProbeAsset asset("ProbeBake.lightprobes");
    const ProbeBakeResult result = ProbeBakeRenderer{}.Bake(scene, asset);
    const std::string forward = ReadRepositoryTextFile(
        {"EngineContent/Shaders/ShadowedMainPass.hlsl", "../EngineContent/Shaders/ShadowedMainPass.hlsl",
         "../../EngineContent/Shaders/ShadowedMainPass.hlsl", "../../../EngineContent/Shaders/ShadowedMainPass.hlsl"});
    const std::string deferred = ReadRepositoryTextFile({"EngineContent/Shaders/DeferredLightingPass.hlsl",
                                                         "../EngineContent/Shaders/DeferredLightingPass.hlsl",
                                                         "../../EngineContent/Shaders/DeferredLightingPass.hlsl",
                                                         "../../../EngineContent/Shaders/DeferredLightingPass.hlsl"});
    const std::string modern = ReadRepositoryTextFile({"EngineContent/Shaders/ClusteredDeferred.hlsl",
                                                       "../EngineContent/Shaders/ClusteredDeferred.hlsl",
                                                       "../../EngineContent/Shaders/ClusteredDeferred.hlsl",
                                                       "../../../EngineContent/Shaders/ClusteredDeferred.hlsl"});
    const std::string d3d12Context = ReadRepositoryTextFile(
        {"src/Runtime/Renderer/D3D12Context.h", "../src/Runtime/Renderer/D3D12Context.h",
         "../../../src/Runtime/Renderer/D3D12Context.h", "../../../../src/Runtime/Renderer/D3D12Context.h"});
    return Check(result.succeeded && result.reflectionProbeCount == 1 && result.shVolumeCount == 1 &&
                     result.shSampleCount == 8 &&
                     asset.GetReflectionPixels().size() == asset.GetBytesPerReflectionProbe() &&
                     asset.GetSHCoefficients().size() == 8 * LightingProbeAsset::SHCoefficientCount &&
                     forward.find("SampleLocalReflectionsAuto") != std::string::npos &&
                     deferred.find("EvaluateLocalSHVolumes") != std::string::npos &&
                     modern.find("SampleLocalReflectionsAuto") != std::string::npos &&
                     forward.find("ProbeLightingConstants : register(b1)") == std::string::npos &&
                     deferred.find("ProbeLightingConstants : register(b1)") == std::string::npos &&
                     modern.find("ProbeLightingConstants : register(b1)") == std::string::npos &&
                     d3d12Context.find("kTextureSlotCount = 16") != std::string::npos,
                 "lighting probe bake or all-pipeline shader contract is incomplete");
}

MYENGINE_REGISTER_TEST("Renderer", "TestLightingProbeComponentAndAssetRoundTrip",
                       TestLightingProbeComponentAndAssetRoundTrip);
MYENGINE_REGISTER_TEST("Renderer", "TestLightingProbeBakeAndShaderContracts", TestLightingProbeBakeAndShaderContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestExtendedRHIContracts", TestExtendedRHIContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestStableRHIDeviceLossContract", TestStableRHIDeviceLossContract);
MYENGINE_REGISTER_TEST("Renderer", "TestMaterialResourceCacheUploadsBc3WhenSupported",
                       TestMaterialResourceCacheUploadsBc3WhenSupported);
MYENGINE_REGISTER_TEST("Renderer", "TestMaterialResourceCacheFallsBackToRgbaWhenBc1Unsupported",
                       TestMaterialResourceCacheFallsBackToRgbaWhenBc1Unsupported);
MYENGINE_REGISTER_TEST("Renderer", "TestMaterialResourceCacheGpuResidencyEvictionAndReupload",
                       TestMaterialResourceCacheGpuResidencyEvictionAndReupload);
MYENGINE_REGISTER_TEST("Renderer", "TestMaterialResourceCacheMeshResidencyAndQualityDegradation",
                       TestMaterialResourceCacheMeshResidencyAndQualityDegradation);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphValidationAndExecution", TestRenderGraphValidationAndExecution);
MYENGINE_REGISTER_TEST("Renderer", "TestNamedShaderBindings", TestNamedShaderBindings);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphComputePassTypeAndUavBarriers",
                       TestRenderGraphComputePassTypeAndUavBarriers);
MYENGINE_REGISTER_TEST("Renderer", "TestGpuSceneDatabaseDirtyUploadAndGeometryArena",
                       TestGpuSceneDatabaseDirtyUploadAndGeometryArena);
MYENGINE_REGISTER_TEST("Renderer", "TestGpuSceneMaterialBindlessSamplerSelection",
                       TestGpuSceneMaterialBindlessSamplerSelection);
MYENGINE_REGISTER_TEST("Renderer", "TestModernBindlessSamplerShaderContract", TestModernBindlessSamplerShaderContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernGpuSceneNormalAndReflectionContracts",
                       TestModernGpuSceneNormalAndReflectionContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestModernEnvironmentLightingMatchesClassicContract",
                       TestModernEnvironmentLightingMatchesClassicContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernClusterBuffersStartInNativeUavState",
                       TestModernClusterBuffersStartInNativeUavState);
MYENGINE_REGISTER_TEST("Renderer", "TestPersistentNativePipelineCacheContracts",
                       TestPersistentNativePipelineCacheContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestD3D12DebugEventUsesAnsiMetadata", TestD3D12DebugEventUsesAnsiMetadata);
MYENGINE_REGISTER_TEST("Renderer", "TestBackendIndependentPassRecording", TestBackendIndependentPassRecording);
MYENGINE_REGISTER_TEST("Renderer", "TestComputeStorageBufferAndAsyncReadback",
                       TestComputeStorageBufferAndAsyncReadback);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphComputeBufferDependencies",
                       TestRenderGraphComputeBufferDependencies);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphTextureSubresourceAccess", TestRenderGraphTextureSubresourceAccess);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphPassCullingAndLifetime", TestRenderGraphPassCullingAndLifetime);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderGraphDescriptorKeyedPooling", TestRenderGraphDescriptorKeyedPooling);
MYENGINE_REGISTER_TEST("Renderer", "TestEnvironmentRadianceHorizonContract", TestEnvironmentRadianceHorizonContract);
MYENGINE_REGISTER_TEST("Renderer", "TestEnvironmentPassSunDirectionDirtyState",
                       TestEnvironmentPassSunDirectionDirtyState);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererSynchronizesEnvironmentSunBeforePrepare",
                       TestRendererSynchronizesEnvironmentSunBeforePrepare);
MYENGINE_REGISTER_TEST("Renderer", "TestShadowedMainPassDirectShadowVisibilityContract",
                       TestShadowedMainPassDirectShadowVisibilityContract);
MYENGINE_REGISTER_TEST("Renderer", "TestShadowDepthAlphaTestIncludesVertexAlpha",
                       TestShadowDepthAlphaTestIncludesVertexAlpha);
MYENGINE_REGISTER_TEST("Renderer", "TestShaderGraphMaskedAlphaTestIncludesVertexAlpha",
                       TestShaderGraphMaskedAlphaTestIncludesVertexAlpha);
MYENGINE_REGISTER_TEST("Renderer", "TestSkinnedGBufferMotionUsesPreviousBonePalette",
                       TestSkinnedGBufferMotionUsesPreviousBonePalette);
MYENGINE_REGISTER_TEST("Renderer", "TestSlangReflectionPreservesSamplerStateBindings",
                       TestSlangReflectionPreservesSamplerStateBindings);
MYENGINE_REGISTER_TEST("Renderer", "TestVulkanStructuredBufferAndScreenUIBindingContracts",
                       TestVulkanStructuredBufferAndScreenUIBindingContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestGpuDrivenShadowSetupFailureFallsBackBeforeGraphMutation",
                       TestGpuDrivenShadowSetupFailureFallsBackBeforeGraphMutation);
MYENGINE_REGISTER_TEST("Renderer", "TestGpuDrivenShadowSetupFailureLeavesGraphUntouched",
                       TestGpuDrivenShadowSetupFailureLeavesGraphUntouched);
MYENGINE_REGISTER_TEST("Renderer", "TestModernTemporalReprojectionShaderContract",
                       TestModernTemporalReprojectionShaderContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernTaaCameraJitterStabilityContract",
                       TestModernTaaCameraJitterStabilityContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernScreenSpaceCompositeShaderContract",
                       TestModernScreenSpaceCompositeShaderContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernSsaoCompositeContract", TestModernSsaoCompositeContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernScreenSpaceSamplingAndConfidenceContracts",
                       TestModernScreenSpaceSamplingAndConfidenceContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestModernScreenSpacePostProcessTuningContract",
                       TestModernScreenSpacePostProcessTuningContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernScreenSpaceDebugRoutingContract",
                       TestModernScreenSpaceDebugRoutingContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernScreenSpaceSlangCompileContracts",
                       TestModernScreenSpaceSlangCompileContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestModernHiZOddDimensionReductionContract",
                       TestModernHiZOddDimensionReductionContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernCompatibilityGBufferPrecedesHiZContract",
                       TestModernCompatibilityGBufferPrecedesHiZContract);
MYENGINE_REGISTER_TEST("Renderer", "TestModernDiagnosticsReadbackIsThrottled",
                       TestModernDiagnosticsReadbackIsThrottled);
MYENGINE_REGISTER_TEST("Renderer", "TestModernTemporalHistoryCommitAndAbort", TestModernTemporalHistoryCommitAndAbort);
MYENGINE_REGISTER_TEST("Renderer", "TestHeadlessRendering", TestHeadlessRendering);
MYENGINE_REGISTER_TEST("Renderer", "TestMeshRendererSubMeshMaterialSlotDraws",
                       TestMeshRendererSubMeshMaterialSlotDraws);
MYENGINE_REGISTER_TEST("Renderer", "TestMainPassSamplerCacheDeduplicatesTextureSamplerStates",
                       TestMainPassSamplerCacheDeduplicatesTextureSamplerStates);
MYENGINE_REGISTER_TEST("Renderer", "TestDeferredPassResourceContracts", TestDeferredPassResourceContracts);
MYENGINE_REGISTER_TEST("Renderer", "TestDeferredLightingShaderSourceContract",
                       TestDeferredLightingShaderSourceContract);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererDeferredPathSubmitsGBufferLightingTransparentAndComposite",
                       TestRendererDeferredPathSubmitsGBufferLightingTransparentAndComposite);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererRenderPathDefaultsToForward", TestRendererRenderPathDefaultsToForward);
MYENGINE_REGISTER_TEST("Renderer", "TestRenderPipelineDeviceProfileResolution",
                       TestRenderPipelineDeviceProfileResolution);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererDefersModernInitializationUntilFirstRender",
                       TestRendererDefersModernInitializationUntilFirstRender);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererStartsShaderPrewarmOffRenderThread",
                       TestRendererStartsShaderPrewarmOffRenderThread);
MYENGINE_REGISTER_TEST("Renderer", "TestShaderPrewarmAsyncContinuesWithChangedRequestSet",
                       TestShaderPrewarmAsyncContinuesWithChangedRequestSet);
MYENGINE_REGISTER_TEST("Renderer", "TestShaderManagerClearCancelsPendingPrewarm",
                       TestShaderManagerClearCancelsPendingPrewarm);
MYENGINE_REGISTER_TEST("Renderer", "TestShaderPrewarmFailureIsCachedWithoutRenderThreadRetry",
                       TestShaderPrewarmFailureIsCachedWithoutRenderThreadRetry);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererFeatureMaskSkipsOptionalPasses",
                       TestRendererFeatureMaskSkipsOptionalPasses);
MYENGINE_REGISTER_TEST("Renderer", "TestMaterialPreviewDirtyDrivenScheduling",
                       TestMaterialPreviewDirtyDrivenScheduling);
MYENGINE_REGISTER_TEST("Renderer", "TestViewportActivityCommitPreservesContinuousInput",
                       TestViewportActivityCommitPreservesContinuousInput);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererOffscreenGraphPostProcessPath",
                       TestRendererOffscreenGraphPostProcessPath);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererBackbufferCompositeGraphTarget",
                       TestRendererBackbufferCompositeGraphTarget);
MYENGINE_REGISTER_TEST("Renderer", "TestRendererGraphHasNoEmptyCompatibilityPasses",
                       TestRendererGraphHasNoEmptyCompatibilityPasses);

} // namespace
