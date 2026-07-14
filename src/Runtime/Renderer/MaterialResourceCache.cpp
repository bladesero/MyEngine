#include "Renderer/MaterialResourceCache.h"

#include "Assets/MeshAsset.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureAsset.h"
#include "Core/RuntimeQualityDegradation.h"
#include "Renderer/RHI/RHIResourceStats.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

namespace {

std::mutex g_CacheRegistryMutex;
std::vector<MaterialResourceCache*> g_CacheRegistry;
struct MeshResidencyEntry {
    std::weak_ptr<void> lifetime;
    std::string path;
    uint64_t bytes = 0, lastUse = 0, lastCollection = 0;
};
std::unordered_map<MeshAsset*, MeshResidencyEntry> g_MeshResidency;
uint64_t g_MeshUseClock = 0, g_MeshCollection = 0;

RHIFilter FilterForTexture(const TextureAsset& texture) {
    return texture.GetFilter() == TextureFilter::Nearest ? RHIFilter::Point : RHIFilter::Linear;
}

RHIAddressMode AddressForTexture(TextureWrap wrap) {
    return wrap == TextureWrap::Clamp ? RHIAddressMode::Clamp : RHIAddressMode::Repeat;
}

uint64_t EstimateTextureUploadBytes(const TextureAsset& texture) {
    if (texture.IsPayloadResident()) {
        uint64_t bytes = 0;
        for (const TextureMipData& mip : texture.GetMips()) {
            bytes += mip.rgba8.size();
        }
        return bytes;
    }

    uint64_t bytes = 0;
    int width = texture.GetWidth();
    int height = texture.GetHeight();
    const int mipLevels = (std::max)(1, texture.GetMipLevels());
    for (int mip = 0; mip < mipLevels && width > 0 && height > 0; ++mip) {
        bytes += static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
        width = (std::max)(1, width / 2);
        height = (std::max)(1, height / 2);
        if (width == 1 && height == 1 && mip + 1 < mipLevels) {
            bytes += 4ull * static_cast<uint64_t>(mipLevels - mip - 1);
            break;
        }
    }
    return bytes;
}

bool CanUploadBc3(const IRHIDevice& device, const TextureAsset& texture) {
    if (!device.IsFormatSupported(RHIFormat::BC3UNorm, RHIResourceUsage::ShaderResource)) {
        return false;
    }
    const auto& mips = texture.GetMips();
    if (mips.empty())
        return false;
    for (const TextureMipData& mip : mips) {
        if (mip.width <= 0 || mip.height <= 0 || mip.bc3.empty())
            return false;
    }
    return true;
}

bool CanUploadBc1(const IRHIDevice& device, const TextureAsset& texture) {
    if (!device.IsFormatSupported(RHIFormat::BC1UNorm, RHIResourceUsage::ShaderResource)) {
        return false;
    }
    const auto& mips = texture.GetMips();
    if (mips.empty())
        return false;
    for (const TextureMipData& mip : mips) {
        if (mip.width <= 0 || mip.height <= 0 || mip.bc1.empty())
            return false;
    }
    return true;
}

} // namespace

MaterialResourceCache::MaterialResourceCache(IRHIDevice* device) : m_Device(device) {
    std::lock_guard<std::mutex> lock(g_CacheRegistryMutex);
    g_CacheRegistry.push_back(this);
}

MaterialResourceCache::~MaterialResourceCache() {
    std::lock_guard<std::mutex> lock(g_CacheRegistryMutex);
    for (auto& entry : m_TextureCache) {
        if (!entry.second.assetLifetime.expired() && entry.first &&
            entry.first->GetGpuHandle() == entry.second.texture.get())
            entry.first->SetGpuHandle(nullptr);
    }
    g_CacheRegistry.erase(std::remove(g_CacheRegistry.begin(), g_CacheRegistry.end(), this), g_CacheRegistry.end());
}

RHISamplerDesc MaterialResourceCache::SamplerDescForTexture(const TextureAsset& texture) {
    RHISamplerDesc desc;
    desc.filter = FilterForTexture(texture);
    desc.addressU = AddressForTexture(texture.GetWrapU());
    desc.addressV = AddressForTexture(texture.GetWrapV());
    desc.addressW = RHIAddressMode::Repeat;
    return desc;
}

bool MaterialResourceCache::SameSamplerDesc(const RHISamplerDesc& left, const RHISamplerDesc& right) {
    return left.filter == right.filter && left.addressU == right.addressU && left.addressV == right.addressV &&
           left.addressW == right.addressW;
}

void MaterialResourceCache::AppendSamplerDesc(std::string& out, const RHISamplerDesc& desc) {
    out += ':';
    out += std::to_string(static_cast<int>(desc.filter));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressU));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressV));
    out += ',';
    out += std::to_string(static_cast<int>(desc.addressW));
}

