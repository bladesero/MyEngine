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
struct RGAccelerationStructureHandle {
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

struct RenderGraphCpuTimings {
    float addPassCpuMs = 0.0f;
    float compileCpuMs = 0.0f;
    float ensureResourcesCpuMs = 0.0f;
    bool topologyCacheHit = false;
    uint64_t topologyCacheHits = 0;
    uint64_t topologyCacheMisses = 0;
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
    GpuAccelerationStructure* GetAccelerationStructure(RGAccelerationStructureHandle handle) const;

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
    void ReadIndirect(RGBufferHandle handle);
    void ReadCopySource(RGBufferHandle handle);
    void ReadWriteUAV(RGBufferHandle handle);
    void ReadAccelerationStructure(RGAccelerationStructureHandle handle);
    void WriteAccelerationStructure(RGAccelerationStructureHandle handle);

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
    struct AccelerationStructureAccess {
        RGAccelerationStructureHandle handle;
        bool read = false;
        bool write = false;
    };
    RenderGraphBuilder(std::vector<Access>& accesses, std::vector<BufferAccess>& bufferAccesses,
                       std::vector<AccelerationStructureAccess>& accelerationStructureAccesses)
        : m_Accesses(accesses), m_BufferAccesses(&bufferAccesses),
          m_AccelerationStructureAccesses(&accelerationStructureAccesses) {}
    std::vector<BufferAccess>* m_BufferAccesses = nullptr;
    std::vector<AccelerationStructureAccess>* m_AccelerationStructureAccesses = nullptr;
};

class RenderGraph {
public:
    using SetupCallback = std::function<void(RenderGraphBuilder&)>;
    using ExecuteCallback = std::function<void(GpuCommandList&, const RenderGraphResources&)>;
    enum class PassType : uint8_t { Graphics, Compute, AccelerationStructureBuild };
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
        InvalidAccelerationStructureHandle,
        MissingResourceAccess,
        DuplicateResourceAccess,
        UninitializedTextureRead,
        UninitializedBufferRead,
        TextureUsageMismatch,
        BufferUsageMismatch,
        AttachmentSizeMismatch,
        AttachmentFormatMismatch,
        TooManyColorAttachments,
        ComputeAttachmentAccess,
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
    RGAccelerationStructureHandle
    ImportAccelerationStructure(const std::string& name,
                                const std::shared_ptr<GpuAccelerationStructure>& accelerationStructure);
    void SetFinalState(RGTextureHandle handle, RHIResourceState finalState);
    void SetFinalState(RGBufferHandle handle, RHIResourceState finalState);
    void AddPass(const std::string& name, SetupCallback setup, ExecuteCallback execute,
                 PassFlags flags = PassFlags::None);
    void AddComputePass(const std::string& name, SetupCallback setup, ExecuteCallback execute,
                        PassFlags flags = PassFlags::None);
    void AddAccelerationStructurePass(const std::string& name, SetupCallback setup, ExecuteCallback execute,
                                      PassFlags flags = PassFlags::None);

    bool Compile();
    bool Prepare();
    bool Execute(GpuCommandList& commandList, GpuTimestampQueryPool* timestampPool = nullptr,
                 uint32_t firstTimestampQuery = 0);
    void BeginFrame();
    void Reset();
    const std::string& GetLastError() const { return m_LastError; }
    ErrorCode GetLastErrorCode() const { return m_LastErrorCode; }
    const std::vector<std::string>& GetExecutionOrder() const { return m_ExecutionOrderNames; }
    const std::vector<PassType>& GetExecutionPassTypes() const { return m_ExecutionPassTypes; }
    const std::vector<std::string>& GetCulledPasses() const { return m_CulledPassNames; }
    bool SetResourceBudget(const RenderGraphResourceBudget&, std::string* error = nullptr);
    const RenderGraphResourceBudget& GetResourceBudget() const { return m_ResourceBudget; }
    const RenderGraphResourceStats& GetResourceStats() const { return m_ResourceStats; }
    const RenderGraphCpuTimings& GetCpuTimings() const { return m_CpuTimings; }

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
        std::vector<RenderGraphBuilder::AccelerationStructureAccess> accelerationStructureAccesses;
        ExecuteCallback execute;
        PassFlags flags = PassFlags::None;
        PassType type = PassType::Graphics;
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
    struct AccelerationStructureResource {
        std::string name;
        std::shared_ptr<GpuAccelerationStructure> accelerationStructure;
    };

    bool ValidateHandle(RGTextureHandle handle, const std::string& passName);
    bool ValidateTextureAccess(const Pass& pass, const RenderGraphBuilder::Access& access);
    bool ValidateBufferAccess(const Pass& pass, const RenderGraphBuilder::BufferAccess& access);
    bool ValidateAccelerationStructureAccess(const Pass& pass,
                                             const RenderGraphBuilder::AccelerationStructureAccess& access);
    bool EnsureResources();
    void AddPassInternal(const std::string& name, SetupCallback setup, ExecuteCallback execute, PassFlags flags,
                         PassType type);
    bool FinalizeFrameRecording();
    bool FrameTopologyMatches() const;
    void MergeRecordedFrame();
    void AdoptRecordedFrame();
    void RecycleActiveTransientResources();
    void AcquirePooledResources();
    void TrimResourcePool();
    bool SetError(ErrorCode code, std::string message);

    IRHIDevice& m_Device;
    std::vector<TextureResource> m_Textures;
    std::vector<BufferResource> m_Buffers;
    std::vector<AccelerationStructureResource> m_AccelerationStructures;
    std::vector<Pass> m_Passes;
    std::vector<TextureResource> m_RecordedTextures;
    std::vector<BufferResource> m_RecordedBuffers;
    std::vector<AccelerationStructureResource> m_RecordedAccelerationStructures;
    std::vector<Pass> m_RecordedPasses;
    std::vector<uint32_t> m_ExecutionOrder;
    std::vector<std::string> m_ExecutionOrderNames;
    std::vector<PassType> m_ExecutionPassTypes;
    std::vector<std::string> m_CulledPassNames;
    std::vector<uint8_t> m_LiveTextures;
    std::vector<uint8_t> m_LiveBuffers;
    std::vector<uint8_t> m_LiveAccelerationStructures;
    std::vector<uint8_t> m_TextureUavWriteScratch;
    std::vector<uint8_t> m_BufferUavWriteScratch;
    std::string m_LastError;
    ErrorCode m_LastErrorCode = ErrorCode::None;
    bool m_Compiled = false;
    bool m_ResourcesReady = false;
    std::unordered_map<std::string, std::vector<PooledTexture>> m_TexturePool;
    std::unordered_map<std::string, std::vector<PooledBuffer>> m_BufferPool;
    RenderGraphResourceBudget m_ResourceBudget;
    RenderGraphResourceStats m_ResourceStats;
    RenderGraphCpuTimings m_CpuTimings;
    bool m_FrameRecording = false;
};

inline RenderGraph::PassFlags operator|(RenderGraph::PassFlags a, RenderGraph::PassFlags b) {
    return static_cast<RenderGraph::PassFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
