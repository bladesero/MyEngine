#include "Renderer/RenderGraph.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

void RenderGraphBuilder::Add(Access access) { m_Accesses.push_back(access); }

void RenderGraphBuilder::ReadTexture(RGTextureHandle h) {
    Add({h, RHIResourceState::ShaderResource, RHILoadOp::Load, RHIStoreOp::Store,
         {}, 1.0f, true, false});
}
void RenderGraphBuilder::WriteColor(RGTextureHandle h, RHILoadOp load,
                                    RHIStoreOp store, ClearColor clear) {
    Add({h, RHIResourceState::RenderTarget, load, store, clear, 1.0f, false, true});
}
void RenderGraphBuilder::WriteDepth(RGTextureHandle h, RHILoadOp load,
                                    RHIStoreOp store, float clearDepth) {
    Add({h, RHIResourceState::DepthWrite, load, store, {}, clearDepth, false, true});
}
void RenderGraphBuilder::ReadWriteUAV(RGTextureHandle h) {
    Add({h, RHIResourceState::UnorderedAccess, RHILoadOp::Load, RHIStoreOp::Store,
         {}, 1.0f, true, true});
}
void RenderGraphBuilder::ReadBuffer(RGBufferHandle h) {
    if (m_BufferAccesses) m_BufferAccesses->push_back(
        {h, RHIResourceState::ShaderResource, true, false});
}
void RenderGraphBuilder::ReadWriteUAV(RGBufferHandle h) {
    if (m_BufferAccesses) m_BufferAccesses->push_back(
        {h, RHIResourceState::UnorderedAccess, true, true});
}

RenderGraph::RenderGraph(IRenderContext& device) : m_Device(device) {}

RGTextureHandle RenderGraph::ImportTexture(const std::string& name,
                                           const std::shared_ptr<GpuTexture>& texture,
                                           RHIResourceState initialState) {
    TextureResource resource;
    resource.name = name;
    resource.texture = texture;
    if (texture) resource.desc = texture->desc;
    resource.initialState = initialState;
    resource.currentState = initialState;
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
    auto pooled = m_TexturePool.find(name);
    if (pooled != m_TexturePool.end()) {
        const auto& old = pooled->second.desc;
        if (old.width == desc.width && old.height == desc.height &&
            old.mipLevels == desc.mipLevels && old.arrayLayers == desc.arrayLayers &&
            old.format == desc.format && old.usage == desc.usage && old.cube == desc.cube) {
            resource.texture = std::move(pooled->second.texture);
            resource.view = std::move(pooled->second.view);
        }
        m_TexturePool.erase(pooled);
    }
    m_Textures.push_back(std::move(resource));
    m_Compiled = false;
    return {static_cast<uint32_t>(m_Textures.size() - 1)};
}

RGBufferHandle RenderGraph::ImportBuffer(const std::string& name,
                                         const std::shared_ptr<GpuBuffer>& buffer,
                                         RHIResourceState initialState) {
    BufferResource resource; resource.name = name; resource.buffer = buffer;
    if (buffer) resource.desc = buffer->desc;
    resource.initialState = resource.currentState = initialState;
    resource.imported = true; m_Buffers.push_back(std::move(resource));
    m_Compiled = false; return {static_cast<uint32_t>(m_Buffers.size() - 1)};
}

RGBufferHandle RenderGraph::CreateBuffer(const std::string& name, const RHIBufferDesc& desc) {
    BufferResource resource; resource.name = name; resource.desc = desc;
    resource.desc.debugName = name;
    auto pooled = m_BufferPool.find(name);
    if (pooled != m_BufferPool.end()) {
        const auto& old = pooled->second.desc;
        if (old.size == desc.size && old.stride == desc.stride && old.usage == desc.usage) {
            resource.buffer = std::move(pooled->second.buffer);
            resource.view = std::move(pooled->second.view);
        }
        m_BufferPool.erase(pooled);
    }
    m_Buffers.push_back(std::move(resource)); m_Compiled = false;
    return {static_cast<uint32_t>(m_Buffers.size() - 1)};
}

void RenderGraph::AddPass(const std::string& name, SetupCallback setup,
                          ExecuteCallback execute) {
    Pass pass;
    pass.name = name;
    pass.execute = std::move(execute);
    RenderGraphBuilder builder(pass.accesses, pass.bufferAccesses);
    setup(builder);
    m_Passes.push_back(std::move(pass));
    m_Compiled = false;
}

bool RenderGraph::ValidateHandle(RGTextureHandle handle, const std::string& passName) {
    if (handle.IsValid() && handle.index < m_Textures.size()) return true;
    m_LastError = "RenderGraph pass '" + passName + "' references an invalid texture";
    return false;
}

