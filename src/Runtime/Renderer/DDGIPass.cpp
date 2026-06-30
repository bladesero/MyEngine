#include "Renderer/DDGIPass.h"

#include "Camera/Camera.h"
#include "Core/Logger.h"
#include "Renderer/ShaderManager.h"

#include <array>
#include <cstring>
#include <vector>

namespace {

struct DDGIUpdateConstants {
    float lightDirection[4];
    float lightColor[4];
    float ambientInfo[4];
};

constexpr uint32_t kMetadataFloat4Count =
    1u + SceneSdfClipmapData::kLevelCount * 3u;
constexpr uint32_t kProbeCount =
    SceneSdfClipmapData::kLevelCount *
    SceneSdfClipmapData::kProbeResolution *
    SceneSdfClipmapData::kProbeResolution *
    SceneSdfClipmapData::kProbeResolution;
constexpr uint32_t kProbeSHFloat4Count = kProbeCount * 9u;

std::vector<std::array<float, 4>> DisabledMetadata()
{
    std::vector<std::array<float, 4>> metadata(kMetadataFloat4Count);
    metadata[0] = {0.0f,
                   static_cast<float>(SceneSdfClipmapData::kLevelCount),
                   static_cast<float>(SceneSdfClipmapData::kSdfResolution),
                   static_cast<float>(SceneSdfClipmapData::kProbeResolution)};
    return metadata;
}

} // namespace

DDGIPass::DDGIPass(IRHIDevice* device)
    : RenderPass(device)
{}

bool DDGIPass::PrepareGraphResources(const Scene& scene,
                                     const SceneLightData& lights)
{
    m_Lights = lights;
    const SceneSdfClipmapData& clipmap = m_Clipmap.Build(scene);
    m_Enabled = clipmap.enabled;
    return EnsureBuffers(clipmap);
}

DDGIPass::GraphResources DDGIPass::GetGraphResources() const
{
    GraphResources resources;
    resources.metadata = m_Metadata;
    resources.metadataView = m_MetadataView;
    resources.sdf = m_Sdf;
    resources.sdfView = m_SdfView;
    resources.voxels = m_Voxels;
    resources.voxelView = m_VoxelView;
    resources.probeSH2 = m_ProbeSH2;
    resources.probeSH2Srv = m_ProbeSH2Srv;
    resources.probeSH2Uav = m_ProbeSH2Uav;
    resources.enabled = m_Enabled;
    resources.probeState = m_ProbeState;
    return resources;
}

void DDGIPass::MarkGraphResourcesShaderResource()
{
    m_ProbeState = RHIResourceState::ShaderResource;
}

bool DDGIPass::UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer,
                            const void* data,
                            uint32_t byteSize,
                            const char* name)
{
    if (!Device() || !buffer || !data || byteSize == 0) return false;
    if (!Device()->UpdateBuffer(buffer, 0, data, byteSize)) {
        Logger::Warn("[DDGIPass] Failed to update ", name, " buffer");
        return false;
    }
    return true;
}

bool DDGIPass::EnsureBuffers(const SceneSdfClipmapData& clipmap)
{
    if (!Device()) return false;

    const uint32_t sdfCount = SceneSdfClipmapData::kLevelCount *
        SceneSdfClipmapData::kSdfResolution *
        SceneSdfClipmapData::kSdfResolution *
        SceneSdfClipmapData::kSdfResolution;
    const uint32_t voxelWordCount = SceneSdfClipmapData::kLevelCount *
        ((SceneSdfClipmapData::kSdfResolution *
          SceneSdfClipmapData::kSdfResolution *
          SceneSdfClipmapData::kSdfResolution + 31u) / 32u);

    if (!m_Metadata) {
        RHIBufferDesc desc;
        desc.size = kMetadataFloat4Count * sizeof(std::array<float, 4>);
        desc.stride = sizeof(std::array<float, 4>);
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = "DDGIMetadata";
        m_Metadata = Device()->CreateBuffer(desc);
        RHIBufferViewDesc viewDesc;
        viewDesc.elementCount = kMetadataFloat4Count;
        viewDesc.usage = RHIResourceUsage::ShaderResource;
        m_MetadataView = Device()->CreateBufferView(m_Metadata, viewDesc);
    }
    if (!m_Sdf) {
        RHIBufferDesc desc;
        desc.size = sdfCount * sizeof(float);
        desc.stride = sizeof(float);
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = "SceneSdfClipmapSdf";
        m_Sdf = Device()->CreateBuffer(desc);
        RHIBufferViewDesc viewDesc;
        viewDesc.elementCount = sdfCount;
        viewDesc.usage = RHIResourceUsage::ShaderResource;
        m_SdfView = Device()->CreateBufferView(m_Sdf, viewDesc);
    }
    if (!m_Voxels) {
        RHIBufferDesc desc;
        desc.size = voxelWordCount * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = "SceneSdfClipmapVoxels";
        m_Voxels = Device()->CreateBuffer(desc);
        RHIBufferViewDesc viewDesc;
        viewDesc.elementCount = voxelWordCount;
        viewDesc.usage = RHIResourceUsage::ShaderResource;
        m_VoxelView = Device()->CreateBufferView(m_Voxels, viewDesc);
    }
    if (!m_ProbeSH2) {
        RHIBufferDesc desc;
        desc.size = kProbeSHFloat4Count * sizeof(std::array<float, 4>);
        desc.stride = sizeof(std::array<float, 4>);
        desc.usage = RHIResourceUsage::ShaderResource |
                     RHIResourceUsage::UnorderedAccess;
        desc.debugName = "DDGIProbeSH2";
        m_ProbeSH2 = Device()->CreateBuffer(desc);
        RHIBufferViewDesc srvDesc;
        srvDesc.elementCount = kProbeSHFloat4Count;
        srvDesc.usage = RHIResourceUsage::ShaderResource;
        m_ProbeSH2Srv = Device()->CreateBufferView(m_ProbeSH2, srvDesc);
        RHIBufferViewDesc uavDesc = srvDesc;
        uavDesc.usage = RHIResourceUsage::UnorderedAccess;
        m_ProbeSH2Uav = Device()->CreateBufferView(m_ProbeSH2, uavDesc);
    }

    if (!m_MetadataView || !m_SdfView || !m_VoxelView || !m_ProbeSH2Srv || !m_ProbeSH2Uav) {
        Logger::Error("[DDGIPass] Failed to create DDGI buffers or views");
        return false;
    }

    const auto disabled = DisabledMetadata();
    const auto& metadata = clipmap.enabled ? clipmap.metadata : disabled;
    UpdateBuffer(m_Metadata, metadata.data(),
                 static_cast<uint32_t>(metadata.size() * sizeof(metadata[0])),
                 "metadata");
    if (clipmap.enabled) {
        UpdateBuffer(m_Sdf, clipmap.sdf.data(),
                     static_cast<uint32_t>(clipmap.sdf.size() * sizeof(float)), "SDF");
        UpdateBuffer(m_Voxels, clipmap.voxelWords.data(),
                     static_cast<uint32_t>(clipmap.voxelWords.size() * sizeof(uint32_t)),
                     "voxel");
    }
    return true;
}

