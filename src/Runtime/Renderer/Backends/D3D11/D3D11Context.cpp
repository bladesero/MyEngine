#include "D3D11Context.h"
#include "Core/Logger.h"
#include "Core/Window.h"
#include "Renderer/RHI/ShaderReflection.h"
#include "Renderer/RHI/RHIResourceStats.h"

#include <d3dcompiler.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// --------------------------------------------------------------------------
// Factory
// --------------------------------------------------------------------------
std::unique_ptr<IRenderContext> CreateD3D11Context() {
    return std::make_unique<D3D11Context>();
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
static std::string WideToUtf8(const wchar_t* value) {
    if (!value || !*value)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

static RHIDeviceIdentity DescribeAdapter(IDXGIAdapter* adapter) {
    RHIDeviceIdentity identity;
    if (!adapter)
        return identity;
    ComPtr<IDXGIAdapter1> adapter1;
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter1))) && SUCCEEDED(adapter1->GetDesc1(&desc))) {
        identity.adapterName = WideToUtf8(desc.Description);
        identity.vendorId = desc.VendorId;
        identity.deviceId = desc.DeviceId;
        identity.subsystemId = desc.SubSysId;
        identity.revision = desc.Revision;
        identity.dedicatedVideoMemoryBytes = static_cast<uint64_t>(desc.DedicatedVideoMemory);
        identity.softwareAdapter = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    }
    LARGE_INTEGER version{};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
        std::ostringstream text;
        text << HIWORD(version.HighPart) << '.' << LOWORD(version.HighPart) << '.' << HIWORD(version.LowPart) << '.'
             << LOWORD(version.LowPart);
        identity.driverVersion = text.str();
    }
    return identity;
}

