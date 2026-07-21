#include "Renderer/RenderGraph.h"
#include "Renderer/RHI/RHIResourceStats.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <queue>
#include <cmath>
#include <unordered_set>

namespace {
bool IsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D24S8 || format == RHIFormat::D32Float;
}

const char* UsageName(RHIResourceUsage usage) {
    switch (usage) {
    case RHIResourceUsage::ShaderResource:
        return "ShaderResource";
    case RHIResourceUsage::RenderTarget:
        return "RenderTarget";
    case RHIResourceUsage::DepthStencil:
        return "DepthStencil";
    case RHIResourceUsage::UnorderedAccess:
        return "UnorderedAccess";
    case RHIResourceUsage::CopySource:
        return "CopySource";
    case RHIResourceUsage::CopyDestination:
        return "CopyDestination";
    default:
        return "requested";
    }
}

bool HasPassFlag(RenderGraph::PassFlags flags, RenderGraph::PassFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

RHITextureViewDesc MakeViewDesc(RGTextureSubresource subresource, RHIResourceUsage usage) {
    RHITextureViewDesc desc;
    desc.firstMip = subresource.firstMip;
    desc.mipCount = subresource.mipCount;
    desc.firstLayer = subresource.firstLayer;
    desc.layerCount = subresource.layerCount;
    desc.usage = usage;
    return desc;
}

bool Overlaps(const RHITextureViewDesc& a, const RHITextureViewDesc& b) {
    const uint32_t aMipEnd = a.firstMip + a.mipCount;
    const uint32_t bMipEnd = b.firstMip + b.mipCount;
    const uint32_t aLayerEnd = a.firstLayer + a.layerCount;
    const uint32_t bLayerEnd = b.firstLayer + b.layerCount;
    const bool mipOverlap = a.firstMip < bMipEnd && b.firstMip < aMipEnd;
    const bool layerOverlap = a.firstLayer < bLayerEnd && b.firstLayer < aLayerEnd;
    return mipOverlap && layerOverlap;
}

bool SameSubresourceView(const RHITextureViewDesc& a, const RHITextureViewDesc& b) {
    return a.firstMip == b.firstMip && a.mipCount == b.mipCount && a.firstLayer == b.firstLayer &&
           a.layerCount == b.layerCount && a.usage == b.usage;
}

uint32_t SubresourceIndex(const RHITextureDesc& desc, uint32_t mip, uint32_t layer) {
    return layer * desc.mipLevels + mip;
}

std::string TexturePoolKey(const RHITextureDesc& desc) {
    std::ostringstream out;
    out << desc.width << 'x' << desc.height << ":m" << desc.mipLevels << ":l" << desc.arrayLayers << ":s"
        << desc.sampleCount << ":q" << desc.sampleQuality << ":f" << static_cast<int>(desc.format) << ":u"
        << static_cast<uint32_t>(desc.usage) << ":c" << desc.cube;
    return out.str();
}

std::string BufferPoolKey(const RHIBufferDesc& desc) {
    std::ostringstream out;
    out << desc.size << ":s" << desc.stride << ":u" << static_cast<uint32_t>(desc.usage);
    return out.str();
}
} // namespace

void RenderGraphBuilder::Add(Access access) {
    m_Accesses.push_back(access);
}

void RenderGraphBuilder::ReadTexture(RGTextureHandle h) {
    Add({h, RHIResourceState::ShaderResource, RHILoadOp::Load, RHIStoreOp::Store, {}, 1.0f, true, false});
}
void RenderGraphBuilder::ReadTexture(RGTextureHandle h, RGTextureSubresource subresource) {
    Access access{h, RHIResourceState::ShaderResource, RHILoadOp::Load, RHIStoreOp::Store, {}, 1.0f, true, false};
    access.viewDesc = MakeViewDesc(subresource, RHIResourceUsage::ShaderResource);
    access.hasViewDesc = true;
    Add(access);
}
void RenderGraphBuilder::WriteColor(RGTextureHandle h, RHILoadOp load, RHIStoreOp store, ClearColor clear) {
    Add({h, RHIResourceState::RenderTarget, load, store, clear, 1.0f, false, true});
}
void RenderGraphBuilder::WriteColor(RGTextureHandle h, RGTextureSubresource subresource, RHILoadOp load,
                                    RHIStoreOp store, ClearColor clear) {
    Access access{h, RHIResourceState::RenderTarget, load, store, clear, 1.0f, false, true};
    access.viewDesc = MakeViewDesc(subresource, RHIResourceUsage::RenderTarget);
    access.hasViewDesc = true;
    Add(access);
}
void RenderGraphBuilder::WriteDepth(RGTextureHandle h, RHILoadOp load, RHIStoreOp store, float clearDepth) {
    Add({h, RHIResourceState::DepthWrite, load, store, {}, clearDepth, false, true});
}
void RenderGraphBuilder::WriteDepth(RGTextureHandle h, RGTextureSubresource subresource, RHILoadOp load,
                                    RHIStoreOp store, float clearDepth) {
    Access access{h, RHIResourceState::DepthWrite, load, store, {}, clearDepth, false, true};
    access.viewDesc = MakeViewDesc(subresource, RHIResourceUsage::DepthStencil);
    access.hasViewDesc = true;
    Add(access);
}
void RenderGraphBuilder::ReadWriteUAV(RGTextureHandle h) {
    Add({h, RHIResourceState::UnorderedAccess, RHILoadOp::Load, RHIStoreOp::Store, {}, 1.0f, true, true});
}
void RenderGraphBuilder::ReadWriteUAV(RGTextureHandle h, RGTextureSubresource subresource) {
    Access access{h, RHIResourceState::UnorderedAccess, RHILoadOp::Load, RHIStoreOp::Store, {}, 1.0f, true, true};
    access.viewDesc = MakeViewDesc(subresource, RHIResourceUsage::UnorderedAccess);
    access.hasViewDesc = true;
    Add(access);
}
void RenderGraphBuilder::ReadBuffer(RGBufferHandle h) {
    if (m_BufferAccesses)
        m_BufferAccesses->push_back({h, RHIResourceState::ShaderResource, true, false});
}
void RenderGraphBuilder::ReadIndirect(RGBufferHandle h) {
    if (m_BufferAccesses)
        m_BufferAccesses->push_back({h, RHIResourceState::IndirectArgument, true, false});
}
void RenderGraphBuilder::ReadCopySource(RGBufferHandle h) {
    if (m_BufferAccesses)
        m_BufferAccesses->push_back({h, RHIResourceState::CopySource, true, false});
}
void RenderGraphBuilder::ReadWriteUAV(RGBufferHandle h) {
    if (m_BufferAccesses)
        m_BufferAccesses->push_back({h, RHIResourceState::UnorderedAccess, true, true});
}
void RenderGraphBuilder::ReadAccelerationStructure(RGAccelerationStructureHandle h) {
    if (m_AccelerationStructureAccesses)
        m_AccelerationStructureAccesses->push_back({h, true, false});
}
void RenderGraphBuilder::WriteAccelerationStructure(RGAccelerationStructureHandle h) {
    if (m_AccelerationStructureAccesses)
        m_AccelerationStructureAccesses->push_back({h, false, true});
}