void MaterialResourceCache::EnsureMeshUploaded(MeshAsset* mesh) {
    if (!mesh || !m_Device)
        return;
    if (mesh->IsUploaded()) {
        TrackGlobalMeshResidency(mesh);
        return;
    }

    const auto& vertices = mesh->GetVertices();
    const auto& indices = mesh->GetIndices();
    if (vertices.empty())
        return;

    const uint32_t vertexBytes = static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex));
    mesh->SetVertexBuffer(m_Device->CreateVertexBuffer(vertices.data(), vertexBytes, sizeof(MeshVertex)));

    if (!indices.empty()) {
        const uint32_t indexBytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
        mesh->SetIndexBuffer(m_Device->CreateIndexBuffer(indices.data(), indexBytes));
    }
    if (mesh->IsUploaded())
        TrackGlobalMeshResidency(mesh);
}

void MaterialResourceCache::EnsureTextureUploaded(TextureAsset* texture) {
    if (!texture || !m_Device)
        return;
    if (auto found = m_TextureCache.find(texture); found != m_TextureCache.end()) {
        found->second.lastUsed = ++m_UseClock;
        return;
    }
    if (texture->HasGpuHandle())
        return;

    const uint64_t estimatedUploadBytes = EstimateTextureUploadBytes(*texture);
    if (m_TextureUploadBudgetBytes > 0 && estimatedUploadBytes > 0 && m_FrameStats.textureUploads > 0 &&
        m_FrameStats.textureUploadBytes + estimatedUploadBytes > m_TextureUploadBudgetBytes) {
        ++m_FrameStats.skippedTextureUploads;
        m_FrameStats.pendingTextureUploadBytes += estimatedUploadBytes;
        return;
    }

    if (!texture->EnsurePayloadLoaded())
        return;
    const auto& mips = texture->GetMips();
    if (mips.empty())
        return;
    const bool uploadBc3 = CanUploadBc3(*m_Device, *texture);
    const bool uploadBc1 = CanUploadBc1(*m_Device, *texture);

    RHITextureDesc desc;
    const uint32_t mipBias =
        std::min<uint32_t>(RuntimeQualityDegradation::Get().level, static_cast<uint32_t>(mips.size() - 1));
    desc.width = static_cast<uint32_t>(mips[mipBias].width);
    desc.height = static_cast<uint32_t>(mips[mipBias].height);
    desc.mipLevels = static_cast<uint32_t>(mips.size()) - mipBias;
    desc.arrayLayers = 1;
    desc.format = uploadBc3 ? RHIFormat::BC3UNorm : uploadBc1 ? RHIFormat::BC1UNorm : RHIFormat::RGBA8UNorm;
    desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
    desc.debugName = texture->GetName();

    std::vector<RHITextureSubresourceData> subresources;
    subresources.reserve(mips.size() - mipBias);
    uint64_t uploadBytes = 0;
    for (uint32_t mip = mipBias; mip < mips.size(); ++mip) {
        const TextureMipData& mipData = mips[mip];
        if (mipData.width <= 0 || mipData.height <= 0)
            return;
        RHITextureSubresourceData source;
        if (uploadBc3) {
            const uint32_t blockColumns = static_cast<uint32_t>((mipData.width + 3) / 4);
            source.data = mipData.bc3.data();
            source.rowPitch = blockColumns * 16u;
            source.slicePitch = static_cast<uint32_t>(mipData.bc3.size());
            uploadBytes += mipData.bc3.size();
        } else if (uploadBc1) {
            const uint32_t blockColumns = static_cast<uint32_t>((mipData.width + 3) / 4);
            source.data = mipData.bc1.data();
            source.rowPitch = blockColumns * 8u;
            source.slicePitch = static_cast<uint32_t>(mipData.bc1.size());
            uploadBytes += mipData.bc1.size();
        } else {
            if (mipData.rgba8.empty())
                return;
            source.data = mipData.rgba8.data();
            source.rowPitch = static_cast<uint32_t>(mipData.width * 4);
            source.slicePitch = static_cast<uint32_t>(mipData.rgba8.size());
            uploadBytes += mipData.rgba8.size();
        }
        source.mipLevel = mip - mipBias;
        source.arrayLayer = 0;
        subresources.push_back(source);
    }

    const auto uploadStart = std::chrono::steady_clock::now();
    auto gpuTexture = m_Device->UploadTexture(desc, subresources.data(), static_cast<uint32_t>(subresources.size()));
    if (!gpuTexture)
        return;

    const float uploadMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count());
    ++m_FrameStats.textureUploads;
    m_FrameStats.textureUploadBytes += uploadBytes;
    m_FrameStats.textureUploadMs += uploadMs;
    texture->SetGpuHandle(gpuTexture.get());
    TextureEntry entry;
    entry.bytes = EstimateRHITextureBytes(gpuTexture->desc);
    entry.assetLifetime = texture->GetLifetimeToken();
    entry.path = texture->GetPath();
    entry.lastUsed = ++m_UseClock;
    entry.texture = std::move(gpuTexture);
    m_TextureCache[texture] = std::move(entry);
}