static DXGI_FORMAT ToDxgiFormat(VertexFormat fmt) {
    switch (fmt) {
    case VertexFormat::Float2:
        return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static DXGI_FORMAT ToDxgiFormat(RHIFormat fmt, bool resource = false) {
    switch (fmt) {
    case RHIFormat::R8UInt:
        return DXGI_FORMAT_R8_UINT;
    case RHIFormat::RGBA8UNorm:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::BGRA8UNorm:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case RHIFormat::RGBA8UNormSrgb:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHIFormat::RG16Float:
        return DXGI_FORMAT_R16G16_FLOAT;
    case RHIFormat::RGBA16Float:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case RHIFormat::R8UNorm:
        return DXGI_FORMAT_R8_UNORM;
    case RHIFormat::R16UInt:
        return DXGI_FORMAT_R16_UINT;
    case RHIFormat::R32UInt:
        return DXGI_FORMAT_R32_UINT;
    case RHIFormat::R32Float:
        return DXGI_FORMAT_R32_FLOAT;
    case RHIFormat::RG32Float:
        return DXGI_FORMAT_R32G32_FLOAT;
    case RHIFormat::RGB32Float:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHIFormat::RGBA32Float:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHIFormat::BC1UNorm:
        return DXGI_FORMAT_BC1_UNORM;
    case RHIFormat::BC3UNorm:
        return DXGI_FORMAT_BC3_UNORM;
    case RHIFormat::D24S8:
        return resource ? DXGI_FORMAT_R24G8_TYPELESS : DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::D32Float:
        return resource ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_D32_FLOAT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

static uint32_t RHIFormatByteSize(RHIFormat format) {
    switch (format) {
    case RHIFormat::R8UInt:
    case RHIFormat::R8UNorm:
        return 1;
    case RHIFormat::R16UInt:
        return 2;
    case RHIFormat::RG16Float:
    case RHIFormat::R32UInt:
    case RHIFormat::R32Float:
    case RHIFormat::RGBA8UNorm:
    case RHIFormat::BGRA8UNorm:
    case RHIFormat::RGBA8UNormSrgb:
    case RHIFormat::D24S8:
    case RHIFormat::D32Float:
        return 4;
    case RHIFormat::RG32Float:
    case RHIFormat::RGBA16Float:
        return 8;
    case RHIFormat::RGB32Float:
        return 12;
    case RHIFormat::RGBA32Float:
        return 16;
    case RHIFormat::BC1UNorm:
    case RHIFormat::BC3UNorm:
        return 0;
    default:
        return 0;
    }
}

static D3D11_PRIMITIVE_TOPOLOGY ToD3D11Topology(RHIPrimitiveTopology topology) {
    switch (topology) {
    case RHIPrimitiveTopology::PointList:
        return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    case RHIPrimitiveTopology::LineList:
        return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case RHIPrimitiveTopology::LineStrip:
        return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case RHIPrimitiveTopology::TriangleStrip:
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default:
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

static D3D11_COMPARISON_FUNC ToD3D11Compare(RHICompareOp op) {
    switch (op) {
    case RHICompareOp::Never:
        return D3D11_COMPARISON_NEVER;
    case RHICompareOp::Less:
        return D3D11_COMPARISON_LESS;
    case RHICompareOp::Equal:
        return D3D11_COMPARISON_EQUAL;
    case RHICompareOp::LessEqual:
        return D3D11_COMPARISON_LESS_EQUAL;
    case RHICompareOp::Greater:
        return D3D11_COMPARISON_GREATER;
    case RHICompareOp::NotEqual:
        return D3D11_COMPARISON_NOT_EQUAL;
    case RHICompareOp::GreaterEqual:
        return D3D11_COMPARISON_GREATER_EQUAL;
    default:
        return D3D11_COMPARISON_ALWAYS;
    }
}

static D3D11_STENCIL_OP ToD3D11StencilOp(RHIStencilOp op) {
    switch (op) {
    case RHIStencilOp::Zero:
        return D3D11_STENCIL_OP_ZERO;
    case RHIStencilOp::Replace:
        return D3D11_STENCIL_OP_REPLACE;
    case RHIStencilOp::IncrementClamp:
        return D3D11_STENCIL_OP_INCR_SAT;
    case RHIStencilOp::DecrementClamp:
        return D3D11_STENCIL_OP_DECR_SAT;
    case RHIStencilOp::Invert:
        return D3D11_STENCIL_OP_INVERT;
    case RHIStencilOp::IncrementWrap:
        return D3D11_STENCIL_OP_INCR;
    case RHIStencilOp::DecrementWrap:
        return D3D11_STENCIL_OP_DECR;
    default:
        return D3D11_STENCIL_OP_KEEP;
    }
}

static D3D11_BLEND ToD3D11Blend(RHIBlendFactor factor) {
    switch (factor) {
    case RHIBlendFactor::Zero:
        return D3D11_BLEND_ZERO;
    case RHIBlendFactor::One:
        return D3D11_BLEND_ONE;
    case RHIBlendFactor::SrcColor:
        return D3D11_BLEND_SRC_COLOR;
    case RHIBlendFactor::OneMinusSrcColor:
        return D3D11_BLEND_INV_SRC_COLOR;
    case RHIBlendFactor::DstColor:
        return D3D11_BLEND_DEST_COLOR;
    case RHIBlendFactor::OneMinusDstColor:
        return D3D11_BLEND_INV_DEST_COLOR;
    case RHIBlendFactor::SrcAlpha:
        return D3D11_BLEND_SRC_ALPHA;
    case RHIBlendFactor::OneMinusSrcAlpha:
        return D3D11_BLEND_INV_SRC_ALPHA;
    case RHIBlendFactor::DstAlpha:
        return D3D11_BLEND_DEST_ALPHA;
    case RHIBlendFactor::OneMinusDstAlpha:
        return D3D11_BLEND_INV_DEST_ALPHA;
    case RHIBlendFactor::ConstantColor:
        return D3D11_BLEND_BLEND_FACTOR;
    case RHIBlendFactor::OneMinusConstantColor:
        return D3D11_BLEND_INV_BLEND_FACTOR;
    case RHIBlendFactor::SrcAlphaSaturate:
        return D3D11_BLEND_SRC_ALPHA_SAT;
    default:
        return D3D11_BLEND_ONE;
    }
}

static D3D11_BLEND_OP ToD3D11BlendOp(RHIBlendOp op) {
    switch (op) {
    case RHIBlendOp::Subtract:
        return D3D11_BLEND_OP_SUBTRACT;
    case RHIBlendOp::ReverseSubtract:
        return D3D11_BLEND_OP_REV_SUBTRACT;
    case RHIBlendOp::Min:
        return D3D11_BLEND_OP_MIN;
    case RHIBlendOp::Max:
        return D3D11_BLEND_OP_MAX;
    default:
        return D3D11_BLEND_OP_ADD;
    }
}

static D3D11_DEPTH_STENCILOP_DESC ToD3D11StencilFace(const RHIStencilFaceState& face) {
    D3D11_DEPTH_STENCILOP_DESC result{};
    result.StencilFailOp = ToD3D11StencilOp(face.failOp);
    result.StencilDepthFailOp = ToD3D11StencilOp(face.depthFailOp);
    result.StencilPassOp = ToD3D11StencilOp(face.passOp);
    result.StencilFunc = ToD3D11Compare(face.compareOp);
    return result;
}

class D3D11FenceRHI final : public GpuFence {
public:
    D3D11FenceRHI(ID3D11Device* device, ID3D11DeviceContext* context, uint64_t initial)
        : m_Device(device), m_Context(context), m_Completed(initial) {}
    bool Signal(uint64_t value) {
        if (!m_Device || !m_Context)
            return false;
        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_EVENT;
        ComPtr<ID3D11Query> query;
        if (FAILED(m_Device->CreateQuery(&desc, &query)))
            return false;
        m_Context->End(query.Get());
        m_Context->Flush();
        m_Query = std::move(query);
        m_Pending = value;
        return true;
    }
    uint64_t GetCompletedValue() const override {
        if (m_Query && m_Context->GetData(m_Query.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            m_Completed = (std::max)(m_Completed, m_Pending);
            m_Query.Reset();
        }
        return m_Completed;
    }
    bool Wait(uint64_t value, uint32_t timeoutMs) override {
        const ULONGLONG start = GetTickCount64();
        while (GetCompletedValue() < value) {
            if (timeoutMs != UINT32_MAX && GetTickCount64() - start >= timeoutMs)
                return false;
            Sleep(0);
        }
        return true;
    }

private:
    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    mutable ComPtr<ID3D11Query> m_Query;
    mutable uint64_t m_Completed = 0, m_Pending = 0;
};

class D3D11QueueRHI final : public GpuQueue {
public:
    explicit D3D11QueueRHI(ID3D11DeviceContext* context) : m_Context(context) {}
    bool Submit(GpuCommandList*, GpuFence* fence, uint64_t value) override {
        auto* native = dynamic_cast<D3D11FenceRHI*>(fence);
        return native && native->Signal(value);
    }
    bool Wait(GpuFence* fence, uint64_t value) override { return fence && fence->Wait(value); }

private:
    ID3D11DeviceContext* m_Context = nullptr;
};

class D3D11TimestampPool final : public GpuTimestampQueryPool {
public:
    uint32_t GetCount() const override { return static_cast<uint32_t>(queries.size()); }
    uint64_t GetFrequency() const override { return frequency; }
    bool ReadResults(uint32_t first, uint32_t count, std::vector<uint64_t>& ticks) override {
        if (!context || first + count > queries.size())
            return false;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};
        if (context->GetData(disjoint.Get(), &timing, sizeof(timing), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            timing.Disjoint)
            return false;
        frequency = timing.Frequency;
        ticks.resize(count);
        for (uint32_t i = 0; i < count; ++i)
            if (context->GetData(queries[first + i].Get(), &ticks[i], sizeof(uint64_t),
                                 D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                return false;
        return true;
    }
    ID3D11DeviceContext* context = nullptr;
    ComPtr<ID3D11Query> disjoint;
    std::vector<ComPtr<ID3D11Query>> queries;
    mutable uint64_t frequency = 0;
    bool active = false;
};

class D3D11ImmediateCommandList final : public GpuCommandList {
public:
    explicit D3D11ImmediateCommandList(D3D11Context& owner) : m_Owner(owner) {}

    void BindShader(GpuShader* shader) override { m_Owner.BindShader(shader); }

    void BindVertexBuffer(GpuBuffer* buffer) override { m_Owner.BindVertexBuffer(buffer); }

    void BindIndexBuffer(GpuBuffer* buffer) override { m_Owner.BindIndexBuffer(buffer); }

    void SetVSConstants(const void* data, uint32_t byteSize) override { m_Owner.SetVSConstants(data, byteSize); }

    void Draw(uint32_t vertexCount, uint32_t startVertex) override { m_Owner.Draw(vertexCount, startVertex); }

    void DrawIndexed(uint32_t indexCount, uint32_t startIndex, uint32_t baseVertex) override {
        m_Owner.DrawIndexed(indexCount, startIndex, baseVertex);
    }

    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex) override {
        m_Owner.DrawInstanced(vertexCount, instanceCount, startVertex);
    }

    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex,
                              uint32_t baseVertex) override {
        m_Owner.DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex);
    }

    void SetViewport(float x, float y, float w, float h) override { m_Owner.SetViewport(x, y, w, h); }

    void BindPSTexture(uint32_t slot, GpuTexture* tex) override { m_Owner.BindPSTexture(slot, tex); }

    void SetBlendMode(GpuBlendMode mode) override { m_Owner.SetBlendMode(mode); }

    void SetRasterState(bool twoSided, bool wireframe) override { m_Owner.SetRasterState(twoSided, wireframe); }
    void SetGraphicsPipeline(GpuGraphicsPipeline* pipeline) override {
        auto* native = dynamic_cast<D3D11GraphicsPipeline*>(pipeline);
        if (!native)
            return;
        m_Owner.BindShader(native->desc.shader.get());
        auto* context = m_Owner.GetDeviceContext();
        context->IASetPrimitiveTopology(ToD3D11Topology(native->desc.topology));
        context->OMSetBlendState(native->blendState.Get(), native->desc.blend.blendConstants,
                                 native->desc.blend.sampleMask);
        context->RSSetState(native->rasterizerState.Get());
        context->OMSetDepthStencilState(native->depthStencilState.Get(), native->desc.depthStencil.stencilReference);
    }
    void SetComputePipeline(GpuComputePipeline* pipeline) override {
        auto* shader =
            pipeline && pipeline->desc.shader ? dynamic_cast<D3D11Shader*>(pipeline->desc.shader.get()) : nullptr;
        m_Owner.GetDeviceContext()->CSSetShader(shader ? shader->cs.Get() : nullptr, nullptr, 0);
    }
    void SetBindGroup(uint32_t, GpuBindGroup* group) override {
        if (!group || !group->GetShader())
            return;
        const auto& reflection = group->GetShader()->reflection;
        for (const auto& value : group->GetConstants()) {
            const auto* binding = reflection.Find(value.first);
            if (binding)
                m_Owner.SetVSConstants(value.second.data(), static_cast<uint32_t>(value.second.size()));
        }
        for (const auto& value : group->GetTextures()) {
            const auto* binding = reflection.Find(value.first);
            auto* view = dynamic_cast<D3D11TextureView*>(value.second.get());
            if (!binding || !view)
                continue;
            ID3D11ShaderResourceView* srv = view->srv.Get();
            if (binding->stages & ShaderStageVertex)
                m_Owner.GetDeviceContext()->VSSetShaderResources(binding->bindPoint, 1, &srv);
            if (binding->stages & ShaderStagePixel)
                m_Owner.GetDeviceContext()->PSSetShaderResources(binding->bindPoint, 1, &srv);
            if (binding->stages & ShaderStageCompute)
                m_Owner.GetDeviceContext()->CSSetShaderResources(binding->bindPoint, 1, &srv);
        }
        for (const auto& value : group->GetSamplers()) {
            const auto* binding = reflection.Find(value.first);
            auto* sampler = dynamic_cast<D3D11Sampler*>(value.second.get());
            if (!binding || !sampler)
                continue;
            ID3D11SamplerState* state = sampler->sampler.Get();
            if (binding->stages & ShaderStageVertex)
                m_Owner.GetDeviceContext()->VSSetSamplers(binding->bindPoint, 1, &state);
            if (binding->stages & ShaderStagePixel)
                m_Owner.GetDeviceContext()->PSSetSamplers(binding->bindPoint, 1, &state);
            if (binding->stages & ShaderStageCompute)
                m_Owner.GetDeviceContext()->CSSetSamplers(binding->bindPoint, 1, &state);
        }
        for (const auto& value : group->GetBuffers()) {
            const auto* binding = reflection.Find(value.first);
            auto* view = dynamic_cast<D3D11BufferView*>(value.second.get());
            if (!binding || !view || !view->srv)
                continue;
            ID3D11ShaderResourceView* srv = view->srv.Get();
            if (binding->stages & ShaderStageVertex)
                m_Owner.GetDeviceContext()->VSSetShaderResources(binding->bindPoint, 1, &srv);
            if (binding->stages & ShaderStagePixel)
                m_Owner.GetDeviceContext()->PSSetShaderResources(binding->bindPoint, 1, &srv);
            if (binding->stages & ShaderStageCompute)
                m_Owner.GetDeviceContext()->CSSetShaderResources(binding->bindPoint, 1, &srv);
        }
        for (const auto& value : group->GetStorageBuffers()) {
            const auto* binding = reflection.Find(value.first);
            auto* view = dynamic_cast<D3D11BufferView*>(value.second.get());
            if (!binding || !view)
                continue;
            if (view->uav) {
                ID3D11UnorderedAccessView* uav = view->uav.Get();
                UINT count = 0;
                m_Owner.GetDeviceContext()->CSSetUnorderedAccessViews(binding->bindPoint, 1, &uav, &count);
            } else if (view->srv) {
                ID3D11ShaderResourceView* srv = view->srv.Get();
                if (binding->stages & ShaderStageVertex)
                    m_Owner.GetDeviceContext()->VSSetShaderResources(binding->bindPoint, 1, &srv);
                if (binding->stages & ShaderStagePixel)
                    m_Owner.GetDeviceContext()->PSSetShaderResources(binding->bindPoint, 1, &srv);
                if (binding->stages & ShaderStageCompute)
                    m_Owner.GetDeviceContext()->CSSetShaderResources(binding->bindPoint, 1, &srv);
            }
        }
        for (const auto& value : group->GetStorageTextures()) {
            const auto* binding = reflection.Find(value.first);
            auto* view = dynamic_cast<D3D11TextureView*>(value.second.get());
            if (!binding || !view || !view->uav)
                continue;
            ID3D11UnorderedAccessView* uav = view->uav.Get();
            UINT count = 0;
            m_Owner.GetDeviceContext()->CSSetUnorderedAccessViews(binding->bindPoint, 1, &uav, &count);
        }
    }
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override { m_Owner.GetDeviceContext()->Dispatch(x, y, z); }
    void DispatchIndirect(GpuBuffer* args, uint64_t offset) override {
        auto* native = dynamic_cast<D3D11Buffer*>(args);
        if (native && offset <= UINT32_MAX)
            m_Owner.GetDeviceContext()->DispatchIndirect(native->buffer.Get(), static_cast<UINT>(offset));
    }
    void CopyBuffer(GpuBuffer* dst, uint32_t dstOffset, GpuBuffer* src, uint32_t srcOffset,
                    uint32_t byteSize) override {
        auto* d = dynamic_cast<D3D11Buffer*>(dst);
        auto* s = dynamic_cast<D3D11Buffer*>(src);
        if (!d || !s)
            return;
        D3D11_BOX box{srcOffset, 0, 0, srcOffset + byteSize, 1, 1};
        m_Owner.GetDeviceContext()->CopySubresourceRegion(d->buffer.Get(), 0, dstOffset, 0, 0, s->buffer.Get(), 0,
                                                          &box);
    }
    void CopyTexture(GpuTexture* dst, GpuTexture* src) override {
        auto* d = dynamic_cast<D3D11Texture*>(dst);
        auto* s = dynamic_cast<D3D11Texture*>(src);
        if (d && s)
            m_Owner.GetDeviceContext()->CopyResource(d->texture.Get(), s->texture.Get());
    }
    void CopyTexture(GpuTexture* dst, const RHITextureRegion& dr, GpuTexture* src,
                     const RHITextureRegion& sr) override {
        auto* d = dynamic_cast<D3D11Texture*>(dst);
        auto* s = dynamic_cast<D3D11Texture*>(src);
        if (!d || !s)
            return;
        const uint32_t width = sr.width ? sr.width : (std::max)(1u, s->desc.width >> sr.mipLevel) - sr.x;
        const uint32_t height = sr.height ? sr.height : (std::max)(1u, s->desc.height >> sr.mipLevel) - sr.y;
        D3D11_BOX box{sr.x, sr.y, sr.z, sr.x + width, sr.y + height, sr.z + (std::max)(1u, sr.depth)};
        m_Owner.GetDeviceContext()->CopySubresourceRegion(
            d->texture.Get(), D3D11CalcSubresource(dr.mipLevel, dr.arrayLayer, d->desc.mipLevels), dr.x, dr.y, dr.z,
            s->texture.Get(), D3D11CalcSubresource(sr.mipLevel, sr.arrayLayer, s->desc.mipLevels), &box);
    }
    void DrawIndirect(GpuBuffer* args, uint64_t offset) override {
        auto* native = dynamic_cast<D3D11Buffer*>(args);
        if (native && offset <= UINT32_MAX)
            m_Owner.GetDeviceContext()->DrawInstancedIndirect(native->buffer.Get(), static_cast<UINT>(offset));
    }
    void DrawIndexedIndirect(GpuBuffer* args, uint64_t offset) override {
        auto* native = dynamic_cast<D3D11Buffer*>(args);
        if (native && offset <= UINT32_MAX)
            m_Owner.GetDeviceContext()->DrawIndexedInstancedIndirect(native->buffer.Get(), static_cast<UINT>(offset));
    }
    void WriteTimestamp(GpuTimestampQueryPool* pool, uint32_t index) override {
        auto* native = dynamic_cast<D3D11TimestampPool*>(pool);
        if (!native || index >= native->queries.size())
            return;
        if (!native->active) {
            m_Owner.GetDeviceContext()->Begin(native->disjoint.Get());
            native->active = true;
        }
        m_Owner.GetDeviceContext()->End(native->queries[index].Get());
    }
    void ResolveTimestamps(GpuTimestampQueryPool* pool, uint32_t, uint32_t) override {
        auto* native = dynamic_cast<D3D11TimestampPool*>(pool);
        if (native && native->active) {
            m_Owner.GetDeviceContext()->End(native->disjoint.Get());
            native->active = false;
        }
    }

    void Transition(GpuResource*, RHIResourceState, RHIResourceState after) override {
        // D3D11 has implicit states, but resources must be unbound before they
        // change from shader input to an output attachment.
        if (after == RHIResourceState::RenderTarget || after == RHIResourceState::DepthWrite ||
            after == RHIResourceState::UnorderedAccess) {
            ID3D11ShaderResourceView* nullViews[16] = {};
            m_Owner.GetDeviceContext()->VSSetShaderResources(0, 16, nullViews);
            m_Owner.GetDeviceContext()->PSSetShaderResources(0, 16, nullViews);
            m_Owner.GetDeviceContext()->CSSetShaderResources(0, 16, nullViews);
        }
        if (after == RHIResourceState::ShaderResource) {
            ID3D11UnorderedAccessView* nullUavs[8] = {};
            UINT counts[8] = {};
            m_Owner.GetDeviceContext()->CSSetUnorderedAccessViews(0, 8, nullUavs, counts);
            m_Owner.GetDeviceContext()->OMSetRenderTargets(0, nullptr, nullptr);
        }
    }

    void BeginRendering(const RenderingInfo& info) override {
        m_StoreDiscard.clear();
        ComPtr<ID3D11DeviceContext1> context1;
        m_Owner.GetDeviceContext()->QueryInterface(IID_PPV_ARGS(&context1));
        ID3D11RenderTargetView* rtvs[8] = {};
        const uint32_t count = info.colorCount > 8 ? 8 : info.colorCount;
        for (uint32_t i = 0; i < count; ++i) {
            auto* view = dynamic_cast<D3D11TextureView*>(info.colors[i].view);
            rtvs[i] = view ? view->rtv.Get() : nullptr;
            if (context1 && rtvs[i] && info.colors[i].loadOp == RHILoadOp::Discard)
                context1->DiscardView(rtvs[i]);
            if (rtvs[i] && info.colors[i].storeOp == RHIStoreOp::Discard)
                m_StoreDiscard.push_back(rtvs[i]);
            if (rtvs[i] && info.colors[i].loadOp == RHILoadOp::Clear) {
                const auto& c = info.colors[i].clearColor;
                const float clear[4] = {c.r, c.g, c.b, c.a};
                m_Owner.GetDeviceContext()->ClearRenderTargetView(rtvs[i], clear);
            }
        }
        ID3D11DepthStencilView* dsv = nullptr;
        if (info.depth) {
            auto* view = dynamic_cast<D3D11TextureView*>(info.depth->view);
            dsv = view ? view->dsv.Get() : nullptr;
            if (context1 && dsv && info.depth->loadOp == RHILoadOp::Discard)
                context1->DiscardView(dsv);
            if (dsv && info.depth->storeOp == RHIStoreOp::Discard)
                m_StoreDiscard.push_back(dsv);
            if (dsv && info.depth->loadOp == RHILoadOp::Clear)
                m_Owner.GetDeviceContext()->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                                                  info.depth->clearDepth, info.depth->clearStencil);
        }
        m_Owner.GetDeviceContext()->OMSetRenderTargets(count, rtvs, dsv);
        m_Owner.SetViewport(0, 0, static_cast<float>(info.width), static_cast<float>(info.height));
    }

    void EndRendering() override {
        ComPtr<ID3D11DeviceContext1> context1;
        m_Owner.GetDeviceContext()->QueryInterface(IID_PPV_ARGS(&context1));
        if (context1)
            for (auto* view : m_StoreDiscard)
                context1->DiscardView(view);
        m_StoreDiscard.clear();
        m_Owner.GetDeviceContext()->OMSetRenderTargets(0, nullptr, nullptr);
    }

private:
    D3D11Context& m_Owner;
    std::vector<ID3D11View*> m_StoreDiscard;
};

class D3D11ReadbackTicket final : public GpuReadbackTicket {
public:
    D3D11ReadbackTicket(ID3D11DeviceContext* context, ComPtr<ID3D11Buffer> staging, ComPtr<ID3D11Query> query,
                        uint32_t size)
        : m_Context(context), m_Staging(std::move(staging)), m_Query(std::move(query)), m_Size(size) {}
    bool IsReady() const override {
        return m_Context && m_Query &&
               m_Context->GetData(m_Query.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
    }
    bool Read(std::vector<uint8_t>& data) override {
        if (!IsReady())
            return false;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_Context->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        data.resize(m_Size);
        std::memcpy(data.data(), mapped.pData, m_Size);
        m_Context->Unmap(m_Staging.Get(), 0);
        return true;
    }
    uint32_t GetSize() const override { return m_Size; }

private:
    ID3D11DeviceContext* m_Context = nullptr;
    ComPtr<ID3D11Buffer> m_Staging;
    ComPtr<ID3D11Query> m_Query;
    uint32_t m_Size = 0;
};

class D3D11TextureReadbackTicket final : public GpuTextureReadbackTicket {
public:
    D3D11TextureReadbackTicket(ID3D11DeviceContext* context, ComPtr<ID3D11Texture2D> staging, ComPtr<ID3D11Query> query,
                               uint32_t width, uint32_t height, uint32_t rowSize, RHIFormat format)
        : m_Context(context), m_Staging(std::move(staging)), m_Query(std::move(query)), m_Width(width),
          m_Height(height), m_RowSize(rowSize), m_Format(format) {}
    bool IsReady() const override {
        return m_Context && m_Query &&
               m_Context->GetData(m_Query.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
    }
    bool Read(std::vector<uint8_t>& data) override {
        if (!IsReady())
            return false;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_Context->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        data.resize(static_cast<size_t>(m_RowSize) * m_Height);
        for (uint32_t y = 0; y < m_Height; ++y)
            std::memcpy(data.data() + static_cast<size_t>(y) * m_RowSize,
                        static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                        m_RowSize);
        m_Context->Unmap(m_Staging.Get(), 0);
        return true;
    }
    uint32_t GetSize() const override { return m_RowSize * m_Height; }
    uint32_t GetRowPitch() const override { return m_RowSize; }
    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    RHIFormat GetFormat() const override { return m_Format; }

private:
    ID3D11DeviceContext* m_Context = nullptr;
    ComPtr<ID3D11Texture2D> m_Staging;
    ComPtr<ID3D11Query> m_Query;
    uint32_t m_Width = 0, m_Height = 0, m_RowSize = 0;
    RHIFormat m_Format = RHIFormat::Unknown;
};

class D3D11SwapChain final : public GpuSwapChain {
public:
    explicit D3D11SwapChain(D3D11Context& owner) : m_Owner(owner) {}

    void Present(bool vsync) override { m_Owner.PresentSwapChain(vsync); }

    bool Resize(uint32_t width, uint32_t height) override { return m_Owner.ResizeSwapChain(width, height); }

    uint32_t GetWidth() const override { return m_Owner.m_SwapChainWidth; }

    uint32_t GetHeight() const override { return m_Owner.m_SwapChainHeight; }

private:
    D3D11Context& m_Owner;
};

// --------------------------------------------------------------------------
// D3D11Context
// --------------------------------------------------------------------------

D3D11Context::~D3D11Context() {
    Shutdown();
}

D3D11Context::D3D11Context()
    : m_SwapChainInterface(std::make_unique<D3D11SwapChain>(*this)),
      m_GraphicsCommandList(std::make_unique<D3D11ImmediateCommandList>(*this)) {
}

bool D3D11Context::Init(IWindow* window) {
    HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
    if (!hwnd) {
        Logger::Error("D3D11Context::Init – invalid HWND");
        return false;
    }

    const int w = (std::max)(1, window->GetPixelWidth());
    const int h = (std::max)(1, window->GetPixelHeight());
    m_SwapChainWidth = static_cast<uint32_t>(w);
    m_SwapChainHeight = static_cast<uint32_t>(h);

    // ---- SwapChain + Device ------------------------------------------------
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = static_cast<UINT>(w);
    scd.BufferDesc.Height = static_cast<UINT>(h);
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    // Prefer 11_1 so vs_5_1 / newer DXBC from dxc still binds; fall back is implicit.
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
                                               (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &scd,
                                               &m_SwapChain, &m_Device, &featureLevel, &m_Context);

    if (FAILED(hr)) {
        Logger::Error("D3D11CreateDeviceAndSwapChain failed: 0x", reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(m_Device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
        m_DeviceIdentity = DescribeAdapter(adapter.Get());
    }

    // ---- Render target + depth buffer --------------------------------------
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
        return CheckDeviceResult(hr, "D3D11 GetBuffer");
    hr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RTV);
    if (FAILED(hr))
        return CheckDeviceResult(hr, "D3D11 CreateRenderTargetView");
    {
        auto texture = std::make_shared<D3D11Texture>();
        texture->texture = backBuffer;
        texture->desc.width = static_cast<uint32_t>(w);
        texture->desc.height = static_cast<uint32_t>(h);
        texture->desc.format = RHIFormat::RGBA8UNorm;
        texture->desc.usage = RHIResourceUsage::RenderTarget;
        m_BackBufferView = std::make_shared<D3D11TextureView>();
        m_BackBufferView->texture = texture;
        m_BackBufferView->rtv = m_RTV;
        m_BackBufferView->desc.usage = RHIResourceUsage::RenderTarget;
    }

    // Create depth stencil buffer
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = static_cast<UINT>(w);
    depthDesc.Height = static_cast<UINT>(h);
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = m_Device->CreateTexture2D(&depthDesc, nullptr, &m_Depth);
    if (FAILED(hr))
        return CheckDeviceResult(hr, "D3D11 CreateTexture2D(depth)");
    hr = m_Device->CreateDepthStencilView(m_Depth.Get(), nullptr, &m_DSV);
    if (FAILED(hr))
        return CheckDeviceResult(hr, "D3D11 CreateDepthStencilView");

    m_Context->OMSetRenderTargets(1, m_RTV.GetAddressOf(), m_DSV.Get());

    // ---- Default viewport --------------------------------------------------
    SetViewport(0, 0, static_cast<float>(w), static_cast<float>(h));
    m_GraphicsQueue = std::make_shared<D3D11QueueRHI>(m_Context.Get());

    ++m_DeviceGeneration;
    m_DeviceLossInfo = {};
    Logger::Info("D3D11Context initialised (", w, "x", h, ") generation=", m_DeviceGeneration);
    return true;
}

void D3D11Context::Shutdown() {
    m_DeviceIdentity = {};
    if (m_Context) {
        m_Context->ClearState();
    }
    m_RasterWireCullNone.Reset();
    m_RasterWireCullBack.Reset();
    m_RasterSolidCullNone.Reset();
    m_RasterSolidCullBack.Reset();
    m_AlphaBlendState.Reset();
    m_OpaqueBlendState.Reset();
    m_DSV.Reset();
    m_Depth.Reset();
    m_CBuffer.Reset();
    m_RTV.Reset();
    m_SwapChain.Reset();
    m_GraphicsQueue.reset();
    m_Context.Reset();
    m_Device.Reset();
    m_SwapChainWidth = 0;
    m_SwapChainHeight = 0;
    m_DeviceLost = false;
    m_LastDeviceError.clear();
    m_DeviceLossInfo = {};
}

void D3D11Context::BeginFrame(float r, float g, float b, float a) {
    if (!m_Context || !m_RTV) {
        return;
    }
    const float color[4] = {r, g, b, a};
    m_Context->ClearRenderTargetView(m_RTV.Get(), color);
    if (m_DSV) {
        m_Context->ClearDepthStencilView(m_DSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        m_Context->OMSetRenderTargets(1, m_RTV.GetAddressOf(), m_DSV.Get());
    } else {
        m_Context->OMSetRenderTargets(1, m_RTV.GetAddressOf(), nullptr);
    }
    // Secondary ImGui platform windows share the immediate context. Restore
    // the main swapchain rect before any main-window rendering starts.
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_SwapChainWidth);
    viewport.Height = static_cast<float>(m_SwapChainHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_Context->RSSetViewports(1, &viewport);
    const D3D11_RECT scissor = {0, 0, static_cast<LONG>(m_SwapChainWidth), static_cast<LONG>(m_SwapChainHeight)};
    m_Context->RSSetScissorRects(1, &scissor);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void D3D11Context::EndFrame() {
    PresentSwapChain(m_VSyncEnabled);
}

GpuSwapChain* D3D11Context::GetSwapChain() {
    return m_SwapChainInterface.get();
}

GpuCommandList* D3D11Context::GetGraphicsCommandList() {
    return m_GraphicsCommandList.get();
}

ImGuiBackendHandles D3D11Context::GetImGuiBackendHandles() {
    ImGuiBackendHandles h;
    h.backend = RHIBackend::D3D11;
    h.device = m_Device.Get();
    h.deviceContext = m_Context.Get();
    h.backBufferRtvPtr = m_RTV.Get();
    h.backBufferDsvPtr = m_DSV.Get();
    h.width = m_SwapChainWidth;
    h.height = m_SwapChainHeight;
    return h;
}

void D3D11Context::PresentSwapChain(bool vsync) {
    if (!m_SwapChain)
        return;
    CheckDeviceResult(m_SwapChain->Present(vsync ? 1 : 0, 0), "D3D11 Present");
}

bool D3D11Context::ResizeSwapChain(uint32_t width, uint32_t height) {
    if (!m_Device || !m_Context || !m_SwapChain)
        return false;
    if (width == 0 || height == 0)
        return false;

    m_Context->OMSetRenderTargets(0, nullptr, nullptr);
    m_BackBufferView.reset();
    m_RTV.Reset();
    m_DSV.Reset();
    m_Depth.Reset();

    const HRESULT hr = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        Logger::Error("D3D11 ResizeBuffers failed: 0x", reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT createHr = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(createHr))
        return CheckDeviceResult(createHr, "D3D11 GetBuffer after resize");
    createHr = m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RTV);
    if (FAILED(createHr))
        return CheckDeviceResult(createHr, "D3D11 CreateRenderTargetView after resize");
    {
        auto texture = std::make_shared<D3D11Texture>();
        texture->texture = backBuffer;
        texture->desc.width = width;
        texture->desc.height = height;
        texture->desc.format = RHIFormat::RGBA8UNorm;
        texture->desc.usage = RHIResourceUsage::RenderTarget;
        m_BackBufferView = std::make_shared<D3D11TextureView>();
        m_BackBufferView->texture = texture;
        m_BackBufferView->rtv = m_RTV;
        m_BackBufferView->desc.usage = RHIResourceUsage::RenderTarget;
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = static_cast<UINT>(width);
    depthDesc.Height = static_cast<UINT>(height);
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    createHr = m_Device->CreateTexture2D(&depthDesc, nullptr, &m_Depth);
    if (FAILED(createHr))
        return CheckDeviceResult(createHr, "D3D11 CreateTexture2D(depth) after resize");
    createHr = m_Device->CreateDepthStencilView(m_Depth.Get(), nullptr, &m_DSV);
    if (FAILED(createHr))
        return CheckDeviceResult(createHr, "D3D11 CreateDepthStencilView after resize");

    m_Context->OMSetRenderTargets(1, m_RTV.GetAddressOf(), m_DSV.Get());
    SetViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

    m_SwapChainWidth = width;
    m_SwapChainHeight = height;
    if (m_ResizeCallback)
        m_ResizeCallback();
    return true;
}

bool D3D11Context::CheckDeviceResult(HRESULT hr, const char* operation) {
    if (SUCCEEDED(hr))
        return true;
    std::ostringstream stream;
    stream << operation << " failed: 0x" << std::hex << static_cast<unsigned long>(hr);
    m_LastDeviceError = stream.str();
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
        m_DeviceLost = true;
        RHIDeviceLossReason reason = RHIDeviceLossReason::Unknown;
        if (hr == DXGI_ERROR_DEVICE_REMOVED)
            reason = RHIDeviceLossReason::Removed;
        else if (hr == DXGI_ERROR_DEVICE_RESET)
            reason = RHIDeviceLossReason::Reset;
        else if (hr == DXGI_ERROR_DEVICE_HUNG)
            reason = RHIDeviceLossReason::Hung;
        m_DeviceLossInfo = {reason, static_cast<int64_t>(hr), m_DeviceGeneration, m_LastDeviceError};
    }
    Logger::Error(m_LastDeviceError);
    return false;
}

// --------------------------------------------------------------------------
// Resource creation
// --------------------------------------------------------------------------

std::shared_ptr<GpuBuffer> D3D11Context::CreateVertexBuffer(const void* data, uint32_t byteSize, uint32_t strideBytes) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = byteSize;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = data;

    auto buf = std::make_shared<D3D11Buffer>();
    buf->desc.size = byteSize;
    buf->desc.stride = strideBytes;
    buf->desc.usage = RHIResourceUsage::VertexBuffer;
    buf->stride = strideBytes;
    HRESULT hr = m_Device->CreateBuffer(&bd, &sd, &buf->buffer);
    if (FAILED(hr)) {
        Logger::Error("CreateVertexBuffer failed");
        return nullptr;
    }
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(buf));
    return buf;
}

std::shared_ptr<GpuBuffer> D3D11Context::CreateIndexBuffer(const void* data, uint32_t byteSize) {
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = byteSize;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = data;

    auto buf = std::make_shared<D3D11IndexBuffer>();
    buf->desc.size = byteSize;
    buf->desc.usage = RHIResourceUsage::IndexBuffer;
    buf->format = DXGI_FORMAT_R32_UINT;
    HRESULT hr = m_Device->CreateBuffer(&bd, &sd, &buf->buffer);
    if (FAILED(hr)) {
        Logger::Error("CreateIndexBuffer failed");
        return nullptr;
    }
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(buf));
    return buf;
}

std::shared_ptr<GpuShader> D3D11Context::CreateShader(const std::string& hlslSource, const std::string& vsEntry,
                                                      const std::string& psEntry, const VertexElement* layout,
                                                      uint32_t layoutCount) {
    auto sh = std::make_shared<D3D11Shader>();

    auto compileShader = [&](const std::string& entry, const std::string& target, ComPtr<ID3DBlob>& outBlob) -> bool {
        ComPtr<ID3DBlob> errBlob;
        HRESULT hr = D3DCompile(hlslSource.c_str(), hlslSource.size(), nullptr, nullptr, nullptr, entry.c_str(),
                                target.c_str(), D3DCOMPILE_ENABLE_STRICTNESS, 0, &outBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) {
                Logger::Error("Shader compile error: ", static_cast<const char*>(errBlob->GetBufferPointer()));
            }
            return false;
        }
        return true;
    };

    ComPtr<ID3DBlob> vsBlob, psBlob;
    if (!compileShader(vsEntry, "vs_5_0", vsBlob))
        return nullptr;
    if (!compileShader(psEntry, "ps_5_0", psBlob))
        return nullptr;

    HRESULT hr = m_Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &sh->vs);
    if (FAILED(hr)) {
        Logger::Error("D3D11 CreateVertexShader (D3DCompile) failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }
    hr = m_Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &sh->ps);
    if (FAILED(hr)) {
        Logger::Error("D3D11 CreatePixelShader (D3DCompile) failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }

    std::vector<D3D11_INPUT_ELEMENT_DESC> descs(layoutCount);
    for (uint32_t i = 0; i < layoutCount; ++i) {
        descs[i] = {layout[i].semantic,
                    layout[i].index,
                    ToDxgiFormat(layout[i].format),
                    0,
                    layout[i].offset,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0};
    }
    if (layoutCount > 0) {
        hr = m_Device->CreateInputLayout(descs.data(), layoutCount, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                         &sh->inputLayout);
        if (FAILED(hr)) {
            Logger::Error("D3D11 CreateInputLayout (D3DCompile) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return nullptr;
        }
    }

    ReflectDxbcProgram(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), psBlob->GetBufferPointer(),
                       psBlob->GetBufferSize(), sh->reflection);
    return sh;
}

std::shared_ptr<GpuShader> D3D11Context::CreateShaderFromBytecode(const void* vsBytecode, size_t vsSize,
                                                                  const void* psBytecode, size_t psSize,
                                                                  const VertexElement* layout, uint32_t layoutCount) {
    if (!vsBytecode || vsSize == 0 || !psBytecode || psSize == 0 || (layoutCount > 0 && !layout)) {
        return nullptr;
    }

    auto sh = std::make_shared<D3D11Shader>();
    sh->vertexBytecode.assign(static_cast<const uint8_t*>(vsBytecode),
                              static_cast<const uint8_t*>(vsBytecode) + vsSize);
    sh->pixelBytecode.assign(static_cast<const uint8_t*>(psBytecode), static_cast<const uint8_t*>(psBytecode) + psSize);

    HRESULT hr = m_Device->CreateVertexShader(vsBytecode, vsSize, nullptr, &sh->vs);
    if (FAILED(hr)) {
        Logger::Error("D3D11 CreateVertexShader failed: 0x", reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }
    hr = m_Device->CreatePixelShader(psBytecode, psSize, nullptr, &sh->ps);
    if (FAILED(hr)) {
        Logger::Error("D3D11 CreatePixelShader failed: 0x", reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }

    std::vector<D3D11_INPUT_ELEMENT_DESC> descs(layoutCount);
    for (uint32_t i = 0; i < layoutCount; ++i) {
        descs[i] = {layout[i].semantic,
                    layout[i].index,
                    ToDxgiFormat(layout[i].format),
                    0,
                    layout[i].offset,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0};
    }
    if (layoutCount > 0) {
        hr = m_Device->CreateInputLayout(descs.data(), layoutCount, vsBytecode, vsSize, &sh->inputLayout);
        if (FAILED(hr)) {
            Logger::Error("D3D11 CreateInputLayout failed (VS signature must match VertexElement layout): 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return nullptr;
        }
    }

    ReflectDxbcProgram(vsBytecode, vsSize, psBytecode, psSize, sh->reflection);
    return sh;
}

std::shared_ptr<GpuShader> D3D11Context::CreateComputeShaderFromBytecode(const void* bytecode, size_t byteSize) {
    if (!m_Device || !bytecode || byteSize == 0)
        return nullptr;
    auto shader = std::make_shared<D3D11Shader>();
    shader->computeBytecode.assign(static_cast<const uint8_t*>(bytecode),
                                   static_cast<const uint8_t*>(bytecode) + byteSize);
    if (FAILED(m_Device->CreateComputeShader(bytecode, byteSize, nullptr, &shader->cs)))
        return nullptr;
    std::string error;
    if (!ReflectDxbcStage(bytecode, byteSize, ShaderStageCompute, shader->reflection, &error))
        Logger::Warn("[RHI] D3D11 compute reflection failed: ", error);
    return shader;
}

// --------------------------------------------------------------------------
// Per-draw constant buffer helper
// --------------------------------------------------------------------------
void D3D11Context::CreateConstantBuffer(uint32_t byteSize) {
    // Round up to 16-byte boundary.
    byteSize = (byteSize + 15u) & ~15u;
    if (m_CBuffer && m_CBufferSize >= byteSize)
        return;

    m_CBuffer.Reset();
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = byteSize;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_Device->CreateBuffer(&bd, nullptr, &m_CBuffer);
    m_CBufferSize = byteSize;
}

// --------------------------------------------------------------------------
// Binding / draw
// --------------------------------------------------------------------------
void D3D11Context::BindShader(GpuShader* shader) {
    auto* s = static_cast<D3D11Shader*>(shader);
    m_Context->VSSetShader(s->vs.Get(), nullptr, 0);
    m_Context->PSSetShader(s->ps.Get(), nullptr, 0);
    m_Context->IASetInputLayout(s->inputLayout.Get());
}

void D3D11Context::BindVertexBuffer(GpuBuffer* buffer) {
    if (!buffer) {
        ID3D11Buffer* nullBuffer = nullptr;
        const UINT stride = 0;
        const UINT offset = 0;
        m_Context->IASetVertexBuffers(0, 1, &nullBuffer, &stride, &offset);
        return;
    }
    auto* b = static_cast<D3D11Buffer*>(buffer);
    UINT offset = 0;
    m_Context->IASetVertexBuffers(0, 1, b->buffer.GetAddressOf(), &b->stride, &offset);
}

void D3D11Context::BindIndexBuffer(GpuBuffer* buffer) {
    if (!buffer) {
        m_Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
        return;
    }
    auto* b = static_cast<D3D11IndexBuffer*>(buffer);
    m_Context->IASetIndexBuffer(b->buffer.Get(), b->format, 0);
}

void D3D11Context::SetVSConstants(const void* data, uint32_t byteSize) {
    CreateConstantBuffer(byteSize);

    D3D11_MAPPED_SUBRESOURCE ms = {};
    m_Context->Map(m_CBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    memcpy(ms.pData, data, byteSize);
    m_Context->Unmap(m_CBuffer.Get(), 0);
    m_Context->VSSetConstantBuffers(0, 1, m_CBuffer.GetAddressOf());
    m_Context->PSSetConstantBuffers(0, 1, m_CBuffer.GetAddressOf());
}

void D3D11Context::Draw(uint32_t vertexCount, uint32_t startVertex) {
    m_Context->Draw(vertexCount, startVertex);
}

void D3D11Context::DrawIndexed(uint32_t indexCount, uint32_t startIndex, uint32_t baseVertex) {
    m_Context->DrawIndexed(indexCount, startIndex, baseVertex);
}

void D3D11Context::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex) {
    m_Context->DrawInstanced(vertexCount, instanceCount, startVertex, 0);
}

void D3D11Context::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex,
                                        uint32_t baseVertex) {
    m_Context->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, 0);
}

void D3D11Context::SetViewport(float x, float y, float w, float h) {
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = x;
    vp.TopLeftY = y;
    vp.Width = w;
    vp.Height = h;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_Context->RSSetViewports(1, &vp);
}

// --------------------------------------------------------------------------
// Texture upload / bind
// --------------------------------------------------------------------------

std::shared_ptr<GpuTexture> D3D11Context::UploadTexture2D(const void* rgba8Data, int width, int height) {
    if (!rgba8Data || width <= 0 || height <= 0)
        return nullptr;

    auto tex = std::make_shared<D3D11Texture>();
    tex->desc.width = static_cast<uint32_t>(width);
    tex->desc.height = static_cast<uint32_t>(height);
    tex->desc.format = RHIFormat::RGBA8UNorm;
    tex->desc.usage = RHIResourceUsage::ShaderResource;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = rgba8Data;
    sd.SysMemPitch = static_cast<UINT>(width * 4);

    HRESULT hr = m_Device->CreateTexture2D(&desc, &sd, &tex->texture);
    if (FAILED(hr)) {
        Logger::Error("D3D11: UploadTexture2D – CreateTexture2D failed");
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_Device->CreateShaderResourceView(tex->texture.Get(), &srvDesc, &tex->srv);
    if (FAILED(hr)) {
        Logger::Error("D3D11: UploadTexture2D – CreateShaderResourceView failed");
        return nullptr;
    }

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    hr = m_Device->CreateSamplerState(&sampDesc, &tex->sampler);
    if (FAILED(hr)) {
        Logger::Error("D3D11: UploadTexture2D – CreateSamplerState failed");
        return nullptr;
    }

    CommitRHIResourceAccounting(std::static_pointer_cast<GpuTexture>(tex));
    return tex;
}

bool D3D11Context::UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer, uint64_t offset, const void* data,
                                uint64_t size) {
    auto native = std::dynamic_pointer_cast<D3D11Buffer>(buffer);
    if (!native || !native->buffer || !data || size == 0 || offset + size > native->desc.size ||
        offset + size > UINT32_MAX)
        return false;
    if (!native->updateShadow.empty()) {
        std::memcpy(native->updateShadow.data() + offset, data, static_cast<size_t>(size));
        m_Context->UpdateSubresource(native->buffer.Get(), 0, nullptr, native->updateShadow.data(), 0, 0);
    } else {
        D3D11_BOX box{static_cast<UINT>(offset), 0, 0, static_cast<UINT>(offset + size), 1, 1};
        m_Context->UpdateSubresource(native->buffer.Get(), 0, &box, data, 0, 0);
    }
    return true;
}

std::shared_ptr<GpuTexture> D3D11Context::UploadTexture(const RHITextureDesc& desc,
                                                        const RHITextureSubresourceData* data, uint32_t count) {
    if (!data || count == 0 || desc.sampleCount > 1)
        return nullptr;
    auto texture = std::dynamic_pointer_cast<D3D11Texture>(CreateTexture(desc));
    if (!texture)
        return nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = data[i];
        if (!src.data || src.mipLevel >= desc.mipLevels || src.arrayLayer >= desc.arrayLayers)
            return nullptr;
        const UINT subresource = D3D11CalcSubresource(src.mipLevel, src.arrayLayer, desc.mipLevels);
        m_Context->UpdateSubresource(texture->texture.Get(), subresource, nullptr, src.data, src.rowPitch,
                                     src.slicePitch);
    }
    return texture;
}

RHIDeviceCapabilities D3D11Context::GetCapabilities() const {
    RHIDeviceCapabilities result;
    result.maxTextureDimension2D = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    result.maxTextureArrayLayers = D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
    result.maxColorAttachments = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
    result.maxSamples = D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT;
    result.computeShaders = true;
    result.storageTextures = true;
    result.timestampQueries = true;
    result.indirectDraw = true;
    result.indirectDispatch = true;
    result.shaderDrawParameters = true;
    return result;
}

bool D3D11Context::IsFormatSupported(RHIFormat format, RHIResourceUsage usage) const {
    if (!m_Device || format == RHIFormat::Unknown)
        return false;
    UINT support = 0;
    if (FAILED(m_Device->CheckFormatSupport(ToDxgiFormat(format), &support)))
        return false;
    if (HasUsage(usage, RHIResourceUsage::ShaderResource) && !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        return false;
    if (HasUsage(usage, RHIResourceUsage::RenderTarget) && !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET))
        return false;
    if (HasUsage(usage, RHIResourceUsage::DepthStencil) && !(support & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL))
        return false;
    if (HasUsage(usage, RHIResourceUsage::UnorderedAccess) &&
        !(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))
        return false;
    return true;
}

std::shared_ptr<GpuFence> D3D11Context::CreateFence(uint64_t initialValue) {
    if (!m_Device || !m_Context)
        return nullptr;
    return std::make_shared<D3D11FenceRHI>(m_Device.Get(), m_Context.Get(), initialValue);
}

std::shared_ptr<GpuTimestampQueryPool> D3D11Context::CreateTimestampQueryPool(uint32_t count) {
    if (!m_Device || !m_Context || count == 0)
        return nullptr;
    auto pool = std::make_shared<D3D11TimestampPool>();
    pool->context = m_Context.Get();
    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    if (FAILED(m_Device->CreateQuery(&desc, &pool->disjoint)))
        return nullptr;
    desc.Query = D3D11_QUERY_TIMESTAMP;
    pool->queries.resize(count);
    for (auto& query : pool->queries)
        if (FAILED(m_Device->CreateQuery(&desc, &query)))
            return nullptr;
    return pool;
}

void D3D11Context::BindPSTexture(uint32_t slot, GpuTexture* tex) {
    if (!tex) {
        ID3D11ShaderResourceView* nullSrv = nullptr;
        ID3D11SamplerState* nullSampler = nullptr;
        m_Context->PSSetShaderResources(slot, 1, &nullSrv);
        m_Context->PSSetSamplers(slot, 1, &nullSampler);
        return;
    }
    auto* d3dTex = static_cast<D3D11Texture*>(tex);
    m_Context->PSSetShaderResources(slot, 1, d3dTex->srv.GetAddressOf());
    m_Context->PSSetSamplers(slot, 1, d3dTex->sampler.GetAddressOf());
}

void D3D11Context::SetBlendMode(GpuBlendMode mode) {
    if (!m_Device || !m_Context)
        return;
    if (!m_OpaqueBlendState || !m_AlphaBlendState) {
        D3D11_BLEND_DESC opaqueDesc = {};
        opaqueDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        m_Device->CreateBlendState(&opaqueDesc, &m_OpaqueBlendState);

        D3D11_BLEND_DESC alphaDesc = opaqueDesc;
        auto& target = alphaDesc.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D11_BLEND_ONE;
        target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        m_Device->CreateBlendState(&alphaDesc, &m_AlphaBlendState);
    }
    ID3D11BlendState* state = mode == GpuBlendMode::Alpha ? m_AlphaBlendState.Get() : m_OpaqueBlendState.Get();
    const float blendFactor[4] = {0, 0, 0, 0};
    m_Context->OMSetBlendState(state, blendFactor, 0xffffffffu);
}

std::shared_ptr<GpuBuffer> D3D11Context::CreateBuffer(const RHIBufferDesc& desc, const void* initialData) {
    if (!m_Device || desc.size == 0)
        return nullptr;
    D3D11_BUFFER_DESC native{};
    native.ByteWidth = desc.size;
    const bool readback = HasUsage(desc.usage, RHIResourceUsage::Readback);
    native.Usage = readback ? D3D11_USAGE_STAGING
                            : (initialData && !HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess) &&
                                       !HasUsage(desc.usage, RHIResourceUsage::CopyDestination)
                                   ? D3D11_USAGE_IMMUTABLE
                                   : D3D11_USAGE_DEFAULT);
    if (readback)
        native.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (HasUsage(desc.usage, RHIResourceUsage::VertexBuffer))
        native.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
    if (HasUsage(desc.usage, RHIResourceUsage::IndexBuffer))
        native.BindFlags |= D3D11_BIND_INDEX_BUFFER;
    if (HasUsage(desc.usage, RHIResourceUsage::ConstantBuffer))
        native.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
    if (HasUsage(desc.usage, RHIResourceUsage::ShaderResource))
        native.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess))
        native.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    if (HasUsage(desc.usage, RHIResourceUsage::IndirectArguments))
        native.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    if (desc.stride > 0 && (native.BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS))) {
        native.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        native.StructureByteStride = desc.stride;
    }
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = initialData;
    auto buffer = std::make_shared<D3D11Buffer>();
    buffer->desc = desc;
    buffer->stride = desc.stride;
    if (FAILED(m_Device->CreateBuffer(&native, initialData ? &data : nullptr, &buffer->buffer)))
        return nullptr;
    if (HasUsage(desc.usage, RHIResourceUsage::CopyDestination)) {
        buffer->updateShadow.resize(desc.size);
        if (initialData)
            std::memcpy(buffer->updateShadow.data(), initialData, desc.size);
    }
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(buffer));
    return buffer;
}

std::shared_ptr<GpuBufferView> D3D11Context::CreateBufferView(const std::shared_ptr<GpuBuffer>& buffer,
                                                              const RHIBufferViewDesc& desc) {
    auto nativeBuffer = std::dynamic_pointer_cast<D3D11Buffer>(buffer);
    if (!nativeBuffer || !nativeBuffer->buffer || nativeBuffer->desc.stride == 0)
        return nullptr;
    auto view = std::make_shared<D3D11BufferView>();
    view->buffer = buffer;
    view->desc = desc;
    const uint32_t count = desc.elementCount ? desc.elementCount : nativeBuffer->desc.size / nativeBuffer->desc.stride;
    if (HasUsage(desc.usage, RHIResourceUsage::ShaderResource)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC d{};
        d.Format = DXGI_FORMAT_UNKNOWN;
        d.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        d.Buffer.FirstElement = desc.firstElement;
        d.Buffer.NumElements = count;
        if (FAILED(m_Device->CreateShaderResourceView(nativeBuffer->buffer.Get(), &d, &view->srv)))
            return nullptr;
    }
    if (HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess)) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC d{};
        d.Format = DXGI_FORMAT_UNKNOWN;
        d.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        d.Buffer.FirstElement = desc.firstElement;
        d.Buffer.NumElements = count;
        if (FAILED(m_Device->CreateUnorderedAccessView(nativeBuffer->buffer.Get(), &d, &view->uav)))
            return nullptr;
    }
    return view;
}