RenderGraph::RenderGraph(IRHIDevice& device) : m_Device(device) {
}

bool RenderGraph::SetResourceBudget(const RenderGraphResourceBudget& value, std::string* error) {
    if (!value.maxTransientBytes || !value.maxTransientResources || !value.maxTransientDescriptors ||
        !value.maxPooledBytes || !std::isfinite(value.poolLowWatermarkRatio) || value.poolLowWatermarkRatio <= 0.0f ||
        value.poolLowWatermarkRatio > 1.0f) {
        if (error)
            *error = "invalid RenderGraph resource budget";
        return false;
    }
    m_ResourceBudget = value;
    if (error)
        error->clear();
    return true;
}

RGTextureHandle RenderGraph::ImportTexture(const std::string& name, const std::shared_ptr<GpuTexture>& texture,
                                           RHIResourceState initialState) {
    return ImportTexture(name, texture, initialState, RHIResourceState::Undefined);
}

RGTextureHandle RenderGraph::ImportTexture(const std::string& name, const std::shared_ptr<GpuTexture>& texture,
                                           RHIResourceState initialState, RHIResourceState finalState) {
    return ImportTexture(name, texture, nullptr, initialState, finalState);
}

RGTextureHandle RenderGraph::ImportTexture(const std::string& name, const std::shared_ptr<GpuTexture>& texture,
                                           const std::shared_ptr<GpuTextureView>& view, RHIResourceState initialState,
                                           RHIResourceState finalState) {
    TextureResource resource;
    resource.name = name;
    resource.texture = texture;
    resource.view = view;
    if (texture)
        resource.desc = texture->desc;
    resource.initialState = initialState;
    resource.currentState = initialState;
    resource.finalState = finalState;
    resource.hasFinalState = finalState != RHIResourceState::Undefined;
    resource.imported = true;
    m_Textures.push_back(std::move(resource));
    m_Compiled = false;
    return {static_cast<uint32_t>(m_Textures.size() - 1)};
}

RGTextureHandle RenderGraph::CreateTexture(const std::string& name, const RHITextureDesc& desc) {
    TextureResource resource;
    resource.name = name;
    resource.desc = desc;
    resource.desc.debugName = name;
    auto pooled = m_TexturePool.find(TexturePoolKey(desc));
    if (pooled != m_TexturePool.end() && !pooled->second.empty()) {
        auto reusable = std::move(pooled->second.back());
        pooled->second.pop_back();
        resource.texture = std::move(reusable.texture);
        resource.view = std::move(reusable.view);
        resource.reusedFromPool = true;
        if (pooled->second.empty())
            m_TexturePool.erase(pooled);
    }
    m_Textures.push_back(std::move(resource));
    m_Compiled = false;
    return {static_cast<uint32_t>(m_Textures.size() - 1)};
}

RGBufferHandle RenderGraph::ImportBuffer(const std::string& name, const std::shared_ptr<GpuBuffer>& buffer,
                                         RHIResourceState initialState) {
    return ImportBuffer(name, buffer, initialState, RHIResourceState::Undefined);
}

RGBufferHandle RenderGraph::ImportBuffer(const std::string& name, const std::shared_ptr<GpuBuffer>& buffer,
                                         RHIResourceState initialState, RHIResourceState finalState) {
    BufferResource resource;
    resource.name = name;
    resource.buffer = buffer;
    if (buffer)
        resource.desc = buffer->desc;
    resource.initialState = resource.currentState = initialState;
    resource.finalState = finalState;
    resource.hasFinalState = finalState != RHIResourceState::Undefined;
    resource.imported = true;
    m_Buffers.push_back(std::move(resource));
    m_Compiled = false;
    return {static_cast<uint32_t>(m_Buffers.size() - 1)};
}

void RenderGraph::SetFinalState(RGTextureHandle handle, RHIResourceState finalState) {
    if (!handle.IsValid() || handle.index >= m_Textures.size())
        return;
    auto& resource = m_Textures[handle.index];
    resource.finalState = finalState;
    resource.hasFinalState = finalState != RHIResourceState::Undefined;
    m_Compiled = false;
}

void RenderGraph::SetFinalState(RGBufferHandle handle, RHIResourceState finalState) {
    if (!handle.IsValid() || handle.index >= m_Buffers.size())
        return;
    auto& resource = m_Buffers[handle.index];
    resource.finalState = finalState;
    resource.hasFinalState = finalState != RHIResourceState::Undefined;
    m_Compiled = false;
}

RGBufferHandle RenderGraph::CreateBuffer(const std::string& name, const RHIBufferDesc& desc) {
    BufferResource resource;
    resource.name = name;
    resource.desc = desc;
    resource.desc.debugName = name;
    auto pooled = m_BufferPool.find(BufferPoolKey(desc));
    if (pooled != m_BufferPool.end() && !pooled->second.empty()) {
        auto reusable = std::move(pooled->second.back());
        pooled->second.pop_back();
        resource.buffer = std::move(reusable.buffer);
        resource.view = std::move(reusable.view);
        resource.reusedFromPool = true;
        if (pooled->second.empty())
            m_BufferPool.erase(pooled);
    }
    m_Buffers.push_back(std::move(resource));
    m_Compiled = false;
    return {static_cast<uint32_t>(m_Buffers.size() - 1)};
}

RGAccelerationStructureHandle
RenderGraph::ImportAccelerationStructure(const std::string& name,
                                         const std::shared_ptr<GpuAccelerationStructure>& accelerationStructure) {
    m_AccelerationStructures.push_back({name, accelerationStructure});
    m_Compiled = false;
    return {static_cast<uint32_t>(m_AccelerationStructures.size() - 1)};
}

void RenderGraph::AddPass(const std::string& name, SetupCallback setup, ExecuteCallback execute, PassFlags flags) {
    Pass pass;
    pass.name = name;
    pass.execute = std::move(execute);
    pass.flags = flags;
    RenderGraphBuilder builder(pass.accesses, pass.bufferAccesses, pass.accelerationStructureAccesses);
    setup(builder);
    m_Passes.push_back(std::move(pass));
    m_Compiled = false;
}

void RenderGraph::AddComputePass(const std::string& name, SetupCallback setup, ExecuteCallback execute,
                                 PassFlags flags) {
    Pass pass;
    pass.name = name;
    pass.execute = std::move(execute);
    pass.flags = flags;
    pass.type = PassType::Compute;
    RenderGraphBuilder builder(pass.accesses, pass.bufferAccesses, pass.accelerationStructureAccesses);
    setup(builder);
    m_Passes.push_back(std::move(pass));
    m_Compiled = false;
}

