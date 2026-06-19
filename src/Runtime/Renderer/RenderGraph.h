#pragma once

#include "Renderer/IRenderContext.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

struct RGTextureHandle {
    uint32_t index = UINT32_MAX;
    bool IsValid() const { return index != UINT32_MAX; }
};
struct RGBufferHandle {
    uint32_t index = UINT32_MAX;
    bool IsValid() const { return index != UINT32_MAX; }
};

class RenderGraph;

class RenderGraphResources {
public:
    GpuTexture* GetTexture(RGTextureHandle handle) const;
    GpuTextureView* GetView(RGTextureHandle handle) const;
    GpuBuffer* GetBuffer(RGBufferHandle handle) const;
    GpuBufferView* GetBufferView(RGBufferHandle handle) const;

private:
    friend class RenderGraph;
    explicit RenderGraphResources(const RenderGraph& graph) : m_Graph(graph) {}
    const RenderGraph& m_Graph;
};

class RenderGraphBuilder {
public:
    void ReadTexture(RGTextureHandle handle);
    void WriteColor(RGTextureHandle handle, RHILoadOp load = RHILoadOp::Load,
                    RHIStoreOp store = RHIStoreOp::Store,
                    ClearColor clear = {});
    void WriteDepth(RGTextureHandle handle, RHILoadOp load = RHILoadOp::Load,
                    RHIStoreOp store = RHIStoreOp::Store, float clearDepth = 1.0f);
    void ReadWriteUAV(RGTextureHandle handle);
    void ReadBuffer(RGBufferHandle handle);
    void ReadWriteUAV(RGBufferHandle handle);

private:
    friend class RenderGraph;
    struct Access {
        RGTextureHandle handle;
        RHIResourceState state = RHIResourceState::Undefined;
        RHILoadOp loadOp = RHILoadOp::Load;
        RHIStoreOp storeOp = RHIStoreOp::Store;
        ClearColor clearColor{};
        float clearDepth = 1.0f;
        bool read = false;
        bool write = false;
    };
    void Add(Access access);
    std::vector<Access>& m_Accesses;
    struct BufferAccess {
        RGBufferHandle handle;
        RHIResourceState state = RHIResourceState::Undefined;
        bool read = false;
        bool write = false;
    };
    RenderGraphBuilder(std::vector<Access>& accesses,
                       std::vector<BufferAccess>& bufferAccesses)
        : m_Accesses(accesses), m_BufferAccesses(&bufferAccesses) {}
    std::vector<BufferAccess>* m_BufferAccesses = nullptr;
};

class RenderGraph {
public:
    using SetupCallback = std::function<void(RenderGraphBuilder&)>;
    using ExecuteCallback = std::function<void(GpuCommandList&, const RenderGraphResources&)>;

    explicit RenderGraph(IRenderContext& device);

    RGTextureHandle ImportTexture(const std::string& name,
                                  const std::shared_ptr<GpuTexture>& texture,
                                  RHIResourceState initialState);
    RGTextureHandle CreateTexture(const std::string& name, const RHITextureDesc& desc);
    RGBufferHandle ImportBuffer(const std::string& name,
                                const std::shared_ptr<GpuBuffer>& buffer,
                                RHIResourceState initialState);
    RGBufferHandle CreateBuffer(const std::string& name, const RHIBufferDesc& desc);
    void AddPass(const std::string& name, SetupCallback setup, ExecuteCallback execute);

    bool Compile();
    bool Execute();
    void Reset();
    const std::string& GetLastError() const { return m_LastError; }
    const std::vector<std::string>& GetExecutionOrder() const { return m_ExecutionOrderNames; }

private:
    friend class RenderGraphResources;
    struct TextureResource {
        std::string name;
        RHITextureDesc desc;
        std::shared_ptr<GpuTexture> texture;
        std::shared_ptr<GpuTextureView> view;
        RHIResourceState initialState = RHIResourceState::Undefined;
        RHIResourceState currentState = RHIResourceState::Undefined;
        bool imported = false;
    };
    struct Pass {
        std::string name;
        std::vector<RenderGraphBuilder::Access> accesses;
        std::vector<RenderGraphBuilder::BufferAccess> bufferAccesses;
        ExecuteCallback execute;
    };
    struct PooledTexture {
        RHITextureDesc desc;
        std::shared_ptr<GpuTexture> texture;
        std::shared_ptr<GpuTextureView> view;
    };
    struct BufferResource {
        std::string name;
        RHIBufferDesc desc;
        std::shared_ptr<GpuBuffer> buffer;
        std::shared_ptr<GpuBufferView> view;
        RHIResourceState initialState = RHIResourceState::Undefined;
        RHIResourceState currentState = RHIResourceState::Undefined;
        bool imported = false;
    };
    struct PooledBuffer {
        RHIBufferDesc desc;
        std::shared_ptr<GpuBuffer> buffer;
        std::shared_ptr<GpuBufferView> view;
    };

    bool ValidateHandle(RGTextureHandle handle, const std::string& passName);
    bool EnsureResources();

    IRenderContext& m_Device;
    std::vector<TextureResource> m_Textures;
    std::vector<BufferResource> m_Buffers;
    std::vector<Pass> m_Passes;
    std::vector<uint32_t> m_ExecutionOrder;
    std::vector<std::string> m_ExecutionOrderNames;
    std::string m_LastError;
    bool m_Compiled = false;
    std::unordered_map<std::string, PooledTexture> m_TexturePool;
    std::unordered_map<std::string, PooledBuffer> m_BufferPool;
};