void D3D11Context::SetRasterState(bool twoSided, bool wireframe) {
    if (!m_Device || !m_Context)
        return;

    auto ensureState = [&](ComPtr<ID3D11RasterizerState>& state, D3D11_CULL_MODE cullMode, D3D11_FILL_MODE fillMode) {
        if (state)
            return;
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = fillMode;
        desc.CullMode = cullMode;
        desc.FrontCounterClockwise = FALSE;
        desc.DepthBias = 0;
        desc.DepthBiasClamp = 0.0f;
        desc.SlopeScaledDepthBias = 0.0f;
        desc.DepthClipEnable = TRUE;
        desc.ScissorEnable = FALSE;
        desc.MultisampleEnable = FALSE;
        desc.AntialiasedLineEnable = FALSE;
        m_Device->CreateRasterizerState(&desc, &state);
    };

    ensureState(m_RasterSolidCullBack, D3D11_CULL_BACK, D3D11_FILL_SOLID);
    ensureState(m_RasterSolidCullNone, D3D11_CULL_NONE, D3D11_FILL_SOLID);
    ensureState(m_RasterWireCullBack, D3D11_CULL_BACK, D3D11_FILL_WIREFRAME);
    ensureState(m_RasterWireCullNone, D3D11_CULL_NONE, D3D11_FILL_WIREFRAME);

    ID3D11RasterizerState* state = nullptr;
    if (wireframe) {
        state = twoSided ? m_RasterWireCullNone.Get() : m_RasterWireCullBack.Get();
    } else {
        state = twoSided ? m_RasterSolidCullNone.Get() : m_RasterSolidCullBack.Get();
    }
    m_Context->RSSetState(state);
}