void RenderGraph::AddAccelerationStructurePass(const std::string& name, SetupCallback setup, ExecuteCallback execute,
                                               PassFlags flags) {
    Pass pass;
    pass.name = name;
    pass.execute = std::move(execute);
    pass.flags = flags;
    pass.type = PassType::AccelerationStructureBuild;
    RenderGraphBuilder builder(pass.accesses, pass.bufferAccesses, pass.accelerationStructureAccesses);
    setup(builder);
    m_Passes.push_back(std::move(pass));
    m_Compiled = false;
}

bool RenderGraph::SetError(ErrorCode code, std::string message) {
    m_LastErrorCode = code;
    m_LastError = std::move(message);
    return false;
}

bool RenderGraph::ValidateHandle(RGTextureHandle handle, const std::string& passName) {
    if (handle.IsValid() && handle.index < m_Textures.size())
        return true;
    return SetError(ErrorCode::InvalidTextureHandle,
                    "RenderGraph pass '" + passName + "' references an invalid texture");
}

bool RenderGraph::ValidateTextureAccess(const Pass& pass, const RenderGraphBuilder::Access& access) {
    if (!ValidateHandle(access.handle, pass.name))
        return false;
    const TextureResource& resource = m_Textures[access.handle.index];
    if (pass.type == PassType::Compute &&
        (access.state == RHIResourceState::RenderTarget || access.state == RHIResourceState::DepthWrite ||
         access.state == RHIResourceState::DepthRead)) {
        return SetError(ErrorCode::ComputeAttachmentAccess, "RenderGraph compute pass '" + pass.name +
                                                                "' cannot use texture '" + resource.name +
                                                                "' as a raster attachment");
    }
    if (access.hasViewDesc) {
        if (access.viewDesc.mipCount == 0 || access.viewDesc.layerCount == 0 ||
            access.viewDesc.firstMip + access.viewDesc.mipCount > resource.desc.mipLevels ||
            access.viewDesc.firstLayer + access.viewDesc.layerCount > resource.desc.arrayLayers) {
            return SetError(ErrorCode::InvalidTextureHandle, "RenderGraph pass '" + pass.name +
                                                                 "' references texture '" + resource.name +
                                                                 "' with an invalid subresource range");
        }
    }
    const auto requireUsage = [&](RHIResourceUsage usage) {
        if (HasUsage(resource.desc.usage, usage))
            return true;
        return SetError(ErrorCode::TextureUsageMismatch, "RenderGraph pass '" + pass.name + "' accesses texture '" +
                                                             resource.name + "' without " + UsageName(usage) +
                                                             " usage");
    };
    switch (access.state) {
    case RHIResourceState::ShaderResource:
        return requireUsage(RHIResourceUsage::ShaderResource);
    case RHIResourceState::RenderTarget:
        if (!requireUsage(RHIResourceUsage::RenderTarget))
            return false;
        if (IsDepthFormat(resource.desc.format)) {
            return SetError(ErrorCode::AttachmentFormatMismatch, "RenderGraph pass '" + pass.name +
                                                                     "' writes depth-format texture '" + resource.name +
                                                                     "' as a color attachment");
        }
        return true;
    case RHIResourceState::DepthWrite:
    case RHIResourceState::DepthRead:
        if (!requireUsage(RHIResourceUsage::DepthStencil))
            return false;
        if (!IsDepthFormat(resource.desc.format)) {
            return SetError(ErrorCode::AttachmentFormatMismatch, "RenderGraph pass '" + pass.name +
                                                                     "' uses non-depth texture '" + resource.name +
                                                                     "' as a depth attachment");
        }
        return true;
    case RHIResourceState::UnorderedAccess:
        return requireUsage(RHIResourceUsage::UnorderedAccess);
    case RHIResourceState::CopySource:
        return requireUsage(RHIResourceUsage::CopySource);
    case RHIResourceState::CopyDestination:
        return requireUsage(RHIResourceUsage::CopyDestination);
    default:
        return true;
    }
}

bool RenderGraph::ValidateBufferAccess(const Pass& pass, const RenderGraphBuilder::BufferAccess& access) {
    if (!access.handle.IsValid() || access.handle.index >= m_Buffers.size()) {
        return SetError(ErrorCode::InvalidBufferHandle,
                        "RenderGraph pass '" + pass.name + "' references an invalid buffer");
    }
    const BufferResource& resource = m_Buffers[access.handle.index];
    const auto requireUsage = [&](RHIResourceUsage usage) {
        if (HasUsage(resource.desc.usage, usage))
            return true;
        return SetError(ErrorCode::BufferUsageMismatch, "RenderGraph pass '" + pass.name + "' accesses buffer '" +
                                                            resource.name + "' without " + UsageName(usage) + " usage");
    };
    switch (access.state) {
    case RHIResourceState::ShaderResource:
        return requireUsage(RHIResourceUsage::ShaderResource);
    case RHIResourceState::UnorderedAccess:
        return requireUsage(RHIResourceUsage::UnorderedAccess);
    case RHIResourceState::CopySource:
        return requireUsage(RHIResourceUsage::CopySource);
    case RHIResourceState::CopyDestination:
        return requireUsage(RHIResourceUsage::CopyDestination);
    case RHIResourceState::IndirectArgument:
        return requireUsage(RHIResourceUsage::IndirectArguments);
    default:
        return true;
    }
}

bool RenderGraph::ValidateAccelerationStructureAccess(const Pass& pass,
                                                      const RenderGraphBuilder::AccelerationStructureAccess& access) {
    if (!access.handle.IsValid() || access.handle.index >= m_AccelerationStructures.size() ||
        !m_AccelerationStructures[access.handle.index].accelerationStructure) {
        return SetError(ErrorCode::InvalidAccelerationStructureHandle,
                        "RenderGraph pass '" + pass.name + "' references an invalid acceleration structure");
    }
    return true;
}