void MaterialResourceCache::EnsureNamedBindingDefaults() {
    if (!m_Device || m_DefaultTextureView)
        return;
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

std::shared_ptr<GpuTextureView> MaterialResourceCache::GetTextureView(GpuTexture* texture) {
    EnsureNamedBindingDefaults();
    if (!texture)
        return m_DefaultTextureView;
    for (auto& entry : m_TextureCache) {
        if (entry.second.texture.get() == texture) {
            entry.second.lastUsed = ++m_UseClock;
            break;
        }
    }
    auto found = m_TextureViews.find(texture);
    if (found != m_TextureViews.end())
        return found->second;

    RHITextureViewDesc desc;
    desc.mipCount = texture->desc.mipLevels;
    desc.layerCount = texture->desc.arrayLayers;
    desc.usage = RHIResourceUsage::ShaderResource;
    auto view = m_Device->CreateTextureView(std::shared_ptr<GpuTexture>(texture, [](GpuTexture*) {}), desc);
    if (view)
        m_TextureViews[texture] = view;
    return view ? view : m_DefaultTextureView;
}

std::shared_ptr<GpuSampler> MaterialResourceCache::GetSamplerForTexture(TextureAsset* texture) {
    EnsureNamedBindingDefaults();
    if (!m_Device || !texture)
        return m_LinearSampler;

    const RHISamplerDesc desc = SamplerDescForTexture(*texture);
    if (m_LinearSampler && SameSamplerDesc(m_LinearSampler->desc, desc)) {
        return m_LinearSampler;
    }
    if (m_ShadowSampler && SameSamplerDesc(m_ShadowSampler->desc, desc)) {
        return m_ShadowSampler;
    }
    for (const auto& cached : m_TextureSamplers) {
        if (cached && SameSamplerDesc(cached->desc, desc))
            return cached;
    }

    auto sampler = m_Device->CreateSampler(desc);
    if (!sampler)
        return m_LinearSampler;
    m_TextureSamplers.push_back(sampler);
    return sampler;
}

std::shared_ptr<GpuTextureView> MaterialResourceCache::GetDefaultTextureView() {
    EnsureNamedBindingDefaults();
    return m_DefaultTextureView;
}

std::shared_ptr<GpuSampler> MaterialResourceCache::GetLinearSampler() {
    EnsureNamedBindingDefaults();
    return m_LinearSampler;
}

std::shared_ptr<GpuSampler> MaterialResourceCache::GetShadowSampler() {
    EnsureNamedBindingDefaults();
    return m_ShadowSampler;
}

GpuTextureGarbageCollectionReport MaterialResourceCache::CollectGlobalTextureGarbage(uint64_t highWatermarkBytes,
                                                                                     float lowWatermarkRatio,
                                                                                     size_t maxEvictions) {
    GpuTextureGarbageCollectionReport report;
    report.bytesBefore = RHIResourceStatsProvider::GetStats().liveResourceBytes;
    report.bytesAfter = report.bytesBefore;
    report.targetBytes = static_cast<uint64_t>(highWatermarkBytes * lowWatermarkRatio);
    report.pressureDetected = report.bytesBefore > highWatermarkBytes;
    if (!report.pressureDetected || !maxEvictions)
        return report;

    struct Candidate {
        MaterialResourceCache* cache;
        TextureAsset* asset;
        std::weak_ptr<void> lifetime;
        std::string path;
        uint64_t lastUsed, bytes;
    };
    std::vector<Candidate> candidates;
    std::lock_guard<std::mutex> lock(g_CacheRegistryMutex);
    for (auto* cache : g_CacheRegistry)
        for (auto& item : cache->m_TextureCache)
            candidates.push_back({cache, item.first, item.second.assetLifetime, item.second.path, item.second.lastUsed,
                                  item.second.bytes});
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.lastUsed != b.lastUsed)
            return a.lastUsed < b.lastUsed;
        if (a.path != b.path)
            return a.path < b.path;
        return a.asset < b.asset;
    });

    for (const Candidate& candidate : candidates) {
        if (report.evictions.size() >= maxEvictions || report.bytesAfter <= report.targetBytes)
            break;
        auto found = candidate.cache->m_TextureCache.find(candidate.asset);
        if (found == candidate.cache->m_TextureCache.end())
            continue;
        const auto view = candidate.cache->m_TextureViews.find(found->second.texture.get());
        const bool assetAlive = !candidate.lifetime.expired();
        if (assetAlive && AssetManager::Get().IsPinned(candidate.path)) {
            report.blockedBytes += candidate.bytes;
            report.blockers.push_back({candidate.path, candidate.bytes, GpuTextureEvictionBlockReason::Pinned});
            continue;
        }
        if (found->second.texture.use_count() > 1 ||
            (view != candidate.cache->m_TextureViews.end() && view->second.use_count() > 1)) {
            report.blockedBytes += candidate.bytes;
            report.blockers.push_back({candidate.path, candidate.bytes, GpuTextureEvictionBlockReason::Referenced});
            continue;
        }
        if (view != candidate.cache->m_TextureViews.end())
            candidate.cache->m_TextureViews.erase(view);
        if (assetAlive && candidate.asset->GetGpuHandle() == found->second.texture.get())
            candidate.asset->SetGpuHandle(nullptr);
        report.evictions.push_back({candidate.path, candidate.bytes});
        candidate.cache->m_FrameStats.textureEvictions++;
        candidate.cache->m_FrameStats.textureEvictedBytes += candidate.bytes;
        candidate.cache->m_TextureCache.erase(found);
        report.bytesAfter = RHIResourceStatsProvider::GetStats().liveResourceBytes;
    }
    report.targetReached = report.bytesAfter <= report.targetBytes;
    return report;
}