std::shared_ptr<GpuTexture> D3D11Context::CreateTexture(const RHITextureDesc& desc) {
    if (!m_Device || desc.width == 0 || desc.height == 0)
        return nullptr;
    if (desc.sampleCount > 1 && desc.mipLevels != 1)
        return nullptr;
    UINT qualityLevels = 0;
    if (FAILED(m_Device->CheckMultisampleQualityLevels(ToDxgiFormat(desc.format), (std::max)(desc.sampleCount, 1u),
                                                       &qualityLevels)) ||
        qualityLevels == 0 || desc.sampleQuality >= qualityLevels)
        return nullptr;
    D3D11_TEXTURE2D_DESC native{};
    native.Width = desc.width;
    native.Height = desc.height;
    native.MipLevels = desc.mipLevels;
    native.ArraySize = desc.arrayLayers;
    native.Format = ToDxgiFormat(desc.format, true);
    native.SampleDesc.Count = (std::max)(desc.sampleCount, 1u);
    native.SampleDesc.Quality = desc.sampleQuality;
    native.Usage = D3D11_USAGE_DEFAULT;
    if (HasUsage(desc.usage, RHIResourceUsage::ShaderResource))
        native.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (HasUsage(desc.usage, RHIResourceUsage::RenderTarget))
        native.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (HasUsage(desc.usage, RHIResourceUsage::DepthStencil))
        native.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    if (HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess))
        native.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    if (desc.cube)
        native.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    auto result = std::make_shared<D3D11Texture>();
    result->desc = desc;
    result->isCube = desc.cube;
    if (FAILED(m_Device->CreateTexture2D(&native, nullptr, &result->texture)))
        return nullptr;
    if (HasUsage(desc.usage, RHIResourceUsage::ShaderResource)) {
        RHITextureViewDesc viewDesc;
        viewDesc.mipCount = desc.mipLevels;
        viewDesc.layerCount = desc.arrayLayers;
        viewDesc.usage = RHIResourceUsage::ShaderResource;
        auto view = std::dynamic_pointer_cast<D3D11TextureView>(CreateTextureView(result, viewDesc));
        if (!view)
            return nullptr;
        result->srv = view->srv;
        RHISamplerDesc samplerDesc;
        samplerDesc.filter =
            HasUsage(desc.usage, RHIResourceUsage::DepthStencil) ? RHIFilter::ComparisonLinear : RHIFilter::Linear;
        samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = RHIAddressMode::Clamp;
        auto sampler = std::dynamic_pointer_cast<D3D11Sampler>(CreateSampler(samplerDesc));
        if (!sampler)
            return nullptr;
        result->sampler = sampler->sampler;
    }
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuTexture>(result));
    return result;
}

