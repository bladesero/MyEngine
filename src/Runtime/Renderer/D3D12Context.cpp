#include "D3D12Context.h"

#include "../Core/Logger.h"
#include "../Core/Window.h"
#include "Renderer/RHI/ShaderReflection.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <d3dcompiler.h>

#include <windows.h>

#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// --------------------------------------------------------------------------
// Factory
// --------------------------------------------------------------------------
std::unique_ptr<IRenderContext> CreateD3D12Context() {
    return std::make_unique<D3D12Context>();
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
DXGI_FORMAT D3D12Context::ToDxgiFormat(VertexFormat fmt) {
    switch (fmt) {
    case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::OffsetHandle(D3D12_CPU_DESCRIPTOR_HANDLE h,
                                                        uint32_t index, uint32_t inc) {
    h.ptr += static_cast<SIZE_T>(index) * inc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Context::OffsetHandle(D3D12_GPU_DESCRIPTOR_HANDLE h,
                                                         uint32_t index, uint32_t inc) {
    h.ptr += static_cast<SIZE_T>(index) * inc;
    return h;
}

static DXGI_FORMAT ToDxgiRHIFormat(RHIFormat fmt, bool resource = false) {
    switch (fmt) {
    case RHIFormat::R8UInt: return DXGI_FORMAT_R8_UINT;
    case RHIFormat::RGBA8UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::BGRA8UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case RHIFormat::RGBA8UNormSrgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHIFormat::RG16Float: return DXGI_FORMAT_R16G16_FLOAT;
    case RHIFormat::RGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case RHIFormat::R8UNorm: return DXGI_FORMAT_R8_UNORM;
    case RHIFormat::R16UInt: return DXGI_FORMAT_R16_UINT;
    case RHIFormat::R32UInt: return DXGI_FORMAT_R32_UINT;
    case RHIFormat::R32Float: return DXGI_FORMAT_R32_FLOAT;
    case RHIFormat::RG32Float: return DXGI_FORMAT_R32G32_FLOAT;
    case RHIFormat::RGB32Float: return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHIFormat::RGBA32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHIFormat::D24S8: return resource ? DXGI_FORMAT_R24G8_TYPELESS : DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::D32Float: return resource ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_D32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

static D3D12_RESOURCE_STATES ToD3D12State(RHIResourceState state) {
    switch (state) {
    case RHIResourceState::ShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case RHIResourceState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case RHIResourceState::DepthRead: return D3D12_RESOURCE_STATE_DEPTH_READ;
    case RHIResourceState::DepthWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case RHIResourceState::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case RHIResourceState::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case RHIResourceState::CopyDestination: return D3D12_RESOURCE_STATE_COPY_DEST;
    case RHIResourceState::IndirectArgument: return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case RHIResourceState::Present: return D3D12_RESOURCE_STATE_PRESENT;
    default: return D3D12_RESOURCE_STATE_COMMON;
    }
}

static const char* SamplerFilterName(RHIFilter filter)
{
    switch (filter) {
    case RHIFilter::Point: return "Point";
    case RHIFilter::Linear: return "Linear";
    case RHIFilter::ComparisonLinear: return "ComparisonLinear";
    }
    return "Unknown";
}

static const char* SamplerAddressName(RHIAddressMode mode)
{
    switch (mode) {
    case RHIAddressMode::Repeat: return "Repeat";
    case RHIAddressMode::Clamp: return "Clamp";
    case RHIAddressMode::Border: return "Border";
    }
    return "Unknown";
}

static uint32_t RHIFormatByteSize12(RHIFormat format) {
    switch (format) {
    case RHIFormat::R8UInt: case RHIFormat::R8UNorm: return 1;
    case RHIFormat::R16UInt: return 2;
    case RHIFormat::RG16Float: case RHIFormat::R32UInt: case RHIFormat::R32Float:
    case RHIFormat::RGBA8UNorm: case RHIFormat::BGRA8UNorm:
    case RHIFormat::RGBA8UNormSrgb: case RHIFormat::D24S8: case RHIFormat::D32Float: return 4;
    case RHIFormat::RG32Float: case RHIFormat::RGBA16Float: return 8;
    case RHIFormat::RGB32Float: return 12;
    case RHIFormat::RGBA32Float: return 16;
    default: return 0;
    }
}

static D3D_PRIMITIVE_TOPOLOGY ToD3D12Topology(RHIPrimitiveTopology topology);

class D3D12FenceRHI final : public GpuFence {
public:
    explicit D3D12FenceRHI(ComPtr<ID3D12Fence> fence) : native(std::move(fence)) {}
    uint64_t GetCompletedValue() const override { return native ? native->GetCompletedValue() : 0; }
    bool Wait(uint64_t value, uint32_t timeoutMs) override {
        if (!native || GetCompletedValue() >= value) return native != nullptr;
        HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!eventHandle || FAILED(native->SetEventOnCompletion(value, eventHandle))) {
            if (eventHandle) CloseHandle(eventHandle); return false;
        }
        const DWORD result = WaitForSingleObject(eventHandle,
            timeoutMs == UINT32_MAX ? INFINITE : timeoutMs);
        CloseHandle(eventHandle); return result == WAIT_OBJECT_0;
    }
    ComPtr<ID3D12Fence> native;
};

class D3D12QueueRHI final : public GpuQueue {
public:
    explicit D3D12QueueRHI(D3D12Context& owner) : m_Owner(owner) {}
    bool Submit(GpuCommandList* commands, GpuFence* fence, uint64_t value) override {
        if (commands) return false; // Immediate list submission occurs in EndFrame.
        auto* nativeFence = dynamic_cast<D3D12FenceRHI*>(fence);
        return nativeFence && SUCCEEDED(m_Owner.GetCommandQueue()->Signal(nativeFence->native.Get(), value));
    }
    bool Wait(GpuFence* fence, uint64_t value) override {
        auto* nativeFence = dynamic_cast<D3D12FenceRHI*>(fence);
        return nativeFence && SUCCEEDED(m_Owner.GetCommandQueue()->Wait(nativeFence->native.Get(), value));
    }
private: D3D12Context& m_Owner;
};

class D3D12TimestampPool final : public GpuTimestampQueryPool {
public:
    uint32_t GetCount() const override { return count; }
    uint64_t GetFrequency() const override { return frequency; }
    bool ReadResults(uint32_t first, uint32_t resultCount,
                     std::vector<uint64_t>& ticks) override {
        if (!readback || first + resultCount > count) return false;
        void* mapped = nullptr;
        D3D12_RANGE range{static_cast<SIZE_T>(first) * 8,
                          static_cast<SIZE_T>(first + resultCount) * 8};
        if (FAILED(readback->Map(0, &range, &mapped)) || !mapped) return false;
        const auto* values = static_cast<const uint64_t*>(mapped);
        ticks.assign(values + first, values + first + resultCount);
        D3D12_RANGE written{0, 0}; readback->Unmap(0, &written); return true;
    }
    ComPtr<ID3D12QueryHeap> heap;
    ComPtr<ID3D12Resource> readback;
    uint32_t count = 0; uint64_t frequency = 0;
};

class D3D12ImmediateCommandList final : public GpuCommandList {
public:
    explicit D3D12ImmediateCommandList(D3D12Context& owner)
        : m_Owner(owner) {}

    void BindShader(GpuShader* shader) override {
        m_ComputeBinding = false;
        m_Owner.BindShader(shader);
    }

    void BindVertexBuffer(GpuBuffer* buffer) override {
        m_Owner.BindVertexBuffer(buffer);
    }

    void BindIndexBuffer(GpuBuffer* buffer) override {
        m_Owner.BindIndexBuffer(buffer);
    }

    void SetVSConstants(const void* data, uint32_t byteSize) override {
        m_Owner.SetVSConstants(data, byteSize);
    }

    void Draw(uint32_t vertexCount, uint32_t startVertex) override {
        m_Owner.Draw(vertexCount, startVertex);
    }

    void DrawIndexed(uint32_t indexCount, uint32_t startIndex,
                     uint32_t baseVertex) override {
        m_Owner.DrawIndexed(indexCount, startIndex, baseVertex);
    }

    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                       uint32_t startVertex) override {
        m_Owner.DrawInstanced(vertexCount, instanceCount, startVertex);
    }

    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                              uint32_t startIndex, uint32_t baseVertex) override {
        m_Owner.DrawIndexedInstanced(
            indexCount, instanceCount, startIndex, baseVertex);
    }
    void DrawIndirect(GpuBuffer* args, uint64_t offset) override {
        m_Owner.DrawIndirect(args, offset, false);
    }
    void DrawIndexedIndirect(GpuBuffer* args, uint64_t offset) override {
        m_Owner.DrawIndirect(args, offset, true);
    }
    void WriteTimestamp(GpuTimestampQueryPool* pool, uint32_t index) override {
        auto* native = dynamic_cast<D3D12TimestampPool*>(pool);
        if (native && index < native->count)
            m_Owner.GetCommandList()->EndQuery(
                native->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
    }
    void ResolveTimestamps(GpuTimestampQueryPool* pool, uint32_t first,
                           uint32_t count) override {
        auto* native = dynamic_cast<D3D12TimestampPool*>(pool);
        if (native && first + count <= native->count)
            m_Owner.GetCommandList()->ResolveQueryData(native->heap.Get(),
                D3D12_QUERY_TYPE_TIMESTAMP, first, count, native->readback.Get(),
                static_cast<uint64_t>(first) * sizeof(uint64_t));
    }

    void SetViewport(float x, float y, float w, float h) override {
        m_Owner.SetViewport(x, y, w, h);
    }

    void BindPSTexture(uint32_t slot, GpuTexture* tex) override {
        m_Owner.BindPSTexture(slot, tex);
    }

    void SetBlendMode(GpuBlendMode mode) override {
        m_Owner.SetBlendMode(mode);
    }

    void SetRasterState(bool twoSided, bool wireframe) override {
        m_Owner.SetRasterState(twoSided, wireframe);
    }
    void SetGraphicsPipeline(GpuGraphicsPipeline* pipeline) override {
        auto* nativePipeline = dynamic_cast<D3D12GraphicsPipeline*>(pipeline);
        auto* shader = pipeline && pipeline->desc.shader
            ? dynamic_cast<D3D12Shader*>(pipeline->desc.shader.get()) : nullptr;
        if (!nativePipeline || !nativePipeline->pipelineState ||
            !shader || !shader->rootSignature) return;
        m_ComputeBinding = false;
        m_Owner.GetCommandList()->SetGraphicsRootSignature(shader->rootSignature.Get());
        if (shader->hasBindlessTable)
            m_Owner.GetCommandList()->SetGraphicsRootDescriptorTable(
                1 + D3D12Context::kTextureSlotCount * 2,
                m_Owner.GetSrvDescriptorHeap()->GetGPUDescriptorHandleForHeapStart());
        m_Owner.GetCommandList()->SetPipelineState(nativePipeline->pipelineState.Get());
        m_Owner.GetCommandList()->IASetPrimitiveTopology(
            ToD3D12Topology(pipeline->desc.topology));
        m_Owner.GetCommandList()->OMSetBlendFactor(pipeline->desc.blend.blendConstants);
        m_Owner.GetCommandList()->OMSetStencilRef(
            pipeline->desc.depthStencil.stencilReference);
    }
    void SetComputePipeline(GpuComputePipeline* pipeline) override {
        auto* shader = pipeline && pipeline->desc.shader
            ? dynamic_cast<D3D12Shader*>(pipeline->desc.shader.get()) : nullptr;
        if (!shader || !shader->computeRootSignature || !shader->computePipelineState) return;
        m_ComputeBinding = true;
        m_Owner.GetCommandList()->SetComputeRootSignature(shader->computeRootSignature.Get());
        m_Owner.GetCommandList()->SetPipelineState(shader->computePipelineState.Get());
    }
    void SetDepthOnlyShader(GpuShader* shader) override { m_Owner.BindDepthOnlyShader(shader); }
    void SetBindGroup(uint32_t, GpuBindGroup* group) override {
        if (!group || !group->GetShader()) return;
        const auto& reflection = group->GetShader()->reflection;
        for (const auto& value : group->GetConstants()) {
            if (reflection.Find(value.first) && !m_ComputeBinding)
                m_Owner.SetVSConstants(value.second.data(), static_cast<uint32_t>(value.second.size()));
        }
        for (const auto& value : group->GetTextures()) {
            const auto* binding = reflection.Find(value.first);
            auto* view = dynamic_cast<D3D12TextureView*>(value.second.get());
            if (!binding || !view) continue;
            D3D12_GPU_DESCRIPTOR_HANDLE samplerHandle{};
            for (const auto& samplerValue : group->GetSamplers()) {
                const auto* samplerBinding = reflection.Find(samplerValue.first);
                if (samplerBinding && samplerBinding->bindPoint == binding->bindPoint) {
                    if (auto* sampler = dynamic_cast<D3D12Sampler*>(samplerValue.second.get()))
                        samplerHandle = sampler->gpu;
                    break;
                }
            }
            if (m_ComputeBinding) {
                m_Owner.GetCommandList()->SetComputeRootDescriptorTable(
                    1 + binding->bindPoint * 2, view->srvGpu);
                if (samplerHandle.ptr)
                    m_Owner.GetCommandList()->SetComputeRootDescriptorTable(
                        2 + binding->bindPoint * 2, samplerHandle);
            } else {
                m_Owner.BindPSTextureDescriptors(binding->bindPoint, view->srvGpu, samplerHandle);
            }
        }
        if (m_ComputeBinding) {
            for (const auto& value : group->GetStorageBuffers()) {
                const auto* binding = reflection.Find(value.first);
                auto* view = dynamic_cast<D3D12BufferView*>(value.second.get());
                if (binding && view && view->uavGpu.ptr)
                    m_Owner.GetCommandList()->SetComputeRootDescriptorTable(
                        1 + D3D12Context::kTextureSlotCount * 2 + binding->bindPoint,
                        view->uavGpu);
            }
        } else {
            for (const auto& value : group->GetStorageBuffers()) {
                const auto* binding = reflection.Find(value.first);
                auto* view = dynamic_cast<D3D12BufferView*>(value.second.get());
                if (binding && view && view->srvGpu.ptr)
                    m_Owner.BindPSTextureDescriptors(binding->bindPoint, view->srvGpu, {});
            }
        }
    }
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        m_Owner.GetCommandList()->Dispatch(x, y, z);
    }
    void CopyBuffer(GpuBuffer* dst, uint32_t dstOffset, GpuBuffer* src,
                    uint32_t srcOffset, uint32_t byteSize) override {
        auto* d = dynamic_cast<D3D12Buffer*>(dst);
        auto* s = dynamic_cast<D3D12Buffer*>(src);
        if (d && s) m_Owner.GetCommandList()->CopyBufferRegion(
            d->resource.Get(), dstOffset, s->resource.Get(), srcOffset, byteSize);
    }
    void CopyTexture(GpuTexture* dst, GpuTexture* src) override {
        RHITextureRegion region;
        CopyTexture(dst, region, src, region);
    }
    void CopyTexture(GpuTexture* dst, const RHITextureRegion& dr,
                     GpuTexture* src, const RHITextureRegion& sr) override {
        auto* d = dynamic_cast<D3D12Texture*>(dst); auto* s = dynamic_cast<D3D12Texture*>(src);
        if (!d || !s) return;
        D3D12_TEXTURE_COPY_LOCATION dl{}; dl.pResource = d->resource.Get();
        dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dl.SubresourceIndex = dr.mipLevel + dr.arrayLayer * d->desc.mipLevels;
        D3D12_TEXTURE_COPY_LOCATION sl{}; sl.pResource = s->resource.Get();
        sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        sl.SubresourceIndex = sr.mipLevel + sr.arrayLayer * s->desc.mipLevels;
        const uint32_t width = sr.width ? sr.width : (std::max)(1u, s->desc.width >> sr.mipLevel) - sr.x;
        const uint32_t height = sr.height ? sr.height : (std::max)(1u, s->desc.height >> sr.mipLevel) - sr.y;
        D3D12_BOX box{sr.x, sr.y, sr.z, sr.x + width, sr.y + height, sr.z + (std::max)(1u, sr.depth)};
        m_Owner.GetCommandList()->CopyTextureRegion(&dl, dr.x, dr.y, dr.z, &sl, &box);
    }
    void UAVBarrier(GpuResource* resource) override {
        auto* buffer = dynamic_cast<D3D12Buffer*>(resource);
        auto* texture = dynamic_cast<D3D12Texture*>(resource);
        ID3D12Resource* native = buffer ? buffer->resource.Get() :
            texture ? texture->resource.Get() : nullptr;
        if (!native) return;
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = native;
        m_Owner.GetCommandList()->ResourceBarrier(1, &barrier);
    }

    void Transition(GpuResource* resource, RHIResourceState before,
                    RHIResourceState after) override {
        auto* texture = dynamic_cast<D3D12Texture*>(resource);
        auto* buffer = dynamic_cast<D3D12Buffer*>(resource);
        ID3D12Resource* native = texture ? texture->resource.Get() :
            buffer ? buffer->resource.Get() : nullptr;
        if (!native || before == after) return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = native;
        barrier.Transition.StateBefore = ToD3D12State(before);
        barrier.Transition.StateAfter = ToD3D12State(after);
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_Owner.GetCommandList()->ResourceBarrier(1, &barrier);
    }
    void TransitionTexture(GpuTexture* texture, const RHITextureViewDesc& range,
                           RHIResourceState before, RHIResourceState after) override {
        auto* native = dynamic_cast<D3D12Texture*>(texture);
        if (!native || !native->resource || before == after) return;
        const auto beforeState = ToD3D12State(before);
        const auto afterState = ToD3D12State(after);
        for (uint32_t layer = 0; layer < range.layerCount; ++layer) {
            for (uint32_t mip = 0; mip < range.mipCount; ++mip) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = native->resource.Get();
                barrier.Transition.StateBefore = beforeState;
                barrier.Transition.StateAfter = afterState;
                barrier.Transition.Subresource =
                    (range.firstMip + mip) + (range.firstLayer + layer) * native->desc.mipLevels;
                m_Owner.GetCommandList()->ResourceBarrier(1, &barrier);
            }
        }
    }

    void BeginRendering(const RenderingInfo& info) override {
        m_StoreDiscard.clear();
        D3D12_CPU_DESCRIPTOR_HANDLE colors[8]{};
        const uint32_t colorCount = (std::min)(info.colorCount, 8u);
        for (uint32_t i = 0; i < colorCount; ++i) {
            auto* view = dynamic_cast<D3D12TextureView*>(info.colors[i].view);
            if (!view) continue;
            colors[i] = view->rtvCpu;
            auto* texture = dynamic_cast<D3D12Texture*>(view->texture.get());
            if (texture && info.colors[i].loadOp == RHILoadOp::Discard)
                m_Owner.GetCommandList()->DiscardResource(texture->resource.Get(), nullptr);
            if (texture && info.colors[i].storeOp == RHIStoreOp::Discard)
                m_StoreDiscard.push_back(texture->resource.Get());
        }
        D3D12_CPU_DESCRIPTOR_HANDLE depth{};
        if (info.depth) {
            auto* view = dynamic_cast<D3D12TextureView*>(info.depth->view);
            if (view) depth = view->dsvCpu;
            auto* texture = view ? dynamic_cast<D3D12Texture*>(view->texture.get()) : nullptr;
            if (texture && info.depth->loadOp == RHILoadOp::Discard)
                m_Owner.GetCommandList()->DiscardResource(texture->resource.Get(), nullptr);
            if (texture && info.depth->storeOp == RHIStoreOp::Discard)
                m_StoreDiscard.push_back(texture->resource.Get());
        }
        m_Owner.PushRenderTargets(colorCount, colors, depth);
        for (uint32_t i = 0; i < colorCount; ++i) {
            if (colors[i].ptr && info.colors[i].loadOp == RHILoadOp::Clear) {
                const auto& c = info.colors[i].clearColor;
                const float clear[4] = {c.r, c.g, c.b, c.a};
                m_Owner.GetCommandList()->ClearRenderTargetView(colors[i], clear, 0, nullptr);
            }
        }
        if (depth.ptr && info.depth->loadOp == RHILoadOp::Clear)
            m_Owner.GetCommandList()->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH,
                info.depth->clearDepth, info.depth->clearStencil, 0, nullptr);
        m_Owner.SetViewport(0, 0, static_cast<float>(info.width), static_cast<float>(info.height));
    }

    void EndRendering() override {
        for (auto* resource : m_StoreDiscard)
            m_Owner.GetCommandList()->DiscardResource(resource, nullptr);
        m_StoreDiscard.clear();
        m_Owner.PopRenderTarget();
    }

private:
    D3D12Context& m_Owner;
    bool m_ComputeBinding = false;
    std::vector<ID3D12Resource*> m_StoreDiscard;
};

class D3D12ReadbackTicket final : public GpuReadbackTicket {
public:
    D3D12ReadbackTicket(ComPtr<ID3D12Resource> resource, ComPtr<ID3D12Fence> fence,
                        uint64_t fenceValue, uint32_t size,
                        std::shared_ptr<D3D12DeferredReleaseQueue> deferredReleaseQueue)
        : m_Resource(std::move(resource)), m_Fence(std::move(fence)),
          m_DeferredReleaseQueue(std::move(deferredReleaseQueue)),
          m_FenceValue(fenceValue), m_Size(size) {}
    ~D3D12ReadbackTicket() override;
    bool IsReady() const override {
        return m_Fence && m_Fence->GetCompletedValue() >= m_FenceValue;
    }
    bool Read(std::vector<uint8_t>& data) override {
        if (!IsReady() || !m_Resource) return false;
        void* mapped = nullptr; D3D12_RANGE range{0, m_Size};
        if (FAILED(m_Resource->Map(0, &range, &mapped)) || !mapped) return false;
        data.resize(m_Size); std::memcpy(data.data(), mapped, m_Size);
        D3D12_RANGE written{0, 0}; m_Resource->Unmap(0, &written); return true;
    }
    uint32_t GetSize() const override { return m_Size; }
private:
    ComPtr<ID3D12Resource> m_Resource;
    ComPtr<ID3D12Fence> m_Fence;
    std::shared_ptr<D3D12DeferredReleaseQueue> m_DeferredReleaseQueue;
    uint64_t m_FenceValue = 0;
    uint32_t m_Size = 0;
};

class D3D12SwapChain final : public GpuSwapChain {
public:
    explicit D3D12SwapChain(D3D12Context& owner)
        : m_Owner(owner) {}

    void Present(bool vsync) override {
        m_Owner.PresentSwapChain(vsync);
    }

    bool Resize(uint32_t width, uint32_t height) override {
        return m_Owner.ResizeSwapChain(width, height);
    }

    uint32_t GetWidth() const override {
        return m_Owner.m_SwapChainWidth;
    }

    uint32_t GetHeight() const override {
        return m_Owner.m_SwapChainHeight;
    }

private:
    D3D12Context& m_Owner;
};

// --------------------------------------------------------------------------
// D3D12Context
// --------------------------------------------------------------------------
D3D12Context::D3D12Context()
    : m_DeferredReleaseQueue(std::make_shared<D3D12DeferredReleaseQueue>())
    , m_SwapChainInterface(std::make_unique<D3D12SwapChain>(*this))
    , m_GraphicsCommandList(std::make_unique<D3D12ImmediateCommandList>(*this)) {}

D3D12Context::~D3D12Context() { Shutdown(); }

static std::string FormatHRESULT(HRESULT hr) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << std::setw(8)
           << std::setfill('0') << static_cast<uint32_t>(hr);
    return stream.str();
}