GpuMeshGarbageCollectionReport MaterialResourceCache::CollectGlobalMeshGarbage(uint64_t highWatermarkBytes,
                                                                               float lowWatermarkRatio,
                                                                               size_t maxEvictions,
                                                                               uint64_t activeGraceCollections) {
    GpuMeshGarbageCollectionReport report;
    report.bytesBefore = RHIResourceStatsProvider::GetStats().liveResourceBytes;
    report.bytesAfter = report.bytesBefore;
    report.targetBytes = static_cast<uint64_t>(highWatermarkBytes * lowWatermarkRatio);
    report.pressureDetected = report.bytesBefore > highWatermarkBytes;
    std::lock_guard<std::mutex> lock(g_CacheRegistryMutex);
    ++g_MeshCollection;
    for (auto it = g_MeshResidency.begin(); it != g_MeshResidency.end();) {
        if (it->second.lifetime.expired() || !it->first->IsUploaded())
            it = g_MeshResidency.erase(it);
        else
            ++it;
    }
    if (!report.pressureDetected || !maxEvictions)
        return report;
    struct Candidate {
        MeshAsset* mesh;
        MeshResidencyEntry entry;
    };
    std::vector<Candidate> candidates;
    for (const auto& item : g_MeshResidency)
        candidates.push_back({item.first, item.second});
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.entry.lastUse != b.entry.lastUse)
            return a.entry.lastUse < b.entry.lastUse;
        if (a.entry.path != b.entry.path)
            return a.entry.path < b.entry.path;
        return a.mesh < b.mesh;
    });
    for (const auto& candidate : candidates) {
        if (report.evictions.size() >= maxEvictions || report.bytesAfter <= report.targetBytes)
            break;
        auto found = g_MeshResidency.find(candidate.mesh);
        if (found == g_MeshResidency.end())
            continue;
        const bool alive = !found->second.lifetime.expired();
        if (alive && AssetManager::Get().IsPinned(found->second.path)) {
            report.blockedBytes += found->second.bytes;
            report.blockers.push_back({found->second.path, found->second.bytes, GpuTextureEvictionBlockReason::Pinned});
            continue;
        }
        if (alive && (candidate.mesh->HasExternalGpuBufferReferences() ||
                      g_MeshCollection - found->second.lastCollection <= activeGraceCollections)) {
            report.blockedBytes += found->second.bytes;
            report.blockers.push_back(
                {found->second.path, found->second.bytes, GpuTextureEvictionBlockReason::Referenced});
            continue;
        }
        if (alive)
            candidate.mesh->InvalidateGpuBuffers();
        report.evictions.push_back({found->second.path, found->second.bytes});
        g_MeshResidency.erase(found);
        report.bytesAfter = RHIResourceStatsProvider::GetStats().liveResourceBytes;
    }
    report.targetReached = report.bytesAfter <= report.targetBytes;
    return report;
}

void MaterialResourceCache::TrackGlobalMeshResidency(MeshAsset* mesh) {
    if (!mesh || !mesh->IsUploaded())
        return;
    std::lock_guard<std::mutex> lock(g_CacheRegistryMutex);
    g_MeshResidency[mesh] = {mesh->GetLifetimeToken(), mesh->GetPath(), mesh->GetGpuBufferBytes(), ++g_MeshUseClock,
                             g_MeshCollection};
}