std::shared_ptr<GpuTextureView> D3D11Context::CreateTextureView(const std::shared_ptr<GpuTexture>& texture,
                                                                const RHITextureViewDesc& desc) {
    auto nativeTexture = std::dynamic_pointer_cast<D3D11Texture>(texture);
    if (!nativeTexture || !nativeTexture->texture)
        return nullptr;
    auto view = std::make_shared<D3D11TextureView>();
    view->texture = texture;
    view->desc = desc;
    const RHITextureDesc& td = nativeTexture->desc;
    const bool arrayTexture = td.array || td.arrayLayers > 1;
    if (HasUsage(desc.usage, RHIResourceUsage::ShaderResource)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC d{};
        d.Format = td.format == RHIFormat::D24S8      ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS
                   : td.format == RHIFormat::D32Float ? DXGI_FORMAT_R32_FLOAT
                                                      : ToDxgiFormat(td.format);
        if (td.sampleCount > 1 && arrayTexture) {
            d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
            d.Texture2DMSArray.FirstArraySlice = desc.firstLayer;
            d.Texture2DMSArray.ArraySize = desc.layerCount;
        } else if (td.sampleCount > 1) {
            d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        } else if (td.cube) {
            d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            d.TextureCube.MostDetailedMip = desc.firstMip;
            d.TextureCube.MipLevels = desc.mipCount;
        } else if (arrayTexture) {
            d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            d.Texture2DArray.MostDetailedMip = desc.firstMip;
            d.Texture2DArray.MipLevels = desc.mipCount;
            d.Texture2DArray.FirstArraySlice = desc.firstLayer;
            d.Texture2DArray.ArraySize = desc.layerCount;
        } else {
            d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            d.Texture2D.MostDetailedMip = desc.firstMip;
            d.Texture2D.MipLevels = desc.mipCount;
        }
        if (FAILED(m_Device->CreateShaderResourceView(nativeTexture->texture.Get(), &d, &view->srv)))
            return nullptr;
    }
    if (HasUsage(desc.usage, RHIResourceUsage::RenderTarget)) {
        D3D11_RENDER_TARGET_VIEW_DESC d{};
        d.Format = ToDxgiFormat(td.format);
        if (td.sampleCount > 1 && arrayTexture) {
            d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
            d.Texture2DMSArray.FirstArraySlice = desc.firstLayer;
            d.Texture2DMSArray.ArraySize = desc.layerCount;
        } else if (td.sampleCount > 1) {
            d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        } else if (arrayTexture) {
            d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            d.Texture2DArray.MipSlice = desc.firstMip;
            d.Texture2DArray.FirstArraySlice = desc.firstLayer;
            d.Texture2DArray.ArraySize = desc.layerCount;
        } else {
            d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            d.Texture2D.MipSlice = desc.firstMip;
        }
        if (FAILED(m_Device->CreateRenderTargetView(nativeTexture->texture.Get(), &d, &view->rtv)))
            return nullptr;
    }
    if (HasUsage(desc.usage, RHIResourceUsage::DepthStencil)) {
        D3D11_DEPTH_STENCIL_VIEW_DESC d{};
        d.Format = ToDxgiFormat(td.format);
        if (td.sampleCount > 1 && arrayTexture) {
            d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
            d.Texture2DMSArray.FirstArraySlice = desc.firstLayer;
            d.Texture2DMSArray.ArraySize = desc.layerCount;
        } else if (td.sampleCount > 1) {
            d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
        } else if (arrayTexture) {
            d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            d.Texture2DArray.MipSlice = desc.firstMip;
            d.Texture2DArray.FirstArraySlice = desc.firstLayer;
            d.Texture2DArray.ArraySize = desc.layerCount;
        } else {
            d.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            d.Texture2D.MipSlice = desc.firstMip;
        }
        if (FAILED(m_Device->CreateDepthStencilView(nativeTexture->texture.Get(), &d, &view->dsv)))
            return nullptr;
    }
    if (HasUsage(desc.usage, RHIResourceUsage::UnorderedAccess)) {
        if (td.sampleCount > 1)
            return nullptr;
        D3D11_UNORDERED_ACCESS_VIEW_DESC d{};
        d.Format = ToDxgiFormat(td.format);
        if (arrayTexture) {
            d.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            d.Texture2DArray.MipSlice = desc.firstMip;
            d.Texture2DArray.FirstArraySlice = desc.firstLayer;
            d.Texture2DArray.ArraySize = desc.layerCount;
        } else {
            d.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            d.Texture2D.MipSlice = desc.firstMip;
        }
        if (FAILED(m_Device->CreateUnorderedAccessView(nativeTexture->texture.Get(), &d, &view->uav)))
            return nullptr;
    }
    return view;
}