static D3D_PRIMITIVE_TOPOLOGY ToD3D12Topology(RHIPrimitiveTopology topology) {
    switch (topology) {
    case RHIPrimitiveTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case RHIPrimitiveTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case RHIPrimitiveTopology::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case RHIPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12TopologyType(RHIPrimitiveTopology topology) {
    switch (topology) {
    case RHIPrimitiveTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case RHIPrimitiveTopology::LineList:
    case RHIPrimitiveTopology::LineStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

static D3D12_COMPARISON_FUNC ToD3D12Compare(RHICompareOp op) {
    switch (op) {
    case RHICompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
    case RHICompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
    case RHICompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
    case RHICompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case RHICompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
    case RHICompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case RHICompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    default: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

static D3D12_STENCIL_OP ToD3D12StencilOp(RHIStencilOp op) {
    switch (op) {
    case RHIStencilOp::Zero: return D3D12_STENCIL_OP_ZERO;
    case RHIStencilOp::Replace: return D3D12_STENCIL_OP_REPLACE;
    case RHIStencilOp::IncrementClamp: return D3D12_STENCIL_OP_INCR_SAT;
    case RHIStencilOp::DecrementClamp: return D3D12_STENCIL_OP_DECR_SAT;
    case RHIStencilOp::Invert: return D3D12_STENCIL_OP_INVERT;
    case RHIStencilOp::IncrementWrap: return D3D12_STENCIL_OP_INCR;
    case RHIStencilOp::DecrementWrap: return D3D12_STENCIL_OP_DECR;
    default: return D3D12_STENCIL_OP_KEEP;
    }
}

static D3D12_BLEND ToD3D12Blend(RHIBlendFactor factor) {
    switch (factor) {
    case RHIBlendFactor::Zero: return D3D12_BLEND_ZERO;
    case RHIBlendFactor::One: return D3D12_BLEND_ONE;
    case RHIBlendFactor::SrcColor: return D3D12_BLEND_SRC_COLOR;
    case RHIBlendFactor::OneMinusSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
    case RHIBlendFactor::DstColor: return D3D12_BLEND_DEST_COLOR;
    case RHIBlendFactor::OneMinusDstColor: return D3D12_BLEND_INV_DEST_COLOR;
    case RHIBlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
    case RHIBlendFactor::OneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
    case RHIBlendFactor::DstAlpha: return D3D12_BLEND_DEST_ALPHA;
    case RHIBlendFactor::OneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
    case RHIBlendFactor::ConstantColor: return D3D12_BLEND_BLEND_FACTOR;
    case RHIBlendFactor::OneMinusConstantColor: return D3D12_BLEND_INV_BLEND_FACTOR;
    case RHIBlendFactor::SrcAlphaSaturate: return D3D12_BLEND_SRC_ALPHA_SAT;
    default: return D3D12_BLEND_ONE;
    }
}

static D3D12_BLEND_OP ToD3D12BlendOp(RHIBlendOp op) {
    switch (op) {
    case RHIBlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
    case RHIBlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
    case RHIBlendOp::Min: return D3D12_BLEND_OP_MIN;
    case RHIBlendOp::Max: return D3D12_BLEND_OP_MAX;
    default: return D3D12_BLEND_OP_ADD;
    }
}

static D3D12_DEPTH_STENCILOP_DESC ToD3D12StencilFace(const RHIStencilFaceState& face) {
    D3D12_DEPTH_STENCILOP_DESC result{};
    result.StencilFailOp = ToD3D12StencilOp(face.failOp);
    result.StencilDepthFailOp = ToD3D12StencilOp(face.depthFailOp);
    result.StencilPassOp = ToD3D12StencilOp(face.passOp);
    result.StencilFunc = ToD3D12Compare(face.compareOp);
    return result;
}

class D3D12DeferredReleaseQueue {
public:
    void BeginFrame(uint64_t completedFenceValue) {
        Collect(completedFenceValue);
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Shutdown) m_RecordingFrame = true;
    }

    void EndFrame(uint64_t submittedFenceValue) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Shutdown) return;
        m_RecordingFrame = false;
        m_LastSubmittedFenceValue = submittedFenceValue;
        if (!m_CurrentFrame.empty() || !m_CurrentFrameActions.empty()) {
            Batch batch;
            batch.fenceValue = submittedFenceValue;
            batch.objects = std::move(m_CurrentFrame);
            batch.actions = std::move(m_CurrentFrameActions);
            m_Batches.push_back(std::move(batch));
            m_CurrentFrame.clear();
            m_CurrentFrameActions.clear();
        }
    }

    void AbortUnsubmittedFrame() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Shutdown) return;
        m_RecordingFrame = false;
        if (m_CurrentFrame.empty() && m_CurrentFrameActions.empty()) return;
        if (m_LastSubmittedFenceValue == 0) {
            m_CurrentFrame.clear();
            for (auto& action : m_CurrentFrameActions) action();
            m_CurrentFrameActions.clear();
        } else {
            Batch batch;
            batch.fenceValue = m_LastSubmittedFenceValue;
            batch.objects = std::move(m_CurrentFrame);
            batch.actions = std::move(m_CurrentFrameActions);
            m_Batches.push_back(std::move(batch));
            m_CurrentFrame.clear();
            m_CurrentFrameActions.clear();
        }
    }

    void AbandonSubmittedFrame() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Shutdown) return;
        m_RecordingFrame = false;
        for (auto& object : m_CurrentFrame) m_Abandoned.push_back(std::move(object));
        for (auto& action : m_CurrentFrameActions)
            m_AbandonedActions.push_back(std::move(action));
        m_CurrentFrame.clear();
        m_CurrentFrameActions.clear();
    }

    void Retire(ComPtr<IUnknown> object) {
        if (!object) return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Shutdown) return;
        if (m_RecordingFrame) {
            m_CurrentFrame.push_back(std::move(object));
        } else if (m_LastSubmittedFenceValue != 0) {
            if (!m_Batches.empty() &&
                m_Batches.back().fenceValue == m_LastSubmittedFenceValue) {
                m_Batches.back().objects.push_back(std::move(object));
            } else {
                Batch batch;
                batch.fenceValue = m_LastSubmittedFenceValue;
                batch.objects.push_back(std::move(object));
                m_Batches.push_back(std::move(batch));
            }
        }
    }

    void RetireAction(std::function<void()> action) {
        if (!action) return;
        bool executeNow = false;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Shutdown || (!m_RecordingFrame && m_LastSubmittedFenceValue == 0)) {
                executeNow = true;
            } else if (m_RecordingFrame) {
                m_CurrentFrameActions.push_back(std::move(action));
            } else if (!m_Batches.empty() &&
                       m_Batches.back().fenceValue == m_LastSubmittedFenceValue) {
                m_Batches.back().actions.push_back(std::move(action));
            } else {
                Batch batch;
                batch.fenceValue = m_LastSubmittedFenceValue;
                batch.actions.push_back(std::move(action));
                m_Batches.push_back(std::move(batch));
            }
        }
        if (executeNow) action();
    }

    void Collect(uint64_t completedFenceValue) {
        std::deque<Batch> completed;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            while (!m_Batches.empty() &&
                   m_Batches.front().fenceValue <= completedFenceValue) {
                completed.push_back(std::move(m_Batches.front()));
                m_Batches.pop_front();
            }
        }
        for (auto& batch : completed)
            for (auto& action : batch.actions) action();
    }

    void ShutdownAndRelease() {
        std::vector<std::function<void()>> actions;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Shutdown = true;
            m_RecordingFrame = false;
            actions = std::move(m_CurrentFrameActions);
            for (auto& batch : m_Batches)
                for (auto& action : batch.actions) actions.push_back(std::move(action));
            for (auto& action : m_AbandonedActions) actions.push_back(std::move(action));
            m_CurrentFrame.clear();
            m_CurrentFrameActions.clear();
            m_Batches.clear();
            m_Abandoned.clear();
            m_AbandonedActions.clear();
        }
        for (auto& action : actions) action();
    }

private:
    struct Batch {
        uint64_t fenceValue = 0;
        std::vector<ComPtr<IUnknown>> objects;
        std::vector<std::function<void()>> actions;
    };

    std::mutex m_Mutex;
    std::deque<Batch> m_Batches;
    std::vector<ComPtr<IUnknown>> m_CurrentFrame;
    std::vector<std::function<void()>> m_CurrentFrameActions;
    std::vector<ComPtr<IUnknown>> m_Abandoned;
    std::vector<std::function<void()>> m_AbandonedActions;
    uint64_t m_LastSubmittedFenceValue = 0;
    bool m_RecordingFrame = false;
    bool m_Shutdown = false;
};

class D3D12TextureReadbackTicket final : public GpuTextureReadbackTicket {
public:
    D3D12TextureReadbackTicket(ComPtr<ID3D12Resource> resource, ComPtr<ID3D12Fence> fence,
        uint64_t fenceValue, uint32_t width, uint32_t height, uint32_t rowSize,
        uint32_t gpuRowPitch, RHIFormat format,
        std::shared_ptr<D3D12DeferredReleaseQueue> releaseQueue)
        : m_Resource(std::move(resource)), m_Fence(std::move(fence)),
          m_ReleaseQueue(std::move(releaseQueue)), m_FenceValue(fenceValue), m_Width(width),
          m_Height(height), m_RowSize(rowSize), m_GpuRowPitch(gpuRowPitch), m_Format(format) {}
    ~D3D12TextureReadbackTicket() override;
    bool IsReady() const override { return m_Fence && m_Fence->GetCompletedValue() >= m_FenceValue; }
    bool Read(std::vector<uint8_t>& data) override {
        if (!IsReady() || !m_Resource) return false;
        void* mapped = nullptr; D3D12_RANGE range{0, static_cast<SIZE_T>(m_GpuRowPitch) * m_Height};
        if (FAILED(m_Resource->Map(0, &range, &mapped)) || !mapped) return false;
        data.resize(static_cast<size_t>(m_RowSize) * m_Height);
        for (uint32_t y = 0; y < m_Height; ++y)
            std::memcpy(data.data() + static_cast<size_t>(y) * m_RowSize,
                        static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * m_GpuRowPitch,
                        m_RowSize);
        D3D12_RANGE written{0, 0}; m_Resource->Unmap(0, &written); return true;
    }
    uint32_t GetSize() const override { return m_RowSize * m_Height; }
    uint32_t GetRowPitch() const override { return m_RowSize; }
    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    RHIFormat GetFormat() const override { return m_Format; }
private:
    ComPtr<ID3D12Resource> m_Resource; ComPtr<ID3D12Fence> m_Fence;
    std::shared_ptr<D3D12DeferredReleaseQueue> m_ReleaseQueue;
    uint64_t m_FenceValue = 0; uint32_t m_Width = 0, m_Height = 0;
    uint32_t m_RowSize = 0, m_GpuRowPitch = 0; RHIFormat m_Format = RHIFormat::Unknown;
};