bool RenderGraph::Compile() {
    m_ResourcesReady = false;
    m_LastError.clear();
    m_LastErrorCode = ErrorCode::None;
    m_ExecutionOrder.clear();
    m_ExecutionOrderNames.clear();
    m_ExecutionPassTypes.clear();
    m_CulledPassNames.clear();
    m_LiveTextures.clear();
    m_LiveBuffers.clear();
    m_LiveAccelerationStructures.clear();
    const uint32_t passCount = static_cast<uint32_t>(m_Passes.size());
    // The last-writer/open-reader sets encode RAW, WAR, and WAW hazards. A new write must wait for both the previous
    // writer and every reader that still observes that version of the resource.
    std::vector<std::vector<uint32_t>> edges(passCount);
    std::vector<uint32_t> indegree(passCount, 0);
    std::vector<int32_t> lastWriter(m_Textures.size(), -1);
    std::vector<std::vector<uint32_t>> readers(m_Textures.size());
    std::vector<int32_t> lastBufferWriter(m_Buffers.size(), -1);
    std::vector<std::vector<uint32_t>> bufferReaders(m_Buffers.size());
    std::vector<int32_t> lastAccelerationStructureWriter(m_AccelerationStructures.size(), -1);
    std::vector<std::vector<uint32_t>> accelerationStructureReaders(m_AccelerationStructures.size());

    for (uint32_t p = 0; p < passCount; ++p) {
        if (m_Passes[p].accesses.empty() && m_Passes[p].bufferAccesses.empty() &&
            m_Passes[p].accelerationStructureAccesses.empty() &&
            !HasPassFlag(m_Passes[p].flags, PassFlags::AllowNoResourceAccess)) {
            return SetError(ErrorCode::MissingResourceAccess,
                            "RenderGraph pass '" + m_Passes[p].name + "' declares no resource access");
        }
        std::vector<RenderGraphBuilder::Access> seenTextureAccesses;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t colorAttachmentCount = 0;
        for (const auto& access : m_Passes[p].accesses) {
            if (!ValidateTextureAccess(m_Passes[p], access))
                return false;
            auto fullView = [&](uint32_t index) {
                RHITextureViewDesc desc;
                desc.mipCount = m_Textures[index].desc.mipLevels;
                desc.layerCount = m_Textures[index].desc.arrayLayers;
                desc.usage = m_Textures[index].desc.usage;
                return desc;
            };
            const RHITextureViewDesc accessView = access.hasViewDesc ? access.viewDesc : fullView(access.handle.index);
            for (const auto& seenAccess : seenTextureAccesses) {
                if (seenAccess.handle.index != access.handle.index)
                    continue;
                const RHITextureViewDesc seenView =
                    seenAccess.hasViewDesc ? seenAccess.viewDesc : fullView(seenAccess.handle.index);
                if (Overlaps(accessView, seenView)) {
                    return SetError(ErrorCode::DuplicateResourceAccess,
                                    "RenderGraph pass '" + m_Passes[p].name +
                                        "' declares overlapping texture subresources more than once");
                }
            }
            seenTextureAccesses.push_back(access);
            const uint32_t r = access.handle.index;
            const TextureResource& texture = m_Textures[r];
            const bool attachment =
                access.state == RHIResourceState::RenderTarget || access.state == RHIResourceState::DepthWrite;
            if (attachment) {
                const uint32_t mip = access.hasViewDesc ? access.viewDesc.firstMip : 0;
                const uint32_t attachmentWidth = (std::max)(1u, texture.desc.width >> mip);
                const uint32_t attachmentHeight = (std::max)(1u, texture.desc.height >> mip);
                if (attachmentWidth == 0 || attachmentHeight == 0) {
                    return SetError(ErrorCode::AttachmentSizeMismatch, "RenderGraph pass '" + m_Passes[p].name +
                                                                           "' uses zero-sized attachment '" +
                                                                           texture.name + "'");
                }
                if (renderWidth == 0 && renderHeight == 0) {
                    renderWidth = attachmentWidth;
                    renderHeight = attachmentHeight;
                } else if (renderWidth != attachmentWidth || renderHeight != attachmentHeight) {
                    return SetError(ErrorCode::AttachmentSizeMismatch,
                                    "RenderGraph pass '" + m_Passes[p].name +
                                        "' declares attachments with mismatched sizes");
                }
                if (access.state == RHIResourceState::RenderTarget)
                    ++colorAttachmentCount;
            }
            if (access.read && !access.write && lastWriter[r] < 0 && !m_Textures[r].imported) {
                return SetError(ErrorCode::UninitializedTextureRead, "RenderGraph pass '" + m_Passes[p].name +
                                                                         "' reads uninitialized texture '" +
                                                                         m_Textures[r].name + "'");
            }
            std::unordered_set<uint32_t> deps;
            if (access.read && lastWriter[r] >= 0)
                deps.insert(static_cast<uint32_t>(lastWriter[r]));
            if (access.write) {
                if (lastWriter[r] >= 0)
                    deps.insert(static_cast<uint32_t>(lastWriter[r]));
                deps.insert(readers[r].begin(), readers[r].end());
                readers[r].clear();
            }
            for (uint32_t dep : deps) {
                if (dep == p)
                    continue;
                if (std::find(edges[dep].begin(), edges[dep].end(), p) == edges[dep].end()) {
                    edges[dep].push_back(p);
                    ++indegree[p];
                }
            }
            if (access.write)
                lastWriter[r] = static_cast<int32_t>(p);
            if (access.read && !access.write)
                readers[r].push_back(p);
        }
        const uint32_t maxColorAttachments = (std::max)(1u, m_Device.GetCapabilities().maxColorAttachments);
        if (colorAttachmentCount > maxColorAttachments) {
            std::ostringstream out;
            out << "RenderGraph pass '" << m_Passes[p].name << "' declares " << colorAttachmentCount
                << " color attachments, but device supports " << maxColorAttachments;
            return SetError(ErrorCode::TooManyColorAttachments, out.str());
        }
        std::unordered_set<uint32_t> seenBuffers;
        for (const auto& access : m_Passes[p].bufferAccesses) {
            if (!ValidateBufferAccess(m_Passes[p], access))
                return false;
            if (!seenBuffers.insert(access.handle.index).second) {
                return SetError(ErrorCode::DuplicateResourceAccess,
                                "RenderGraph pass '" + m_Passes[p].name + "' declares the same buffer more than once");
            }
            const uint32_t r = access.handle.index;
            if (access.read && !access.write && lastBufferWriter[r] < 0 && !m_Buffers[r].imported) {
                return SetError(ErrorCode::UninitializedBufferRead, "RenderGraph pass '" + m_Passes[p].name +
                                                                        "' reads uninitialized buffer '" +
                                                                        m_Buffers[r].name + "'");
            }
            std::unordered_set<uint32_t> deps;
            if (access.read && lastBufferWriter[r] >= 0)
                deps.insert(static_cast<uint32_t>(lastBufferWriter[r]));
            if (access.write) {
                if (lastBufferWriter[r] >= 0)
                    deps.insert(static_cast<uint32_t>(lastBufferWriter[r]));
                deps.insert(bufferReaders[r].begin(), bufferReaders[r].end());
                bufferReaders[r].clear();
            }
            for (uint32_t dep : deps)
                if (dep != p && std::find(edges[dep].begin(), edges[dep].end(), p) == edges[dep].end()) {
                    edges[dep].push_back(p);
                    ++indegree[p];
                }
            if (access.write)
                lastBufferWriter[r] = static_cast<int32_t>(p);
            if (access.read && !access.write)
                bufferReaders[r].push_back(p);
        }
        std::unordered_set<uint32_t> seenAccelerationStructures;
        for (const auto& access : m_Passes[p].accelerationStructureAccesses) {
            if (!ValidateAccelerationStructureAccess(m_Passes[p], access))
                return false;
            if (!seenAccelerationStructures.insert(access.handle.index).second) {
                return SetError(ErrorCode::DuplicateResourceAccess,
                                "RenderGraph pass '" + m_Passes[p].name +
                                    "' declares the same acceleration structure more than once");
            }
            const uint32_t r = access.handle.index;
            std::unordered_set<uint32_t> deps;
            if (access.read && lastAccelerationStructureWriter[r] >= 0)
                deps.insert(static_cast<uint32_t>(lastAccelerationStructureWriter[r]));
            if (access.write) {
                if (lastAccelerationStructureWriter[r] >= 0)
                    deps.insert(static_cast<uint32_t>(lastAccelerationStructureWriter[r]));
                deps.insert(accelerationStructureReaders[r].begin(), accelerationStructureReaders[r].end());
                accelerationStructureReaders[r].clear();
            }
            for (uint32_t dep : deps)
                if (dep != p && std::find(edges[dep].begin(), edges[dep].end(), p) == edges[dep].end()) {
                    edges[dep].push_back(p);
                    ++indegree[p];
                }
            if (access.write)
                lastAccelerationStructureWriter[r] = static_cast<int32_t>(p);
            if (access.read && !access.write)
                accelerationStructureReaders[r].push_back(p);
        }
    }
    for (uint32_t i = 0; i < m_Textures.size(); ++i) {
        const auto& resource = m_Textures[i];
        if (!resource.hasFinalState)
            continue;
        RenderGraphBuilder::Access access;
        access.handle = {i};
        access.state = resource.finalState;
        Pass finalPass;
        finalPass.name = "FinalState";
        if (!ValidateTextureAccess(finalPass, access))
            return false;
    }
    for (uint32_t i = 0; i < m_Buffers.size(); ++i) {
        const auto& resource = m_Buffers[i];
        if (!resource.hasFinalState)
            continue;
        RenderGraphBuilder::BufferAccess access;
        access.handle = {i};
        access.state = resource.finalState;
        Pass finalPass;
        finalPass.name = "FinalState";
        if (!ValidateBufferAccess(finalPass, access))
            return false;
    }

    // Imported or final-state resources are externally observable roots. Walking their producers backwards keeps
    // required inputs live while allowing an unobserved transient write-only branch to be culled.
    std::vector<uint8_t> livePass(passCount, 0);
    std::vector<uint8_t> liveTextures(m_Textures.size(), 0);
    std::vector<uint8_t> liveBuffers(m_Buffers.size(), 0);
    std::vector<uint8_t> liveAccelerationStructures(m_AccelerationStructures.size(), 1);
    for (uint32_t i = 0; i < m_Textures.size(); ++i) {
        liveTextures[i] = m_Textures[i].imported || m_Textures[i].hasFinalState;
    }
    for (uint32_t i = 0; i < m_Buffers.size(); ++i) {
        liveBuffers[i] = m_Buffers[i].imported || m_Buffers[i].hasFinalState;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int32_t p = static_cast<int32_t>(passCount) - 1; p >= 0; --p) {
            bool hasWrites = false;
            for (const auto& access : m_Passes[p].accesses)
                hasWrites = hasWrites || access.write;
            for (const auto& access : m_Passes[p].bufferAccesses)
                hasWrites = hasWrites || access.write;
            for (const auto& access : m_Passes[p].accelerationStructureAccesses)
                hasWrites = hasWrites || access.write;
            bool passLive =
                livePass[p] != 0 || HasPassFlag(m_Passes[p].flags, PassFlags::AllowNoResourceAccess) || !hasWrites;
            for (const auto& access : m_Passes[p].accesses) {
                if (access.write && liveTextures[access.handle.index]) {
                    passLive = true;
                    break;
                }
            }
            if (!passLive) {
                for (const auto& access : m_Passes[p].bufferAccesses) {
                    if (access.write && liveBuffers[access.handle.index]) {
                        passLive = true;
                        break;
                    }
                }
            }
            if (!passLive) {
                for (const auto& access : m_Passes[p].accelerationStructureAccesses) {
                    if (access.write && liveAccelerationStructures[access.handle.index]) {
                        passLive = true;
                        break;
                    }
                }
            }
            if (!passLive)
                continue;
            if (!livePass[p]) {
                livePass[p] = 1;
                changed = true;
            }
            for (const auto& access : m_Passes[p].accesses) {
                if (access.read && !liveTextures[access.handle.index]) {
                    liveTextures[access.handle.index] = 1;
                    changed = true;
                }
            }
            for (const auto& access : m_Passes[p].bufferAccesses) {
                if (access.read && !liveBuffers[access.handle.index]) {
                    liveBuffers[access.handle.index] = 1;
                    changed = true;
                }
            }
            for (const auto& access : m_Passes[p].accelerationStructureAccesses) {
                if (access.read && !liveAccelerationStructures[access.handle.index]) {
                    liveAccelerationStructures[access.handle.index] = 1;
                    changed = true;
                }
            }
        }
    }
    m_LiveTextures = liveTextures;
    m_LiveBuffers = liveBuffers;
    m_LiveAccelerationStructures = liveAccelerationStructures;

    std::vector<uint32_t> liveIndegree(passCount, 0);
    for (uint32_t p = 0; p < passCount; ++p) {
        if (!livePass[p]) {
            m_CulledPassNames.push_back(m_Passes[p].name);
            continue;
        }
        for (uint32_t next : edges[p]) {
            if (livePass[next])
                ++liveIndegree[next];
        }
    }

    std::queue<uint32_t> ready;
    for (uint32_t p = 0; p < passCount; ++p) {
        if (livePass[p] && liveIndegree[p] == 0)
            ready.push(p);
    }
    while (!ready.empty()) {
        const uint32_t p = ready.front();
        ready.pop();
        m_ExecutionOrder.push_back(p);
        m_ExecutionOrderNames.push_back(m_Passes[p].name);
        m_ExecutionPassTypes.push_back(m_Passes[p].type);
        for (uint32_t next : edges[p]) {
            if (!livePass[next])
                continue;
            if (--liveIndegree[next] == 0)
                ready.push(next);
        }
    }
    const uint32_t livePassCount = static_cast<uint32_t>(std::count(livePass.begin(), livePass.end(), uint8_t{1}));
    if (m_ExecutionOrder.size() != livePassCount) {
        return SetError(ErrorCode::DependencyCycle, "RenderGraph contains a dependency cycle");
    }
    m_Compiled = true;
    return true;
}