std::shared_ptr<GpuSampler> D3D11Context::CreateSampler(const RHISamplerDesc& desc) {
    auto result = std::make_shared<D3D11Sampler>();
    result->desc = desc;
    D3D11_SAMPLER_DESC d{};
    d.Filter = desc.filter == RHIFilter::Point              ? D3D11_FILTER_MIN_MAG_MIP_POINT
               : desc.filter == RHIFilter::ComparisonLinear ? D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
                                                            : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    auto address = [](RHIAddressMode m) {
        return m == RHIAddressMode::Clamp    ? D3D11_TEXTURE_ADDRESS_CLAMP
               : m == RHIAddressMode::Border ? D3D11_TEXTURE_ADDRESS_BORDER
                                             : D3D11_TEXTURE_ADDRESS_WRAP;
    };
    d.AddressU = address(desc.addressU);
    d.AddressV = address(desc.addressV);
    d.AddressW = address(desc.addressW);
    d.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    d.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_Device->CreateSamplerState(&d, &result->sampler)))
        return nullptr;
    return result;
}

std::shared_ptr<GpuGraphicsPipeline> D3D11Context::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    auto shader = std::dynamic_pointer_cast<D3D11Shader>(desc.shader);
    if (!m_Device || !shader)
        return nullptr;

    auto pipeline = std::make_shared<D3D11GraphicsPipeline>();
    pipeline->desc = desc;

    D3D11_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = desc.blend.alphaToCoverageEnable;
    blend.IndependentBlendEnable = desc.blend.independentBlendEnable;
    const size_t attachmentCount = (std::min)(desc.blend.attachments.size(), size_t{8});
    for (size_t i = 0; i < 8; ++i) {
        const RHIBlendAttachmentState state =
            attachmentCount ? desc.blend.attachments[(std::min)(i, attachmentCount - 1)] : RHIBlendAttachmentState{};
        auto& target = blend.RenderTarget[i];
        target.BlendEnable = state.blendEnable;
        target.SrcBlend = ToD3D11Blend(state.srcColorFactor);
        target.DestBlend = ToD3D11Blend(state.dstColorFactor);
        target.BlendOp = ToD3D11BlendOp(state.colorOp);
        target.SrcBlendAlpha = ToD3D11Blend(state.srcAlphaFactor);
        target.DestBlendAlpha = ToD3D11Blend(state.dstAlphaFactor);
        target.BlendOpAlpha = ToD3D11BlendOp(state.alphaOp);
        target.RenderTargetWriteMask = state.colorWriteMask;
    }
    if (FAILED(m_Device->CreateBlendState(&blend, &pipeline->blendState)))
        return nullptr;

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = desc.rasterizer.fillMode == RHIFillMode::Wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    raster.CullMode = desc.rasterizer.cullMode == RHICullMode::None    ? D3D11_CULL_NONE
                      : desc.rasterizer.cullMode == RHICullMode::Front ? D3D11_CULL_FRONT
                                                                       : D3D11_CULL_BACK;
    raster.FrontCounterClockwise = desc.rasterizer.frontFace == RHIFrontFace::CounterClockwise;
    raster.DepthBias = desc.rasterizer.depthBias;
    raster.DepthBiasClamp = desc.rasterizer.depthBiasClamp;
    raster.SlopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
    raster.DepthClipEnable = desc.rasterizer.depthClipEnable;
    raster.ScissorEnable = desc.rasterizer.scissorEnable;
    raster.MultisampleEnable = desc.rasterizer.multisampleEnable || desc.multisample.sampleCount > 1;
    raster.AntialiasedLineEnable = desc.rasterizer.antialiasedLineEnable;
    if (FAILED(m_Device->CreateRasterizerState(&raster, &pipeline->rasterizerState)))
        return nullptr;

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = desc.depthStencil.depthTestEnable;
    depth.DepthWriteMask =
        desc.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = ToD3D11Compare(desc.depthStencil.depthCompareOp);
    depth.StencilEnable = desc.depthStencil.stencilEnable;
    depth.StencilReadMask = desc.depthStencil.stencilReadMask;
    depth.StencilWriteMask = desc.depthStencil.stencilWriteMask;
    depth.FrontFace = ToD3D11StencilFace(desc.depthStencil.frontFace);
    depth.BackFace = ToD3D11StencilFace(desc.depthStencil.backFace);
    if (FAILED(m_Device->CreateDepthStencilState(&depth, &pipeline->depthStencilState)))
        return nullptr;
    return pipeline;
}