class D3D12DescriptorPool : public std::enable_shared_from_this<D3D12DescriptorPool> {
public:
    D3D12DescriptorPool(D3D12_CPU_DESCRIPTOR_HANDLE cpuBase,
                        D3D12_GPU_DESCRIPTOR_HANDLE gpuBase,
                        uint32_t increment, uint32_t capacity, uint32_t firstIndex)
        : m_CpuBase(cpuBase), m_GpuBase(gpuBase), m_Increment(increment),
          m_Capacity(capacity), m_NextIndex(firstIndex) {}

    bool Allocate(const std::shared_ptr<D3D12DeferredReleaseQueue>& queue,
                  D3D12_CPU_DESCRIPTOR_HANDLE& cpu,
                  D3D12_GPU_DESCRIPTOR_HANDLE& gpu,
                  std::shared_ptr<D3D12DescriptorLease>* lease);

    void Release(uint32_t index) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FreeIndices.push_back(index);
    }

private:
    std::mutex m_Mutex;
    D3D12_CPU_DESCRIPTOR_HANDLE m_CpuBase{};
    D3D12_GPU_DESCRIPTOR_HANDLE m_GpuBase{};
    uint32_t m_Increment = 0;
    uint32_t m_Capacity = 0;
    uint32_t m_NextIndex = 0;
    std::vector<uint32_t> m_FreeIndices;
};

class D3D12DescriptorLease {
public:
    D3D12DescriptorLease(std::shared_ptr<D3D12DescriptorPool> pool,
                         std::shared_ptr<D3D12DeferredReleaseQueue> queue,
                         uint32_t index)
        : m_Pool(std::move(pool)), m_Queue(std::move(queue)), m_Index(index) {}

    ~D3D12DescriptorLease() {
        auto pool = std::move(m_Pool);
        if (!pool) return;
        const uint32_t index = m_Index;
        if (m_Queue) {
            m_Queue->RetireAction([pool = std::move(pool), index]() {
                pool->Release(index);
            });
        } else {
            pool->Release(index);
        }
    }

private:
    std::shared_ptr<D3D12DescriptorPool> m_Pool;
    std::shared_ptr<D3D12DeferredReleaseQueue> m_Queue;
    uint32_t m_Index = 0;
};

bool D3D12DescriptorPool::Allocate(
    const std::shared_ptr<D3D12DeferredReleaseQueue>& queue,
    D3D12_CPU_DESCRIPTOR_HANDLE& cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu,
    std::shared_ptr<D3D12DescriptorLease>* lease) {
    uint32_t index = 0;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_FreeIndices.empty()) {
            index = m_FreeIndices.back();
            m_FreeIndices.pop_back();
        } else {
            if (m_NextIndex >= m_Capacity) return false;
            index = m_NextIndex++;
        }
    }
    cpu = m_CpuBase;
    cpu.ptr += static_cast<SIZE_T>(index) * m_Increment;
    gpu = m_GpuBase;
    if (gpu.ptr != 0) gpu.ptr += static_cast<UINT64>(index) * m_Increment;
    if (lease) {
        *lease = std::make_shared<D3D12DescriptorLease>(
            shared_from_this(), queue, index);
    }
    return true;
}

template <typename T>
static void RetireD3D12Object(const std::shared_ptr<D3D12DeferredReleaseQueue>& queue,
                              ComPtr<T>& object) {
    if (!object) return;
    if (!queue) {
        object.Reset();
        return;
    }
    ComPtr<IUnknown> unknown;
    unknown.Attach(object.Detach());
    queue->Retire(std::move(unknown));
}

D3D12Buffer::~D3D12Buffer() {
    RetireD3D12Object(deferredReleaseQueue, resource);
}

D3D12Shader::~D3D12Shader() {
    RetireD3D12Object(deferredReleaseQueue, rootSignature);
    RetireD3D12Object(deferredReleaseQueue, pipelineState);
    RetireD3D12Object(deferredReleaseQueue, alphaPipelineState);
    RetireD3D12Object(deferredReleaseQueue, depthOnlyPipelineState);
    RetireD3D12Object(deferredReleaseQueue, wireframePipelineState);
    RetireD3D12Object(deferredReleaseQueue, twoSidedPipelineState);
    RetireD3D12Object(deferredReleaseQueue, computeRootSignature);
    RetireD3D12Object(deferredReleaseQueue, computePipelineState);
}

D3D12GraphicsPipeline::~D3D12GraphicsPipeline() {
    RetireD3D12Object(deferredReleaseQueue, pipelineState);
}

D3D12Texture::~D3D12Texture() {
    RetireD3D12Object(deferredReleaseQueue, resource);
}

D3D12ReadbackTicket::~D3D12ReadbackTicket() {
    RetireD3D12Object(m_DeferredReleaseQueue, m_Resource);
}

D3D12TextureReadbackTicket::~D3D12TextureReadbackTicket() {
    RetireD3D12Object(m_ReleaseQueue, m_Resource);
}

static std::string WideToUtf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

static bool IsEnvironmentFlagEnabled(const char* name) {
    char value[8] = {};
    return GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value))) > 0 &&
        std::strcmp(value, "0") != 0;
}

static void EnableD3D12Diagnostics() {
#if !defined(NDEBUG)
    const bool enableGpuValidation =
        IsEnvironmentFlagEnabled("MYENGINE_D3D12_GPU_VALIDATION");
    const bool enableDebugLayer = enableGpuValidation ||
        IsEnvironmentFlagEnabled("MYENGINE_D3D12_DEBUG_LAYER");
    if (enableDebugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            ComPtr<ID3D12Debug1> debug1;
            if (enableGpuValidation && SUCCEEDED(debug.As(&debug1))) {
                debug1->SetEnableGPUBasedValidation(TRUE);
                Logger::Info("D3D12 Debug Layer and GPU-based validation enabled");
            } else if (enableGpuValidation) {
                Logger::Warn("D3D12 Debug Layer enabled, but GPU-based validation is unavailable");
            } else {
                Logger::Info("D3D12 Debug Layer enabled");
            }
        } else {
            Logger::Warn("D3D12 Debug Layer unavailable; install Windows Graphics Tools for validation output");
        }
    } else {
        Logger::Info("D3D12 validation layer disabled; set MYENGINE_D3D12_DEBUG_LAYER=1 "
                     "or MYENGINE_D3D12_GPU_VALIDATION=1 to enable it");
    }
#endif

    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings1;
        if (SUCCEEDED(dredSettings.As(&dredSettings1))) {
            dredSettings1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
        Logger::Info("D3D12 DRED breadcrumbs and page-fault reporting enabled");
    } else {
        Logger::Warn("D3D12 DRED settings interface unavailable");
    }
}