bool RenderGraph::EnsureResources() {
    m_ResourceStats.transientRequestedBytes = 0;
    m_ResourceStats.transientAllocatedBytes = 0;
    m_ResourceStats.transientReusedBytes = 0;
    m_ResourceStats.transientTextures = 0;
    m_ResourceStats.transientBuffers = 0;
    m_ResourceStats.transientDescriptors = 0;
    m_ResourceStats.transientBudgetExceeded = false;
    for (uint32_t i = 0; i < m_Textures.size(); ++i)
        if (!m_Textures[i].imported && (i >= m_LiveTextures.size() || m_LiveTextures[i])) {
            m_ResourceStats.transientRequestedBytes += EstimateRHITextureBytes(m_Textures[i].desc);
            ++m_ResourceStats.transientTextures;
            ++m_ResourceStats.transientDescriptors;
        }
    for (uint32_t i = 0; i < m_Buffers.size(); ++i)
        if (!m_Buffers[i].imported && (i >= m_LiveBuffers.size() || m_LiveBuffers[i])) {
            m_ResourceStats.transientRequestedBytes += m_Buffers[i].desc.size;
            ++m_ResourceStats.transientBuffers;
            if (m_Buffers[i].desc.stride > 0 && (HasUsage(m_Buffers[i].desc.usage, RHIResourceUsage::ShaderResource) ||
                                                 HasUsage(m_Buffers[i].desc.usage, RHIResourceUsage::UnorderedAccess)))
                ++m_ResourceStats.transientDescriptors;
        }
    for (uint32_t passIndex : m_ExecutionOrder) {
        const auto& pass = m_Passes[passIndex];
        for (const auto& access : pass.accesses) {
            const bool manualAttachment =
                HasPassFlag(pass.flags, PassFlags::ManualRenderingScope) &&
                (access.state == RHIResourceState::RenderTarget || access.state == RHIResourceState::DepthWrite);
            if (access.hasViewDesc && !manualAttachment)
                ++m_ResourceStats.transientDescriptors;
        }
    }
    const uint64_t resources =
        static_cast<uint64_t>(m_ResourceStats.transientTextures) + m_ResourceStats.transientBuffers;
    if (m_ResourceStats.transientRequestedBytes > m_ResourceBudget.maxTransientBytes ||
        resources > m_ResourceBudget.maxTransientResources ||
        m_ResourceStats.transientDescriptors > m_ResourceBudget.maxTransientDescriptors) {
        m_ResourceStats.transientBudgetExceeded = true;
        return SetError(ErrorCode::TransientBudgetExceeded, "RenderGraph transient resource budget exceeded");
    }
    for (uint32_t index = 0; index < m_Textures.size(); ++index) {
        if (index < m_LiveTextures.size() && !m_LiveTextures[index])
            continue;
        auto& resource = m_Textures[index];
        if (!resource.texture) {
            resource.texture = m_Device.CreateTexture(resource.desc);
            if (!resource.texture) {
                return SetError(ErrorCode::ResourceCreationFailed,
                                "RHI failed to create RenderGraph texture '" + resource.name + "'");
            }
            m_ResourceStats.transientAllocatedBytes += EstimateRHITextureBytes(resource.desc);
        } else if (resource.reusedFromPool) {
            m_ResourceStats.transientReusedBytes += EstimateRHITextureBytes(resource.desc);
        }
        if (!resource.view) {
            RHITextureViewDesc viewDesc;
            viewDesc.mipCount = resource.desc.mipLevels;
            viewDesc.layerCount = resource.desc.arrayLayers;
            viewDesc.usage = resource.desc.usage;
            resource.view = m_Device.CreateTextureView(resource.texture, viewDesc);
            if (!resource.view) {
                return SetError(ErrorCode::ResourceCreationFailed,
                                "RHI failed to create RenderGraph view '" + resource.name + "'");
            }
        }
        resource.currentState = resource.initialState;
        resource.subresourceStates.assign(resource.desc.mipLevels * resource.desc.arrayLayers, resource.initialState);
    }
    for (uint32_t passIndex = 0; passIndex < m_Passes.size(); ++passIndex) {
        if (std::find(m_ExecutionOrder.begin(), m_ExecutionOrder.end(), passIndex) == m_ExecutionOrder.end())
            continue;
        auto& pass = m_Passes[passIndex];
        for (auto& access : pass.accesses) {
            if (!access.hasViewDesc || access.view)
                continue;
            const bool manualAttachment =
                HasPassFlag(pass.flags, PassFlags::ManualRenderingScope) &&
                (access.state == RHIResourceState::RenderTarget || access.state == RHIResourceState::DepthWrite);
            if (manualAttachment)
                continue;
            auto& resource = m_Textures[access.handle.index];
            access.view = m_Device.CreateTextureView(resource.texture, access.viewDesc);
            if (!access.view) {
                return SetError(ErrorCode::ResourceCreationFailed,
                                "RHI failed to create RenderGraph subresource view '" + resource.name + "'");
            }
        }
    }
    for (uint32_t index = 0; index < m_Buffers.size(); ++index) {
        if (index < m_LiveBuffers.size() && !m_LiveBuffers[index])
            continue;
        auto& resource = m_Buffers[index];
        if (!resource.buffer) {
            resource.buffer = m_Device.CreateBuffer(resource.desc);
            if (!resource.buffer) {
                return SetError(ErrorCode::ResourceCreationFailed,
                                "RHI failed to create RenderGraph buffer '" + resource.name + "'");
            }
            m_ResourceStats.transientAllocatedBytes += resource.desc.size;
        } else if (resource.reusedFromPool) {
            m_ResourceStats.transientReusedBytes += resource.desc.size;
        }
        if (!resource.view && resource.desc.stride > 0 &&
            (HasUsage(resource.desc.usage, RHIResourceUsage::ShaderResource) ||
             HasUsage(resource.desc.usage, RHIResourceUsage::UnorderedAccess))) {
            RHIBufferViewDesc viewDesc;
            viewDesc.elementCount = resource.desc.size / resource.desc.stride;
            viewDesc.usage = resource.desc.usage;
            resource.view = m_Device.CreateBufferView(resource.buffer, viewDesc);
            if (!resource.view) {
                return SetError(ErrorCode::ResourceCreationFailed,
                                "RHI failed to create RenderGraph buffer view '" + resource.name + "'");
            }
        }
        resource.currentState = resource.initialState;
    }
    return true;
}