std::shared_ptr<GpuReadbackTicket> D3D11Context::ReadbackBufferAsync(const std::shared_ptr<GpuBuffer>& buffer) {
    auto source = std::dynamic_pointer_cast<D3D11Buffer>(buffer);
    if (!source || !source->buffer || source->desc.size == 0)
        return nullptr;
    D3D11_BUFFER_DESC desc{};
    source->buffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.StructureByteStride = 0;
    ComPtr<ID3D11Buffer> staging;
    if (FAILED(m_Device->CreateBuffer(&desc, nullptr, &staging)))
        return nullptr;
    D3D11_QUERY_DESC queryDesc{};
    queryDesc.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> query;
    if (FAILED(m_Device->CreateQuery(&queryDesc, &query)))
        return nullptr;
    m_Context->CopyResource(staging.Get(), source->buffer.Get());
    m_Context->End(query.Get());
    return std::make_shared<D3D11ReadbackTicket>(m_Context.Get(), std::move(staging), std::move(query),
                                                 source->desc.size);
}

std::shared_ptr<GpuTextureReadbackTicket> D3D11Context::ReadbackTextureAsync(const std::shared_ptr<GpuTexture>& texture,
                                                                             const RHITextureRegion& requested) {
    auto source = std::dynamic_pointer_cast<D3D11Texture>(texture);
    if (!source || !source->texture || source->desc.sampleCount > 1)
        return nullptr;
    const uint32_t mipWidth = (std::max)(1u, source->desc.width >> requested.mipLevel);
    const uint32_t mipHeight = (std::max)(1u, source->desc.height >> requested.mipLevel);
    const uint32_t width = requested.width ? requested.width : mipWidth - requested.x;
    const uint32_t height = requested.height ? requested.height : mipHeight - requested.y;
    const uint32_t bytes = RHIFormatByteSize(source->desc.format);
    if (!bytes || requested.x + width > mipWidth || requested.y + height > mipHeight ||
        requested.mipLevel >= source->desc.mipLevels || requested.arrayLayer >= source->desc.arrayLayers)
        return nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = ToDxgiFormat(source->desc.format);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_Device->CreateTexture2D(&desc, nullptr, &staging)))
        return nullptr;
    D3D11_BOX box{requested.x, requested.y, requested.z, requested.x + width, requested.y + height, requested.z + 1};
    const UINT subresource = D3D11CalcSubresource(requested.mipLevel, requested.arrayLayer, source->desc.mipLevels);
    m_Context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source->texture.Get(), subresource, &box);
    D3D11_QUERY_DESC queryDesc{};
    queryDesc.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> query;
    if (FAILED(m_Device->CreateQuery(&queryDesc, &query)))
        return nullptr;
    m_Context->End(query.Get());
    return std::make_shared<D3D11TextureReadbackTicket>(m_Context.Get(), std::move(staging), std::move(query), width,
                                                        height, width * bytes, source->desc.format);
}
