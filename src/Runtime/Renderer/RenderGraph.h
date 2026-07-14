#pragma once

#include "Renderer/RHI/IRHIDevice.h"
#include "Renderer/RHI/GpuCommandList.h"

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

struct RenderGraphResourceBudget {
    uint64_t maxTransientBytes = 512ull * 1024ull * 1024ull;
    uint32_t maxTransientResources = 256;
    uint32_t maxTransientDescriptors = 2048;
    uint64_t maxPooledBytes = 512ull * 1024ull * 1024ull;
    float poolLowWatermarkRatio = 0.8f;
};
struct RenderGraphResourceStats {
    uint64_t transientRequestedBytes = 0, transientAllocatedBytes = 0, transientReusedBytes = 0;
    uint64_t pooledBytes = 0, poolEvictedBytes = 0;
    uint32_t transientTextures = 0, transientBuffers = 0, transientDescriptors = 0;
    uint32_t pooledTextures = 0, pooledBuffers = 0, poolEvictions = 0;
    bool transientBudgetExceeded = false;
};

struct RGTextureSubresource {
    uint32_t firstMip = 0;
    uint32_t mipCount = 1;
    uint32_t firstLayer = 0;
    uint32_t layerCount = 1;
};

class RenderGraph;

class RenderGraphResources {
public:
    GpuTexture* GetTexture(RGTextureHandle handle) const;
    GpuTextureView* GetView(RGTextureHandle handle) const;
    GpuTextureView* GetView(RGTextureHandle handle, RGTextureSubresource subresource) const;
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
    void ReadTexture(RGTextureHandle handle, RGTextureSubresource subresource);
    void WriteColor(RGTextureHandle handle, RHILoadOp load = RHILoadOp::Load, RHIStoreOp store = RHIStoreOp::Store,
                    ClearColor clear = {});
    void WriteColor(RGTextureHandle handle, RGTextureSubresource subresource, RHILoadOp load = RHILoadOp::Load,
                    RHIStoreOp store = RHIStoreOp::Store, ClearColor clear = {});
    void WriteDepth(RGTextureHandle handle, RHILoadOp load = RHILoadOp::Load, RHIStoreOp store = RHIStoreOp::Store,
                    float clearDepth = 1.0f);
    void WriteDepth(RGTextureHandle handle, RGTextureSubresource subresource, RHILoadOp load = RHILoadOp::Load,
                    RHIStoreOp store = RHIStoreOp::Store, float clearDepth = 1.0f);
    void ReadWriteUAV(RGTextureHandle handle);
    void ReadWriteUAV(RGTextureHandle handle, RGTextureSubresource subresource);
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
        RHITextureViewDesc viewDesc{};
        bool hasViewDesc = false;
        std::shared_ptr<GpuTextureView> view;
    };
    void Add(Access access);
    std::vector<Access>& m_Accesses;
    struct BufferAccess {
        RGBufferHandle handle;
        RHIResourceState state = RHIResourceState::Undefined;
        bool read = false;
        bool write = false;
    };
    RenderGraphBuilder(std::vector<Access>& accesses, std::vector<BufferAccess>& bufferAccesses)
        : m_Accesses(accesses), m_BufferAccesses(&bufferAccesses) {}
    std::vector<BufferAccess>* m_BufferAccesses = nullptr;
};

class RenderGraph {
public:
    using SetupCallback = std::function<void(RenderGraphBuilder&)>;
    using ExecuteCallback = std::function<void(GpuCommandList&, const RenderGraphResources&)>;
    enum class PassFlags : uint32_t {
        None = 0,
        AllowNoResourceAccess = 1u << 0,
        ManualRenderingScope = 1u << 1,
        ManualResourceTransitions = 1u << 2,
    };
    enum class ErrorCode {
        None,
        InvalidTextureHandle,
        InvalidBufferHandle,
        MissingResourceAccess,
        DuplicateResourceAccess,
        UninitializedTextureRead,
        UninitializedBufferRead,
        TextureUsageMismatch,
        BufferUsageMismatch,
        AttachmentSizeMismatch,
        AttachmentFormatMismatch,
        TooManyColorAttachments,
        DependencyCycle,
        ResourceCreationFailed,
        TransientBudgetExceeded,
    };

    explicit RenderGraph(IRHIDevice& device);