bool RenderGraph::Prepare() {
    if (!m_Compiled && !Compile())
        return false;
    if (m_ResourcesReady)
        return true;
    m_ResourcesReady = EnsureResources();
    return m_ResourcesReady;
}

bool RenderGraph::Execute(GpuCommandList& commandList) {
    if (!Prepare())
        return false;
    RenderGraphResources resources(*this);
    m_TextureUavWriteScratch.assign(m_Textures.size(), 0);
    m_BufferUavWriteScratch.assign(m_Buffers.size(), 0);
    auto& textureUavWrites = m_TextureUavWriteScratch;
    auto& bufferUavWrites = m_BufferUavWriteScratch;
    for (uint32_t passIndex : m_ExecutionOrder) {
        Pass& pass = m_Passes[passIndex];
        std::array<RenderingAttachment, 8> colors{};
        uint32_t colorCount = 0;
        RenderingAttachment depth;
        bool hasDepth = false;
        uint32_t width = 0, height = 0;
        const bool manualTransitions = HasPassFlag(pass.flags, PassFlags::ManualResourceTransitions);
        for (const auto& access : pass.accesses) {
            auto& resource = m_Textures[access.handle.index];
            const auto transitionWholeTexture = [&](RHIResourceState after) {
                if (resource.subresourceStates.empty()) {
                    if (resource.currentState != after)
                        commandList.Transition(resource.texture.get(), resource.currentState, after);
                    resource.currentState = after;
                    return;
                }
                const bool anySubresourceNeedsTransition =
                    std::any_of(resource.subresourceStates.begin(), resource.subresourceStates.end(),
                                [&](RHIResourceState state) { return state != after; });
                if (!anySubresourceNeedsTransition) {
                    resource.currentState = after;
                    return;
                }
                const bool uniformBefore =
                    std::all_of(resource.subresourceStates.begin(), resource.subresourceStates.end(),
                                [&](RHIResourceState state) { return state == resource.currentState; });
                if (uniformBefore) {
                    if (resource.currentState != after)
                        commandList.Transition(resource.texture.get(), resource.currentState, after);
                } else {
                    for (uint32_t layer = 0; layer < resource.desc.arrayLayers; ++layer) {
                        for (uint32_t mip = 0; mip < resource.desc.mipLevels; ++mip) {
                            const uint32_t subresource = SubresourceIndex(resource.desc, mip, layer);
                            if (subresource >= resource.subresourceStates.size())
                                continue;
                            const RHIResourceState before = resource.subresourceStates[subresource];
                            if (before == after)
                                continue;
                            RHITextureViewDesc range;
                            range.firstMip = mip;
                            range.mipCount = 1;
                            range.firstLayer = layer;
                            range.layerCount = 1;
                            range.usage = resource.desc.usage;
                            commandList.TransitionTexture(resource.texture.get(), range, before, after);
                        }
                    }
                }
                std::fill(resource.subresourceStates.begin(), resource.subresourceStates.end(), after);
                resource.currentState = after;
            };
            if (!manualTransitions && access.state == RHIResourceState::UnorderedAccess &&
                resource.currentState == RHIResourceState::UnorderedAccess && textureUavWrites[access.handle.index]) {
                commandList.UAVBarrier(resource.texture.get());
            }
            if (manualTransitions) {
                // Manual passes emit their own per-face/per-mip barriers. Only mirror their declared terminal state so
                // later graph-managed passes do not emit a duplicate whole-resource transition.
                resource.currentState = resource.hasFinalState ? resource.finalState : access.state;
                std::fill(resource.subresourceStates.begin(), resource.subresourceStates.end(), resource.currentState);
            } else if (access.hasViewDesc) {
                for (uint32_t layer = 0; layer < access.viewDesc.layerCount; ++layer) {
                    for (uint32_t mip = 0; mip < access.viewDesc.mipCount; ++mip) {
                        const uint32_t absoluteMip = access.viewDesc.firstMip + mip;
                        const uint32_t absoluteLayer = access.viewDesc.firstLayer + layer;
                        const uint32_t subresource = SubresourceIndex(resource.desc, absoluteMip, absoluteLayer);
                        RHIResourceState before = subresource < resource.subresourceStates.size()
                                                      ? resource.subresourceStates[subresource]
                                                      : resource.currentState;
                        if (before == access.state)
                            continue;
                        RHITextureViewDesc range = access.viewDesc;
                        range.firstMip = absoluteMip;
                        range.mipCount = 1;
                        range.firstLayer = absoluteLayer;
                        range.layerCount = 1;
                        commandList.TransitionTexture(resource.texture.get(), range, before, access.state);
                        if (subresource < resource.subresourceStates.size())
                            resource.subresourceStates[subresource] = access.state;
                    }
                }
            } else if (resource.currentState != access.state ||
                       std::any_of(resource.subresourceStates.begin(), resource.subresourceStates.end(),
                                   [&](RHIResourceState state) { return state != access.state; })) {
                transitionWholeTexture(access.state);
            }
            resource.currentState = access.state;
            textureUavWrites[access.handle.index] =
                access.state == RHIResourceState::UnorderedAccess && access.write ? 1 : 0;
            const uint32_t mip = access.hasViewDesc ? access.viewDesc.firstMip : 0;
            width = (std::max)(1u, resource.desc.width >> mip);
            height = (std::max)(1u, resource.desc.height >> mip);
            RenderingAttachment attachment;
            attachment.view = access.hasViewDesc ? access.view.get() : resource.view.get();
            attachment.loadOp = access.loadOp;
            attachment.storeOp = access.storeOp;
            attachment.clearColor = access.clearColor;
            attachment.clearDepth = access.clearDepth;
            if (access.state == RHIResourceState::RenderTarget && colorCount < colors.size())
                colors[colorCount++] = attachment;
            if (access.state == RHIResourceState::DepthWrite) {
                depth = attachment;
                hasDepth = true;
            }
        }
        for (const auto& access : pass.bufferAccesses) {
            auto& resource = m_Buffers[access.handle.index];
            if (!manualTransitions && access.state == RHIResourceState::UnorderedAccess &&
                resource.currentState == RHIResourceState::UnorderedAccess && bufferUavWrites[access.handle.index]) {
                commandList.UAVBarrier(resource.buffer.get());
            }
            if (manualTransitions) {
                resource.currentState = resource.hasFinalState ? resource.finalState : access.state;
            } else if (resource.currentState != access.state) {
                commandList.Transition(resource.buffer.get(), resource.currentState, access.state);
                resource.currentState = access.state;
            }
            bufferUavWrites[access.handle.index] =
                access.state == RHIResourceState::UnorderedAccess && access.write ? 1 : 0;
        }
        const bool rendering =
            (colorCount != 0 || hasDepth) && !HasPassFlag(pass.flags, PassFlags::ManualRenderingScope);
        if (rendering) {
            RenderingInfo info{colors.data(), colorCount, hasDepth ? &depth : nullptr, width, height};
            commandList.BeginRendering(info);
        }
        commandList.BeginDebugEvent(pass.name.c_str());
        if (pass.execute)
            pass.execute(commandList, resources);
        if (rendering)
            commandList.EndRendering();
        commandList.EndDebugEvent();
    }
    for (auto& resource : m_Textures) {
        if (resource.hasFinalState &&
            (resource.currentState != resource.finalState ||
             std::any_of(resource.subresourceStates.begin(), resource.subresourceStates.end(),
                         [&](RHIResourceState state) { return state != resource.finalState; }))) {
            const bool uniformBefore =
                std::all_of(resource.subresourceStates.begin(), resource.subresourceStates.end(),
                            [&](RHIResourceState state) { return state == resource.currentState; });
            if (uniformBefore) {
                commandList.Transition(resource.texture.get(), resource.currentState, resource.finalState);
            } else {
                for (uint32_t layer = 0; layer < resource.desc.arrayLayers; ++layer) {
                    for (uint32_t mip = 0; mip < resource.desc.mipLevels; ++mip) {
                        const uint32_t subresource = SubresourceIndex(resource.desc, mip, layer);
                        if (subresource >= resource.subresourceStates.size())
                            continue;
                        const RHIResourceState before = resource.subresourceStates[subresource];
                        if (before == resource.finalState)
                            continue;
                        RHITextureViewDesc range;
                        range.firstMip = mip;
                        range.mipCount = 1;
                        range.firstLayer = layer;
                        range.layerCount = 1;
                        range.usage = resource.desc.usage;
                        commandList.TransitionTexture(resource.texture.get(), range, before, resource.finalState);
                    }
                }
            }
            resource.currentState = resource.finalState;
            std::fill(resource.subresourceStates.begin(), resource.subresourceStates.end(), resource.finalState);
        }
    }
    for (auto& resource : m_Buffers) {
        if (resource.hasFinalState && resource.currentState != resource.finalState) {
            commandList.Transition(resource.buffer.get(), resource.currentState, resource.finalState);
            resource.currentState = resource.finalState;
        }
    }
    return true;
}

