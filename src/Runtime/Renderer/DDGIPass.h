#pragma once

#include "Renderer/RenderPass.h"
#include "Renderer/SceneLighting.h"
#include "Renderer/SceneSdfClipmap.h"

#include <memory>

struct ShaderHandle;

class DDGIPass final : public RenderPass {
public:
    struct GraphResources {
        std::shared_ptr<GpuBuffer> metadata;
        std::shared_ptr<GpuBufferView> metadataView;
        std::shared_ptr<GpuBuffer> sdf;
        std::shared_ptr<GpuBufferView> sdfView;
        std::shared_ptr<GpuBuffer> voxels;
        std::shared_ptr<GpuBufferView> voxelView;
        std::shared_ptr<GpuBuffer> probeSH2;
        std::shared_ptr<GpuBufferView> probeSH2Srv;
        std::shared_ptr<GpuBufferView> probeSH2Uav;
        bool enabled = false;
        RHIResourceState metadataState = RHIResourceState::ShaderResource;
        RHIResourceState sdfState = RHIResourceState::ShaderResource;
        RHIResourceState voxelState = RHIResourceState::ShaderResource;
        RHIResourceState probeState = RHIResourceState::ShaderResource;
    };

    explicit DDGIPass(IRHIDevice* device);

    void Execute(GpuCommandList& commands, const Scene& scene,
                 const Camera& camera) override;
    bool PrepareGraphResources(const Scene& scene,
                               const SceneLightData& lights);
    GraphResources GetGraphResources() const;
    void MarkGraphResourcesShaderResource();
    const SceneSdfClipmapData& GetClipmapData() const { return m_Clipmap.GetData(); }

private:
    bool EnsureBuffers(const SceneSdfClipmapData& clipmap);
    bool UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer,
                      const void* data,
                      uint32_t byteSize,
                      const char* name);
    GpuShader* GetOrCreateShader();
    GpuComputePipeline* GetOrCreatePipeline();

    SceneSdfClipmapBuilder m_Clipmap;
    SceneLightData m_Lights;
    bool m_Enabled = false;

    std::shared_ptr<GpuBuffer> m_Metadata;
    std::shared_ptr<GpuBufferView> m_MetadataView;
    std::shared_ptr<GpuBuffer> m_Sdf;
    std::shared_ptr<GpuBufferView> m_SdfView;
    std::shared_ptr<GpuBuffer> m_Voxels;
    std::shared_ptr<GpuBufferView> m_VoxelView;
    std::shared_ptr<GpuBuffer> m_ProbeSH2;
    std::shared_ptr<GpuBufferView> m_ProbeSH2Srv;
    std::shared_ptr<GpuBufferView> m_ProbeSH2Uav;
    RHIResourceState m_ProbeState = RHIResourceState::ShaderResource;

    std::shared_ptr<ShaderHandle> m_ShaderHandle;
    uint64_t m_ShaderVersion = 0;
    std::shared_ptr<GpuComputePipeline> m_Pipeline;
    bool m_LoggedInvalidBindGroup = false;
};
