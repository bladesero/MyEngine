#include "Renderer/MaterialResourceCache.h"

#include "Assets/MeshAsset.h"
#include "Assets/TextureAsset.h"

#include <chrono>
#include <cstring>

namespace {

RHIFilter FilterForTexture(const TextureAsset& texture)
{
    return texture.GetFilter() == TextureFilter::Nearest ? RHIFilter::Point : RHIFilter::Linear;
}

RHIAddressMode AddressForTexture(TextureWrap wrap)
{
    return wrap == TextureWrap::Clamp ? RHIAddressMode::Clamp : RHIAddressMode::Repeat;
}

} // namespace

MaterialResourceCache::MaterialResourceCache(IRHIDevice* device)
    : m_Device(device)
{}

RHISamplerDesc MaterialResourceCache::SamplerDescForTexture(const TextureAsset& texture)
{
    RHISamplerDesc desc;
    desc.filter = FilterForTexture(texture);
    desc.addressU = AddressForTexture(texture.GetWrapU());
    desc.addressV = AddressForTexture(texture.GetWrapV());
    desc.addressW = RHIAddressMode::Repeat;
    return desc;
}

bool MaterialResourceCache::SameSamplerDesc(
    const RHISamplerDesc& left,
    const RHISamplerDesc& right)
{
    return left.filter == right.filter &&
           left.addressU == right.addressU &&
           left.addressV == right.addressV &&
           left.addressW == right.addressW;
}

void MaterialResourceCache::AppendSamplerDesc(std::string& out, const RHISamplerDesc& desc)
{
    out += ':';
    out += std::to_string(static_cast<int>(desc.filter));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressU));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressV));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressW));
}

void MaterialResourceCache::EnsureMeshUploaded(MeshAsset* mesh)
{
    if (!mesh || mesh->IsUploaded() || !m_Device) return;

    const auto& vertices = mesh->GetVertices();
    const auto& indices = mesh->GetIndices();
    if (vertices.empty()) return;

    const uint32_t vertexBytes = static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex));
    mesh->SetVertexBuffer(
        m_Device->CreateVertexBuffer(vertices.data(), vertexBytes, sizeof(MeshVertex)));

    if (!indices.empty()) {
        const uint32_t indexBytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
        mesh->SetIndexBuffer(m_Device->CreateIndexBuffer(indices.data(), indexBytes));
    }
}

void MaterialResourceCache::EnsureTextureUploaded(TextureAsset* texture)
{
    if (!texture || !m_Device) return;
    if (texture->HasGpuHandle()) return;
    if (m_TextureCache.count(texture)) return;

    const auto& mips = texture->GetMips();
    if (mips.empty()) return;

    RHITextureDesc desc;
    desc.width = static_cast<uint32_t>(texture->GetWidth());
    desc.height = static_cast<uint32_t>(texture->GetHeight());
    desc.mipLevels = static_cast<uint32_t>(mips.size());
    desc.arrayLayers = 1;
    desc.format = RHIFormat::RGBA8UNorm;
    desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
    desc.debugName = texture->GetName();

    std::vector<RHITextureSubresourceData> subresources;
    subresources.reserve(mips.size());
    uint64_t uploadBytes = 0;
    for (uint32_t mip = 0; mip < mips.size(); ++mip) {
        const TextureMipData& mipData = mips[mip];
        if (mipData.rgba8.empty() || mipData.width <= 0 || mipData.height <= 0) return;
        RHITextureSubresourceData source;
        source.data = mipData.rgba8.data();
        source.rowPitch = static_cast<uint32_t>(mipData.width * 4);
        source.slicePitch = static_cast<uint32_t>(mipData.rgba8.size());
        source.mipLevel = mip;
        source.arrayLayer = 0;
        subresources.push_back(source);
        uploadBytes += mipData.rgba8.size();
    }

    const auto uploadStart = std::chrono::steady_clock::now();
    auto gpuTexture = m_Device->UploadTexture(
        desc, subresources.data(), static_cast<uint32_t>(subresources.size()));
    if (!gpuTexture) return;

    const float uploadMs = static_cast<float>(std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - uploadStart).count());
    ++m_FrameStats.textureUploads;
    m_FrameStats.textureUploadBytes += uploadBytes;
    m_FrameStats.textureUploadMs += uploadMs;
    texture->SetGpuHandle(gpuTexture.get());
    m_TextureCache[texture] = std::move(gpuTexture);
}

void MaterialResourceCache::EnsureNamedBindingDefaults()
{
    if (!m_Device || m_DefaultTextureView) return;
    const uint8_t white[4] = {255, 255, 255, 255};
    m_DefaultTexture = m_Device->UploadTexture2D(white, 1, 1);
    RHITextureViewDesc viewDesc;
    viewDesc.usage = RHIResourceUsage::ShaderResource;
    m_DefaultTextureView = m_Device->CreateTextureView(m_DefaultTexture, viewDesc);

    RHISamplerDesc linear;
    m_LinearSampler = m_Device->CreateSampler(linear);
    RHISamplerDesc shadow = linear;
    shadow.filter = RHIFilter::ComparisonLinear;
    shadow.addressU = shadow.addressV = shadow.addressW = RHIAddressMode::Clamp;
    m_ShadowSampler = m_Device->CreateSampler(shadow);
}

std::shared_ptr<GpuTextureView> MaterialResourceCache::GetTextureView(GpuTexture* texture)
{
    EnsureNamedBindingDefaults();
    if (!texture) return m_DefaultTextureView;
    auto found = m_TextureViews.find(texture);
    if (found != m_TextureViews.end()) return found->second;

    RHITextureViewDesc desc;
    desc.mipCount = texture->desc.mipLevels;
    desc.layerCount = texture->desc.arrayLayers;
    desc.usage = RHIResourceUsage::ShaderResource;
    auto view = m_Device->CreateTextureView(
        std::shared_ptr<GpuTexture>(texture, [](GpuTexture*) {}), desc);
    if (view) m_TextureViews[texture] = view;
    return view ? view : m_DefaultTextureView;
}

std::shared_ptr<GpuSampler> MaterialResourceCache::GetSamplerForTexture(TextureAsset* texture)
{
    EnsureNamedBindingDefaults();
    if (!m_Device || !texture) return m_LinearSampler;

    const RHISamplerDesc desc = SamplerDescForTexture(*texture);
    if (m_LinearSampler && SameSamplerDesc(m_LinearSampler->desc, desc)) {
        return m_LinearSampler;
    }
    if (m_ShadowSampler && SameSamplerDesc(m_ShadowSampler->desc, desc)) {
        return m_ShadowSampler;
    }
    for (const auto& cached : m_TextureSamplers) {
        if (cached && SameSamplerDesc(cached->desc, desc)) return cached;
    }

    auto sampler = m_Device->CreateSampler(desc);
    if (!sampler) return m_LinearSampler;
    m_TextureSamplers.push_back(sampler);
    return sampler;
}

std::shared_ptr<GpuTextureView> MaterialResourceCache::GetDefaultTextureView()
{
    EnsureNamedBindingDefaults();
    return m_DefaultTextureView;
}

std::shared_ptr<GpuSampler> MaterialResourceCache::GetLinearSampler()
{
    EnsureNamedBindingDefaults();
    return m_LinearSampler;
}

std::shared_ptr<GpuSampler> MaterialResourceCache::GetShadowSampler()
{
    EnsureNamedBindingDefaults();
    return m_ShadowSampler;
}