    RGTextureHandle ImportTexture(const std::string& name, const std::shared_ptr<GpuTexture>& texture,
                                  RHIResourceState initialState);
    RGTextureHandle ImportTexture(const std::string& name, const std::shared_ptr<GpuTexture>& texture,
                                  RHIResourceState initialState, RHIResourceState finalState);
    RGTextureHandle ImportTexture(const std::string& name, const std::shared_ptr<GpuTexture>& texture,
                                  const std::shared_ptr<GpuTextureView>& view, RHIResourceState initialState,
                                  RHIResourceState finalState);
    RGTextureHandle CreateTexture(const std::string& name, const RHITextureDesc& desc);
    RGBufferHandle ImportBuffer(const std::string& name, const std::shared_ptr<GpuBuffer>& buffer,
                                RHIResourceState initialState);
    RGBufferHandle ImportBuffer(const std::string& name, const std::shared_ptr<GpuBuffer>& buffer,
                                RHIResourceState initialState, RHIResourceState finalState);
    RGBufferHandle CreateBuffer(const std::string& name, const RHIBufferDesc& desc);
    void SetFinalState(RGTextureHandle handle, RHIResourceState finalState);
    void SetFinalState(RGBufferHandle handle, RHIResourceState finalState);
    void AddPass(const std::string& name, SetupCallback setup, ExecuteCallback execute,
                 PassFlags flags = PassFlags::None);

    bool Compile();
    bool Execute(GpuCommandList& commandList);
    void Reset();
    const std::string& GetLastError() const { return m_LastError; }
    ErrorCode GetLastErrorCode() const { return m_LastErrorCode; }
    const std::vector<std::string>& GetExecutionOrder() const { return m_ExecutionOrderNames; }
    const std::vector<std::string>& GetCulledPasses() const { return m_CulledPassNames; }
    bool SetResourceBudget(const RenderGraphResourceBudget&, std::string* error = nullptr);
    const RenderGraphResourceBudget& GetResourceBudget() const { return m_ResourceBudget; }
    const RenderGraphResourceStats& GetResourceStats() const { return m_ResourceStats; }

private:
    friend class RenderGraphResources;
    struct TextureResource {
        std::string name;
        RHITextureDesc desc;
        std::shared_ptr<GpuTexture> texture;
        std::shared_ptr<GpuTextureView> view;
        RHIResourceState initialState = RHIResourceState::Undefined;
        RHIResourceState currentState = RHIResourceState::Undefined;
        RHIResourceState finalState = RHIResourceState::Undefined;
        std::vector<RHIResourceState> subresourceStates;
        bool hasFinalState = false;
        bool imported = false;
        bool reusedFromPool = false;
    };
    struct Pass {
        std::string name;
        std::vector<RenderGraphBuilder::Access> accesses;
        std::vector<RenderGraphBuilder::BufferAccess> bufferAccesses;
        ExecuteCallback execute;
        PassFlags flags = PassFlags::None;
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
        RHIResourceState finalState = RHIResourceState::Undefined;
        bool hasFinalState = false;
        bool imported = false;
        bool reusedFromPool = false;
    };
    struct PooledBuffer {
        RHIBufferDesc desc;
        std::shared_ptr<GpuBuffer> buffer;
        std::shared_ptr<GpuBufferView> view;
    };

    bool ValidateHandle(RGTextureHandle handle, const std::string& passName);
    bool ValidateTextureAccess(const Pass& pass, const RenderGraphBuilder::Access& access);
    bool ValidateBufferAccess(const Pass& pass, const RenderGraphBuilder::BufferAccess& access);
    bool EnsureResources();
    bool SetError(ErrorCode code, std::string message);

    IRHIDevice& m_Device;
    std::vector<TextureResource> m_Textures;
    std::vector<BufferResource> m_Buffers;
    std::vector<Pass> m_Passes;
    std::vector<uint32_t> m_ExecutionOrder;
    std::vector<std::string> m_ExecutionOrderNames;
    std::vector<std::string> m_CulledPassNames;
    std::vector<uint8_t> m_LiveTextures;
    std::vector<uint8_t> m_LiveBuffers;
    std::string m_LastError;
    ErrorCode m_LastErrorCode = ErrorCode::None;
    bool m_Compiled = false;
    std::unordered_map<std::string, std::vector<PooledTexture>> m_TexturePool;
    std::unordered_map<std::string, std::vector<PooledBuffer>> m_BufferPool;
    RenderGraphResourceBudget m_ResourceBudget;
    RenderGraphResourceStats m_ResourceStats;
};

inline RenderGraph::PassFlags operator|(RenderGraph::PassFlags a, RenderGraph::PassFlags b) {
    return static_cast<RenderGraph::PassFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