bool D3D12Context::Init(IWindow* window) {
    if (!window) return false;

    m_DeferredReleaseQueue = std::make_shared<D3D12DeferredReleaseQueue>();

    HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
    if (!hwnd) {
        Logger::Error("D3D12Context::Init – invalid HWND");
        return false;
    }

    const uint32_t w = static_cast<uint32_t>(window->GetWidth());
    const uint32_t h = static_cast<uint32_t>(window->GetHeight());
    m_SwapChainWidth = w;
    m_SwapChainHeight = h;

    // Diagnostics must be enabled before device creation.
    EnableD3D12Diagnostics();

    // ---- Device / Queue ----------------------------------------------------
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   IID_PPV_ARGS(&m_Device));
    if (FAILED(hr)) {
        Logger::Error("D3D12CreateDevice failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC cqDesc = {};
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = m_Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&m_CommandQueue));
    if (FAILED(hr)) {
        Logger::Error("CreateCommandQueue failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }
    m_CommandQueue->SetName(L"MyEngine D3D12 Direct Queue");
    D3D12_INDIRECT_ARGUMENT_DESC indirectArg{};
    D3D12_COMMAND_SIGNATURE_DESC indirectDesc{};
    indirectDesc.NumArgumentDescs = 1;
    indirectDesc.pArgumentDescs = &indirectArg;
    indirectArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    indirectDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    if (FAILED(m_Device->CreateCommandSignature(
            &indirectDesc, nullptr, IID_PPV_ARGS(&m_DrawIndirectSignature)))) return false;
    indirectArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    indirectDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    if (FAILED(m_Device->CreateCommandSignature(
            &indirectDesc, nullptr, IID_PPV_ARGS(&m_DrawIndexedIndirectSignature)))) return false;
    m_GraphicsQueue = std::make_shared<D3D12QueueRHI>(*this);

    // ---- SwapChain ---------------------------------------------------------
    ComPtr<IDXGIFactory4> factory;
    {
        UINT flags = 0;
        hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            Logger::Error("CreateDXGIFactory2 failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount = kFrameCount;
    scd.Width = w;
    scd.Height = h;
    scd.Format = m_RtvFormat;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(
        m_CommandQueue.Get(), hwnd, &scd, nullptr, nullptr, &swapChain1);
    if (FAILED(hr)) {
        Logger::Error("CreateSwapChainForHwnd failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    hr = swapChain1.As(&m_SwapChain);
    if (FAILED(hr)) {
        Logger::Error("swapChain1.As<IDXGISwapChain3> failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    // ---- Descriptor heaps --------------------------------------------------
    // RTV heap (swapchain + offscreen)
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kFrameCount + kOffscreenRtvCount;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_RtvHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(RTV) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }

        m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        auto baseHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < kFrameCount; ++i) {
            m_BackBuffers[i].Reset();
            m_RtvHandles[i] = OffsetHandle(baseHandle, i, m_RtvDescriptorSize);
            hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i]));
            if (FAILED(hr)) {
                Logger::Error("GetBuffer failed: 0x",
                              reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
                return false;
            }
            m_Device->CreateRenderTargetView(m_BackBuffers[i].Get(), nullptr,
                                               m_RtvHandles[i]);
            auto texture = std::make_shared<D3D12Texture>();
            texture->resource = m_BackBuffers[i];
            texture->desc.width = static_cast<uint32_t>(w);
            texture->desc.height = static_cast<uint32_t>(h);
            texture->desc.format = RHIFormat::RGBA8UNorm;
            texture->desc.usage = RHIResourceUsage::RenderTarget;
            m_BackBufferViews[i] = std::make_shared<D3D12TextureView>();
            m_BackBufferViews[i]->texture = texture;
            m_BackBufferViews[i]->rtvCpu = m_RtvHandles[i];
            m_BackBufferViews[i]->desc.usage = RHIResourceUsage::RenderTarget;
        }
    }

    // Offscreen RTV heap (PostProcessPass)
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kOffscreenRtvCount;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_OffscreenRtvHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(offscreen RTV) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_OffscreenRtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_RtvDescriptorPool = std::make_shared<D3D12DescriptorPool>(
            m_OffscreenRtvHeap->GetCPUDescriptorHandleForHeapStart(),
            D3D12_GPU_DESCRIPTOR_HANDLE{}, m_OffscreenRtvDescriptorSize,
            kOffscreenRtvCount, 0);
    }

    // DSV heap (PostProcessPass depth)
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvDesc.NumDescriptors = kDsvDescriptorCount;
        dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_Device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_DsvHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(DSV) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_DsvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        m_DsvDescriptorPool = std::make_shared<D3D12DescriptorPool>(
            m_DsvHeap->GetCPUDescriptorHandleForHeapStart(),
            D3D12_GPU_DESCRIPTOR_HANDLE{}, m_DsvDescriptorSize,
            kDsvDescriptorCount, 0);
    }

    // SRV heap (shader-visible, required by ImGui dx12 backend for fonts)
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = kDefaultSrvDescriptorCount;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_Device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_SrvHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(SRV) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }

        m_FontSrvCpuHandle = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        m_FontSrvGpuHandle = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
    }

    m_SrvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_SrvDescriptorPool = std::make_shared<D3D12DescriptorPool>(
        m_SrvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_SrvHeap->GetGPUDescriptorHandleForHeapStart(), m_SrvDescriptorSize,
        kDefaultSrvDescriptorCount, 1);

    // Sampler heap (samplers must be in a separate heap type in D3D12)
    {
        D3D12_DESCRIPTOR_HEAP_DESC sampDesc = {};
        sampDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        sampDesc.NumDescriptors = kDefaultSamplerDescriptorCount;
        sampDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_Device->CreateDescriptorHeap(&sampDesc, IID_PPV_ARGS(&m_SamplerHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(Sampler) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_SamplerDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        m_SamplerDescriptorPool = std::make_shared<D3D12DescriptorPool>(
            m_SamplerHeap->GetCPUDescriptorHandleForHeapStart(),
            m_SamplerHeap->GetGPUDescriptorHandleForHeapStart(), m_SamplerDescriptorSize,
            kDefaultSamplerDescriptorCount, 0);
    }

    // Upload command allocator + list (one-shot copies for texture uploads)
    {
        hr = m_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_UploadCommandAllocator));
        if (FAILED(hr)) {
            Logger::Error("CreateCommandAllocator(upload) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_UploadCommandAllocator->SetName(L"MyEngine D3D12 Upload Allocator");
        hr = m_Device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_UploadCommandAllocator.Get(),
            nullptr, IID_PPV_ARGS(&m_UploadCommandList));
        if (FAILED(hr)) {
            Logger::Error("CreateCommandList(upload) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_UploadCommandList->SetName(L"MyEngine D3D12 Upload Command List");
        if (!CheckDeviceResult(m_UploadCommandList->Close(), "Close(upload command list during init)"))
            return false;
    }

    // ---- Command allocators & list (per-frame) ----------------------------
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        hr = m_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Frames[i].commandAllocator));
        if (FAILED(hr)) {
            Logger::Error("CreateCommandAllocator failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        const std::wstring allocatorName = L"MyEngine D3D12 Frame Allocator " +
            std::to_wstring(i);
        m_Frames[i].commandAllocator->SetName(allocatorName.c_str());
    }

    hr = m_Device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Frames[0].commandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_CommandList));
    if (FAILED(hr)) {
        Logger::Error("CreateCommandList failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }
    m_CommandList->SetName(L"MyEngine D3D12 Frame Command List");
    if (!CheckDeviceResult(m_CommandList->Close(), "Close(frame command list during init)"))
        return false;
    m_FrameCommandListClosed = true;

    // ---- Constant buffers (per-frame) ------------------------------------
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        auto& fr = m_Frames[i];
        fr.constantBufferCapacity = kDefaultConstantBufferCapacity;
        fr.constantBufferOffset = 0;
        fr.fenceValue = 0;

        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = fr.constantBufferCapacity;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        hr = m_Device->CreateCommittedResource(
            &props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&fr.constantBufferUpload));
        if (FAILED(hr)) {
            Logger::Error("CreateCommittedResource(constantBufferUpload) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }

        D3D12_RANGE range = { 0, fr.constantBufferCapacity };
        hr = fr.constantBufferUpload->Map(0, nullptr,
                                           reinterpret_cast<void**>(&fr.constantBufferMapped));
        if (FAILED(hr) || !fr.constantBufferMapped) {
            Logger::Error("Map(constantBufferUpload) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
    }

    // ---- Fence / event -----------------------------------------------------
    hr = m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
    if (FAILED(hr)) {
        Logger::Error("CreateFence failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_FenceEvent) {
        Logger::Error("CreateEvent failed");
        return false;
    }

    m_RenderFrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    if (!CreateMainDepthBuffer()) {
        Logger::Error("D3D12Context::Init – CreateMainDepthBuffer failed");
        return false;
    }

    Logger::Info("D3D12Context initialised (", w, "x", h, ")");
    return true;
}

void D3D12Context::Shutdown() {

    // Wait for all GPU work to finish before releasing resources.
    if (m_Fence && m_FenceEvent) {
        const uint64_t fenceToWait = m_NextFenceValue > 0 ? (m_NextFenceValue - 1) : 0;
        if (fenceToWait > 0) {
            if (m_Fence->GetCompletedValue() < fenceToWait) {
                m_Fence->SetEventOnCompletion(fenceToWait, m_FenceEvent);
                WaitForSingleObject(m_FenceEvent, INFINITE);
            }
        }
    }

    if (m_IsRecording && m_CommandList) {
        CheckDeviceResult(m_CommandList->Close(), "Close(frame command list during shutdown)");
        m_FrameCommandListClosed = true;
        if (m_DeferredReleaseQueue) m_DeferredReleaseQueue->AbortUnsubmittedFrame();
        m_IsRecording = false;
    }

    if (m_DeferredReleaseQueue) m_DeferredReleaseQueue->ShutdownAndRelease();

    m_DefaultSampler.reset();
    m_DefaultSampLease.reset();
    m_SamplerCache.clear();
    m_DefaultTexture.Reset();
    m_MainDepthBuffer.Reset();
    for (auto& backBuffer : m_BackBuffers) {
        backBuffer.Reset();
    }
    m_CurrentRenderTarget = {};
    m_DsvHeap.Reset();
    m_RtvHeap.Reset();
    m_OffscreenRtvHeap.Reset();
    m_SrvHeap.Reset();
    m_UploadCommandList.Reset();
    m_UploadCommandAllocator.Reset();
    for (auto& frame : m_Frames) {
        if (frame.constantBufferUpload && frame.constantBufferMapped) {
            frame.constantBufferUpload->Unmap(0, nullptr);
        }
        frame.constantBufferMapped = nullptr;
        frame.constantBufferUpload.Reset();
        frame.commandAllocator.Reset();
        frame.constantBufferCapacity = 0;
        frame.constantBufferOffset = 0;
        frame.fenceValue = 0;
    }
    m_CommandList.Reset();
    m_SamplerHeap.Reset();
    m_RenderTargetStack.clear();
    m_SwapChain.Reset();
    m_Fence.Reset();
    m_DrawIndirectSignature.Reset();
    m_DrawIndexedIndirectSignature.Reset();
    m_GraphicsQueue.reset();
    m_CommandQueue.Reset();
    m_Device.Reset();

    if (m_FenceEvent) {
        CloseHandle(m_FenceEvent);
        m_FenceEvent = nullptr;
    }

    m_SwapChainWidth = 0;
    m_SwapChainHeight = 0;
    m_RenderFrameIndex = 0;
    m_NextFenceValue = 1;
    m_NextRtvSlot = kFrameCount;
    m_NextOffscreenRtvSlot = 0;
    m_NextDsvSlot = 0;
    m_NextSrvSlot = 1;
    m_NextSampSlot = 0;
    m_UniqueSamplerDescriptorCount = 0;
    m_BoundShader = nullptr;
    m_DeviceLost = false;
    m_DredDumped = false;
    m_DeviceLossSuppressionLogged = false;
    m_LastDeviceError.clear();
}

void D3D12Context::WaitForFrame(uint32_t frameIndex) {
    const uint64_t fenceValue = m_Frames[frameIndex].fenceValue;
    if (!m_Fence || fenceValue == 0) return;

    if (m_Fence->GetCompletedValue() < fenceValue) {
        if (m_FenceEvent) {
            if (!CheckDeviceResult(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent),
                                   "SetEventOnCompletion(frame)")) return;
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }
    }
}

void D3D12Context::WaitForGpuIdle()
{
    if (!m_CommandQueue || !m_Fence || !m_FenceEvent) return;
    const uint64_t fenceValue = m_NextFenceValue++;
    if (!CheckDeviceResult(m_CommandQueue->Signal(m_Fence.Get(), fenceValue),
                           "Signal(wait for GPU idle)")) return;
    if (m_Fence->GetCompletedValue() < fenceValue) {
        if (!CheckDeviceResult(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent),
                               "SetEventOnCompletion(wait for GPU idle)")) return;
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
    for (auto& frame : m_Frames) {
        frame.fenceValue = fenceValue;
    }
    if (m_DeferredReleaseQueue) {
        m_DeferredReleaseQueue->Collect(m_Fence->GetCompletedValue());
    }
}

bool D3D12Context::UploadBufferData(ID3D12Resource* destination,
                                    ID3D12Resource* uploadBuffer,
                                    uint64_t byteSize,
                                    D3D12_RESOURCE_STATES finalState) {
    if (!destination || !uploadBuffer || !m_UploadCommandAllocator || !m_UploadCommandList ||
        !m_CommandQueue || !m_Fence || !m_FenceEvent) {
        return false;
    }

    if (!CanUseDevice("UploadBufferData")) return false;
    if (!CheckDeviceResult(m_UploadCommandAllocator->Reset(), "Reset(upload command allocator)"))
        return false;
    if (!CheckDeviceResult(m_UploadCommandList->Reset(m_UploadCommandAllocator.Get(), nullptr),
                           "Reset(upload command list)")) return false;

    m_UploadCommandList->CopyBufferRegion(destination, 0, uploadBuffer, 0, byteSize);

    if (finalState != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = destination;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = finalState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_UploadCommandList->ResourceBarrier(1, &barrier);
    }

    if (!CheckDeviceResult(m_UploadCommandList->Close(), "Close(upload command list)"))
        return false;

    ID3D12CommandList* cmdLists[] = { m_UploadCommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(1, cmdLists);

    const uint64_t fenceValue = m_NextFenceValue++;
    if (!CheckDeviceResult(m_CommandQueue->Signal(m_Fence.Get(), fenceValue),
                           "Signal(buffer upload)")) return false;
    if (m_Fence->GetCompletedValue() < fenceValue) {
        if (!CheckDeviceResult(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent),
                               "SetEventOnCompletion(buffer upload)")) return false;
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
    return true;
}

void D3D12Context::TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList,
                                              uint32_t frameIndex) {
    auto* resource = m_BackBuffers[frameIndex].Get();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

void D3D12Context::TransitionToPresent(ID3D12GraphicsCommandList* cmdList,
                                        uint32_t frameIndex) {
    auto* resource = m_BackBuffers[frameIndex].Get();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

void D3D12Context::BeginFrame(float r, float g, float b, float a) {
    if (m_IsRecording) {
        // Avoid nested BeginFrame calls.
        return;
    }
    if (!CanUseDevice("BeginFrame") || !m_SwapChain) return;

    m_RenderFrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
    WaitForFrame(m_RenderFrameIndex);

    auto& fr = m_Frames[m_RenderFrameIndex];
    fr.constantBufferOffset = 0;

    if (!m_FrameCommandListClosed && m_CommandList) {
        const HRESULT closeHr = m_CommandList->Close();
        if (SUCCEEDED(closeHr)) {
            m_FrameCommandListClosed = true;
        } else {
            Logger::Warn("Close(stale frame command list) failed during recovery: ",
                         FormatHRESULT(closeHr));
        }
    }

    if (!CheckDeviceResult(fr.commandAllocator->Reset(), "Reset(frame command allocator)")) return;
    HRESULT resetHr = m_CommandList
        ? m_CommandList->Reset(fr.commandAllocator.Get(), nullptr)
        : E_POINTER;
    if (SUCCEEDED(resetHr)) {
        m_FrameCommandListClosed = false;
    } else {
        Logger::Warn("Reset(frame command list) failed during recovery: ",
                     FormatHRESULT(resetHr), "; recreating command list");
        if (!RecreateFrameCommandList(fr.commandAllocator.Get())) return;
    }
    if (m_DeferredReleaseQueue) {
        const uint64_t completedFenceValue = m_Fence ? m_Fence->GetCompletedValue() : 0;
        m_DeferredReleaseQueue->BeginFrame(completedFenceValue);
    }
    m_IsRecording = true;

    // Set descriptors for ImGui (fonts SRV) + engine textures.
    EnsureDefaultResources();
    if (m_DeviceLost) {
        CheckDeviceResult(m_CommandList->Close(),
                          "Close(frame command list after device removal)");
        m_FrameCommandListClosed = true;
        if (m_DeferredReleaseQueue) m_DeferredReleaseQueue->AbortUnsubmittedFrame();
        m_IsRecording = false;
        return;
    }
    ID3D12DescriptorHeap* heaps[] = { m_SrvHeap.Get(), m_SamplerHeap.Get() };
    m_CommandList->SetDescriptorHeaps(2, heaps);

    TransitionToRenderTarget(m_CommandList.Get(), m_RenderFrameIndex);
    m_RenderTargetStack.clear();
    m_DepthOnlyBound = false;
    m_CullNone = false;
    m_Wireframe = false;

    const D3D12_CPU_DESCRIPTOR_HANDLE colorRtv = GetMainColorRtv();
    const D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = GetMainDsvHandle();
    const D3D12_CPU_DESCRIPTOR_HANDLE* depthDsvPtr =
        depthDsv.ptr != 0 ? &depthDsv : nullptr;
    m_CommandList->OMSetRenderTargets(1, &colorRtv, FALSE, depthDsvPtr);
    m_CurrentRenderTarget.colorRtv = colorRtv;
    m_CurrentRenderTarget.depthDsv = depthDsv;
    m_CurrentRenderTarget.hasColorTarget = true;

    const float color[4] = { r, g, b, a };
    m_CommandList->ClearRenderTargetView(colorRtv, color, 0, nullptr);
    if (depthDsv.ptr != 0) {
        m_CommandList->ClearDepthStencilView(
            depthDsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    }

    // Default pipeline state expectations.
    m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (m_HasViewport) {
        m_CommandList->RSSetViewports(1, &m_Viewport);
        m_CommandList->RSSetScissorRects(1, &m_ScissorRect);
    }
}

void D3D12Context::EndFrame() {
    if (!m_IsRecording) return;

    const uint32_t frameIndex = m_RenderFrameIndex;
    TransitionToPresent(m_CommandList.Get(), frameIndex);

    if (!CheckDeviceResult(m_CommandList->Close(), "Close(frame command list)")) {
        if (m_DeferredReleaseQueue) m_DeferredReleaseQueue->AbortUnsubmittedFrame();
        m_IsRecording = false;
        return;
    }
    m_FrameCommandListClosed = true;

    ID3D12CommandList* cmdLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(1, cmdLists);

    PresentSwapChain(m_VSyncEnabled);
    if (m_DeviceLost) {
        if (m_DeferredReleaseQueue) m_DeferredReleaseQueue->AbandonSubmittedFrame();
        m_IsRecording = false;
        return;
    }

    const uint64_t fenceValue = m_NextFenceValue++;
    if (!CheckDeviceResult(m_CommandQueue->Signal(m_Fence.Get(), fenceValue),
                           "Signal(frame)")) {
        if (m_DeferredReleaseQueue) m_DeferredReleaseQueue->AbandonSubmittedFrame();
        m_IsRecording = false;
        return;
    }
    m_Frames[frameIndex].fenceValue = fenceValue;
    if (m_DeferredReleaseQueue) m_DeferredReleaseQueue->EndFrame(fenceValue);

    m_IsRecording = false;
}

GpuSwapChain* D3D12Context::GetSwapChain() {
    return m_SwapChainInterface.get();
}

GpuCommandList* D3D12Context::GetGraphicsCommandList() {
    if (!m_IsRecording) return nullptr;
    return m_GraphicsCommandList.get();
}

ImGuiBackendHandles D3D12Context::GetImGuiBackendHandles() {
    ImGuiBackendHandles h;
    h.backend = RHIBackend::D3D12;
    h.device = m_Device.Get();
    h.framesInFlight = kFrameCount;
    h.srvHeap = m_SrvHeap.Get();
    h.fontSrvCpuHandle = m_FontSrvCpuHandle.ptr;
    h.fontSrvGpuHandle = m_FontSrvGpuHandle.ptr;
    h.commandList = m_CommandList.Get();
    return h;
}

void D3D12Context::PresentSwapChain(bool vsync) {
    if (!m_SwapChain) return;
    const HRESULT presentHr = m_SwapChain->Present(vsync ? 1 : 0, 0);
    CheckDeviceResult(presentHr, "D3D12 Present");
}

bool D3D12Context::RecreateFrameCommandList(ID3D12CommandAllocator* allocator)
{
    if (!m_Device || !allocator) return false;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    const HRESULT hr = m_Device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
        IID_PPV_ARGS(&commandList));
    if (!CheckDeviceResult(hr, "CreateCommandList(frame recovery)")) return false;
    commandList->SetName(L"MyEngine D3D12 Frame Command List");
    m_CommandList = std::move(commandList);
    m_FrameCommandListClosed = false;
    return true;
}

bool D3D12Context::ResizeSwapChain(uint32_t width, uint32_t height) {
    if (!m_Device || !m_SwapChain || !m_RtvHeap) return false;
    if (width == 0 || height == 0) return false;
    if (m_IsRecording) return false;

    WaitForGpuIdle();

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        m_BackBufferViews[i].reset();
        m_BackBuffers[i].Reset();
    }
    m_MainDepthBuffer.Reset();
    m_MainDsvHandle = {};
    m_NextDsvSlot = 0;

    HRESULT hr = m_SwapChain->ResizeBuffers(
        kFrameCount, width, height, m_RtvFormat, 0);
    if (FAILED(hr)) {
        Logger::Error("D3D12 ResizeBuffers failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    auto baseHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        m_RtvHandles[i] = OffsetHandle(baseHandle, i, m_RtvDescriptorSize);
        hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i]));
        if (FAILED(hr)) {
            Logger::Error("D3D12 GetBuffer after resize failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_Device->CreateRenderTargetView(m_BackBuffers[i].Get(), nullptr,
                                         m_RtvHandles[i]);
        auto texture = std::make_shared<D3D12Texture>();
        texture->resource = m_BackBuffers[i];
        texture->desc.width = width; texture->desc.height = height;
        texture->desc.format = RHIFormat::RGBA8UNorm;
        texture->desc.usage = RHIResourceUsage::RenderTarget;
        m_BackBufferViews[i] = std::make_shared<D3D12TextureView>();
        m_BackBufferViews[i]->texture = texture;
        m_BackBufferViews[i]->rtvCpu = m_RtvHandles[i];
        m_BackBufferViews[i]->desc.usage = RHIResourceUsage::RenderTarget;
    }

    m_SwapChainWidth = width;
    m_SwapChainHeight = height;
    m_RenderFrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    if (!CreateMainDepthBuffer()) {
        Logger::Error("D3D12 ResizeSwapChain – CreateMainDepthBuffer failed");
        return false;
    }

    return true;
}

bool D3D12Context::CheckDeviceResult(HRESULT hr, const char* operation) {
    if (SUCCEEDED(hr)) return true;
    const std::string error = std::string(operation) + " failed: " + FormatHRESULT(hr);
    Logger::Error(error);
    if (!m_DeviceLost) m_LastDeviceError = error;
    ReportDeviceRemovedReason(operation);
    if (!m_DeviceLost && (hr == DXGI_ERROR_DEVICE_REMOVED ||
                          hr == DXGI_ERROR_DEVICE_RESET ||
                          hr == DXGI_ERROR_DEVICE_HUNG)) {
        m_DeviceLost = true;
        DumpDredDiagnostics();
    }
    return false;
}

bool D3D12Context::CanUseDevice(const char* operation) {
    if (m_Device && !m_DeviceLost) return true;
    if (m_DeviceLost && !m_DeviceLossSuppressionLogged) {
        Logger::Warn("D3D12: skipping ", operation,
                     " because the device was already removed; first error: ",
                     m_LastDeviceError);
        m_DeviceLossSuppressionLogged = true;
    }
    return false;
}

void D3D12Context::ReportDeviceRemovedReason(const char* operation) {
    if (!m_Device) return;
    const HRESULT reason = m_Device->GetDeviceRemovedReason();
    if (SUCCEEDED(reason)) return;

    if (!m_DeviceLost) {
        m_DeviceLost = true;
        m_LastDeviceError = std::string(operation) +
            " detected device removal: " + FormatHRESULT(reason);
        Logger::Error(m_LastDeviceError);
    } else {
        Logger::Error("D3D12 device removed reason after ", operation,
                      ": ", FormatHRESULT(reason));
    }
    DumpDredDiagnostics();
}

void D3D12Context::DumpDredDiagnostics() {
    if (m_DredDumped || !m_Device) return;
    m_DredDumped = true;

    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (FAILED(m_Device.As(&dred))) {
        Logger::Error("D3D12 DRED 1.1 interface unavailable");
        return;
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
    const HRESULT breadcrumbsHr = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
    if (SUCCEEDED(breadcrumbsHr)) {
        uint32_t nodeIndex = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode;
             node; node = node->pNext, ++nodeIndex) {
            const UINT completed = node->pLastBreadcrumbValue
                ? *node->pLastBreadcrumbValue : 0;
            const std::string queueName = node->pCommandQueueDebugNameA
                ? node->pCommandQueueDebugNameA : WideToUtf8(node->pCommandQueueDebugNameW);
            const std::string listName = node->pCommandListDebugNameA
                ? node->pCommandListDebugNameA : WideToUtf8(node->pCommandListDebugNameW);
            Logger::Error("DRED breadcrumb node #", nodeIndex,
                          " queue=", queueName.empty() ? "<unnamed>" : queueName,
                          " list=", listName.empty() ? "<unnamed>" : listName,
                          " completed=", completed, "/", node->BreadcrumbCount);
            if (node->pCommandHistory && completed > 0 && completed <= node->BreadcrumbCount) {
                Logger::Error("DRED last completed op=",
                              static_cast<uint32_t>(node->pCommandHistory[completed - 1]));
            }
            if (node->pCommandHistory && completed < node->BreadcrumbCount) {
                Logger::Error("DRED next pending op=",
                              static_cast<uint32_t>(node->pCommandHistory[completed]));
            }
            for (UINT i = 0; i < node->BreadcrumbContextsCount; ++i) {
                const auto& context = node->pBreadcrumbContexts[i];
                const std::string contextText = WideToUtf8(context.pContextString);
                Logger::Error("DRED context breadcrumb=", context.BreadcrumbIndex,
                              " text=", contextText.empty() ? "<none>" : contextText);
            }
        }
        if (!breadcrumbs.pHeadAutoBreadcrumbNode)
            Logger::Error("DRED breadcrumbs: no command history reported");
    } else {
        Logger::Error("DRED GetAutoBreadcrumbsOutput1 failed: ", FormatHRESULT(breadcrumbsHr));
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault{};
    const HRESULT pageFaultHr = dred->GetPageFaultAllocationOutput1(&pageFault);
    if (SUCCEEDED(pageFaultHr)) {
        Logger::Error("DRED page-fault VA=0x", std::hex, pageFault.PageFaultVA, std::dec);
        auto logAllocations = [](const char* label, const D3D12_DRED_ALLOCATION_NODE1* head) {
            uint32_t count = 0;
            for (auto* node = head; node && count < 64; node = node->pNext, ++count) {
                const std::string objectName = node->ObjectNameA
                    ? node->ObjectNameA : WideToUtf8(node->ObjectNameW);
                Logger::Error("DRED ", label, " allocation #", count,
                              " name=", objectName.empty() ? "<unnamed>" : objectName,
                              " type=", static_cast<uint32_t>(node->AllocationType));
            }
            if (!head) Logger::Error("DRED ", label, " allocations: none reported");
        };
        logAllocations("existing", pageFault.pHeadExistingAllocationNode);
        logAllocations("recently-freed", pageFault.pHeadRecentFreedAllocationNode);
    } else {
        Logger::Error("DRED GetPageFaultAllocationOutput1 failed: ", FormatHRESULT(pageFaultHr));
    }
}

std::shared_ptr<GpuBuffer> D3D12Context::CreateVertexBuffer(
    const void* data, uint32_t byteSize, uint32_t strideBytes) {
    if (!CanUseDevice("CreateVertexBuffer")) return nullptr;
    if (!data || byteSize == 0 || byteSize % 4 != 0) {
        // Accept any size, but log in case something is clearly wrong.
        // We keep this minimal to avoid over-rejecting.
    }

    auto buf = std::make_shared<D3D12VertexBuffer>();
    buf->deferredReleaseQueue = m_DeferredReleaseQueue;
    buf->desc.size = byteSize;
    buf->desc.stride = strideBytes;
    buf->desc.usage = RHIResourceUsage::VertexBuffer;
    buf->byteSize = byteSize;
    buf->stride = strideBytes;

    const uint64_t alignedSize = (static_cast<uint64_t>(byteSize) + 255ull) & ~255ull;

    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = alignedSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_Device->CreateCommittedResource(
        &props, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buf->resource));
    if (!CheckDeviceResult(hr, "CreateCommittedResource(vertex buffer)")) return nullptr;

    ComPtr<ID3D12Resource> uploadBuffer;
    D3D12_HEAP_PROPERTIES uploadProps = {};
    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    hr = m_Device->CreateCommittedResource(
        &uploadProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
    if (!CheckDeviceResult(hr, "CreateCommittedResource(vertex upload buffer)")) return nullptr;

    void* mapped = nullptr;
    hr = uploadBuffer->Map(0, nullptr, &mapped);
    if (!CheckDeviceResult(hr, "Map(vertex upload buffer)") || !mapped) {
        if (!mapped) ReportDeviceRemovedReason("Map(vertex upload buffer) returned null");
        return nullptr;
    }
    std::memcpy(mapped, data, byteSize);
    uploadBuffer->Unmap(0, nullptr);

    if (!UploadBufferData(buf->resource.Get(), uploadBuffer.Get(), byteSize,
                          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)) {
        Logger::Error("CreateVertexBuffer upload submission failed");
        ReportDeviceRemovedReason("CreateVertexBuffer upload submission");
        return nullptr;
    }

    return buf;
}

std::shared_ptr<GpuBuffer> D3D12Context::CreateIndexBuffer(
    const void* data, uint32_t byteSize) {
    if (!CanUseDevice("CreateIndexBuffer")) return nullptr;
    if (!data || byteSize == 0) return nullptr;

    auto buf = std::make_shared<D3D12IndexBuffer>();
    buf->deferredReleaseQueue = m_DeferredReleaseQueue;
    buf->desc.size = byteSize;
    buf->desc.usage = RHIResourceUsage::IndexBuffer;
    buf->byteSize = byteSize;
    buf->format = DXGI_FORMAT_R32_UINT;

    const uint64_t alignedSize = (static_cast<uint64_t>(byteSize) + 255ull) & ~255ull;

    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = alignedSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_Device->CreateCommittedResource(
        &props, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buf->resource));
    if (!CheckDeviceResult(hr, "CreateCommittedResource(index buffer)")) return nullptr;

    ComPtr<ID3D12Resource> uploadBuffer;
    D3D12_HEAP_PROPERTIES uploadProps = {};
    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    hr = m_Device->CreateCommittedResource(
        &uploadProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
    if (!CheckDeviceResult(hr, "CreateCommittedResource(index upload buffer)")) return nullptr;

    void* mapped = nullptr;
    hr = uploadBuffer->Map(0, nullptr, &mapped);
    if (!CheckDeviceResult(hr, "Map(index upload buffer)") || !mapped) {
        if (!mapped) ReportDeviceRemovedReason("Map(index upload buffer) returned null");
        return nullptr;
    }
    std::memcpy(mapped, data, byteSize);
    uploadBuffer->Unmap(0, nullptr);

    if (!UploadBufferData(buf->resource.Get(), uploadBuffer.Get(), byteSize,
                          D3D12_RESOURCE_STATE_INDEX_BUFFER)) {
        Logger::Error("CreateIndexBuffer upload submission failed");
        ReportDeviceRemovedReason("CreateIndexBuffer upload submission");
        return nullptr;
    }

    return buf;
}

std::shared_ptr<GpuBuffer> D3D12Context::CreateBuffer(
    const RHIBufferDesc& desc, const void* initialData) {
    if (!CanUseDevice("CreateBuffer") || desc.size == 0) return nullptr;
    const bool readback = HasUsage(desc.usage, RHIResourceUsage::Readback);
    if (readback && initialData) {
        Logger::Error("[RHI] Initial data for a readback buffer is unsupported");
        return nullptr;
    }

    auto buffer = std::make_shared<D3D12Buffer>();
    buffer->deferredReleaseQueue = m_DeferredReleaseQueue;
    buffer->desc = desc;
    buffer->byteSize = desc.size;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = readback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC native{}; native.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    native.Width = (static_cast<uint64_t>(desc.size) + 255ull) & ~255ull;
    native.Height = 1; native.DepthOrArraySize = 1; native.MipLevels = 1;
    native.SampleDesc.Count = 1; native.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess))
        native.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    const bool unorderedAccess = HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess);
    const bool copyDestinationOnly = HasUsage(desc.usage, RHIResourceUsage::CopyDestination) &&
        !HasUsage(desc.usage, RHIResourceUsage::VertexBuffer) &&
        !HasUsage(desc.usage, RHIResourceUsage::IndexBuffer) &&
        !HasUsage(desc.usage, RHIResourceUsage::ConstantBuffer) &&
        !HasUsage(desc.usage, RHIResourceUsage::ShaderResource) &&
        !unorderedAccess;
    const D3D12_RESOURCE_STATES finalState = readback ? D3D12_RESOURCE_STATE_COPY_DEST :
        unorderedAccess ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS :
        copyDestinationOnly ? D3D12_RESOURCE_STATE_COPY_DEST :
        D3D12_RESOURCE_STATE_GENERIC_READ;
    const D3D12_RESOURCE_STATES initialState = initialData && !readback ?
        D3D12_RESOURCE_STATE_COPY_DEST : finalState;

    HRESULT hr = m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &native,
        initialState, nullptr, IID_PPV_ARGS(&buffer->resource));
    if (!CheckDeviceResult(hr, "CreateCommittedResource(generic buffer)")) return nullptr;

    if (initialData && !readback) {
        ComPtr<ID3D12Resource> uploadBuffer;
        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        hr = m_Device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &native,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
        if (!CheckDeviceResult(hr, "CreateCommittedResource(generic upload buffer)")) return nullptr;

        void* mapped = nullptr;
        hr = uploadBuffer->Map(0, nullptr, &mapped);
        if (!CheckDeviceResult(hr, "Map(generic upload buffer)") || !mapped) {
            if (!mapped) ReportDeviceRemovedReason("Map(generic upload buffer) returned null");
            return nullptr;
        }
        std::memcpy(mapped, initialData, desc.size);
        uploadBuffer->Unmap(0, nullptr);

        if (!UploadBufferData(buffer->resource.Get(), uploadBuffer.Get(), desc.size, finalState)) {
            Logger::Error("[RHI] Generic buffer upload submission failed");
            ReportDeviceRemovedReason("Generic buffer upload submission");
            return nullptr;
        }
    }

    return buffer;
}