GpuShader* DDGIPass::GetOrCreateShader()
{
    if (!Device()) return nullptr;
    if (!m_ShaderHandle) {
        m_ShaderHandle = ShaderManager::Get().GetOrCreateCompute(
            "Content/Engine/Shaders/DDGIProbeUpdate.shader");
    }
    if (!m_ShaderHandle || !m_ShaderHandle->shader) return nullptr;
    if (m_ShaderVersion != m_ShaderHandle->version) {
        m_ShaderVersion = m_ShaderHandle->version;
        m_Pipeline.reset();
    }
    return m_ShaderHandle->shader.get();
}

GpuComputePipeline* DDGIPass::GetOrCreatePipeline()
{
    if (!Device() || !GetOrCreateShader()) return nullptr;
    if (!m_Pipeline) {
        ComputePipelineDesc desc;
        desc.shader = m_ShaderHandle->shader;
        m_Pipeline = Device()->CreateComputePipeline(desc);
        if (!m_Pipeline) {
            Logger::Error("[DDGIPass] Failed to create compute pipeline");
        }
    }
    return m_Pipeline.get();
}

void DDGIPass::Execute(GpuCommandList& commands, const Scene&, const Camera&)
{
    if (!m_Enabled) return;
    if (!GetOrCreatePipeline() || !m_ShaderHandle || !m_ShaderHandle->shader) return;

    DDGIUpdateConstants constants{};
    constants.lightDirection[0] = m_Lights.direction.x;
    constants.lightDirection[1] = m_Lights.direction.y;
    constants.lightDirection[2] = m_Lights.direction.z;
    constants.lightDirection[3] = m_Lights.directionalIntensity;
    constants.lightColor[0] = m_Lights.color.x;
    constants.lightColor[1] = m_Lights.color.y;
    constants.lightColor[2] = m_Lights.color.z;
    constants.lightColor[3] = 1.0f;
    constants.ambientInfo[0] = m_Lights.ambientIntensity;
    constants.ambientInfo[1] = static_cast<float>(SceneSdfClipmapData::kLevelCount);
    constants.ambientInfo[2] = static_cast<float>(SceneSdfClipmapData::kProbeResolution);
    constants.ambientInfo[3] = 0.0f;

    auto bindings = Device()->CreateBindGroup(m_ShaderHandle->shader);
    if (!bindings) return;
    bindings->SetConstants("DDGIUpdateParams", &constants, sizeof(constants));
    bindings->SetStorageBuffer("g_DDGIMetadata", m_MetadataView);
    bindings->SetStorageBuffer("g_SceneSdf", m_SdfView);
    bindings->SetStorageBuffer("g_SceneVoxels", m_VoxelView);
    bindings->SetStorageBuffer("g_DDGIProbeSH2Out", m_ProbeSH2Uav);
    std::string error;
    if (!bindings->Validate(&error)) {
        if (!m_LoggedInvalidBindGroup) {
            Logger::Error("[DDGIPass] Invalid bind group: ", error);
            m_LoggedInvalidBindGroup = true;
        }
        return;
    }

    commands.SetComputePipeline(m_Pipeline.get());
    commands.SetBindGroup(0, bindings.get());
    commands.Dispatch(SceneSdfClipmapData::kProbeResolution,
                      SceneSdfClipmapData::kProbeResolution,
                      SceneSdfClipmapData::kLevelCount *
                          SceneSdfClipmapData::kProbeResolution);
    commands.UAVBarrier(m_ProbeSH2.get());
}