void RenderGraph::Reset() {
    m_ResourceStats.poolEvictions = 0;
    m_ResourceStats.poolEvictedBytes = 0;
    // Pool by descriptor rather than debug name so equivalent transient resources can be reused across frames without
    // coupling allocation lifetime to a particular pass name.
    for (uint32_t index = 0; index < m_Textures.size(); ++index) {
        auto& resource = m_Textures[index];
        if (!resource.imported && resource.texture) {
            if (index >= m_LiveTextures.size() || m_LiveTextures[index]) {
                m_TexturePool[TexturePoolKey(resource.desc)].push_back(
                    {resource.desc, std::move(resource.texture), std::move(resource.view)});
            }
        }
    }
    for (uint32_t index = 0; index < m_Buffers.size(); ++index) {
        auto& resource = m_Buffers[index];
        if (!resource.imported && resource.buffer) {
            if (index >= m_LiveBuffers.size() || m_LiveBuffers[index]) {
                m_BufferPool[BufferPoolKey(resource.desc)].push_back(
                    {resource.desc, std::move(resource.buffer), std::move(resource.view)});
            }
        }
    }
    auto poolBytes = [&]() {
        uint64_t total = 0;
        for (const auto& pair : m_TexturePool)
            for (const auto& item : pair.second)
                total += EstimateRHITextureBytes(item.desc);
        for (const auto& pair : m_BufferPool)
            for (const auto& item : pair.second)
                total += item.desc.size;
        return total;
    };
    uint64_t total = poolBytes();
    if (total > m_ResourceBudget.maxPooledBytes) {
        const uint64_t target =
            static_cast<uint64_t>(m_ResourceBudget.maxPooledBytes * m_ResourceBudget.poolLowWatermarkRatio);
        std::vector<std::string> textureKeys, bufferKeys;
        for (const auto& pair : m_TexturePool)
            textureKeys.push_back(pair.first);
        for (const auto& pair : m_BufferPool)
            bufferKeys.push_back(pair.first);
        std::sort(textureKeys.begin(), textureKeys.end());
        std::sort(bufferKeys.begin(), bufferKeys.end());
        for (const auto& key : textureKeys) {
            auto found = m_TexturePool.find(key);
            while (found != m_TexturePool.end() && !found->second.empty() && total > target) {
                const uint64_t bytes = EstimateRHITextureBytes(found->second.back().desc);
                found->second.pop_back();
                total = total >= bytes ? total - bytes : 0;
                ++m_ResourceStats.poolEvictions;
                m_ResourceStats.poolEvictedBytes += bytes;
            }
            if (found != m_TexturePool.end() && found->second.empty())
                m_TexturePool.erase(found);
        }
        for (const auto& key : bufferKeys) {
            auto found = m_BufferPool.find(key);
            while (found != m_BufferPool.end() && !found->second.empty() && total > target) {
                const uint64_t bytes = found->second.back().desc.size;
                found->second.pop_back();
                total = total >= bytes ? total - bytes : 0;
                ++m_ResourceStats.poolEvictions;
                m_ResourceStats.poolEvictedBytes += bytes;
            }
            if (found != m_BufferPool.end() && found->second.empty())
                m_BufferPool.erase(found);
        }
    }
    m_ResourceStats.pooledBytes = total;
    m_ResourceStats.pooledTextures = 0;
    m_ResourceStats.pooledBuffers = 0;
    for (const auto& pair : m_TexturePool)
        m_ResourceStats.pooledTextures += static_cast<uint32_t>(pair.second.size());
    for (const auto& pair : m_BufferPool)
        m_ResourceStats.pooledBuffers += static_cast<uint32_t>(pair.second.size());
    m_Textures.clear();
    m_Buffers.clear();
    m_AccelerationStructures.clear();
    m_Passes.clear();
    m_ExecutionOrder.clear();
    m_ExecutionOrderNames.clear();
    m_ExecutionPassTypes.clear();
    m_CulledPassNames.clear();
    m_LiveTextures.clear();
    m_LiveBuffers.clear();
    m_LiveAccelerationStructures.clear();
    m_LastError.clear();
    m_LastErrorCode = ErrorCode::None;
    m_Compiled = false;
    m_ResourcesReady = false;
}