std::shared_ptr<GpuBufferView> D3D12Context::CreateBufferView(
    const std::shared_ptr<GpuBuffer>& buffer, const RHIBufferViewDesc& desc) {
    if (!CanUseDevice("CreateBufferView")) return nullptr;
    auto nativeBuffer = std::dynamic_pointer_cast<D3D12Buffer>(buffer);
    if (!nativeBuffer || !nativeBuffer->resource || nativeBuffer->desc.stride == 0) return nullptr;
    auto view = std::make_shared<D3D12BufferView>(); view->buffer = buffer; view->desc = desc;
    const uint32_t count = desc.elementCount ? desc.elementCount :
        nativeBuffer->desc.size / nativeBuffer->desc.stride;
    if (HasUsage(desc.usage, RHIResourceUsage::ShaderResource)) {
        view->srvCpu = AllocSrvSlot(view->srvGpu, &view->srvLease);
        if (view->srvCpu.ptr == 0 || view->srvGpu.ptr == 0) return nullptr;
        D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Format = DXGI_FORMAT_UNKNOWN;
        d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Buffer.FirstElement = desc.firstElement; d.Buffer.NumElements = count;
        d.Buffer.StructureByteStride = nativeBuffer->desc.stride;
        m_Device->CreateShaderResourceView(nativeBuffer->resource.Get(), &d, view->srvCpu);
    }
    if (HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess)) {
        view->uavCpu = AllocSrvSlot(view->uavGpu, &view->uavLease);
        if (view->uavCpu.ptr == 0 || view->uavGpu.ptr == 0) return nullptr;
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{}; d.Format = DXGI_FORMAT_UNKNOWN;
        d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        d.Buffer.FirstElement = desc.firstElement; d.Buffer.NumElements = count;
        d.Buffer.StructureByteStride = nativeBuffer->desc.stride;
        m_Device->CreateUnorderedAccessView(nativeBuffer->resource.Get(), nullptr, &d, view->uavCpu);
    }
    return view;
}