bool RenderGraph::Compile() {
    m_LastError.clear();
    m_ExecutionOrder.clear();
    m_ExecutionOrderNames.clear();
    const uint32_t passCount = static_cast<uint32_t>(m_Passes.size());
    std::vector<std::vector<uint32_t>> edges(passCount);
    std::vector<uint32_t> indegree(passCount, 0);
    std::vector<int32_t> lastWriter(m_Textures.size(), -1);
    std::vector<std::vector<uint32_t>> readers(m_Textures.size());
    std::vector<int32_t> lastBufferWriter(m_Buffers.size(), -1);
    std::vector<std::vector<uint32_t>> bufferReaders(m_Buffers.size());

    for (uint32_t p = 0; p < passCount; ++p) {
        std::unordered_set<uint32_t> seen;
        for (const auto& access : m_Passes[p].accesses) {
            if (!ValidateHandle(access.handle, m_Passes[p].name)) return false;
            if (!seen.insert(access.handle.index).second) {
                m_LastError = "RenderGraph pass '" + m_Passes[p].name +
                              "' declares the same texture more than once";
                return false;
            }
            const uint32_t r = access.handle.index;
            if (access.read && !access.write && lastWriter[r] < 0 && !m_Textures[r].imported) {
                m_LastError = "RenderGraph pass '" + m_Passes[p].name +
                              "' reads uninitialized texture '" + m_Textures[r].name + "'";
                return false;
            }
            std::unordered_set<uint32_t> deps;
            if (access.read && lastWriter[r] >= 0) deps.insert(static_cast<uint32_t>(lastWriter[r]));
            if (access.write) {
                if (lastWriter[r] >= 0) deps.insert(static_cast<uint32_t>(lastWriter[r]));
                deps.insert(readers[r].begin(), readers[r].end());
                readers[r].clear();
            }
            for (uint32_t dep : deps) {
                if (dep == p) continue;
                if (std::find(edges[dep].begin(), edges[dep].end(), p) == edges[dep].end()) {
                    edges[dep].push_back(p);
                    ++indegree[p];
                }
            }
            if (access.write) lastWriter[r] = static_cast<int32_t>(p);
            if (access.read && !access.write) readers[r].push_back(p);
        }
        std::unordered_set<uint32_t> seenBuffers;
        for (const auto& access : m_Passes[p].bufferAccesses) {
            if (!access.handle.IsValid() || access.handle.index >= m_Buffers.size()) {
                m_LastError = "RenderGraph pass '" + m_Passes[p].name +
                              "' references an invalid buffer"; return false;
            }
            if (!seenBuffers.insert(access.handle.index).second) {
                m_LastError = "RenderGraph pass '" + m_Passes[p].name +
                              "' declares the same buffer more than once"; return false;
            }
            const uint32_t r = access.handle.index;
            if (access.read && !access.write && lastBufferWriter[r] < 0 && !m_Buffers[r].imported) {
                m_LastError = "RenderGraph pass '" + m_Passes[p].name +
                              "' reads uninitialized buffer '" + m_Buffers[r].name + "'";
                return false;
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
            for (uint32_t dep : deps) if (dep != p &&
                std::find(edges[dep].begin(), edges[dep].end(), p) == edges[dep].end()) {
                edges[dep].push_back(p); ++indegree[p];
            }
            if (access.write) lastBufferWriter[r] = static_cast<int32_t>(p);
            if (access.read && !access.write) bufferReaders[r].push_back(p);
        }
    }

    std::queue<uint32_t> ready;
    for (uint32_t p = 0; p < passCount; ++p) if (indegree[p] == 0) ready.push(p);
    while (!ready.empty()) {
        const uint32_t p = ready.front(); ready.pop();
        m_ExecutionOrder.push_back(p);
        m_ExecutionOrderNames.push_back(m_Passes[p].name);
        for (uint32_t next : edges[p]) if (--indegree[next] == 0) ready.push(next);
    }
    if (m_ExecutionOrder.size() != passCount) {
        m_LastError = "RenderGraph contains a dependency cycle";
        return false;
    }
    m_Compiled = true;
    return true;
}

bool RenderGraph::EnsureResources() {
    for (auto& resource : m_Textures) {
        if (!resource.texture) {
            resource.texture = m_Device.CreateTexture(resource.desc);
            if (!resource.texture) {
                m_LastError = "RHI failed to create RenderGraph texture '" + resource.name + "'";
                return false;
            }
        }
        if (!resource.view) {
            RHITextureViewDesc viewDesc;
            viewDesc.mipCount = resource.desc.mipLevels;
            viewDesc.layerCount = resource.desc.arrayLayers;
            viewDesc.usage = resource.desc.usage;
            resource.view = m_Device.CreateTextureView(resource.texture, viewDesc);
            if (!resource.view) {
                m_LastError = "RHI failed to create RenderGraph view '" + resource.name + "'";
                return false;
            }
        }
        resource.currentState = resource.initialState;
    }
    for (auto& resource : m_Buffers) {
        if (!resource.buffer) {
            resource.buffer = m_Device.CreateBuffer(resource.desc);
            if (!resource.buffer) {
                m_LastError = "RHI failed to create RenderGraph buffer '" + resource.name + "'";
                return false;
            }
        }
        if (!resource.view && resource.desc.stride > 0 &&
            (HasUsage(resource.desc.usage, RHIResourceUsage::ShaderResource) ||
             HasUsage(resource.desc.usage, RHIResourceUsage::UnorderedAccess))) {
            RHIBufferViewDesc viewDesc;
            viewDesc.elementCount = resource.desc.size / resource.desc.stride;
            viewDesc.usage = resource.desc.usage;
            resource.view = m_Device.CreateBufferView(resource.buffer, viewDesc);
            if (!resource.view) {
                m_LastError = "RHI failed to create RenderGraph buffer view '" + resource.name + "'";
                return false;
            }
        }
        resource.currentState = resource.initialState;
    }
    return true;
}

bool RenderGraph::Execute() {
    if (!m_Compiled && !Compile()) return false;
    if (!EnsureResources()) return false;
    GpuCommandList* commandList = m_Device.GetGraphicsCommandList();
    if (!commandList) { m_LastError = "RHI returned no graphics command list"; return false; }
    RenderGraphResources resources(*this);
    for (uint32_t passIndex : m_ExecutionOrder) {
        Pass& pass = m_Passes[passIndex];
        std::vector<RenderingAttachment> colors;
        RenderingAttachment depth;
        bool hasDepth = false;
        uint32_t width = 0, height = 0;
        for (const auto& access : pass.accesses) {
            auto& resource = m_Textures[access.handle.index];
            if (resource.currentState != access.state) {
                commandList->Transition(resource.texture.get(), resource.currentState, access.state);
                resource.currentState = access.state;
            }
            width = resource.desc.width; height = resource.desc.height;
            RenderingAttachment attachment;
            attachment.view = resource.view.get();
            attachment.loadOp = access.loadOp;
            attachment.storeOp = access.storeOp;
            attachment.clearColor = access.clearColor;
            attachment.clearDepth = access.clearDepth;
            if (access.state == RHIResourceState::RenderTarget) colors.push_back(attachment);
            if (access.state == RHIResourceState::DepthWrite) { depth = attachment; hasDepth = true; }
        }
        for (const auto& access : pass.bufferAccesses) {
            auto& resource = m_Buffers[access.handle.index];
            if (resource.currentState != access.state) {
                commandList->Transition(resource.buffer.get(), resource.currentState, access.state);
                resource.currentState = access.state;
            }
        }
        const bool rendering = !colors.empty() || hasDepth;
        if (rendering) {
            RenderingInfo info{colors.data(), static_cast<uint32_t>(colors.size()),
                               hasDepth ? &depth : nullptr, width, height};
            commandList->BeginRendering(info);
        }
        if (pass.execute) pass.execute(*commandList, resources);
        if (rendering) commandList->EndRendering();
    }
    return true;
}

void RenderGraph::Reset() {
    for (auto& resource : m_Textures) {
        if (!resource.imported && resource.texture) {
            m_TexturePool[resource.name] = {resource.desc, std::move(resource.texture),
                                            std::move(resource.view)};
        }
    }
    for (auto& resource : m_Buffers) if (!resource.imported && resource.buffer) {
        m_BufferPool[resource.name] = {resource.desc, std::move(resource.buffer),
                                       std::move(resource.view)};
    }
    m_Textures.clear(); m_Buffers.clear(); m_Passes.clear(); m_ExecutionOrder.clear();
    m_ExecutionOrderNames.clear(); m_LastError.clear(); m_Compiled = false;
}

GpuTexture* RenderGraphResources::GetTexture(RGTextureHandle h) const {
    return h.index < m_Graph.m_Textures.size() ? m_Graph.m_Textures[h.index].texture.get() : nullptr;
}
GpuTextureView* RenderGraphResources::GetView(RGTextureHandle h) const {
    return h.index < m_Graph.m_Textures.size() ? m_Graph.m_Textures[h.index].view.get() : nullptr;
}
GpuBuffer* RenderGraphResources::GetBuffer(RGBufferHandle h) const {
    return h.index < m_Graph.m_Buffers.size() ? m_Graph.m_Buffers[h.index].buffer.get() : nullptr;
}
GpuBufferView* RenderGraphResources::GetBufferView(RGBufferHandle h) const {
    return h.index < m_Graph.m_Buffers.size() ? m_Graph.m_Buffers[h.index].view.get() : nullptr;
}