GpuTexture* RenderGraphResources::GetTexture(RGTextureHandle h) const {
    return h.index < m_Graph.m_Textures.size() ? m_Graph.m_Textures[h.index].texture.get() : nullptr;
}
GpuTextureView* RenderGraphResources::GetView(RGTextureHandle h) const {
    return h.index < m_Graph.m_Textures.size() ? m_Graph.m_Textures[h.index].view.get() : nullptr;
}
GpuTextureView* RenderGraphResources::GetView(RGTextureHandle h, RGTextureSubresource subresource) const {
    if (h.index >= m_Graph.m_Textures.size())
        return nullptr;
    const RHITextureViewDesc target = MakeViewDesc(subresource, RHIResourceUsage::ShaderResource);
    for (const auto& pass : m_Graph.m_Passes) {
        for (const auto& access : pass.accesses) {
            if (access.handle.index != h.index || !access.hasViewDesc || !access.view)
                continue;
            if (SameSubresourceView(access.viewDesc, target))
                return access.view.get();
        }
    }
    return nullptr;
}
GpuBuffer* RenderGraphResources::GetBuffer(RGBufferHandle h) const {
    return h.index < m_Graph.m_Buffers.size() ? m_Graph.m_Buffers[h.index].buffer.get() : nullptr;
}
GpuBufferView* RenderGraphResources::GetBufferView(RGBufferHandle h) const {
    return h.index < m_Graph.m_Buffers.size() ? m_Graph.m_Buffers[h.index].view.get() : nullptr;
}
GpuAccelerationStructure* RenderGraphResources::GetAccelerationStructure(RGAccelerationStructureHandle h) const {
    return h.index < m_Graph.m_AccelerationStructures.size()
               ? m_Graph.m_AccelerationStructures[h.index].accelerationStructure.get()
               : nullptr;
}