namespace {

bool CreateTextureRootSignature(ID3D12Device* device, ID3D12RootSignature** outRootSig)
{
    if (!device || !outRootSig) return false;

    D3D12_DESCRIPTOR_RANGE ranges[D3D12Context::kTextureSlotCount * 2 + 1] = {};
    for (uint32_t slot = 0; slot < D3D12Context::kTextureSlotCount; ++slot) {
        auto& srv = ranges[slot * 2];
        srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv.NumDescriptors = 1;
        srv.BaseShaderRegister = slot;
        srv.RegisterSpace = 0;

        auto& sampler = ranges[slot * 2 + 1];
        sampler.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampler.NumDescriptors = 1;
        sampler.BaseShaderRegister = slot;
        sampler.RegisterSpace = 0;
    }

    auto& bindless = ranges[D3D12Context::kTextureSlotCount * 2];
    bindless.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    bindless.NumDescriptors = D3D12Context::kDefaultSrvDescriptorCount;
    bindless.BaseShaderRegister = 0;
    bindless.RegisterSpace = 1;

    D3D12_ROOT_PARAMETER rootParams[2 + D3D12Context::kTextureSlotCount * 2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    for (uint32_t i = 0; i < D3D12Context::kTextureSlotCount * 2; ++i) {
        rootParams[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[i + 1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
        rootParams[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    auto& bindlessParam = rootParams[1 + D3D12Context::kTextureSlotCount * 2];
    bindlessParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    bindlessParam.DescriptorTable.NumDescriptorRanges = 1;
    bindlessParam.DescriptorTable.pDescriptorRanges = &bindless;
    bindlessParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = _countof(rootParams);
    rsDesc.pParameters = rootParams;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Error("D3D12SerializeRootSignature failed: ",
                          static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = device->CreateRootSignature(
        0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(outRootSig));
    return SUCCEEDED(hr);
}

} // namespace

bool CreateComputeRootSignature(ID3D12Device* device, ID3D12RootSignature** outRootSig);

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetMainColorRtv() const
{
    return m_RtvHandles[m_RenderFrameIndex];
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetMainDsvHandle() const
{
    return m_MainDsvHandle;
}

bool D3D12Context::CreateMainDepthBuffer()
{
    if (!CanUseDevice("CreateMainDepthBuffer") ||
        m_SwapChainWidth == 0 || m_SwapChainHeight == 0) {
        return false;
    }

    m_MainDepthBuffer.Reset();
    m_MainDsvHandle = {};
    m_MainDsvLease.reset();

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_SwapChainWidth;
    desc.Height = m_SwapChainHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kDepthTypelessFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = kDepthFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    HRESULT hr = m_Device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
        IID_PPV_ARGS(&m_MainDepthBuffer));
    if (!CheckDeviceResult(hr, "CreateCommittedResource(main depth buffer)")) return false;

    m_MainDsvHandle = AllocDsvSlot(&m_MainDsvLease);
    if (m_MainDsvHandle.ptr == 0) return false;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = kDepthFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    m_Device->CreateDepthStencilView(m_MainDepthBuffer.Get(), &dsvDesc, m_MainDsvHandle);
    return true;
}

void D3D12Context::PushRenderTarget(const D3D12_CPU_DESCRIPTOR_HANDLE* colorRtv,
                                      D3D12_CPU_DESCRIPTOR_HANDLE depthDsv)
{
    if (!m_IsRecording || !m_CommandList) return;

    m_RenderTargetStack.push_back(m_CurrentRenderTarget);

    const D3D12_CPU_DESCRIPTOR_HANDLE* depthDsvPtr =
        depthDsv.ptr != 0 ? &depthDsv : nullptr;
    if (colorRtv && colorRtv->ptr != 0) {
        m_CommandList->OMSetRenderTargets(1, colorRtv, FALSE, depthDsvPtr);
        m_CurrentRenderTarget.colorRtv = *colorRtv;
        m_CurrentRenderTarget.depthDsv = depthDsv;
        m_CurrentRenderTarget.hasColorTarget = true;
    } else {
        m_CommandList->OMSetRenderTargets(0, nullptr, FALSE, depthDsvPtr);
        m_CurrentRenderTarget.colorRtv = {};
        m_CurrentRenderTarget.depthDsv = depthDsv;
        m_CurrentRenderTarget.hasColorTarget = false;
    }
}

void D3D12Context::PopRenderTarget()
{
    if (!m_IsRecording || !m_CommandList || m_RenderTargetStack.empty()) return;

    const RenderTargetBinding restore = m_RenderTargetStack.back();
    m_RenderTargetStack.pop_back();

    if (restore.hasColorTarget && restore.colorRtv.ptr != 0) {
        const D3D12_CPU_DESCRIPTOR_HANDLE* dsv =
            restore.depthDsv.ptr != 0 ? &restore.depthDsv : nullptr;
        m_CommandList->OMSetRenderTargets(1, &restore.colorRtv, FALSE, dsv);
    } else if (restore.depthDsv.ptr != 0) {
        m_CommandList->OMSetRenderTargets(0, nullptr, FALSE, &restore.depthDsv);
    } else {
        m_CommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    }
    m_CurrentRenderTarget = restore;
}

void D3D12Context::BindDepthOnlyShader(GpuShader* shader)
{
    if (!m_IsRecording || !shader) return;
    auto* s = static_cast<D3D12Shader*>(shader);
    m_BoundShader = s;
    m_DepthOnlyBound = true;
    m_CommandList->SetGraphicsRootSignature(s->rootSignature.Get());
    if (s->depthOnlyPipelineState) {
        m_CommandList->SetPipelineState(s->depthOnlyPipelineState.Get());
    }
}

std::shared_ptr<GpuTexture> D3D12Context::CreateDepthTexture(
    int width, int height, bool cube, uint32_t arraySize)
{
    if (!CanUseDevice("CreateDepthTexture") || width <= 0 || height <= 0) return nullptr;

    EnsureDefaultResources();
    if (m_DeviceLost) return nullptr;

    if (cube) {
        arraySize = 6;
    } else if (arraySize == 0) {
        arraySize = 1;
    }

    auto tex = std::make_shared<D3D12Texture>();
    tex->deferredReleaseQueue = m_DeferredReleaseQueue;
    tex->isCube = cube;
    tex->arraySize = arraySize;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(width);
    desc.Height = static_cast<UINT>(height);
    desc.DepthOrArraySize = arraySize;
    desc.MipLevels = 1;
    desc.Format = kDepthTypelessFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = kDepthFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    HRESULT hr = m_Device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
        IID_PPV_ARGS(&tex->resource));
    if (!CheckDeviceResult(hr, "CreateCommittedResource(depth texture)")) return nullptr;

    tex->srvCpu = AllocSrvSlot(tex->srvGpu, &tex->srvLease);
    if (tex->srvCpu.ptr == 0 || tex->srvGpu.ptr == 0) return nullptr;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    if (cube) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = 1;
    } else if (arraySize > 1) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.ArraySize = arraySize;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    }
    m_Device->CreateShaderResourceView(tex->resource.Get(), &srvDesc, tex->srvCpu);

    {
        RHISamplerDesc samplerDesc;
        samplerDesc.filter = RHIFilter::ComparisonLinear;
        samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = RHIAddressMode::Clamp;
        auto sampler = std::dynamic_pointer_cast<D3D12Sampler>(CreateSampler(samplerDesc));
        if (!sampler) return nullptr;
        tex->sampler = sampler;
        tex->sampCpu = sampler->cpu;
        tex->sampGpu = sampler->gpu;
        tex->sampLease = sampler->lease;
    }

    if (cube) {
        tex->dsvFaces.resize(6);
        tex->dsvFaceLeases.resize(6);
        for (uint32_t face = 0; face < 6; ++face) {
            tex->dsvFaces[face] = AllocDsvSlot(&tex->dsvFaceLeases[face]);
            if (tex->dsvFaces[face].ptr == 0) return nullptr;
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = kDepthFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.ArraySize = 1;
            dsvDesc.Texture2DArray.FirstArraySlice = face;
            dsvDesc.Texture2DArray.MipSlice = 0;
            m_Device->CreateDepthStencilView(
                tex->resource.Get(), &dsvDesc, tex->dsvFaces[face]);
        }
        tex->dsvCpu = tex->dsvFaces[0];
    } else if (arraySize > 1) {
        tex->dsvFaces.resize(arraySize);
        tex->dsvFaceLeases.resize(arraySize);
        for (uint32_t slice = 0; slice < arraySize; ++slice) {
            tex->dsvFaces[slice] = AllocDsvSlot(&tex->dsvFaceLeases[slice]);
            if (tex->dsvFaces[slice].ptr == 0) return nullptr;
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = kDepthFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.ArraySize = 1;
            dsvDesc.Texture2DArray.FirstArraySlice = slice;
            dsvDesc.Texture2DArray.MipSlice = 0;
            m_Device->CreateDepthStencilView(
                tex->resource.Get(), &dsvDesc, tex->dsvFaces[slice]);
        }
        tex->dsvCpu = tex->dsvFaces[0];
    } else {
        tex->dsvCpu = AllocDsvSlot(&tex->dsvLease);
        if (tex->dsvCpu.ptr == 0) return nullptr;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = kDepthFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        m_Device->CreateDepthStencilView(tex->resource.Get(), &dsvDesc, tex->dsvCpu);
    }

    return tex;
}

bool D3D12Context::BuildShaderPipelines(D3D12Shader& shader,
                                          const D3D12_SHADER_BYTECODE& vs,
                                          const D3D12_SHADER_BYTECODE& ps,
                                          const VertexElement* layout,
                                          uint32_t layoutCount)
{
    (void)vs;
    (void)ps;
    (void)layout;
    (void)layoutCount;
    // Graphics PSOs are created exclusively from GraphicsPipelineDesc.
    return shader.rootSignature != nullptr;
}

std::shared_ptr<GpuShader> D3D12Context::CreateShader(
    const std::string& hlslSource,
    const std::string& vsEntry,
    const std::string& psEntry,
    const VertexElement* layout,
    uint32_t layoutCount) {
    if (!CanUseDevice("CreateShader")) return nullptr;
    if (layoutCount > 0 && !layout) return nullptr;

    auto sh = std::make_shared<D3D12Shader>();
    sh->deferredReleaseQueue = m_DeferredReleaseQueue;
    if (layout && layoutCount) sh->vertexLayout.assign(layout, layout + layoutCount);

    auto compileShader = [&](const std::string& entry,
                             const std::string& target,
                             ComPtr<ID3DBlob>& outBlob) -> bool {
        ComPtr<ID3DBlob> errBlob;
        HRESULT hr = D3DCompile(
            hlslSource.c_str(), hlslSource.size(),
            nullptr, nullptr, nullptr,
            entry.c_str(), target.c_str(),
            D3DCOMPILE_ENABLE_STRICTNESS, 0,
            &outBlob, &errBlob);

        if (FAILED(hr)) {
            if (errBlob) {
                Logger::Error("Shader compile error: ",
                    static_cast<const char*>(errBlob->GetBufferPointer()));
            }
            return false;
        }
        return true;
    };

    ComPtr<ID3DBlob> vsBlob, psBlob;
    if (!compileShader(vsEntry, "vs_5_0", vsBlob)) return nullptr;
    if (!compileShader(psEntry, "ps_5_0", psBlob)) return nullptr;
    sh->vertexBytecode.assign(
        static_cast<const uint8_t*>(vsBlob->GetBufferPointer()),
        static_cast<const uint8_t*>(vsBlob->GetBufferPointer()) + vsBlob->GetBufferSize());
    sh->pixelBytecode.assign(
        static_cast<const uint8_t*>(psBlob->GetBufferPointer()),
        static_cast<const uint8_t*>(psBlob->GetBufferPointer()) + psBlob->GetBufferSize());

    if (!CreateTextureRootSignature(m_Device.Get(), &sh->rootSignature)) {
        ReportDeviceRemovedReason("CreateRootSignature(shader)");
        return nullptr;
    }
    sh->hasBindlessTable = true;

    const D3D12_SHADER_BYTECODE vs = {
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    const D3D12_SHADER_BYTECODE ps = {
        psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    if (!BuildShaderPipelines(*sh, vs, ps, layout, layoutCount)) {
        return nullptr;
    }

    ReflectDxbcProgram(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                       psBlob->GetBufferPointer(), psBlob->GetBufferSize(), sh->reflection);
    return sh;
}

std::shared_ptr<GpuShader> D3D12Context::CreateShaderFromBytecode(
    const void* vsBytecode,
    size_t vsSize,
    const void* psBytecode,
    size_t psSize,
    const VertexElement* layout,
    uint32_t layoutCount) {
    if (!CanUseDevice("CreateShaderFromBytecode")) return nullptr;
    if (!vsBytecode || vsSize == 0 || !psBytecode || psSize == 0 ||
        (layoutCount > 0 && !layout)) {
        return nullptr;
    }

    auto sh = std::make_shared<D3D12Shader>();
    sh->deferredReleaseQueue = m_DeferredReleaseQueue;
    if (layout && layoutCount) sh->vertexLayout.assign(layout, layout + layoutCount);
    sh->vertexBytecode.assign(static_cast<const uint8_t*>(vsBytecode),
                              static_cast<const uint8_t*>(vsBytecode) + vsSize);
    sh->pixelBytecode.assign(static_cast<const uint8_t*>(psBytecode),
                             static_cast<const uint8_t*>(psBytecode) + psSize);

    if (!CreateTextureRootSignature(m_Device.Get(), &sh->rootSignature)) {
        ReportDeviceRemovedReason("CreateRootSignature(bytecode shader)");
        return nullptr;
    }
    sh->hasBindlessTable = true;

    const D3D12_SHADER_BYTECODE vs = { vsBytecode, vsSize };
    const D3D12_SHADER_BYTECODE ps = { psBytecode, psSize };
    if (!BuildShaderPipelines(*sh, vs, ps, layout, layoutCount)) {
        return nullptr;
    }

    ReflectDxbcProgram(vsBytecode, vsSize, psBytecode, psSize, sh->reflection);
    return sh;
}

std::shared_ptr<GpuShader> D3D12Context::CreateComputeShaderFromBytecode(
    const void* bytecode, size_t byteSize) {
    if (!CanUseDevice("CreateComputeShaderFromBytecode") || !bytecode || byteSize == 0)
        return nullptr;
    auto shader = std::make_shared<D3D12Shader>();
    shader->deferredReleaseQueue = m_DeferredReleaseQueue;
    shader->computeBytecode.assign(static_cast<const uint8_t*>(bytecode),
                                   static_cast<const uint8_t*>(bytecode) + byteSize);
    if (!CreateComputeRootSignature(m_Device.Get(), &shader->computeRootSignature)) {
        ReportDeviceRemovedReason("CreateRootSignature(compute shader)");
        return nullptr;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = shader->computeRootSignature.Get();
    desc.CS = {bytecode, byteSize};
    const HRESULT pipelineHr = m_Device->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(&shader->computePipelineState));
    if (!CheckDeviceResult(pipelineHr, "CreateComputePipelineState")) return nullptr;
    std::string error;
    if (!ReflectDxbcStage(bytecode, byteSize, ShaderStageCompute, shader->reflection, &error))
        Logger::Warn("[RHI] D3D12 compute reflection failed: ", error);
    return shader;
}

std::shared_ptr<GpuGraphicsPipeline> D3D12Context::CreateGraphicsPipeline(
    const GraphicsPipelineDesc& desc) {
    if (!CanUseDevice("CreateGraphicsPipeline")) return nullptr;
    auto shader = std::dynamic_pointer_cast<D3D12Shader>(desc.shader);
    if (!m_Device || !shader || !shader->rootSignature ||
        shader->vertexBytecode.empty() || shader->pixelBytecode.empty()) return nullptr;
    auto pipeline = std::make_shared<D3D12GraphicsPipeline>();
    pipeline->deferredReleaseQueue = m_DeferredReleaseQueue;
    pipeline->desc = desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC native{};
    native.pRootSignature = shader->rootSignature.Get();
    native.VS = {shader->vertexBytecode.data(), shader->vertexBytecode.size()};
    native.PS = {shader->pixelBytecode.data(), shader->pixelBytecode.size()};
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements(shader->vertexLayout.size());
    for (size_t i = 0; i < shader->vertexLayout.size(); ++i) {
        const auto& element = shader->vertexLayout[i];
        inputElements[i] = {
            element.semantic, element.index, ToDxgiFormat(element.format), 0,
            element.offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    }
    native.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
    native.RasterizerState.FillMode = desc.rasterizer.fillMode == RHIFillMode::Wireframe
        ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    native.RasterizerState.CullMode = desc.rasterizer.cullMode == RHICullMode::None
        ? D3D12_CULL_MODE_NONE : desc.rasterizer.cullMode == RHICullMode::Front
            ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK;
    native.RasterizerState.FrontCounterClockwise =
        desc.rasterizer.frontFace == RHIFrontFace::CounterClockwise;
    native.RasterizerState.DepthBias = desc.rasterizer.depthBias;
    native.RasterizerState.DepthBiasClamp = desc.rasterizer.depthBiasClamp;
    native.RasterizerState.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
    native.RasterizerState.DepthClipEnable = desc.rasterizer.depthClipEnable;
    native.RasterizerState.MultisampleEnable =
        desc.rasterizer.multisampleEnable || desc.multisample.sampleCount > 1;
    native.RasterizerState.AntialiasedLineEnable = desc.rasterizer.antialiasedLineEnable;

    native.BlendState.AlphaToCoverageEnable = desc.blend.alphaToCoverageEnable;
    native.BlendState.IndependentBlendEnable = desc.blend.independentBlendEnable;
    const size_t attachmentCount = (std::min)(desc.blend.attachments.size(), size_t{8});
    for (size_t i = 0; i < 8; ++i) {
        const RHIBlendAttachmentState state = attachmentCount
            ? desc.blend.attachments[(std::min)(i, attachmentCount - 1)]
            : RHIBlendAttachmentState{};
        auto& blend = native.BlendState.RenderTarget[i];
        blend.BlendEnable = state.blendEnable;
        blend.SrcBlend = ToD3D12Blend(state.srcColorFactor);
        blend.DestBlend = ToD3D12Blend(state.dstColorFactor);
        blend.BlendOp = ToD3D12BlendOp(state.colorOp);
        blend.SrcBlendAlpha = ToD3D12Blend(state.srcAlphaFactor);
        blend.DestBlendAlpha = ToD3D12Blend(state.dstAlphaFactor);
        blend.BlendOpAlpha = ToD3D12BlendOp(state.alphaOp);
        blend.RenderTargetWriteMask = state.colorWriteMask;
    }

    native.DepthStencilState.DepthEnable = desc.depthStencil.depthTestEnable;
    native.DepthStencilState.DepthWriteMask = desc.depthStencil.depthWriteEnable
        ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    native.DepthStencilState.DepthFunc = ToD3D12Compare(desc.depthStencil.depthCompareOp);
    native.DepthStencilState.StencilEnable = desc.depthStencil.stencilEnable;
    native.DepthStencilState.StencilReadMask = desc.depthStencil.stencilReadMask;
    native.DepthStencilState.StencilWriteMask = desc.depthStencil.stencilWriteMask;
    native.DepthStencilState.FrontFace = ToD3D12StencilFace(desc.depthStencil.frontFace);
    native.DepthStencilState.BackFace = ToD3D12StencilFace(desc.depthStencil.backFace);
    native.SampleMask = desc.blend.sampleMask;
    native.PrimitiveTopologyType = ToD3D12TopologyType(desc.topology);
    native.NumRenderTargets = static_cast<UINT>((std::min)(desc.colorFormats.size(), size_t{8}));
    for (UINT i = 0; i < native.NumRenderTargets; ++i)
        native.RTVFormats[i] = ToDxgiRHIFormat(desc.colorFormats[i]);
    native.DSVFormat = desc.depthFormat == RHIFormat::Unknown
        ? DXGI_FORMAT_UNKNOWN : ToDxgiRHIFormat(desc.depthFormat);
    native.SampleDesc.Count = (std::max)(desc.multisample.sampleCount, 1u);
    native.SampleDesc.Quality = desc.multisample.sampleQuality;
    native.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    const HRESULT pipelineHr = m_Device->CreateGraphicsPipelineState(
        &native, IID_PPV_ARGS(&pipeline->pipelineState));
    if (!CheckDeviceResult(pipelineHr, "CreateGraphicsPipelineState(RHI pipeline)")) return nullptr;
    return pipeline;
}

void D3D12Context::ApplyBoundPipelineState()
{
    if (!m_IsRecording || !m_BoundShader || m_DepthOnlyBound) return;

    ID3D12PipelineState* pso = m_BoundShader->pipelineState.Get();
    if (m_Wireframe && m_BoundShader->wireframePipelineState) {
        pso = m_BoundShader->wireframePipelineState.Get();
    } else if (m_CullNone && m_BoundShader->twoSidedPipelineState) {
        pso = m_BoundShader->twoSidedPipelineState.Get();
    } else if (m_BlendMode == GpuBlendMode::Alpha) {
        pso = m_BoundShader->alphaPipelineState.Get();
    }
    m_CommandList->SetPipelineState(pso);
}

void D3D12Context::SetRasterState(bool twoSided, bool wireframe)
{
    m_CullNone = twoSided;
    m_Wireframe = wireframe;
    ApplyBoundPipelineState();
}

void D3D12Context::BindShader(GpuShader* shader) {
    if (!m_IsRecording || !shader) return;
    auto* s = static_cast<D3D12Shader*>(shader);
    m_BoundShader = s;
    m_DepthOnlyBound = false;
    m_CommandList->SetGraphicsRootSignature(s->rootSignature.Get());
    ApplyBoundPipelineState();
}

void D3D12Context::SetBlendMode(GpuBlendMode mode)
{
    m_BlendMode = mode;
    ApplyBoundPipelineState();
}

void D3D12Context::BindVertexBuffer(GpuBuffer* buffer) {
    if (!m_IsRecording || !buffer) return;
    auto* b = static_cast<D3D12VertexBuffer*>(buffer);

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = b->resource->GetGPUVirtualAddress();
    vbv.SizeInBytes = b->byteSize;
    vbv.StrideInBytes = b->stride;
    m_CommandList->IASetVertexBuffers(0, 1, &vbv);
}

void D3D12Context::BindIndexBuffer(GpuBuffer* buffer) {
    if (!m_IsRecording) return;
    if (!buffer) return;

    auto* b = static_cast<D3D12IndexBuffer*>(buffer);
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = b->resource->GetGPUVirtualAddress();
    ibv.SizeInBytes = b->byteSize;
    ibv.Format = b->format;
    m_CommandList->IASetIndexBuffer(&ibv);
}

void D3D12Context::SetVSConstants(const void* data, uint32_t byteSize) {
    if (!m_IsRecording || !data || byteSize == 0) return;

    auto& fr = m_Frames[m_RenderFrameIndex];

    // D3D12 constant buffer view alignment requirement is 256 bytes.
    const uint32_t alignedSize = (byteSize + 255u) & ~255u;
    uint32_t alignedOffset = (fr.constantBufferOffset + 255u) & ~255u;

    if (alignedOffset + alignedSize > fr.constantBufferCapacity) {
        // Grow upload buffer (rare). We only do this while the frame isn't
        // executing yet; BeginFrame already waited for the frame fence.
        uint32_t newCap = fr.constantBufferCapacity;
        while (newCap < alignedOffset + alignedSize) newCap *= 2;

        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = newCap;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> newRes;
        HRESULT hr = m_Device->CreateCommittedResource(
            &props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&newRes));
        if (!CheckDeviceResult(hr, "CreateCommittedResource(grow constant buffer)") || !newRes) {
            if (!newRes) ReportDeviceRemovedReason("Grow constant buffer returned null");
            return;
        }

        uint8_t* mapped = nullptr;
        hr = newRes->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        if (!CheckDeviceResult(hr, "Map(grown constant buffer)") || !mapped) {
            if (!mapped) ReportDeviceRemovedReason("Map(grown constant buffer) returned null");
            return;
        }

        RetireD3D12Object(m_DeferredReleaseQueue, fr.constantBufferUpload);
        fr.constantBufferUpload = newRes;
        fr.constantBufferMapped = mapped;
        fr.constantBufferCapacity = newCap;
    }

    std::memcpy(fr.constantBufferMapped + alignedOffset, data, byteSize);

    const D3D12_GPU_VIRTUAL_ADDRESS addr =
        fr.constantBufferUpload->GetGPUVirtualAddress() + alignedOffset;
    m_CommandList->SetGraphicsRootConstantBufferView(0, addr);

    fr.constantBufferOffset = alignedOffset + alignedSize;
}

void D3D12Context::Draw(uint32_t vertexCount, uint32_t startVertex) {
    if (!m_IsRecording) return;
    m_CommandList->DrawInstanced(vertexCount, 1, startVertex, 0);
}

void D3D12Context::DrawIndexed(uint32_t indexCount, uint32_t startIndex,
                               uint32_t baseVertex) {
    if (!m_IsRecording) return;
    m_CommandList->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
}

void D3D12Context::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                                 uint32_t startVertex) {
    if (!m_IsRecording) return;
    m_CommandList->DrawInstanced(vertexCount, instanceCount, startVertex, 0);
}

void D3D12Context::DrawIndexedInstanced(
    uint32_t indexCount, uint32_t instanceCount,
    uint32_t startIndex, uint32_t baseVertex) {
    if (!m_IsRecording) return;
    m_CommandList->DrawIndexedInstanced(
        indexCount, instanceCount, startIndex, baseVertex, 0);
}

void D3D12Context::DrawIndirect(GpuBuffer* arguments, uint64_t offset, bool indexed) {
    auto* buffer = dynamic_cast<D3D12Buffer*>(arguments);
    ID3D12CommandSignature* signature = indexed ? m_DrawIndexedIndirectSignature.Get()
                                                : m_DrawIndirectSignature.Get();
    if (m_IsRecording && buffer && buffer->resource && signature)
        m_CommandList->ExecuteIndirect(signature, 1, buffer->resource.Get(), offset, nullptr, 0);
}

void D3D12Context::SetViewport(float x, float y, float w, float h) {
    m_Viewport = {};
    m_Viewport.TopLeftX = x;
    m_Viewport.TopLeftY = y;
    m_Viewport.Width = w;
    m_Viewport.Height = h;
    m_Viewport.MinDepth = 0.0f;
    m_Viewport.MaxDepth = 1.0f;
    m_ScissorRect.left = static_cast<LONG>(x);
    m_ScissorRect.top = static_cast<LONG>(y);
    m_ScissorRect.right = static_cast<LONG>(x + w);
    m_ScissorRect.bottom = static_cast<LONG>(y + h);
    m_HasViewport = true;

    if (m_IsRecording && m_CommandList) {
        m_CommandList->RSSetViewports(1, &m_Viewport);
        m_CommandList->RSSetScissorRects(1, &m_ScissorRect);
    }
}

// --------------------------------------------------------------------------
// Texture upload
// --------------------------------------------------------------------------

void D3D12Context::EnsureDefaultResources()
{
    if (m_DefaultTexture || !CanUseDevice("EnsureDefaultResources")) return;

    // --- Default 1x1 black texture ---
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 1;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = m_Device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&m_DefaultTexture));
        if (!CheckDeviceResult(hr, "CreateCommittedResource(default texture)")) return;
    }

    // Default SRV
    m_DefaultTexSrvCpu = AllocSrvSlot(m_DefaultTexSrvGpu, &m_DefaultTexSrvLease);
    if (m_DefaultTexSrvCpu.ptr == 0 || m_DefaultTexSrvGpu.ptr == 0) {
        m_DefaultTexture.Reset();
        return;
    }
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        m_Device->CreateShaderResourceView(m_DefaultTexture.Get(), &srvDesc, m_DefaultTexSrvCpu);
    }

    // Default sampler
    RHISamplerDesc defaultSamplerDesc;
    m_DefaultSampler = std::dynamic_pointer_cast<D3D12Sampler>(CreateSampler(defaultSamplerDesc));
    if (!m_DefaultSampler || m_DefaultSampler->cpu.ptr == 0 || m_DefaultSampler->gpu.ptr == 0) {
        m_DefaultTexture.Reset();
        m_DefaultTexSrvCpu = {};
        m_DefaultTexSrvGpu = {};
        m_DefaultTexSrvLease.reset();
        m_DefaultSampler.reset();
        return;
    }
    m_DefaultSampCpu = m_DefaultSampler->cpu;
    m_DefaultSampGpu = m_DefaultSampler->gpu;
    m_DefaultSampLease = m_DefaultSampler->lease;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocSrvSlot(
    D3D12_GPU_DESCRIPTOR_HANDLE& outGpu,
    std::shared_ptr<D3D12DescriptorLease>* lease)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    if (!m_SrvDescriptorPool ||
        !m_SrvDescriptorPool->Allocate(m_DeferredReleaseQueue, cpu, outGpu, lease)) {
        Logger::Error("[RHI] D3D12 SRV/UAV descriptor heap exhausted");
        outGpu = {};
        return {};
    }
    return cpu;
}

void D3D12Context::PushRenderTargets(uint32_t colorCount,
                                     const D3D12_CPU_DESCRIPTOR_HANDLE* colorRtvs,
                                     D3D12_CPU_DESCRIPTOR_HANDLE depthDsv) {
    if (!m_IsRecording || !m_CommandList) return;
    m_RenderTargetStack.push_back(m_CurrentRenderTarget);
    const auto* depth = depthDsv.ptr ? &depthDsv : nullptr;
    m_CommandList->OMSetRenderTargets(colorCount, colorCount ? colorRtvs : nullptr, FALSE, depth);
    m_CurrentRenderTarget.colorRtv = colorCount ? colorRtvs[0] : D3D12_CPU_DESCRIPTOR_HANDLE{};
    m_CurrentRenderTarget.depthDsv = depthDsv;
    m_CurrentRenderTarget.hasColorTarget = colorCount != 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocRtvSlot(
    std::shared_ptr<D3D12DescriptorLease>* lease)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!m_RtvDescriptorPool ||
        !m_RtvDescriptorPool->Allocate(m_DeferredReleaseQueue, cpu, gpu, lease)) {
        Logger::Error("[RHI] D3D12 RTV descriptor heap exhausted");
        return {};
    }
    return cpu;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocDsvSlot(
    std::shared_ptr<D3D12DescriptorLease>* lease)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!m_DsvDescriptorPool ||
        !m_DsvDescriptorPool->Allocate(m_DeferredReleaseQueue, cpu, gpu, lease)) {
        Logger::Error("[RHI] D3D12 DSV descriptor heap exhausted");
        return {};
    }
    return cpu;
}

void D3D12Context::ResetPostProcessDescriptorAllocators()
{
    if (m_DeferredReleaseQueue && m_Fence) {
        m_DeferredReleaseQueue->Collect(m_Fence->GetCompletedValue());
    }
}

void D3D12Context::BindPSTextureDescriptors(
    uint32_t slot,
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu,
    D3D12_GPU_DESCRIPTOR_HANDLE sampGpu)
{
    if (!m_IsRecording || !m_CommandList) return;
    EnsureDefaultResources();
    if (m_DeviceLost) return;

    const D3D12_GPU_DESCRIPTOR_HANDLE srv =
        srvGpu.ptr ? srvGpu : m_DefaultTexSrvGpu;
    const D3D12_GPU_DESCRIPTOR_HANDLE samp =
        sampGpu.ptr ? sampGpu : m_DefaultSampGpu;

    const uint32_t srvParam  = 1 + slot * 2;
    const uint32_t sampParam = 2 + slot * 2;
    m_CommandList->SetGraphicsRootDescriptorTable(srvParam, srv);
    m_CommandList->SetGraphicsRootDescriptorTable(sampParam, samp);
}

std::shared_ptr<GpuShader> D3D12Context::CreateFullscreenShaderFromBytecode(
    const void* vsBytecode,
    size_t vsSize,
    const void* psBytecode,
    size_t psSize,
    DXGI_FORMAT rtvFormat){
    (void)rtvFormat;
    return CreateShaderFromBytecode(
        vsBytecode, vsSize, psBytecode, psSize, nullptr, 0);
}
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocSampSlot(
    D3D12_GPU_DESCRIPTOR_HANDLE& outGpu,
    std::shared_ptr<D3D12DescriptorLease>* lease)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    if (!m_SamplerDescriptorPool ||
        !m_SamplerDescriptorPool->Allocate(m_DeferredReleaseQueue, cpu, outGpu, lease)) {
        Logger::Error("[RHI] D3D12 sampler descriptor heap exhausted unique=",
                      m_UniqueSamplerDescriptorCount, " capacity=",
                      kDefaultSamplerDescriptorCount);
        outGpu = {};
        return {};
    }
    return cpu;
}

std::shared_ptr<GpuTexture> D3D12Context::UploadTexture2D(
    const void* rgba8Data, int width, int height)
{
    if (!CanUseDevice("UploadTexture2D") || !rgba8Data || width <= 0 || height <= 0)
        return nullptr;

    EnsureDefaultResources();
    if (m_DeviceLost) return nullptr;

    // 1. Create default-heap texture (destination)
    ComPtr<ID3D12Resource> texResource;
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = static_cast<UINT64>(width);
        desc.Height = static_cast<UINT>(height);
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = m_Device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&texResource));
        if (!CheckDeviceResult(hr, "CreateCommittedResource(texture upload destination)"))
            return nullptr;
    }

    // 2. Query required upload buffer size and layout
    D3D12_RESOURCE_DESC texDesc = texResource->GetDesc();
    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    m_Device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

    // 3. Create upload buffer and copy pixel rows
    ComPtr<ID3D12Resource> uploadBuffer;
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = uploadSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_Device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuffer));
        if (!CheckDeviceResult(hr, "CreateCommittedResource(texture upload buffer)"))
            return nullptr;
    }

    {
        void* mapped = nullptr;
        const HRESULT mapHr = uploadBuffer->Map(0, nullptr, &mapped);
        if (!CheckDeviceResult(mapHr, "Map(texture upload buffer)") || !mapped) {
            if (!mapped) ReportDeviceRemovedReason("Map(texture upload buffer) returned null");
            return nullptr;
        }
        const uint8_t* src = static_cast<const uint8_t*>(rgba8Data);
        uint8_t* dst = static_cast<uint8_t*>(mapped);
        const UINT srcRowPitch = static_cast<UINT>(width) * 4;
        for (int row = 0; row < height; ++row) {
            std::memcpy(dst + row * footprint.Footprint.RowPitch,
                        src + row * srcRowPitch,
                        srcRowPitch);
        }
        uploadBuffer->Unmap(0, nullptr);
    }

    // 4. GPU copy: upload buffer → default texture
    {
        if (!CheckDeviceResult(m_UploadCommandAllocator->Reset(),
                               "Reset(texture upload command allocator)")) return nullptr;
        if (!CheckDeviceResult(m_UploadCommandList->Reset(m_UploadCommandAllocator.Get(), nullptr),
                               "Reset(texture upload command list)")) return nullptr;

        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = texResource.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = uploadBuffer.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprint;

        m_UploadCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        // Transition to shader resource
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texResource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_UploadCommandList->ResourceBarrier(1, &barrier);

        if (!CheckDeviceResult(m_UploadCommandList->Close(),
                               "Close(texture upload command list)")) return nullptr;

        ID3D12CommandList* cmdLists[] = { m_UploadCommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, cmdLists);

        // Wait for copy to finish before we let the upload buffer go
        const uint64_t fenceVal = m_NextFenceValue++;
        if (!CheckDeviceResult(m_CommandQueue->Signal(m_Fence.Get(), fenceVal),
                               "Signal(texture upload)")) return nullptr;
        if (m_Fence->GetCompletedValue() < fenceVal) {
            if (!CheckDeviceResult(m_Fence->SetEventOnCompletion(fenceVal, m_FenceEvent),
                                   "SetEventOnCompletion(texture upload)")) return nullptr;
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }
    }

    // 5. Create SRV and sampler in descriptor heaps
    auto tex = std::make_shared<D3D12Texture>();
    tex->deferredReleaseQueue = m_DeferredReleaseQueue;
    tex->resource = texResource;
    tex->desc.width = static_cast<uint32_t>(width);
    tex->desc.height = static_cast<uint32_t>(height);
    tex->desc.format = RHIFormat::RGBA8UNorm;
    tex->desc.usage = RHIResourceUsage::ShaderResource;

    tex->srvCpu = AllocSrvSlot(tex->srvGpu, &tex->srvLease);
    if (tex->srvCpu.ptr == 0 || tex->srvGpu.ptr == 0) return nullptr;
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        m_Device->CreateShaderResourceView(tex->resource.Get(), &srvDesc, tex->srvCpu);
    }

    {
        RHISamplerDesc samplerDesc;
        auto sampler = std::dynamic_pointer_cast<D3D12Sampler>(CreateSampler(samplerDesc));
        if (!sampler) return nullptr;
        tex->sampler = sampler;
        tex->sampCpu = sampler->cpu;
        tex->sampGpu = sampler->gpu;
        tex->sampLease = sampler->lease;
    }

    return tex;
}

bool D3D12Context::UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer,
                                uint64_t offset, const void* data, uint64_t size) {
    auto destination = std::dynamic_pointer_cast<D3D12Buffer>(buffer);
    if (!destination || !destination->resource || !data || !size ||
        offset + size > destination->desc.size || !CanUseDevice("UpdateBuffer")) return false;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (size + 255ull) & ~255ull; desc.Height = 1; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    if (FAILED(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) return false;
    void* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, &mapped)) || !mapped) return false;
    std::memcpy(mapped, data, static_cast<size_t>(size)); upload->Unmap(0, nullptr);
    if (FAILED(m_UploadCommandAllocator->Reset()) ||
        FAILED(m_UploadCommandList->Reset(m_UploadCommandAllocator.Get(), nullptr))) return false;
    m_UploadCommandList->CopyBufferRegion(destination->resource.Get(), offset, upload.Get(), 0, size);
    if (FAILED(m_UploadCommandList->Close())) return false;
    ID3D12CommandList* lists[] = {m_UploadCommandList.Get()};
    m_CommandQueue->ExecuteCommandLists(1, lists);
    const uint64_t fenceValue = m_NextFenceValue++;
    if (FAILED(m_CommandQueue->Signal(m_Fence.Get(), fenceValue))) return false;
    if (m_Fence->GetCompletedValue() < fenceValue) {
        if (FAILED(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent))) return false;
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
    return true;
}

std::shared_ptr<GpuTexture> D3D12Context::UploadTexture(
    const RHITextureDesc& desc, const RHITextureSubresourceData* data, uint32_t count) {
    const uint32_t subresourceCount = desc.mipLevels * desc.arrayLayers;
    if (!data || count != subresourceCount || desc.sampleCount > 1) return nullptr;
    auto texture = std::dynamic_pointer_cast<D3D12Texture>(CreateTexture(desc));
    if (!texture) return nullptr;
    const D3D12_RESOURCE_DESC nativeDesc = texture->resource->GetDesc();
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresourceCount);
    std::vector<UINT> rows(subresourceCount);
    std::vector<UINT64> rowSizes(subresourceCount);
    UINT64 totalSize = 0;
    m_Device->GetCopyableFootprints(&nativeDesc, 0, subresourceCount, 0,
        footprints.data(), rows.data(), rowSizes.data(), &totalSize);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadDesc{}; uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = totalSize; uploadDesc.Height = 1; uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1; uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    if (FAILED(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) return nullptr;
    uint8_t* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped))) || !mapped) return nullptr;
    std::vector<bool> uploaded(subresourceCount, false);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = data[i];
        const uint32_t index = src.mipLevel + src.arrayLayer * desc.mipLevels;
        if (!src.data || index >= subresourceCount || uploaded[index] ||
            src.rowPitch < rowSizes[index]) {
            upload->Unmap(0, nullptr); return nullptr;
        }
        uploaded[index] = true;
        uint8_t* dstSlice = mapped + footprints[index].Offset;
        const uint8_t* srcSlice = static_cast<const uint8_t*>(src.data);
        for (UINT row = 0; row < rows[index]; ++row)
            std::memcpy(dstSlice + static_cast<size_t>(row) * footprints[index].Footprint.RowPitch,
                        srcSlice + static_cast<size_t>(row) * src.rowPitch,
                        static_cast<size_t>(rowSizes[index]));
    }
    upload->Unmap(0, nullptr);
    if (FAILED(m_UploadCommandAllocator->Reset()) ||
        FAILED(m_UploadCommandList->Reset(m_UploadCommandAllocator.Get(), nullptr))) return nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t index = data[i].mipLevel + data[i].arrayLayer * desc.mipLevels;
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = texture->resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = index;
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprints[index];
        m_UploadCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture->resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = HasUsage(desc.usage, RHIResourceUsage::ShaderResource)
        ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_UploadCommandList->ResourceBarrier(1, &barrier);
    if (FAILED(m_UploadCommandList->Close())) return nullptr;
    ID3D12CommandList* lists[] = {m_UploadCommandList.Get()}; m_CommandQueue->ExecuteCommandLists(1, lists);
    const uint64_t fenceValue = m_NextFenceValue++;
    if (FAILED(m_CommandQueue->Signal(m_Fence.Get(), fenceValue))) return nullptr;
    if (m_Fence->GetCompletedValue() < fenceValue) {
        if (FAILED(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent))) return nullptr;
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
    return texture;
}

RHIDeviceCapabilities D3D12Context::GetCapabilities() const {
    RHIDeviceCapabilities result;
    result.maxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    result.maxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
    result.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    result.maxSamples = D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT;
    result.maxBindlessResources = kDefaultSrvDescriptorCount;
    result.timestampQueries = true; result.indirectDraw = true;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (m_Device && SUCCEEDED(m_Device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
        result.bindlessResources = options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2;
    }
    return result;
}

bool D3D12Context::IsFormatSupported(RHIFormat format, RHIResourceUsage usage) const {
    if (!m_Device || format == RHIFormat::Unknown) return false;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = ToDxgiRHIFormat(format);
    if (FAILED(m_Device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) return false;
    if (HasUsage(usage, RHIResourceUsage::ShaderResource) &&
        !(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)) return false;
    if (HasUsage(usage, RHIResourceUsage::RenderTarget) &&
        !(support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET)) return false;
    if (HasUsage(usage, RHIResourceUsage::DepthStencil) &&
        !(support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)) return false;
    if (HasUsage(usage, RHIResourceUsage::UnorderedAccess) &&
        !(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) return false;
    return true;
}

std::shared_ptr<GpuFence> D3D12Context::CreateFence(uint64_t initialValue) {
    if (!m_Device) return nullptr;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(m_Device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&fence)))) return nullptr;
    return std::make_shared<D3D12FenceRHI>(std::move(fence));
}

std::shared_ptr<GpuTimestampQueryPool> D3D12Context::CreateTimestampQueryPool(uint32_t count) {
    if (!m_Device || !m_CommandQueue || count == 0) return nullptr;
    auto pool = std::make_shared<D3D12TimestampPool>(); pool->count = count;
    D3D12_QUERY_HEAP_DESC query{}; query.Count = count; query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    if (FAILED(m_Device->CreateQueryHeap(&query, IID_PPV_ARGS(&pool->heap)))) return nullptr;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<uint64_t>(count) * sizeof(uint64_t); desc.Height = 1;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pool->readback)))) return nullptr;
    m_CommandQueue->GetTimestampFrequency(&pool->frequency);
    return pool;
}

void D3D12Context::BindPSTexture(uint32_t slot, GpuTexture* tex)
{
    if (!m_IsRecording || !m_CommandList) return;
    EnsureDefaultResources();
    if (m_DeviceLost) return;

    auto* d3dTex = static_cast<D3D12Texture*>(tex);
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = d3dTex ? d3dTex->srvGpu : m_DefaultTexSrvGpu;
    const D3D12_GPU_DESCRIPTOR_HANDLE sampGpu = d3dTex ? d3dTex->sampGpu : m_DefaultSampGpu;

    // Root signature layout: [0]=CBV, [1]=SRV(t0), [2]=Samp(s0), [3]=SRV(t1), [4]=Samp(s1)
    const uint32_t srvParam  = 1 + slot * 2;
    const uint32_t sampParam = 2 + slot * 2;

    m_CommandList->SetGraphicsRootDescriptorTable(srvParam, srvGpu);
    m_CommandList->SetGraphicsRootDescriptorTable(sampParam, sampGpu);
}

bool CreateComputeRootSignature(ID3D12Device* device, ID3D12RootSignature** outRootSig)
{
    if (!device || !outRootSig) return false;
    constexpr uint32_t textureSlots = D3D12Context::kTextureSlotCount;
    constexpr uint32_t storageSlots = 8;
    D3D12_DESCRIPTOR_RANGE ranges[textureSlots * 2 + storageSlots] = {};
    D3D12_ROOT_PARAMETER params[1 + textureSlots * 2 + storageSlots] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    for (uint32_t slot = 0; slot < textureSlots; ++slot) {
        auto& srv = ranges[slot * 2]; srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv.NumDescriptors = 1; srv.BaseShaderRegister = slot;
        auto& sampler = ranges[slot * 2 + 1]; sampler.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampler.NumDescriptors = 1; sampler.BaseShaderRegister = slot;
    }
    for (uint32_t slot = 0; slot < storageSlots; ++slot) {
        auto& uav = ranges[textureSlots * 2 + slot]; uav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uav.NumDescriptors = 1; uav.BaseShaderRegister = slot;
    }
    for (uint32_t i = 0; i < textureSlots * 2 + storageSlots; ++i) {
        params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i + 1].DescriptorTable.NumDescriptorRanges = 1;
        params[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters = _countof(params);
    desc.pParameters = params;
    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)))
        return false;
    return SUCCEEDED(device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(outRootSig)));
}

std::shared_ptr<GpuTexture> D3D12Context::CreateTexture(const RHITextureDesc& desc) {
    if (!CanUseDevice("CreateTexture") || desc.width == 0 || desc.height == 0) return nullptr;
    if (desc.sampleCount > 1 && desc.mipLevels != 1) return nullptr;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa{};
    msaa.Format = ToDxgiRHIFormat(desc.format);
    msaa.SampleCount = (std::max)(desc.sampleCount, 1u);
    if (FAILED(m_Device->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaa, sizeof(msaa))) ||
        msaa.NumQualityLevels == 0 || desc.sampleQuality >= msaa.NumQualityLevels)
        return nullptr;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC native{};
    native.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    native.Width = desc.width; native.Height = desc.height;
    native.DepthOrArraySize = static_cast<UINT16>(desc.arrayLayers);
    native.MipLevels = static_cast<UINT16>(desc.mipLevels);
    native.Format = ToDxgiRHIFormat(desc.format, true);
    native.SampleDesc.Count = (std::max)(desc.sampleCount, 1u);
    native.SampleDesc.Quality = desc.sampleQuality;
    native.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (HasUsage(desc.usage, RHIResourceUsage::RenderTarget)) native.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (HasUsage(desc.usage, RHIResourceUsage::DepthStencil)) native.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess)) native.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_CLEAR_VALUE clear{}; clear.Format = ToDxgiRHIFormat(desc.format);
    if (HasUsage(desc.usage, RHIResourceUsage::DepthStencil)) clear.DepthStencil.Depth = 1.0f;
    auto result = std::make_shared<D3D12Texture>(); result->desc = desc;
    result->deferredReleaseQueue = m_DeferredReleaseQueue;
    result->arraySize = desc.arrayLayers; result->isCube = desc.cube;
    const D3D12_CLEAR_VALUE* clearPtr = (native.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) ? &clear : nullptr;
    const HRESULT textureHr = m_Device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &native, D3D12_RESOURCE_STATE_COMMON,
        clearPtr, IID_PPV_ARGS(&result->resource));
    if (!CheckDeviceResult(textureHr, "CreateCommittedResource(texture)")) return nullptr;
    if (HasUsage(desc.usage, RHIResourceUsage::ShaderResource)) {
        RHITextureViewDesc viewDesc; viewDesc.mipCount = desc.mipLevels;
        viewDesc.layerCount = desc.arrayLayers; viewDesc.usage = RHIResourceUsage::ShaderResource;
        auto view = std::dynamic_pointer_cast<D3D12TextureView>(CreateTextureView(result, viewDesc));
        if (!view) return nullptr;
        result->srvCpu = view->srvCpu; result->srvGpu = view->srvGpu;
        result->srvLease = std::move(view->srvLease);
        RHISamplerDesc samplerDesc;
        samplerDesc.filter = HasUsage(desc.usage, RHIResourceUsage::DepthStencil)
            ? RHIFilter::ComparisonLinear : RHIFilter::Linear;
        samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = RHIAddressMode::Clamp;
        auto sampler = std::dynamic_pointer_cast<D3D12Sampler>(CreateSampler(samplerDesc));
        if (!sampler) return nullptr;
        result->sampler = sampler;
        result->sampCpu = sampler->cpu; result->sampGpu = sampler->gpu;
        result->sampLease = sampler->lease;
    }
    return result;
}

std::shared_ptr<GpuTextureView> D3D12Context::CreateTextureView(
    const std::shared_ptr<GpuTexture>& texture, const RHITextureViewDesc& desc) {
    if (!CanUseDevice("CreateTextureView")) return nullptr;
    auto nativeTexture = std::dynamic_pointer_cast<D3D12Texture>(texture);
    if (!nativeTexture || !nativeTexture->resource) return nullptr;
    auto view = std::make_shared<D3D12TextureView>(); view->texture = texture; view->desc = desc;
    const RHITextureDesc& td = nativeTexture->desc;
    if (HasUsage(desc.usage, RHIResourceUsage::ShaderResource)) {
        view->srvCpu = AllocSrvSlot(view->srvGpu, &view->srvLease);
        if (view->srvCpu.ptr == 0 || view->srvGpu.ptr == 0) return nullptr;
        const UINT64 heapBase = m_SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
        view->bindlessIndex = static_cast<uint32_t>(
            (view->srvGpu.ptr - heapBase) / m_SrvDescriptorSize);
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};
        d.Format = td.format == RHIFormat::D24S8 ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : td.format == RHIFormat::D32Float ? DXGI_FORMAT_R32_FLOAT : ToDxgiRHIFormat(td.format);
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (td.sampleCount > 1 && td.arrayLayers > 1) { d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY; d.Texture2DMSArray.FirstArraySlice = desc.firstLayer; d.Texture2DMSArray.ArraySize = desc.layerCount; }
        else if (td.sampleCount > 1) { d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS; }
        else if (td.cube) { d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; d.TextureCube.MostDetailedMip = desc.firstMip; d.TextureCube.MipLevels = desc.mipCount; }
        else if (td.arrayLayers > 1) { d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.MostDetailedMip = desc.firstMip; d.Texture2DArray.MipLevels = desc.mipCount; d.Texture2DArray.FirstArraySlice = desc.firstLayer; d.Texture2DArray.ArraySize = desc.layerCount; }
        else { d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MostDetailedMip = desc.firstMip; d.Texture2D.MipLevels = desc.mipCount; }
        m_Device->CreateShaderResourceView(nativeTexture->resource.Get(), &d, view->srvCpu);
    }
    if (HasUsage(desc.usage, RHIResourceUsage::RenderTarget)) {
        view->rtvCpu = AllocRtvSlot(&view->rtvLease);
        if (view->rtvCpu.ptr == 0) return nullptr;
        D3D12_RENDER_TARGET_VIEW_DESC d{}; d.Format = ToDxgiRHIFormat(td.format);
        if (td.sampleCount > 1 && td.arrayLayers > 1) { d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY; d.Texture2DMSArray.FirstArraySlice = desc.firstLayer; d.Texture2DMSArray.ArraySize = desc.layerCount; }
        else if (td.sampleCount > 1) { d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS; }
        else if (td.arrayLayers > 1) { d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.MipSlice = desc.firstMip; d.Texture2DArray.FirstArraySlice = desc.firstLayer; d.Texture2DArray.ArraySize = desc.layerCount; }
        else { d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; d.Texture2D.MipSlice = desc.firstMip; }
        m_Device->CreateRenderTargetView(nativeTexture->resource.Get(), &d, view->rtvCpu);
    }
    if (HasUsage(desc.usage, RHIResourceUsage::DepthStencil)) {
        view->dsvCpu = AllocDsvSlot(&view->dsvLease);
        if (view->dsvCpu.ptr == 0) return nullptr;
        D3D12_DEPTH_STENCIL_VIEW_DESC d{}; d.Format = ToDxgiRHIFormat(td.format);
        if (td.sampleCount > 1 && td.arrayLayers > 1) { d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY; d.Texture2DMSArray.FirstArraySlice = desc.firstLayer; d.Texture2DMSArray.ArraySize = desc.layerCount; }
        else if (td.sampleCount > 1) { d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS; }
        else if (td.arrayLayers > 1) { d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.MipSlice = desc.firstMip; d.Texture2DArray.FirstArraySlice = desc.firstLayer; d.Texture2DArray.ArraySize = desc.layerCount; }
        else { d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; d.Texture2D.MipSlice = desc.firstMip; }
        m_Device->CreateDepthStencilView(nativeTexture->resource.Get(), &d, view->dsvCpu);
    }
    if (HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess)) {
        if (td.sampleCount > 1) return nullptr;
        view->uavCpu = AllocSrvSlot(view->uavGpu, &view->uavLease);
        if (view->uavCpu.ptr == 0 || view->uavGpu.ptr == 0) return nullptr;
        D3D12_UNORDERED_ACCESS_VIEW_DESC d{}; d.Format = ToDxgiRHIFormat(td.format); d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; d.Texture2D.MipSlice = desc.firstMip;
        m_Device->CreateUnorderedAccessView(nativeTexture->resource.Get(), nullptr, &d, view->uavCpu);
    }
    return view;
}

std::shared_ptr<GpuSampler> D3D12Context::CreateSampler(const RHISamplerDesc& desc) {
    if (!CanUseDevice("CreateSampler")) return nullptr;
    const SamplerCacheKey key{desc};
    auto cached = m_SamplerCache.find(key);
    if (cached != m_SamplerCache.end()) {
        if (auto sampler = cached->second.lock()) {
            return sampler;
        }
        m_SamplerCache.erase(cached);
    }

    auto result = std::make_shared<D3D12Sampler>(); result->desc = desc;
    result->cpu = AllocSampSlot(result->gpu, &result->lease);
    if (result->cpu.ptr == 0 || result->gpu.ptr == 0) {
        Logger::Error("[RHI] D3D12 failed to allocate sampler descriptor desc=(",
                      SamplerFilterName(desc.filter), ",",
                      SamplerAddressName(desc.addressU), ",",
                      SamplerAddressName(desc.addressV), ",",
                      SamplerAddressName(desc.addressW), ") unique=",
                      m_UniqueSamplerDescriptorCount, " capacity=",
                      kDefaultSamplerDescriptorCount);
        return nullptr;
    }
    D3D12_SAMPLER_DESC d{};
    d.Filter = desc.filter == RHIFilter::Point ? D3D12_FILTER_MIN_MAG_MIP_POINT : desc.filter == RHIFilter::ComparisonLinear ? D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    auto address = [](RHIAddressMode m) { return m == RHIAddressMode::Clamp ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : m == RHIAddressMode::Border ? D3D12_TEXTURE_ADDRESS_MODE_BORDER : D3D12_TEXTURE_ADDRESS_MODE_WRAP; };
    d.AddressU = address(desc.addressU); d.AddressV = address(desc.addressV); d.AddressW = address(desc.addressW);
    d.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; d.MaxLOD = D3D12_FLOAT32_MAX;
    m_Device->CreateSampler(&d, result->cpu);
    ++m_UniqueSamplerDescriptorCount;
    m_SamplerCache[key] = result;
    return result;
}

std::shared_ptr<GpuReadbackTicket> D3D12Context::ReadbackBufferAsync(
    const std::shared_ptr<GpuBuffer>& buffer) {
    if (!CanUseDevice("ReadbackBufferAsync")) return nullptr;
    auto source = std::dynamic_pointer_cast<D3D12Buffer>(buffer);
    if (!source || !source->resource || source->desc.size == 0 || !m_IsRecording) return nullptr;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = (static_cast<uint64_t>(source->desc.size) + 255ull) & ~255ull;
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    const HRESULT readbackHr = m_Device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&readback));
    if (!CheckDeviceResult(readbackHr, "CreateCommittedResource(readback buffer)")) return nullptr;
    m_CommandList->CopyBufferRegion(readback.Get(), 0, source->resource.Get(), 0, source->desc.size);
    return std::make_shared<D3D12ReadbackTicket>(
        std::move(readback), m_Fence, m_NextFenceValue, source->desc.size,
        m_DeferredReleaseQueue);
}

std::shared_ptr<GpuTextureReadbackTicket> D3D12Context::ReadbackTextureAsync(
    const std::shared_ptr<GpuTexture>& texture, const RHITextureRegion& requested) {
    auto source = std::dynamic_pointer_cast<D3D12Texture>(texture);
    if (!source || !source->resource || source->desc.sampleCount > 1 || !m_IsRecording)
        return nullptr;
    const uint32_t mipWidth = (std::max)(1u, source->desc.width >> requested.mipLevel);
    const uint32_t mipHeight = (std::max)(1u, source->desc.height >> requested.mipLevel);
    const uint32_t width = requested.width ? requested.width : mipWidth - requested.x;
    const uint32_t height = requested.height ? requested.height : mipHeight - requested.y;
    const uint32_t bpp = RHIFormatByteSize12(source->desc.format);
    if (!bpp || requested.x + width > mipWidth || requested.y + height > mipHeight ||
        requested.mipLevel >= source->desc.mipLevels || requested.arrayLayer >= source->desc.arrayLayers)
        return nullptr;
    D3D12_RESOURCE_DESC regionDesc = source->resource->GetDesc();
    regionDesc.Width = width; regionDesc.Height = height; regionDesc.DepthOrArraySize = 1;
    regionDesc.MipLevels = 1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows = 0; UINT64 rowSize = 0, total = 0;
    m_Device->GetCopyableFootprints(&regionDesc, 0, 1, 0, &footprint, &rows, &rowSize, &total);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc{}; readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = total; readbackDesc.Height = 1; readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1; readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    if (FAILED(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return nullptr;
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = source->resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = requested.mipLevel + requested.arrayLayer * source->desc.mipLevels;
    D3D12_BOX box{requested.x, requested.y, requested.z,
                  requested.x + width, requested.y + height, requested.z + 1};
    m_CommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    return std::make_shared<D3D12TextureReadbackTicket>(std::move(readback), m_Fence,
        m_NextFenceValue, width, height, width * bpp, footprint.Footprint.RowPitch,
        source->desc.format, m_DeferredReleaseQueue);
}
