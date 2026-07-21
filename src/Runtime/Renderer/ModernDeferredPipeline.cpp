#include "Renderer/ModernDeferredPipeline.h"

#include "Camera/Camera.h"
#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/LightComponent.h"
#include "Renderer/MeshShader.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {
std::mutex g_GpuSceneRegistryMutex;
std::unordered_map<IRHIDevice*, std::weak_ptr<GpuSceneDatabase>> g_GpuSceneRegistry;
std::mutex g_ModernBindingDiagnosticsMutex;
std::unordered_set<std::string> g_ModernBindingDiagnostics;
constexpr std::array<const char*, kGpuSceneMaterialSamplerCount> kMaterialSamplerBindingNames = {
    "g_LinearRepeatSampler",       "g_PointRepeatSampler",         "g_LinearClampURepeatVSampler",
    "g_PointClampURepeatVSampler", "g_LinearRepeatUClampVSampler", "g_PointRepeatUClampVSampler",
    "g_LinearClampSampler",        "g_PointClampSampler",
};

std::shared_ptr<GpuSceneDatabase> AcquireGpuScene(IRHIDevice* device) {
    std::lock_guard<std::mutex> lock(g_GpuSceneRegistryMutex);
    if (auto existing = g_GpuSceneRegistry[device].lock())
        return existing;
    auto scene = std::make_shared<GpuSceneDatabase>(device);
    g_GpuSceneRegistry[device] = scene;
    return scene;
}

uint32_t GrowIndirectCapacity(uint32_t required) {
    uint32_t capacity = 1024;
    while (capacity < required)
        capacity *= 2;
    return capacity;
}

float Halton(uint32_t index, uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

bool MatricesNearlyEqual(const Mat4& left, const Mat4& right, float epsilon = 1e-5f) {
    const float* a = left.Data();
    const float* b = right.Data();
    for (uint32_t index = 0; index < 16; ++index) {
        if (std::abs(a[index] - b[index]) > epsilon)
            return false;
    }
    return true;
}

bool FloatsNearlyEqual(float left, float right, float epsilon = 1e-5f) {
    return std::abs(left - right) <= epsilon;
}

bool BindModernPass(GpuCommandList& commands, const char* passName, const std::shared_ptr<GpuBindGroup>& bindings) {
    std::string error;
    if (bindings && bindings->GetShader() && !bindings->GetShader()->reflection.bindings.empty() &&
        bindings->Validate(&error)) {
        commands.SetBindGroup(0, bindings.get());
        return true;
    }
    if (bindings && bindings->GetShader() && bindings->GetShader()->reflection.bindings.empty())
        error = "shader reflection is empty";
    const std::string diagnostic =
        std::string(passName ? passName : "<unnamed>") + ": " + (bindings ? error : "failed to create bind group");
    std::lock_guard<std::mutex> lock(g_ModernBindingDiagnosticsMutex);
    if (g_ModernBindingDiagnostics.insert(diagnostic).second)
        Logger::Error("[ModernDeferred] Invalid shader bindings for ", diagnostic);
    return false;
}

bool SetMaterialSamplerTable(const std::shared_ptr<GpuBindGroup>& bindings,
                             const std::array<std::shared_ptr<GpuSampler>, kGpuSceneMaterialSamplerCount>& samplers) {
    if (!bindings)
        return false;
    bool complete = true;
    for (uint32_t index = 0; index < samplers.size(); ++index)
        complete = bindings->SetSampler(kMaterialSamplerBindingNames[index], samplers[index]) && complete;
    return complete;
}

bool HasMaterialSamplerTable(const std::array<std::shared_ptr<GpuSampler>, kGpuSceneMaterialSamplerCount>& samplers) {
    return std::all_of(samplers.begin(), samplers.end(), [](const auto& sampler) { return sampler != nullptr; });
}
} // namespace

ModernDeferredPipeline::ModernDeferredPipeline(IRHIDevice* device, IRHIReadbackService* readbackService)
    : m_Device(device), m_ReadbackService(readbackService), m_GpuScene(AcquireGpuScene(device)) {
    m_Ready = EnsurePipelines();
}

ModernDeferredPipeline::~ModernDeferredPipeline() = default;

std::shared_ptr<GpuBindGroup> ModernDeferredPipeline::AcquireBindGroup(const std::shared_ptr<GpuShader>& shader) {
    if (!m_Device || !shader)
        return nullptr;
    if (m_Device->GetBackend() != RHIBackend::D3D12)
        return m_Device->CreateBindGroup(shader);
    auto& group = m_D3D12BindGroups[shader.get()];
    if (!group)
        group = m_Device->CreateBindGroup(shader);
    return group;
}

void ModernDeferredPipeline::InvalidateTemporalHistory(std::string reason, bool resetObjectHistory) {
    m_HistoryValid = false;
    m_HistoryResetReason = reason.empty() ? "explicit reset" : std::move(reason);
    m_JitterSequenceIndex = 0;
    m_PendingJitterSequenceIndex = 0;
    std::fill(std::begin(m_PreviousJitterUv), std::end(m_PreviousJitterUv), 0.0f);
    std::fill(std::begin(m_PendingJitterUv), std::end(m_PendingJitterUv), 0.0f);
    if (resetObjectHistory && m_GpuScene)
        m_GpuScene->ResetTemporalState();
    if (resetObjectHistory) {
        m_HasPreviousViewProjection = false;
        m_HasPreviousProjection = false;
    }
}

void ModernDeferredPipeline::CommitTemporalFrame() {
    ++m_ShadowSubmissionSerial;
    m_RetiredShadowStreams.erase(
        std::remove_if(m_RetiredShadowStreams.begin(), m_RetiredShadowStreams.end(),
                       [this](const auto& retired) { return retired.releaseSubmission <= m_ShadowSubmissionSerial; }),
        m_RetiredShadowStreams.end());
    m_Stats.retiredShadowStreams = static_cast<uint32_t>(m_RetiredShadowStreams.size());
    for (const auto& stream : m_PendingShadowStreams) {
        if (!stream)
            continue;
        stream->argsState = RHIResourceState::IndirectArgument;
        stream->countState = RHIResourceState::IndirectArgument;
    }
    m_PendingShadowStreams.clear();
    if (!m_TemporalFramePending)
        return;
    m_HistoryPing = m_PendingHistoryPing;
    m_PreviousViewProjection = m_PendingViewProjection;
    m_HasPreviousViewProjection = true;
    m_PreviousCameraPosition = m_PendingCameraPosition;
    m_PreviousCameraForward = m_PendingCameraForward;
    m_PreviousProjection = m_PendingProjection;
    m_PreviousPostSettings = m_PendingPostSettings;
    m_HasPreviousProjection = true;
    m_JitterSequenceIndex = m_PendingJitterSequenceIndex;
    std::copy(std::begin(m_PendingJitterUv), std::end(m_PendingJitterUv), std::begin(m_PreviousJitterUv));
    m_LastCommittedFrameNumber = m_CurrentFrameNumber;
    m_HasCommittedFrameNumber = true;
    m_HistoryValid = true;
    m_GeometryHistoryInShaderState = true;
    m_PostColorInShaderState = true;
    m_SSGIResourcesInShaderState = m_SSGIResourcesInShaderState || m_PostSettings.ssgiEnabled;
    m_SSRResourcesInShaderState = m_SSRResourcesInShaderState || m_PostSettings.ssrEnabled;
    m_TAAResourcesInShaderState = m_TAAResourcesInShaderState || m_PostSettings.taaEnabled;
    m_EffectsResourceInShaderState =
        m_EffectsResourceInShaderState || m_PostSettings.ssgiEnabled || m_PostSettings.ssrEnabled;
    m_ScreenSpaceDebugInShaderState =
        m_ScreenSpaceDebugInShaderState || m_SSGIDebugOutputSrv || m_SSRDebugOutputSrv || m_PostSettings.taaEnabled;
    m_TemporalFramePending = false;
}

void ModernDeferredPipeline::AbortTemporalFrame(const std::string& reason) {
    // The imported states still describe the last successfully submitted frame. Keep them unchanged when graph
    // preparation or execution aborts, otherwise the next frame would skip a required UAV transition.
    m_PendingShadowStreams.clear();
    if (!m_TemporalFramePending)
        return;
    m_TemporalFramePending = false;
    m_HistoryValid = false;
    m_HistoryResetReason = reason.empty() ? "render graph frame aborted" : reason;
}

void ModernDeferredPipeline::AbortGpuDrivenShadowFrame() {
    // Shadow stream states are committed with the rest of the graph. If Renderer rebuilds only the shadow portion on
    // the CPU, these transitions were never recorded and must not leak into the next frame's imported state.
    m_PendingShadowStreams.clear();
}

void ModernDeferredPipeline::SetDirectionalShadowInput(bool enabled, const std::shared_ptr<GpuTextureView>& shadowSrv,
                                                       const Mat4* cascadeViewProjection, uint32_t cascadeCount,
                                                       const float* cascadeSplits, float intensity) {
    const uint32_t count = (std::min)(cascadeCount, 3u);
    const bool usable = enabled && shadowSrv && cascadeViewProjection && count > 0;
    for (uint32_t cascade = 0; cascade < 3; ++cascade)
        m_ClusterConstants.shadowViewProjection[cascade] =
            usable && cascade < count ? cascadeViewProjection[cascade] : Mat4::Identity();
    m_ClusterConstants.cascadeSplits =
        usable && cascadeSplits ? Vec4{cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3]} : Vec4{};
    m_ClusterConstants.shadowInfo =
        Vec4{usable ? 1.0f : 0.0f, std::clamp(intensity, 0.0f, 1.0f), static_cast<float>(count), 0.0f};
    m_DirectionalShadowSrv = usable ? shadowSrv : m_ShadowFallbackSrv;
}

void ModernDeferredPipeline::SetProbeInput(std::shared_ptr<GpuTextureView> reflectionAtlas,
                                           std::shared_ptr<GpuBufferView> reflectionMetadata,
                                           std::shared_ptr<GpuBufferView> shVolumeMetadata,
                                           std::shared_ptr<GpuBufferView> shCoefficients, uint32_t reflectionCount,
                                           uint32_t shVolumeCount, uint32_t reflectionMipCount) {
    m_ProbeReflectionAtlas = std::move(reflectionAtlas);
    m_ProbeReflectionMetadata = std::move(reflectionMetadata);
    m_ProbeSHVolumeMetadata = std::move(shVolumeMetadata);
    m_ProbeSHCoefficients = std::move(shCoefficients);
    m_ProbeReflectionCount = reflectionCount;
    m_ProbeSHVolumeCount = shVolumeCount;
    m_ProbeReflectionMipCount = reflectionMipCount;
    m_ClusterConstants.localReflectionProbeCount = reflectionCount;
    m_ClusterConstants.localSHProbeVolumeCount = shVolumeCount;
    m_ClusterConstants.localReflectionMipCount = static_cast<float>(reflectionMipCount);
}

void ModernDeferredPipeline::ConsumeDiagnosticsReadback() {
    const auto consume = [](std::shared_ptr<GpuReadbackTicket>& ticket, uint32_t& value) {
        if (!ticket || !ticket->IsReady())
            return;
        std::vector<uint8_t> bytes;
        if (ticket->Read(bytes) && bytes.size() >= sizeof(uint32_t))
            std::memcpy(&value, bytes.data(), sizeof(uint32_t));
        ticket.reset();
    };
    consume(m_IndirectCountReadback, m_LastIndirectDrawCount);
    consume(m_VisibleIndirectCountReadback, m_LastVisibleIndirectDrawCount);
    consume(m_ClusterOverflowReadback, m_LastClusterOverflow);
}

void ModernDeferredPipeline::Resize(uint32_t width, uint32_t height) {
    width = (std::max)(width, 1u);
    height = (std::max)(height, 1u);
    if (width == m_Width && height == m_Height)
        return;
    m_Width = width;
    m_Height = height;
    m_HiZ.reset();
    m_HiZSrv.reset();
    m_HiZMipSrvs.clear();
    m_HiZMipUavs.clear();
    m_HiZInShaderState = false;
    m_ClusterCounts.reset();
    m_ClusterCountsView.reset();
    m_ClusterOffsets.reset();
    m_ClusterOffsetsView.reset();
    m_ClusterLightIndices.reset();
    m_ClusterLightIndicesView.reset();
    m_ClusterOverflow.reset();
    m_ClusterOverflowView.reset();
    m_Hdr.reset();
    m_HdrRtv.reset();
    m_HdrSrv.reset();
    m_HdrUav.reset();
    m_ClusterBuffersInShaderState = false;
    m_ClusterOverflowState = RHIResourceState::UnorderedAccess;
    m_HdrInShaderState = false;
    m_SSGITrace = {};
    m_SSGIHistory[0] = {};
    m_SSGIHistory[1] = {};
    m_SSGIFilter[0] = {};
    m_SSGIFilter[1] = {};
    m_SSRTrace = {};
    m_SSRHistory[0] = {};
    m_SSRHistory[1] = {};
    m_SSRFilter[0] = {};
    m_SSRFilter[1] = {};
    m_EffectsHdr = {};
    m_ScreenSpaceDebug = {};
    m_SSGIDebugOutputSrv.reset();
    m_SSRDebugOutputSrv.reset();
    m_TAAHistoryAgeDebugOutputSrv.reset();
    m_TAARejectReasonDebugOutputSrv.reset();
    m_TAAHistory[0] = {};
    m_TAAHistory[1] = {};
    m_DepthHistory[0] = {};
    m_DepthHistory[1] = {};
    m_NormalHistory[0] = {};
    m_NormalHistory[1] = {};
    m_PostColor = {};
    m_FrameDepthHistoryRead = {};
    m_FrameDepthHistoryWrite = {};
    m_FrameNormalHistoryRead = {};
    m_FrameNormalHistoryWrite = {};
    m_FrameEnvironment = {};
    m_FrameScreenSpaceDebug = {};
    m_GeometryHistoryInShaderState = false;
    m_PostColorInShaderState = false;
    m_SSGIResourcesInShaderState = false;
    m_SSRResourcesInShaderState = false;
    m_TAAResourcesInShaderState = false;
    m_EffectsResourceInShaderState = false;
    m_ScreenSpaceDebugInShaderState = false;
    m_TemporalFramePending = false;
    m_HasCommittedFrameNumber = false;
    m_HasPreviousViewProjection = false;
    m_HasPreviousProjection = false;
    m_JitterSequenceIndex = 0;
    m_PendingJitterSequenceIndex = 0;
    m_HistoryValid = false;
    m_HistoryResetReason = "viewport resize";
}

bool ModernDeferredPipeline::EnsurePipelines() {
    if (!m_Device) {
        m_InitializationError = "missing RHI device";
        return false;
    }
    RHISamplerDesc linearClamp;
    linearClamp.filter = RHIFilter::Linear;
    linearClamp.addressU = linearClamp.addressV = linearClamp.addressW = RHIAddressMode::Clamp;
    m_LinearClampSampler = m_Device->CreateSampler(linearClamp);
    RHISamplerDesc pointClamp = linearClamp;
    pointClamp.filter = RHIFilter::Point;
    m_PointClampSampler = m_Device->CreateSampler(pointClamp);
    for (uint32_t index = 0; index < m_MaterialSamplers.size(); ++index) {
        RHISamplerDesc materialSampler;
        materialSampler.filter = (index & kGpuSceneMaterialSamplerPointBit) != 0 ? RHIFilter::Point : RHIFilter::Linear;
        materialSampler.addressU =
            (index & kGpuSceneMaterialSamplerClampUBit) != 0 ? RHIAddressMode::Clamp : RHIAddressMode::Repeat;
        materialSampler.addressV =
            (index & kGpuSceneMaterialSamplerClampVBit) != 0 ? RHIAddressMode::Clamp : RHIAddressMode::Repeat;
        materialSampler.addressW = RHIAddressMode::Repeat;
        m_MaterialSamplers[index] = m_Device->CreateSampler(materialSampler);
    }
    RHISamplerDesc shadowSampler = linearClamp;
    shadowSampler.filter = RHIFilter::ComparisonLinear;
    m_ShadowSampler = m_Device->CreateSampler(shadowSampler);
    RHITextureDesc shadowFallback;
    shadowFallback.width = shadowFallback.height = 1;
    shadowFallback.arrayLayers = 3;
    shadowFallback.format = RHIFormat::D32Float;
    shadowFallback.usage =
        RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
    shadowFallback.debugName = "ModernShadowFallback";
    const std::array<float, 3> fullyLitDepth = {1.0f, 1.0f, 1.0f};
    std::array<RHITextureSubresourceData, 3> shadowFallbackLayers{};
    for (uint32_t layer = 0; layer < shadowFallbackLayers.size(); ++layer) {
        shadowFallbackLayers[layer].data = &fullyLitDepth[layer];
        shadowFallbackLayers[layer].rowPitch = sizeof(float);
        shadowFallbackLayers[layer].slicePitch = sizeof(float);
        shadowFallbackLayers[layer].arrayLayer = layer;
    }
    // The fallback occupies bindless slot zero before the first scene extract. It must contain real, fully-lit depth
    // data and be in shader-resource state before any Modern draw binds the global descriptor table; a merely-created
    // depth image remains UNDEFINED on Vulkan even when the shader's shadow-enabled branch is false.
    m_ShadowFallback = m_Device->UploadTexture(shadowFallback, shadowFallbackLayers.data(),
                                               static_cast<uint32_t>(shadowFallbackLayers.size()));
    RHITextureViewDesc shadowFallbackSrv;
    shadowFallbackSrv.layerCount = 3;
    shadowFallbackSrv.usage = RHIResourceUsage::ShaderResource;
    m_ShadowFallbackSrv = m_Device->CreateTextureView(m_ShadowFallback, shadowFallbackSrv);
    m_DirectionalShadowSrv = m_ShadowFallbackSrv;
    RHITextureDesc environmentFallback;
    environmentFallback.width = environmentFallback.height = 1;
    environmentFallback.arrayLayers = 6;
    environmentFallback.format = RHIFormat::RGBA8UNorm;
    environmentFallback.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
    environmentFallback.cube = true;
    environmentFallback.debugName = "ModernEnvironmentFallback";
    const std::array<uint8_t, 4> blackPixel = {0, 0, 0, 255};
    std::array<RHITextureSubresourceData, 6> fallbackFaces{};
    for (uint32_t face = 0; face < fallbackFaces.size(); ++face) {
        fallbackFaces[face].data = blackPixel.data();
        fallbackFaces[face].rowPitch = static_cast<uint32_t>(blackPixel.size());
        fallbackFaces[face].slicePitch = static_cast<uint32_t>(blackPixel.size());
        fallbackFaces[face].arrayLayer = face;
    }
    m_EnvironmentFallback =
        m_Device->UploadTexture(environmentFallback, fallbackFaces.data(), static_cast<uint32_t>(fallbackFaces.size()));
    RHITextureViewDesc environmentFallbackSrv;
    environmentFallbackSrv.layerCount = 6;
    environmentFallbackSrv.usage = RHIResourceUsage::ShaderResource;
    m_EnvironmentFallbackSrv = m_Device->CreateTextureView(m_EnvironmentFallback, environmentFallbackSrv);
    m_EnvironmentCubeSrv = m_EnvironmentFallbackSrv;

    RHIBufferDesc environmentSHFallback;
    environmentSHFallback.size = 9u * 4u * sizeof(float);
    environmentSHFallback.stride = 4u * sizeof(float);
    environmentSHFallback.usage = RHIResourceUsage::ShaderResource;
    environmentSHFallback.debugName = "ModernEnvironmentSHFallback";
    const std::array<float, 9u * 4u> blackSH{};
    m_EnvironmentSHFallback = m_Device->CreateBuffer(environmentSHFallback, blackSH.data());
    RHIBufferViewDesc environmentSHFallbackSrv;
    environmentSHFallbackSrv.elementCount = 9;
    environmentSHFallbackSrv.usage = RHIResourceUsage::ShaderResource;
    m_EnvironmentSHFallbackSrv = m_Device->CreateBufferView(m_EnvironmentSHFallback, environmentSHFallbackSrv);

    RHITextureDesc probeReflectionFallback;
    probeReflectionFallback.width = probeReflectionFallback.height = 1;
    probeReflectionFallback.arrayLayers = 1;
    probeReflectionFallback.format = RHIFormat::RGBA8UNorm;
    probeReflectionFallback.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
    probeReflectionFallback.debugName = "ModernProbeReflectionFallback";
    const RHITextureSubresourceData probePixel{blackPixel.data(), 4, 4, 0, 0};
    m_ProbeReflectionFallback = m_Device->UploadTexture(probeReflectionFallback, &probePixel, 1);
    RHITextureViewDesc probeReflectionView;
    probeReflectionView.layerCount = 1;
    probeReflectionView.usage = RHIResourceUsage::ShaderResource;
    m_ProbeReflectionFallbackSrv = m_Device->CreateTextureView(m_ProbeReflectionFallback, probeReflectionView);

    const auto createProbeFallback = [this](uint32_t stride, const char* name, std::shared_ptr<GpuBuffer>& buffer,
                                            std::shared_ptr<GpuBufferView>& view) {
        std::array<uint8_t, 192> zero{};
        RHIBufferDesc desc;
        desc.size = stride;
        desc.stride = stride;
        desc.usage = RHIResourceUsage::ShaderResource;
        desc.debugName = name;
        buffer = m_Device->CreateBuffer(desc, zero.data());
        RHIBufferViewDesc viewDesc;
        viewDesc.elementCount = 1;
        viewDesc.usage = RHIResourceUsage::ShaderResource;
        view = m_Device->CreateBufferView(buffer, viewDesc);
    };
    createProbeFallback(192, "ModernProbeReflectionMetadataFallback", m_ProbeReflectionMetadataFallback,
                        m_ProbeReflectionMetadataFallbackSrv);
    createProbeFallback(112, "ModernProbeSHVolumeMetadataFallback", m_ProbeSHVolumeMetadataFallback,
                        m_ProbeSHVolumeMetadataFallbackSrv);
    createProbeFallback(16, "ModernProbeSHCoefficientFallback", m_ProbeSHCoefficientFallback,
                        m_ProbeSHCoefficientFallbackSrv);
    m_ProbeReflectionAtlas = m_ProbeReflectionFallbackSrv;
    m_ProbeReflectionMetadata = m_ProbeReflectionMetadataFallbackSrv;
    m_ProbeSHVolumeMetadata = m_ProbeSHVolumeMetadataFallbackSrv;
    m_ProbeSHCoefficients = m_ProbeSHCoefficientFallbackSrv;
    if (!HasMaterialSamplerTable(m_MaterialSamplers) || !m_LinearClampSampler || !m_PointClampSampler ||
        !m_ShadowSampler || !m_ShadowFallback || !m_ShadowFallbackSrv || !m_EnvironmentFallback ||
        !m_EnvironmentFallbackSrv || !m_EnvironmentSHFallback || !m_EnvironmentSHFallbackSrv ||
        !m_ProbeReflectionFallbackSrv || !m_ProbeReflectionMetadataFallbackSrv || !m_ProbeSHVolumeMetadataFallbackSrv ||
        !m_ProbeSHCoefficientFallbackSrv) {
        m_InitializationError = "failed to create Modern fixed samplers or environment/shadow fallback";
        return false;
    }
    ShaderManager::Get().PrewarmCacheArtifacts(
        {EngineShaders::kModernCulling, EngineShaders::kModernOcclusionCulling, EngineShaders::kModernDepth,
         EngineShaders::kModernGBuffer, EngineShaders::kModernHiZInit, EngineShaders::kModernHiZReduce,
         EngineShaders::kClusterCount, EngineShaders::kClusterPrefix, EngineShaders::kClusterScatter,
         EngineShaders::kClusterLighting, EngineShaders::kModernSSGITrace, EngineShaders::kModernSSRTrace,
         EngineShaders::kModernTemporal, EngineShaders::kModernAtrous, EngineShaders::kModernEffectsComposite,
         EngineShaders::kModernTAA, EngineShaders::kModernBloomTone});
    m_CullingHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernCulling);
    m_OcclusionCullingHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernOcclusionCulling);
    m_DepthHandle =
        ShaderManager::Get().GetOrCreate(EngineShaders::kModernDepth, k_MeshVertexLayout, k_MeshVertexLayoutCount);
    m_GBufferHandle =
        ShaderManager::Get().GetOrCreate(EngineShaders::kModernGBuffer, k_MeshVertexLayout, k_MeshVertexLayoutCount);
    m_HiZInitHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernHiZInit);
    m_HiZReduceHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernHiZReduce);
    m_ClusterCountHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kClusterCount);
    m_ClusterPrefixHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kClusterPrefix);
    m_ClusterScatterHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kClusterScatter);
    m_ClusterLightingHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kClusterLighting);
    m_SSGITraceHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernSSGITrace);
    m_SSRTraceHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernSSRTrace);
    m_TemporalHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernTemporal);
    m_AtrousHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernAtrous);
    m_EffectsCompositeHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernEffectsComposite);
    m_TAAHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernTAA);
    m_BloomToneHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kModernBloomTone);
    m_CullingShader = m_CullingHandle ? m_CullingHandle->shader : nullptr;
    m_OcclusionCullingShader = m_OcclusionCullingHandle ? m_OcclusionCullingHandle->shader : nullptr;
    m_DepthShader = m_DepthHandle ? m_DepthHandle->shader : nullptr;
    m_GBufferShader = m_GBufferHandle ? m_GBufferHandle->shader : nullptr;
    m_HiZInitShader = m_HiZInitHandle ? m_HiZInitHandle->shader : nullptr;
    m_HiZReduceShader = m_HiZReduceHandle ? m_HiZReduceHandle->shader : nullptr;
    m_ClusterCountShader = m_ClusterCountHandle ? m_ClusterCountHandle->shader : nullptr;
    m_ClusterPrefixShader = m_ClusterPrefixHandle ? m_ClusterPrefixHandle->shader : nullptr;
    m_ClusterScatterShader = m_ClusterScatterHandle ? m_ClusterScatterHandle->shader : nullptr;
    m_ClusterLightingShader = m_ClusterLightingHandle ? m_ClusterLightingHandle->shader : nullptr;
    m_SSGITraceShader = m_SSGITraceHandle ? m_SSGITraceHandle->shader : nullptr;
    m_SSRTraceShader = m_SSRTraceHandle ? m_SSRTraceHandle->shader : nullptr;
    m_TemporalShader = m_TemporalHandle ? m_TemporalHandle->shader : nullptr;
    m_AtrousShader = m_AtrousHandle ? m_AtrousHandle->shader : nullptr;
    m_EffectsCompositeShader = m_EffectsCompositeHandle ? m_EffectsCompositeHandle->shader : nullptr;
    m_TAAShader = m_TAAHandle ? m_TAAHandle->shader : nullptr;
    m_BloomToneShader = m_BloomToneHandle ? m_BloomToneHandle->shader : nullptr;
    if (!m_CullingShader || !m_OcclusionCullingShader || !m_DepthShader || !m_GBufferShader || !m_HiZInitShader ||
        !m_HiZReduceShader || !m_ClusterCountShader || !m_ClusterPrefixShader || !m_ClusterScatterShader ||
        !m_ClusterLightingShader || !m_SSGITraceShader || !m_SSRTraceShader || !m_TemporalShader || !m_AtrousShader ||
        !m_EffectsCompositeShader || !m_TAAShader || !m_BloomToneShader) {
        m_InitializationError = "Modern culling/depth shader variants are unavailable";
        return false;
    }
    ComputePipelineDesc compute;
    compute.shader = m_CullingShader;
    m_CullingPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_OcclusionCullingShader;
    m_OcclusionCullingPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_HiZInitShader;
    m_HiZInitPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_HiZReduceShader;
    m_HiZReducePipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_ClusterCountShader;
    m_ClusterCountPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_ClusterPrefixShader;
    m_ClusterPrefixPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_ClusterScatterShader;
    m_ClusterScatterPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_ClusterLightingShader;
    m_ClusterLightingPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_SSGITraceShader;
    m_SSGITracePipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_SSRTraceShader;
    m_SSRTracePipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_TemporalShader;
    m_TemporalPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_AtrousShader;
    m_AtrousPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_EffectsCompositeShader;
    m_EffectsCompositePipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_TAAShader;
    m_TAAPipeline = m_Device->CreateComputePipeline(compute);
    compute.shader = m_BloomToneShader;
    m_BloomTonePipeline = m_Device->CreateComputePipeline(compute);
    GraphicsPipelineDesc depth;
    depth.shader = m_DepthShader;
    depth.depthFormat = RHIFormat::D24S8;
    depth.depthStencil.depthTestEnable = true;
    depth.depthStencil.depthWriteEnable = true;
    depth.depthStencil.depthCompareOp = RHICompareOp::LessEqual;
    // Indirect batches contain both one- and two-sided materials. Rasterize both faces and let the material-aware
    // pixel shader reject backfaces for one-sided surfaces so AlphaTest foliage/cloth keeps its authored topology.
    depth.rasterizer.cullMode = RHICullMode::None;
    m_DepthPipeline = m_Device->CreateGraphicsPipeline(depth);
    depth.depthStencil.depthCompareOp = RHICompareOp::Less;
    depth.rasterizer.depthBias = 1536;
    depth.rasterizer.slopeScaledDepthBias = 2.0f;
    m_ShadowDepthPipeline = m_Device->CreateGraphicsPipeline(depth);
    GraphicsPipelineDesc gbuffer;
    gbuffer.shader = m_GBufferShader;
    gbuffer.colorFormats = {RHIFormat::RGBA8UNorm, RHIFormat::RGBA16Float, RHIFormat::RGBA8UNorm,
                            RHIFormat::RGBA16Float, RHIFormat::RG16Float};
    gbuffer.depthFormat = RHIFormat::D24S8;
    gbuffer.depthStencil.depthTestEnable = true;
    gbuffer.depthStencil.depthWriteEnable = false;
    gbuffer.depthStencil.depthCompareOp = RHICompareOp::LessEqual;
    gbuffer.rasterizer.cullMode = RHICullMode::None;
    gbuffer.blend.attachments.resize(gbuffer.colorFormats.size());
    m_GBufferPipeline = m_Device->CreateGraphicsPipeline(gbuffer);
    if (!m_CullingPipeline || !m_OcclusionCullingPipeline || !m_DepthPipeline || !m_ShadowDepthPipeline ||
        !m_GBufferPipeline || !m_HiZInitPipeline || !m_HiZReducePipeline || !m_ClusterCountPipeline ||
        !m_ClusterPrefixPipeline || !m_ClusterScatterPipeline || !m_ClusterLightingPipeline || !m_SSGITracePipeline ||
        !m_SSRTracePipeline || !m_TemporalPipeline || !m_AtrousPipeline || !m_EffectsCompositePipeline ||
        !m_TAAPipeline || !m_BloomTonePipeline) {
        m_InitializationError = "RHI failed to create Modern culling/depth pipelines";
        return false;
    }
    m_InitializationError.clear();
    return true;
}

bool ModernDeferredPipeline::EnsureClusterResources() {
    if (m_ClusterCounts && m_Hdr)
        return true;
    const uint32_t tileX = (m_Width + 31u) / 32u;
    const uint32_t tileY = (m_Height + 31u) / 32u;
    const uint32_t clusterCount = tileX * tileY * 24u;
    const auto createBuffer = [&](const char* name, uint32_t elementCount, std::shared_ptr<GpuBuffer>& buffer,
                                  std::shared_ptr<GpuBufferView>& view) {
        RHIBufferDesc desc;
        desc.size = (std::max)(elementCount, 1u) * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage =
            RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess | RHIResourceUsage::CopySource;
        desc.debugName = name;
        buffer = m_Device->CreateBuffer(desc);
        RHIBufferViewDesc viewDesc;
        viewDesc.elementCount = (std::max)(elementCount, 1u);
        viewDesc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess;
        view = m_Device->CreateBufferView(buffer, viewDesc);
        return buffer && view;
    };
    if (!createBuffer("ClusterCounts", clusterCount, m_ClusterCounts, m_ClusterCountsView) ||
        !createBuffer("ClusterOffsets", clusterCount, m_ClusterOffsets, m_ClusterOffsetsView) ||
        !createBuffer("ClusterLightIndices", clusterCount * 128u, m_ClusterLightIndices, m_ClusterLightIndicesView) ||
        !createBuffer("ClusterOverflow", 1, m_ClusterOverflow, m_ClusterOverflowView))
        return false;

    RHITextureDesc hdr;
    hdr.width = m_Width;
    hdr.height = m_Height;
    hdr.format = RHIFormat::RGBA16Float;
    hdr.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess | RHIResourceUsage::RenderTarget;
    hdr.debugName = "ModernDeferredHDR";
    m_Hdr = m_Device->CreateTexture(hdr);
    RHITextureViewDesc rtv;
    rtv.usage = RHIResourceUsage::RenderTarget;
    m_HdrRtv = m_Device->CreateTextureView(m_Hdr, rtv);
    RHITextureViewDesc srv;
    srv.usage = RHIResourceUsage::ShaderResource;
    m_HdrSrv = m_Device->CreateTextureView(m_Hdr, srv);
    RHITextureViewDesc uav;
    uav.usage = RHIResourceUsage::UnorderedAccess;
    m_HdrUav = m_Device->CreateTextureView(m_Hdr, uav);
    return m_Hdr && m_HdrRtv && m_HdrSrv && m_HdrUav;
}

bool ModernDeferredPipeline::EnsureTemporalResources() {
    if (m_PostColor.texture)
        return true;
    const uint32_t halfWidth = (std::max)(1u, (m_Width + 1u) / 2u);
    const uint32_t halfHeight = (std::max)(1u, (m_Height + 1u) / 2u);
    const auto createTexture = [&](const char* name, uint32_t width, uint32_t height, bool renderTarget,
                                   ComputeTexture& output, RHIFormat format = RHIFormat::RGBA16Float) {
        RHITextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess;
        if (renderTarget)
            desc.usage = desc.usage | RHIResourceUsage::RenderTarget;
        desc.debugName = name;
        output.texture = m_Device->CreateTexture(desc);
        RHITextureViewDesc srv;
        srv.usage = RHIResourceUsage::ShaderResource;
        output.srv = m_Device->CreateTextureView(output.texture, srv);
        RHITextureViewDesc uav;
        uav.usage = RHIResourceUsage::UnorderedAccess;
        output.uav = m_Device->CreateTextureView(output.texture, uav);
        if (renderTarget) {
            RHITextureViewDesc rtv;
            rtv.usage = RHIResourceUsage::RenderTarget;
            output.rtv = m_Device->CreateTextureView(output.texture, rtv);
        }
        return output.texture && output.srv && output.uav && (!renderTarget || output.rtv);
    };
    return createTexture("SSGITrace", halfWidth, halfHeight, false, m_SSGITrace) &&
           createTexture("SSGIHistory0", halfWidth, halfHeight, false, m_SSGIHistory[0]) &&
           createTexture("SSGIHistory1", halfWidth, halfHeight, false, m_SSGIHistory[1]) &&
           createTexture("SSGIFilter0", halfWidth, halfHeight, false, m_SSGIFilter[0]) &&
           createTexture("SSGIFilter1", halfWidth, halfHeight, false, m_SSGIFilter[1]) &&
           createTexture("SSRTrace", halfWidth, halfHeight, false, m_SSRTrace) &&
           createTexture("SSRHistory0", halfWidth, halfHeight, false, m_SSRHistory[0]) &&
           createTexture("SSRHistory1", halfWidth, halfHeight, false, m_SSRHistory[1]) &&
           createTexture("SSRFilter0", halfWidth, halfHeight, false, m_SSRFilter[0]) &&
           createTexture("SSRFilter1", halfWidth, halfHeight, false, m_SSRFilter[1]) &&
           createTexture("ModernEffectsHDR", m_Width, m_Height, true, m_EffectsHdr) &&
           createTexture("ModernScreenSpaceDebug", m_Width, m_Height, false, m_ScreenSpaceDebug) &&
           createTexture("TAAHistory0", m_Width, m_Height, false, m_TAAHistory[0]) &&
           createTexture("TAAHistory1", m_Width, m_Height, false, m_TAAHistory[1]) &&
           createTexture("DepthHistory0", m_Width, m_Height, false, m_DepthHistory[0], RHIFormat::R32Float) &&
           createTexture("DepthHistory1", m_Width, m_Height, false, m_DepthHistory[1], RHIFormat::R32Float) &&
           createTexture("NormalHistory0", m_Width, m_Height, false, m_NormalHistory[0], RHIFormat::RGBA8UNorm) &&
           createTexture("NormalHistory1", m_Width, m_Height, false, m_NormalHistory[1], RHIFormat::RGBA8UNorm) &&
           createTexture("ModernPostColor", m_Width, m_Height, true, m_PostColor);
}

void ModernDeferredPipeline::UpdateHistoryValidity(const Camera& camera, const ModernPostProcessSettings& settings) {
    const Vec3 position = camera.GetPosition();
    const Vec3 forward = camera.GetForward();
    const Mat4 projection = camera.GetProj();
    std::string reason;
    if (!m_HasPreviousProjection)
        reason = "first frame";
    else if ((position - m_PreviousCameraPosition).LengthSq() > 4.0f || forward.Dot(m_PreviousCameraForward) < 0.9f)
        reason = "camera cut or teleport";
    else if (!MatricesNearlyEqual(projection, m_PreviousProjection))
        reason = "camera projection changed";
    else if (settings.ssgiEnabled != m_PreviousPostSettings.ssgiEnabled ||
             settings.ssrEnabled != m_PreviousPostSettings.ssrEnabled ||
             settings.taaEnabled != m_PreviousPostSettings.taaEnabled)
        reason = "screen-space effect toggle changed";
    else if (settings.taaEnabled &&
             !FloatsNearlyEqual(settings.taaJitterSpread, m_PreviousPostSettings.taaJitterSpread))
        reason = "TAA jitter spread changed";
    else if (!FloatsNearlyEqual(settings.ssgiMaxDistance, m_PreviousPostSettings.ssgiMaxDistance) ||
             settings.ssgiStepCount != m_PreviousPostSettings.ssgiStepCount ||
             !FloatsNearlyEqual(settings.ssrMaxDistance, m_PreviousPostSettings.ssrMaxDistance) ||
             !FloatsNearlyEqual(settings.ssrMaxRoughness, m_PreviousPostSettings.ssrMaxRoughness) ||
             settings.ssrStepCount != m_PreviousPostSettings.ssrStepCount)
        reason = "screen-space trace settings changed";
    if (!reason.empty()) {
        m_HistoryValid = false;
        m_HistoryResetReason = std::move(reason);
        m_JitterSequenceIndex = 0;
    }
    // Prepared frames can still fail RenderGraph preparation or device submission. Only CommitTemporalFrame may
    // advance the camera/projection state that shaders treat as the previous displayed frame.
    m_PendingCameraPosition = position;
    m_PendingCameraForward = forward;
    m_PendingProjection = projection;
    m_PendingPostSettings = settings;
}

bool ModernDeferredPipeline::EnsureHiZResources() {
    if (m_HiZ)
        return true;
    uint32_t dimension = (std::max)(m_Width, m_Height);
    uint32_t mipLevels = 1;
    while (dimension > 1) {
        dimension >>= 1u;
        ++mipLevels;
    }
    RHITextureDesc desc;
    desc.width = m_Width;
    desc.height = m_Height;
    desc.mipLevels = mipLevels;
    desc.format = RHIFormat::RG32Float;
    desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess;
    desc.debugName = "ModernHiZMinMax";
    m_HiZ = m_Device->CreateTexture(desc);
    if (!m_HiZ)
        return false;
    RHITextureViewDesc full;
    full.mipCount = mipLevels;
    full.usage = RHIResourceUsage::ShaderResource;
    m_HiZSrv = m_Device->CreateTextureView(m_HiZ, full);
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        RHITextureViewDesc srv;
        srv.firstMip = mip;
        srv.usage = RHIResourceUsage::ShaderResource;
        m_HiZMipSrvs.push_back(m_Device->CreateTextureView(m_HiZ, srv));
        RHITextureViewDesc uav = srv;
        uav.usage = RHIResourceUsage::UnorderedAccess;
        m_HiZMipUavs.push_back(m_Device->CreateTextureView(m_HiZ, uav));
        if (!m_HiZMipSrvs.back() || !m_HiZMipUavs.back())
            return false;
    }
    return m_HiZSrv != nullptr;
}

bool ModernDeferredPipeline::EnsureIndirectBuffers(uint32_t candidateCount) {
    if (candidateCount > GpuSceneDatabase::kMaxCandidateObjects) {
        m_Stats.indirectBudgetExceeded = true;
        return false;
    }
    const uint32_t required = (std::max)(candidateCount, 1u);
    if (m_IndirectArgs && m_IndirectArgsUav && m_IndirectCount && m_IndirectCountUav && required <= m_IndirectCapacity)
        return true;
    m_IndirectCapacity = GrowIndirectCapacity(required);
    RHIBufferDesc args;
    args.size = m_IndirectCapacity * sizeof(RHIObjectDrawIndexedIndirectArgs);
    args.stride = sizeof(RHIObjectDrawIndexedIndirectArgs);
    args.usage =
        RHIResourceUsage::UnorderedAccess | RHIResourceUsage::IndirectArguments | RHIResourceUsage::ShaderResource;
    args.debugName = "ModernDepthIndirectArgs";
    m_IndirectArgs = m_Device->CreateBuffer(args);
    RHIBufferViewDesc argsView;
    argsView.elementCount = m_IndirectCapacity;
    argsView.usage = RHIResourceUsage::UnorderedAccess;
    m_IndirectArgsUav = m_Device->CreateBufferView(m_IndirectArgs, argsView);
    RHIBufferDesc count;
    count.size = sizeof(uint32_t);
    count.stride = sizeof(uint32_t);
    count.usage = RHIResourceUsage::UnorderedAccess | RHIResourceUsage::IndirectArguments |
                  RHIResourceUsage::CopyDestination | RHIResourceUsage::CopySource;
    count.debugName = "ModernDepthIndirectCount";
    const uint32_t zero = 0;
    m_IndirectCount = m_Device->CreateBuffer(count, &zero);
    RHIBufferViewDesc countView;
    countView.elementCount = 1;
    countView.usage = RHIResourceUsage::UnorderedAccess;
    m_IndirectCountUav = m_Device->CreateBufferView(m_IndirectCount, countView);
    m_IndirectArgsState = RHIResourceState::UnorderedAccess;
    m_IndirectCountState = RHIResourceState::UnorderedAccess;
    return m_IndirectArgs && m_IndirectArgsUav && m_IndirectCount && m_IndirectCountUav;
}

std::shared_ptr<ModernDeferredPipeline::ShadowIndirectStream>
ModernDeferredPipeline::EnsureShadowIndirectStream(const std::string& name) {
    if (!m_Device || m_IndirectCapacity == 0)
        return nullptr;
    auto& existing = m_ShadowIndirectStreams[name];
    if (existing && existing->args && existing->argsUav && existing->count && existing->countUav &&
        existing->cullingBindings && existing->depthBindings && existing->capacity >= m_IndirectCapacity) {
        return existing;
    }

    auto stream = std::make_shared<ShadowIndirectStream>();
    stream->capacity = m_IndirectCapacity;
    RHIBufferDesc args;
    args.size = stream->capacity * sizeof(RHIObjectDrawIndexedIndirectArgs);
    args.stride = sizeof(RHIObjectDrawIndexedIndirectArgs);
    args.usage =
        RHIResourceUsage::UnorderedAccess | RHIResourceUsage::IndirectArguments | RHIResourceUsage::ShaderResource;
    args.debugName = name + "Args";
    stream->args = m_Device->CreateBuffer(args);
    RHIBufferViewDesc argsView;
    argsView.elementCount = stream->capacity;
    argsView.usage = RHIResourceUsage::UnorderedAccess;
    stream->argsUav = m_Device->CreateBufferView(stream->args, argsView);

    RHIBufferDesc count;
    count.size = sizeof(uint32_t);
    count.stride = sizeof(uint32_t);
    count.usage =
        RHIResourceUsage::UnorderedAccess | RHIResourceUsage::IndirectArguments | RHIResourceUsage::CopyDestination;
    count.debugName = name + "Count";
    const uint32_t zero = 0;
    stream->count = m_Device->CreateBuffer(count, &zero);
    RHIBufferViewDesc countView;
    countView.elementCount = 1;
    countView.usage = RHIResourceUsage::UnorderedAccess;
    stream->countUav = m_Device->CreateBufferView(stream->count, countView);
    stream->cullingBindings = m_Device->CreateBindGroup(m_CullingShader);
    stream->depthBindings = m_Device->CreateBindGroup(m_DepthShader);
    if (!stream->args || !stream->argsUav || !stream->count || !stream->countUav || !stream->cullingBindings ||
        !stream->depthBindings) {
        return nullptr;
    }
    if (existing) {
        // Vulkan destroys native buffers when the last wrapper reference disappears. Keep replaced streams alive for
        // more than the two currently supported frames in flight; use successful submission count rather than the
        // viewport frame number so skipped/aborted frames cannot age resources out early.
        m_RetiredShadowStreams.push_back(
            {m_ShadowSubmissionSerial + kShadowStreamRetireSubmissions, std::move(existing)});
        m_Stats.retiredShadowStreams = static_cast<uint32_t>(m_RetiredShadowStreams.size());
    }
    existing = stream;
    return existing;
}

bool ModernDeferredPipeline::Prepare(const Scene& scene, const Camera& camera, uint64_t frameNumber,
                                     const ModernPostProcessSettings& settings) {
    m_Stats = {};
    m_TemporalFramePending = false;
    m_CurrentFrameNumber = frameNumber;
    m_LastShadowSetupError.clear();
    m_Stats.retiredShadowStreams = static_cast<uint32_t>(m_RetiredShadowStreams.size());
    m_FrameDepthHistoryRead = {};
    m_FrameDepthHistoryWrite = {};
    m_FrameNormalHistoryRead = {};
    m_FrameNormalHistoryWrite = {};
    m_FrameEnvironment = {};
    m_FrameScreenSpaceDebug = {};
    ConsumeDiagnosticsReadback();
    // These counters feed diagnostics only. Creating three committed readback resources every frame puts descriptor
    // and resource allocation noise directly on the render-submission p95 path. Sample once immediately and then at
    // a fixed cadence; the most recently completed values remain visible between samples.
    const bool diagnosticsTicketsIdle =
        !m_IndirectCountReadback && !m_VisibleIndirectCountReadback && !m_ClusterOverflowReadback;
    m_DiagnosticsReadbackThisFrame = m_ReadbackService && diagnosticsTicketsIdle &&
                                     (m_LastDiagnosticsReadbackFrame == 0 ||
                                      frameNumber >= m_LastDiagnosticsReadbackFrame + kDiagnosticsReadbackInterval);
    if (m_DiagnosticsReadbackThisFrame)
        m_LastDiagnosticsReadbackFrame = frameNumber;
    m_Stats.indirectDrawCount = m_LastIndirectDrawCount;
    m_Stats.hizVisibleDrawCount = m_LastVisibleIndirectDrawCount;
    m_Stats.clusterOverflow = m_LastClusterOverflow;
    if (!m_Ready)
        return false;
    const auto gpuSceneStart = std::chrono::steady_clock::now();
    if (!m_GpuScene->Update(scene, frameNumber)) {
        const auto& sceneStats = m_GpuScene->GetStats();
        m_Stats.indirectBudgetExceeded = sceneStats.candidateBudgetExceeded;
        return false;
    }
    const auto& sceneStats = m_GpuScene->GetStats();
    m_Stats.gpuScenePrepareCpuMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gpuSceneStart).count());
    m_Stats.materialResolves = sceneStats.materialResolves;
    m_Stats.materialCacheHits = sceneStats.materialCacheHits;
    m_Stats.texturedMaterials = sceneStats.texturedMaterials;
    m_Stats.candidateObjects = sceneStats.candidateObjects;
    m_Stats.localLights = sceneStats.localLights;
    m_Stats.gpuSceneUploadBytes = sceneStats.uploadBytes + sceneStats.geometryUploadBytes;
    if (!EnsureIndirectBuffers(sceneStats.candidateObjects) || !EnsureHiZResources() || !EnsureClusterResources() ||
        !EnsureTemporalResources())
        return false;
    UpdateHistoryValidity(camera, settings);
    if (m_HasCommittedFrameNumber && frameNumber != m_LastCommittedFrameNumber + 1u) {
        m_HistoryValid = false;
        m_HistoryResetReason = "viewport frame gap";
        m_JitterSequenceIndex = 0;
    }
    const Mat4 view = camera.GetView();
    const Mat4 projection = camera.GetProj();
    Mat4 jitteredProjection = projection;
    float jitterUv[2]{};
    if (settings.taaEnabled) {
        // Advance the Halton phase only after a frame is successfully submitted. Global application frames can skip
        // a viewport or abort its graph; using frameNumber here makes those events appear as visible sample jumps.
        const uint32_t jitterIndex = (m_JitterSequenceIndex % 16u) + 1u;
        const float jitterSpread = std::clamp(settings.taaJitterSpread, 0.0f, 2.0f);
        const float jitterX = (Halton(jitterIndex, 2u) - 0.5f) * jitterSpread;
        const float jitterY = (Halton(jitterIndex, 3u) - 0.5f) * jitterSpread;
        jitterUv[0] = jitterX / static_cast<float>(m_Width);
        jitterUv[1] = jitterY / static_cast<float>(m_Height);
        if (camera.GetProjectionMode() == ProjectionMode::Perspective) {
            jitteredProjection.m[2][0] += 2.0f * jitterX / static_cast<float>(m_Width);
            jitteredProjection.m[2][1] -= 2.0f * jitterY / static_cast<float>(m_Height);
        } else {
            // Orthographic clip.w is one, so the offset belongs to the translation row. Writing m[2] makes the
            // apparent jitter depth-dependent and visibly bends otherwise static geometry.
            jitteredProjection.m[3][0] += 2.0f * jitterX / static_cast<float>(m_Width);
            jitteredProjection.m[3][1] -= 2.0f * jitterY / static_cast<float>(m_Height);
        }
    }
    m_PendingJitterSequenceIndex = m_JitterSequenceIndex + (settings.taaEnabled ? 1u : 0u);
    const Mat4 jitteredViewProjection = view * jitteredProjection;
    m_CullingConstants.viewProjection = camera.GetViewProj();
    m_CullingConstants.objectCount = sceneStats.candidateObjects;
    m_CullingConstants.renderSize[0] = m_Width;
    m_CullingConstants.renderSize[1] = m_Height;
    m_CullingConstants.hizMipCount = static_cast<uint32_t>(m_HiZMipUavs.size());
    m_DepthConstants.viewProjection = jitteredViewProjection;
    m_GBufferConstants.viewProjection = jitteredViewProjection;
    m_GBufferConstants.previousViewProjection =
        m_HasPreviousViewProjection ? m_PreviousViewProjection : m_GBufferConstants.viewProjection;
    m_PendingViewProjection = m_GBufferConstants.viewProjection;
    m_ClusterConstants.view = camera.GetView();
    // Depth and GBuffer were rasterized with the jittered projection. Reconstructing positions with the unjittered
    // inverse shifts every sample by the TAA jitter and corrupts cluster selection, SSGI and SSR inputs.
    if (!Mat4Invert(jitteredProjection, m_ClusterConstants.inverseProjection) ||
        !Mat4Invert(jitteredViewProjection, m_ClusterConstants.inverseViewProjection)) {
        m_InitializationError = "camera matrices are not invertible";
        return false;
    }
    m_ClusterConstants.renderSize[0] = m_Width;
    m_ClusterConstants.renderSize[1] = m_Height;
    m_ClusterConstants.tileCount[0] = (m_Width + 31u) / 32u;
    m_ClusterConstants.tileCount[1] = (m_Height + 31u) / 32u;
    m_ClusterConstants.clusterCount = m_ClusterConstants.tileCount[0] * m_ClusterConstants.tileCount[1] * 24u;
    m_ClusterConstants.lightCount = sceneStats.localLights;
    m_ClusterConstants.nearPlane = camera.GetNear();
    m_ClusterConstants.farPlane = camera.GetFar();
    m_ClusterConstants.cameraPosition = camera.GetPosition();
    m_ClusterConstants.directionalLight = Vec4{-0.55f, -1.0f, -0.45f, 0.0f};
    m_ClusterConstants.directionalColorAmbient = Vec4{1.0f, 1.0f, 1.0f, scene.GetAmbientIntensity()};
    scene.ForEach([this](Actor& actor) {
        if (m_ClusterConstants.directionalLight.w > 0.0f || !actor.IsActive())
            return;
        const auto* light = actor.GetComponent<LightComponent>();
        if (!light || !light->IsEnabled() || light->GetLightType() != LightType::Directional)
            return;
        const Vec3 direction = light->GetDirection();
        const Vec3 color = light->GetColor();
        m_ClusterConstants.directionalLight = Vec4{direction.x, direction.y, direction.z, light->GetIntensity()};
        m_ClusterConstants.directionalColorAmbient.x = color.x;
        m_ClusterConstants.directionalColorAmbient.y = color.y;
        m_ClusterConstants.directionalColorAmbient.z = color.z;
    });
    m_PostSettings = settings;
    m_ScreenSpaceConstants.fullSize[0] = m_Width;
    m_ScreenSpaceConstants.fullSize[1] = m_Height;
    m_ScreenSpaceConstants.effectSize[0] = (std::max)(1u, (m_Width + 1u) / 2u);
    m_ScreenSpaceConstants.effectSize[1] = (std::max)(1u, (m_Height + 1u) / 2u);
    m_ScreenSpaceConstants.fullTexelSize[0] = 1.0f / m_Width;
    m_ScreenSpaceConstants.fullTexelSize[1] = 1.0f / m_Height;
    m_ScreenSpaceConstants.effectTexelSize[0] = 1.0f / m_ScreenSpaceConstants.effectSize[0];
    m_ScreenSpaceConstants.effectTexelSize[1] = 1.0f / m_ScreenSpaceConstants.effectSize[1];
    m_ScreenSpaceConstants.currentJitterUv[0] = jitterUv[0];
    m_ScreenSpaceConstants.currentJitterUv[1] = jitterUv[1];
    m_ScreenSpaceConstants.previousJitterUv[0] = m_HasPreviousViewProjection ? m_PreviousJitterUv[0] : jitterUv[0];
    m_ScreenSpaceConstants.previousJitterUv[1] = m_HasPreviousViewProjection ? m_PreviousJitterUv[1] : jitterUv[1];
    m_PendingJitterUv[0] = jitterUv[0];
    m_PendingJitterUv[1] = jitterUv[1];
    m_ScreenSpaceConstants.frameIndex = static_cast<uint32_t>(frameNumber % 16u);
    m_ScreenSpaceConstants.view = view;
    m_ScreenSpaceConstants.projection = jitteredProjection;
    if (!Mat4Invert(jitteredProjection, m_ScreenSpaceConstants.inverseProjection) ||
        !Mat4Invert(jitteredViewProjection, m_ScreenSpaceConstants.inverseViewProjection) ||
        !Mat4Invert(m_GBufferConstants.previousViewProjection, m_ScreenSpaceConstants.previousInverseViewProjection))
        return false;
    m_ScreenSpaceConstants.previousViewProjection = m_GBufferConstants.previousViewProjection;
    m_ScreenSpaceConstants.historyValid = m_HistoryValid ? 1u : 0u;
    m_ScreenSpaceConstants.cameraPositionAmbient =
        Vec4{camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z, scene.GetAmbientIntensity()};
    const Vec3 previousCameraPosition = m_HasPreviousProjection ? m_PreviousCameraPosition : camera.GetPosition();
    m_ScreenSpaceConstants.previousCameraPosition =
        Vec4{previousCameraPosition.x, previousCameraPosition.y, previousCameraPosition.z, 0.0f};
    m_ScreenSpaceConstants.ssgiIntensity = settings.ssgiIntensity;
    m_ScreenSpaceConstants.ssgiMaxDistance = settings.ssgiMaxDistance;
    m_ScreenSpaceConstants.ssgiHistoryWeight = settings.ssgiHistoryWeight;
    m_ScreenSpaceConstants.ssrMaxDistance = settings.ssrMaxDistance;
    m_ScreenSpaceConstants.ssrMaxRoughness = settings.ssrMaxRoughness;
    m_ScreenSpaceConstants.ssrHistoryWeight = settings.ssrHistoryWeight;
    m_ScreenSpaceConstants.taaHistoryWeight = settings.taaHistoryWeight;
    m_ScreenSpaceConstants.taaHistoryClipExpansion = settings.taaHistoryClipExpansion;
    m_ScreenSpaceConstants.exposure = settings.exposure;
    m_ScreenSpaceConstants.gamma = settings.gamma;
    m_ScreenSpaceConstants.bloomThreshold = settings.bloomThreshold;
    m_ScreenSpaceConstants.bloomIntensity = settings.bloomIntensity;
    const bool consoleQuality = m_QualityProfile == QualityProfile::Console;
    m_ScreenSpaceConstants.ssgiStepCount =
        consoleQuality ? (std::min)(settings.ssgiStepCount, 16u) : settings.ssgiStepCount;
    m_ScreenSpaceConstants.ssrStepCount =
        consoleQuality ? (std::min)(settings.ssrStepCount, 24u) : settings.ssrStepCount;
    m_Stats.clusterCount = m_ClusterConstants.clusterCount;
    m_Stats.indirectDrawCapacity = m_IndirectCapacity;
    return true;
}

bool ModernDeferredPipeline::AddGpuDrivenShadowView(RenderGraph& graph, const std::string& name,
                                                    RGTextureHandle shadowTarget, RGTextureSubresource subresource,
                                                    const Mat4& viewProjection) {
    const auto fail = [this, &name](std::string reason) {
        ++m_Stats.gpuShadowSetupFailures;
        m_LastShadowSetupError = name + ": " + std::move(reason);
        const std::string diagnostic = "GpuShadowSetup: " + m_LastShadowSetupError;
        std::lock_guard<std::mutex> lock(g_ModernBindingDiagnosticsMutex);
        if (g_ModernBindingDiagnostics.insert(diagnostic).second)
            Logger::Error("[ModernDeferred] ", m_LastShadowSetupError);
        return false;
    };
    if (!shadowTarget.IsValid())
        return fail("invalid shadow target");
    if (!m_Device || !m_GpuScene || !m_CullingPipeline || !m_CullingShader || !m_ShadowDepthPipeline ||
        !m_DepthShader) {
        return fail("shadow pipelines are unavailable");
    }

    if (m_CullingConstants.objectCount == 0) {
        graph.AddPass(
            name + "Clear",
            [shadowTarget, subresource](RenderGraphBuilder& builder) {
                builder.WriteDepth(shadowTarget, subresource, RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
            },
            [](GpuCommandList&, const RenderGraphResources&) {});
        return true;
    }

    const auto& geometry = m_GpuScene->GetGeometryArena();
    if (!m_GpuScene->GetObjectBuffer() || !m_GpuScene->GetObjectView() || !m_GpuScene->GetMaterialBuffer() ||
        !m_GpuScene->GetMaterialView() || !HasMaterialSamplerTable(m_MaterialSamplers) || !geometry.GetVertexBuffer() ||
        !geometry.GetIndexBuffer()) {
        return fail("GPU scene resources are incomplete");
    }
    const auto stream = EnsureShadowIndirectStream(name);
    if (!stream)
        return fail("indirect stream allocation failed");

    CullingConstants cullingConstants = m_CullingConstants;
    cullingConstants.viewProjection = viewProjection;
    if (!stream->cullingBindings->GetShader() || stream->cullingBindings->GetShader()->reflection.bindings.empty())
        return fail("culling shader reflection is empty");
    if (!stream->cullingBindings->SetConstants("CullingConstants", &cullingConstants, sizeof(cullingConstants)) ||
        !stream->cullingBindings->SetBuffer("g_Objects", m_GpuScene->GetObjectView()) ||
        !stream->cullingBindings->SetStorageBuffer("g_DrawArgs", stream->argsUav) ||
        !stream->cullingBindings->SetStorageBuffer("g_DrawCount", stream->countUav)) {
        return fail("culling bindings do not match the shader contract");
    }
    std::string validationError;
    if (!stream->cullingBindings->Validate(&validationError))
        return fail("culling bindings are incomplete: " + validationError);

    DepthConstants depthConstants = m_DepthConstants;
    depthConstants.viewProjection = viewProjection;
    if (!stream->depthBindings->GetShader() || stream->depthBindings->GetShader()->reflection.bindings.empty())
        return fail("depth shader reflection is empty");
    bool depthBindingsValid =
        stream->depthBindings->SetConstants("DepthViewConstants", &depthConstants, sizeof(depthConstants));
    if (m_Device->GetBackend() == RHIBackend::D3D12) {
        const uint32_t indirectObjectIndex = 0;
        depthBindingsValid =
            depthBindingsValid && stream->depthBindings->SetConstants("ModernDrawConstants", &indirectObjectIndex,
                                                                      sizeof(indirectObjectIndex));
    }
    depthBindingsValid = depthBindingsValid &&
                         stream->depthBindings->SetBuffer("g_Objects", m_GpuScene->GetObjectView()) &&
                         stream->depthBindings->SetBuffer("g_Materials", m_GpuScene->GetMaterialView()) &&
                         SetMaterialSamplerTable(stream->depthBindings, m_MaterialSamplers);
    if (!depthBindingsValid)
        return fail("depth bindings do not match the shader contract");
    validationError.clear();
    if (!stream->depthBindings->Validate(&validationError))
        return fail("depth bindings are incomplete: " + validationError);

    const auto objectBuffer = graph.ImportBuffer(name + "Objects", m_GpuScene->GetObjectBuffer(),
                                                 RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
    const auto materialBuffer = graph.ImportBuffer(name + "Materials", m_GpuScene->GetMaterialBuffer(),
                                                   RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
    const auto indirectArgs =
        graph.ImportBuffer(name + "Args", stream->args, stream->argsState, RHIResourceState::IndirectArgument);
    const auto indirectCount =
        graph.ImportBuffer(name + "Count", stream->count, stream->countState, RHIResourceState::IndirectArgument);
    graph.AddComputePass(
        name + "Reset", [indirectCount](RenderGraphBuilder& builder) { builder.ReadWriteUAV(indirectCount); },
        [stream](GpuCommandList& commands, const RenderGraphResources&) {
            commands.ClearStorageBuffer(stream->countUav.get(), 0);
        });
    graph.AddComputePass(
        name + "Cull",
        [objectBuffer, indirectArgs, indirectCount](RenderGraphBuilder& builder) {
            builder.ReadBuffer(objectBuffer);
            builder.ReadWriteUAV(indirectArgs);
            builder.ReadWriteUAV(indirectCount);
        },
        [this, stream, objectCount = cullingConstants.objectCount,
         passName = name + "Cull"](GpuCommandList& commands, const RenderGraphResources&) {
            commands.SetComputePipeline(m_CullingPipeline.get());
            if (!BindModernPass(commands, passName.c_str(), stream->cullingBindings))
                return;
            commands.Dispatch((objectCount + 63u) / 64u, 1, 1);
        });
    graph.AddPass(
        name + "Draw",
        [shadowTarget, subresource, objectBuffer, materialBuffer, indirectArgs,
         indirectCount](RenderGraphBuilder& builder) {
            builder.ReadBuffer(objectBuffer);
            builder.ReadBuffer(materialBuffer);
            builder.ReadIndirect(indirectArgs);
            builder.ReadIndirect(indirectCount);
            builder.WriteDepth(shadowTarget, subresource, RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
        },
        [this, stream, passName = name + "Draw"](GpuCommandList& commands, const RenderGraphResources&) {
            commands.SetGraphicsPipeline(m_ShadowDepthPipeline.get());
            if (!BindModernPass(commands, passName.c_str(), stream->depthBindings))
                return;
            commands.SetVertexBuffer(m_GpuScene->GetGeometryArena().GetVertexBuffer().get());
            commands.SetIndexBuffer(m_GpuScene->GetGeometryArena().GetIndexBuffer().get());
            commands.DrawIndexedIndirectCount(stream->args.get(), 0, stream->count.get(), 0, stream->capacity,
                                              sizeof(RHIObjectDrawIndexedIndirectArgs));
        });
    if (std::find(m_PendingShadowStreams.begin(), m_PendingShadowStreams.end(), stream) ==
        m_PendingShadowStreams.end()) {
        m_PendingShadowStreams.push_back(stream);
    }
    return true;
}

void ModernDeferredPipeline::AddDepthPrepass(RenderGraph& graph, RGTextureHandle sceneDepth) {
    const auto objectBuffer = graph.ImportBuffer("GpuSceneObjects", m_GpuScene->GetObjectBuffer(),
                                                 RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
    const auto materialBuffer = graph.ImportBuffer("GpuSceneDepthMaterials", m_GpuScene->GetMaterialBuffer(),
                                                   RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
    const auto indirectArgs = graph.ImportBuffer("ModernDepthIndirectArgs", m_IndirectArgs, m_IndirectArgsState,
                                                 RHIResourceState::IndirectArgument);
    const auto indirectCount = graph.ImportBuffer("ModernDepthIndirectCount", m_IndirectCount, m_IndirectCountState,
                                                  RHIResourceState::IndirectArgument);
    graph.AddComputePass(
        "ResetFrustumCullingCounter",
        [indirectCount](RenderGraphBuilder& builder) { builder.ReadWriteUAV(indirectCount); },
        [this](GpuCommandList& commands, const RenderGraphResources&) {
            commands.ClearStorageBuffer(m_IndirectCountUav.get(), 0);
        });
    graph.AddComputePass(
        "GpuFrustumCulling",
        [objectBuffer, indirectArgs, indirectCount](RenderGraphBuilder& builder) {
            builder.ReadBuffer(objectBuffer);
            builder.ReadWriteUAV(indirectArgs);
            builder.ReadWriteUAV(indirectCount);
        },
        [this](GpuCommandList& commands, const RenderGraphResources&) {
            commands.SetComputePipeline(m_CullingPipeline.get());
            auto bindings = AcquireBindGroup(m_CullingShader);
            if (!bindings)
                return;
            bindings->SetConstants("CullingConstants", &m_CullingConstants, sizeof(m_CullingConstants));
            bindings->SetBuffer("g_Objects", m_GpuScene->GetObjectView());
            bindings->SetStorageBuffer("g_DrawArgs", m_IndirectArgsUav);
            bindings->SetStorageBuffer("g_DrawCount", m_IndirectCountUav);
            if (!BindModernPass(commands, "GpuFrustumCulling", bindings))
                return;
            commands.Dispatch((m_CullingConstants.objectCount + 63u) / 64u, 1, 1);
        });
    graph.AddPass(
        "DepthPrepassIndirect",
        [sceneDepth, objectBuffer, materialBuffer, indirectArgs, indirectCount](RenderGraphBuilder& builder) {
            builder.ReadBuffer(objectBuffer);
            builder.ReadBuffer(materialBuffer);
            builder.ReadIndirect(indirectArgs);
            builder.ReadIndirect(indirectCount);
            builder.WriteDepth(sceneDepth, RHILoadOp::Clear, RHIStoreOp::Store, 1.0f);
        },
        [this](GpuCommandList& commands, const RenderGraphResources&) {
            const auto vertexBuffer = m_GpuScene->GetGeometryArena().GetVertexBuffer();
            const auto indexBuffer = m_GpuScene->GetGeometryArena().GetIndexBuffer();
            // Vulkan validates indexed-draw state even when the indirect count buffer contains zero. During the
            // startup frame the immutable scene extract can legitimately be empty, so keep the depth clear but do
            // not encode an indexed draw until a geometry arena exists.
            if (m_CullingConstants.objectCount == 0 || !vertexBuffer || !indexBuffer)
                return;
            commands.SetGraphicsPipeline(m_DepthPipeline.get());
            auto bindings = AcquireBindGroup(m_DepthShader);
            if (!bindings)
                return;
            bindings->SetConstants("DepthViewConstants", &m_DepthConstants, sizeof(m_DepthConstants));
            if (m_Device->GetBackend() == RHIBackend::D3D12) {
                const uint32_t indirectObjectIndex = 0;
                bindings->SetConstants("ModernDrawConstants", &indirectObjectIndex, sizeof(indirectObjectIndex));
            }
            bindings->SetBuffer("g_Objects", m_GpuScene->GetObjectView());
            bindings->SetBuffer("g_Materials", m_GpuScene->GetMaterialView());
            SetMaterialSamplerTable(bindings, m_MaterialSamplers);
            if (!BindModernPass(commands, "DepthPrepassIndirect", bindings))
                return;
            commands.SetVertexBuffer(vertexBuffer.get());
            commands.SetIndexBuffer(indexBuffer.get());
            commands.DrawIndexedIndirectCount(m_IndirectArgs.get(), 0, m_IndirectCount.get(), 0,
                                              m_Stats.indirectDrawCapacity, sizeof(RHIObjectDrawIndexedIndirectArgs));
        });
    m_IndirectArgsState = RHIResourceState::IndirectArgument;
    m_IndirectCountState = RHIResourceState::IndirectArgument;
    if (m_DiagnosticsReadbackThisFrame) {
        graph.AddComputePass(
            "CullingStatsReadback",
            [indirectCount](RenderGraphBuilder& builder) { builder.ReadCopySource(indirectCount); },
            [this](GpuCommandList&, const RenderGraphResources&) {
                if (!m_IndirectCountReadback)
                    m_IndirectCountReadback = m_ReadbackService->ReadbackBufferAsync(m_IndirectCount);
            });
        m_IndirectCountState = RHIResourceState::CopySource;
    }
}

RGTextureHandle ModernDeferredPipeline::AddHiZPasses(RenderGraph& graph, RGTextureHandle sceneDepth,
                                                     const std::shared_ptr<GpuTextureView>& sceneDepthSrv) {
    const auto hiz =
        graph.ImportTexture("ModernHiZ", m_HiZ, m_HiZSrv,
                            m_HiZInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined,
                            RHIResourceState::ShaderResource);
    graph.AddComputePass(
        "HiZInit",
        [sceneDepth, hiz](RenderGraphBuilder& builder) {
            builder.ReadTexture(sceneDepth);
            builder.ReadWriteUAV(hiz, RGTextureSubresource{0, 1, 0, 1});
        },
        [this, sceneDepthSrv](GpuCommandList& commands, const RenderGraphResources&) {
            HiZConstants constants{{m_Width, m_Height}, {m_Width, m_Height}};
            commands.SetComputePipeline(m_HiZInitPipeline.get());
            auto bindings = AcquireBindGroup(m_HiZInitShader);
            if (!bindings)
                return;
            bindings->SetConstants("HiZConstants", &constants, sizeof(constants));
            bindings->SetTexture("g_SourceDepth", sceneDepthSrv);
            bindings->SetStorageTexture("g_DestinationHiZ", m_HiZMipUavs[0]);
            if (!BindModernPass(commands, "HiZInit", bindings))
                return;
            commands.Dispatch((m_Width + 7u) / 8u, (m_Height + 7u) / 8u, 1);
        });
    for (uint32_t mip = 1; mip < m_HiZMipUavs.size(); ++mip) {
        const uint32_t sourceWidth = (std::max)(1u, m_Width >> (mip - 1u));
        const uint32_t sourceHeight = (std::max)(1u, m_Height >> (mip - 1u));
        const uint32_t destinationWidth = (std::max)(1u, m_Width >> mip);
        const uint32_t destinationHeight = (std::max)(1u, m_Height >> mip);
        graph.AddComputePass(
            "HiZReduceMip" + std::to_string(mip),
            [hiz, mip](RenderGraphBuilder& builder) {
                builder.ReadTexture(hiz, RGTextureSubresource{mip - 1u, 1, 0, 1});
                builder.ReadWriteUAV(hiz, RGTextureSubresource{mip, 1, 0, 1});
            },
            [this, mip, sourceWidth, sourceHeight, destinationWidth, destinationHeight](GpuCommandList& commands,
                                                                                        const RenderGraphResources&) {
                HiZConstants constants{{sourceWidth, sourceHeight}, {destinationWidth, destinationHeight}};
                commands.SetComputePipeline(m_HiZReducePipeline.get());
                auto bindings = AcquireBindGroup(m_HiZReduceShader);
                if (!bindings)
                    return;
                bindings->SetConstants("HiZConstants", &constants, sizeof(constants));
                bindings->SetTexture("g_SourceHiZ", m_HiZMipSrvs[mip - 1u]);
                bindings->SetStorageTexture("g_DestinationHiZ", m_HiZMipUavs[mip]);
                if (!BindModernPass(commands, "HiZReduce", bindings))
                    return;
                commands.Dispatch((destinationWidth + 7u) / 8u, (destinationHeight + 7u) / 8u, 1);
            });
    }
    m_HiZInShaderState = true;
    return hiz;
}

void ModernDeferredPipeline::AddHiZOcclusionCulling(RenderGraph& graph, RGTextureHandle hiz) {
    const auto objects = graph.ImportBuffer("GpuSceneObjectsOcclusion", m_GpuScene->GetObjectBuffer(),
                                            RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
    // The depth prepass has already consumed the frustum stream, so compact the HiZ-visible set in place. Keeping one
    // authoritative stream prevents the counted arguments from diverging from the arguments executed by GBuffer.
    const auto visibleArgs = graph.ImportBuffer("ModernVisibleIndirectArgs", m_IndirectArgs, m_IndirectArgsState,
                                                RHIResourceState::UnorderedAccess);
    const auto visibleCount = graph.ImportBuffer("ModernVisibleIndirectCount", m_IndirectCount, m_IndirectCountState,
                                                 RHIResourceState::UnorderedAccess);
    graph.AddComputePass(
        "ResetHiZOcclusionCounter", [visibleCount](RenderGraphBuilder& builder) { builder.ReadWriteUAV(visibleCount); },
        [this](GpuCommandList& commands, const RenderGraphResources&) {
            commands.ClearStorageBuffer(m_IndirectCountUav.get(), 0);
        });
    graph.AddComputePass(
        "GpuHiZOcclusionCulling",
        [objects, hiz, visibleArgs, visibleCount](RenderGraphBuilder& builder) {
            builder.ReadBuffer(objects);
            builder.ReadTexture(hiz);
            builder.ReadWriteUAV(visibleArgs);
            builder.ReadWriteUAV(visibleCount);
        },
        [this](GpuCommandList& commands, const RenderGraphResources&) {
            commands.SetComputePipeline(m_OcclusionCullingPipeline.get());
            auto bindings = AcquireBindGroup(m_OcclusionCullingShader);
            if (!bindings)
                return;
            bindings->SetConstants("CullingConstants", &m_CullingConstants, sizeof(m_CullingConstants));
            bindings->SetBuffer("g_Objects", m_GpuScene->GetObjectView());
            bindings->SetTexture("g_HiZ", m_HiZSrv);
            bindings->SetStorageBuffer("g_DrawArgs", m_IndirectArgsUav);
            bindings->SetStorageBuffer("g_DrawCount", m_IndirectCountUav);
            if (!BindModernPass(commands, "GpuHiZOcclusionCulling", bindings))
                return;
            commands.Dispatch((m_CullingConstants.objectCount + 63u) / 64u, 1, 1);
        });
    m_IndirectArgsState = RHIResourceState::UnorderedAccess;
    m_IndirectCountState = RHIResourceState::UnorderedAccess;
    if (m_DiagnosticsReadbackThisFrame) {
        graph.AddComputePass(
            "HiZOcclusionStatsReadback",
            [visibleCount](RenderGraphBuilder& builder) { builder.ReadCopySource(visibleCount); },
            [this](GpuCommandList&, const RenderGraphResources&) {
                if (!m_VisibleIndirectCountReadback)
                    m_VisibleIndirectCountReadback = m_ReadbackService->ReadbackBufferAsync(m_IndirectCount);
            });
        m_IndirectCountState = RHIResourceState::CopySource;
    }
}

void ModernDeferredPipeline::AddGBufferPass(RenderGraph& graph, RGTextureHandle albedo, RGTextureHandle normal,
                                            RGTextureHandle material, RGTextureHandle emissive,
                                            RGTextureHandle velocity, RGTextureHandle sceneDepth) {
    const auto objects = graph.ImportBuffer("GpuSceneObjectsGBuffer", m_GpuScene->GetObjectBuffer(),
                                            RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
    const auto materials = graph.ImportBuffer("GpuSceneMaterials", m_GpuScene->GetMaterialBuffer(),
                                              RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
    const auto visibleArgs = graph.ImportBuffer("ModernGBufferIndirectArgs", m_IndirectArgs, m_IndirectArgsState,
                                                RHIResourceState::IndirectArgument);
    const auto visibleCount = graph.ImportBuffer("ModernGBufferIndirectCount", m_IndirectCount, m_IndirectCountState,
                                                 RHIResourceState::IndirectArgument);
    graph.AddPass(
        "GBufferIndirect",
        [objects, materials, visibleArgs, visibleCount, albedo, normal, material, emissive, velocity,
         sceneDepth](RenderGraphBuilder& builder) {
            builder.ReadBuffer(objects);
            builder.ReadBuffer(materials);
            builder.ReadIndirect(visibleArgs);
            builder.ReadIndirect(visibleCount);
            // Compatibility GBuffer runs first so its depth participates in the HiZ used by culling, SSGI and SSR.
            // Preserve those CPU-submitted/skinned/custom-shader pixels while the indirect Standard batch fills the
            // remaining samples.
            builder.WriteColor(albedo, RHILoadOp::Load, RHIStoreOp::Store);
            builder.WriteColor(normal, RHILoadOp::Load, RHIStoreOp::Store);
            builder.WriteColor(material, RHILoadOp::Load, RHIStoreOp::Store);
            builder.WriteColor(emissive, RHILoadOp::Load, RHIStoreOp::Store);
            builder.WriteColor(velocity, RHILoadOp::Load, RHIStoreOp::Store);
            builder.WriteDepth(sceneDepth, RHILoadOp::Load, RHIStoreOp::Store, 1.0f);
        },
        [this](GpuCommandList& commands, const RenderGraphResources&) {
            const auto vertexBuffer = m_GpuScene->GetGeometryArena().GetVertexBuffer();
            const auto indexBuffer = m_GpuScene->GetGeometryArena().GetIndexBuffer();
            // Preserve the attachment clears on an empty startup frame without issuing an invalid indexed command.
            if (m_CullingConstants.objectCount == 0 || !vertexBuffer || !indexBuffer)
                return;
            commands.SetGraphicsPipeline(m_GBufferPipeline.get());
            auto bindings = AcquireBindGroup(m_GBufferShader);
            if (!bindings)
                return;
            bindings->SetConstants("ModernGBufferConstants", &m_GBufferConstants, sizeof(m_GBufferConstants));
            if (m_Device->GetBackend() == RHIBackend::D3D12) {
                const uint32_t indirectObjectIndex = 0;
                bindings->SetConstants("ModernDrawConstants", &indirectObjectIndex, sizeof(indirectObjectIndex));
            }
            bindings->SetBuffer("g_Objects", m_GpuScene->GetObjectView());
            bindings->SetBuffer("g_Materials", m_GpuScene->GetMaterialView());
            SetMaterialSamplerTable(bindings, m_MaterialSamplers);
            if (!BindModernPass(commands, "GBufferIndirect", bindings))
                return;
            commands.SetVertexBuffer(vertexBuffer.get());
            commands.SetIndexBuffer(indexBuffer.get());
            commands.DrawIndexedIndirectCount(m_IndirectArgs.get(), 0, m_IndirectCount.get(), 0,
                                              m_Stats.indirectDrawCapacity, sizeof(RHIObjectDrawIndexedIndirectArgs));
        });
    m_IndirectArgsState = RHIResourceState::IndirectArgument;
    m_IndirectCountState = RHIResourceState::IndirectArgument;
}

RGTextureHandle ModernDeferredPipeline::AddClusteredLightingPasses(
    RenderGraph& graph, const Camera&, RGTextureHandle gbufferAlbedo,
    const std::shared_ptr<GpuTextureView>& gbufferAlbedoSrv, RGTextureHandle gbufferNormal,
    const std::shared_ptr<GpuTextureView>& gbufferNormalSrv, RGTextureHandle gbufferMaterial,
    const std::shared_ptr<GpuTextureView>& gbufferMaterialSrv, RGTextureHandle gbufferEmissive,
    const std::shared_ptr<GpuTextureView>& gbufferEmissiveSrv, RGTextureHandle sceneDepth,
    const std::shared_ptr<GpuTextureView>& sceneDepthSrv, RGTextureHandle environmentCube,
    const std::shared_ptr<GpuTextureView>& environmentCubeSrv, RGBufferHandle environmentSH,
    const std::shared_ptr<GpuBufferView>& environmentSHSrv, RGTextureHandle directionalShadow) {
    RGTextureHandle lightingEnvironment = environmentCube;
    std::shared_ptr<GpuTextureView> lightingEnvironmentSrv = environmentCubeSrv;
    if (!lightingEnvironment.IsValid() || !lightingEnvironmentSrv) {
        lightingEnvironment =
            graph.ImportTexture("ModernEnvironmentFallback", m_EnvironmentFallback, m_EnvironmentFallbackSrv,
                                RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
        lightingEnvironmentSrv = m_EnvironmentFallbackSrv;
    }
    RGBufferHandle lightingEnvironmentSH = environmentSH;
    std::shared_ptr<GpuBufferView> lightingEnvironmentSHSrv = environmentSHSrv;
    if (!lightingEnvironmentSH.IsValid() || !lightingEnvironmentSHSrv) {
        lightingEnvironmentSH = graph.ImportBuffer("ModernEnvironmentSHFallback", m_EnvironmentSHFallback,
                                                   RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
        lightingEnvironmentSHSrv = m_EnvironmentSHFallbackSrv;
    }
    m_FrameEnvironment = lightingEnvironment;
    m_EnvironmentCubeSrv = lightingEnvironmentSrv;
    // CreateBuffer initializes ShaderResource|UnorderedAccess buffers in UAV state. Importing the first frame as
    // Undefined would record a COMMON->UAV barrier whose before-state does not match the native D3D12 resource.
    const RHIResourceState clusterInitial =
        m_ClusterBuffersInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::UnorderedAccess;
    const auto lights = graph.ImportBuffer("GpuSceneLights", m_GpuScene->GetLightBuffer(),
                                           RHIResourceState::ShaderResource, RHIResourceState::ShaderResource);
    const auto counts =
        graph.ImportBuffer("ClusterCounts", m_ClusterCounts, clusterInitial, RHIResourceState::ShaderResource);
    const auto offsets =
        graph.ImportBuffer("ClusterOffsets", m_ClusterOffsets, clusterInitial, RHIResourceState::ShaderResource);
    const auto indices = graph.ImportBuffer("ClusterLightIndices", m_ClusterLightIndices, clusterInitial,
                                            RHIResourceState::ShaderResource);
    const auto overflow = graph.ImportBuffer("ClusterOverflow", m_ClusterOverflow, m_ClusterOverflowState,
                                             RHIResourceState::ShaderResource);
    const auto hdr =
        graph.ImportTexture("ModernDeferredHDR", m_Hdr, m_HdrRtv,
                            m_HdrInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined,
                            RHIResourceState::ShaderResource);

    if (m_ClusterConstants.lightCount == 0) {
        graph.AddComputePass(
            "ResetEmptyClusterData",
            [counts, overflow](RenderGraphBuilder& builder) {
                builder.ReadWriteUAV(counts);
                builder.ReadWriteUAV(overflow);
            },
            [this](GpuCommandList& commands, const RenderGraphResources&) {
                commands.ClearStorageBuffer(m_ClusterCountsView.get(), 0);
                commands.ClearStorageBuffer(m_ClusterOverflowView.get(), 0);
            });
    } else {
        graph.AddComputePass(
            "ResetClusterOverflowCounter", [overflow](RenderGraphBuilder& builder) { builder.ReadWriteUAV(overflow); },
            [this](GpuCommandList& commands, const RenderGraphResources&) {
                commands.ClearStorageBuffer(m_ClusterOverflowView.get(), 0);
            });
        graph.AddComputePass(
            "ClusterLightCount",
            [lights, counts, overflow](RenderGraphBuilder& builder) {
                builder.ReadBuffer(lights);
                builder.ReadWriteUAV(counts);
                builder.ReadWriteUAV(overflow);
            },
            [this](GpuCommandList& commands, const RenderGraphResources&) {
                commands.SetComputePipeline(m_ClusterCountPipeline.get());
                auto bindings = AcquireBindGroup(m_ClusterCountShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ClusterConstants", &m_ClusterConstants, sizeof(m_ClusterConstants));
                bindings->SetBuffer("g_Lights", m_GpuScene->GetLightView());
                bindings->SetStorageBuffer("g_ClusterCountsOut", m_ClusterCountsView);
                bindings->SetStorageBuffer("g_ClusterOverflow", m_ClusterOverflowView);
                if (!BindModernPass(commands, "ClusterLightCount", bindings))
                    return;
                commands.Dispatch((m_ClusterConstants.clusterCount + 63u) / 64u, 1, 1);
            });
        graph.AddComputePass(
            "ClusterPrefixScan",
            [counts, offsets](RenderGraphBuilder& builder) {
                builder.ReadBuffer(counts);
                builder.ReadWriteUAV(offsets);
            },
            [this](GpuCommandList& commands, const RenderGraphResources&) {
                commands.SetComputePipeline(m_ClusterPrefixPipeline.get());
                auto bindings = AcquireBindGroup(m_ClusterPrefixShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ClusterConstants", &m_ClusterConstants, sizeof(m_ClusterConstants));
                bindings->SetBuffer("g_ClusterCounts", m_ClusterCountsView);
                bindings->SetStorageBuffer("g_ClusterOffsetsOut", m_ClusterOffsetsView);
                if (!BindModernPass(commands, "ClusterPrefixScan", bindings))
                    return;
                commands.Dispatch(1, 1, 1);
            });
        graph.AddComputePass(
            "ClusterLightScatter",
            [lights, counts, offsets, indices](RenderGraphBuilder& builder) {
                builder.ReadBuffer(lights);
                builder.ReadBuffer(counts);
                builder.ReadBuffer(offsets);
                builder.ReadWriteUAV(indices);
            },
            [this](GpuCommandList& commands, const RenderGraphResources&) {
                commands.SetComputePipeline(m_ClusterScatterPipeline.get());
                auto bindings = AcquireBindGroup(m_ClusterScatterShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ClusterConstants", &m_ClusterConstants, sizeof(m_ClusterConstants));
                bindings->SetBuffer("g_Lights", m_GpuScene->GetLightView());
                bindings->SetBuffer("g_ClusterCounts", m_ClusterCountsView);
                bindings->SetBuffer("g_ClusterOffsets", m_ClusterOffsetsView);
                bindings->SetStorageBuffer("g_ClusterLightIndicesOut", m_ClusterLightIndicesView);
                if (!BindModernPass(commands, "ClusterLightScatter", bindings))
                    return;
                commands.Dispatch((m_ClusterConstants.clusterCount + 63u) / 64u, 1, 1);
            });
    }
    graph.AddComputePass(
        "ComputeDeferredLighting",
        [gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferEmissive, sceneDepth, lights, counts, offsets, indices,
         hdr, lightingEnvironment, lightingEnvironmentSH, directionalShadow,
         shadowEnabled = m_ClusterConstants.shadowInfo.x > 0.5f](RenderGraphBuilder& builder) {
            builder.ReadTexture(gbufferAlbedo);
            builder.ReadTexture(gbufferNormal);
            builder.ReadTexture(gbufferMaterial);
            builder.ReadTexture(gbufferEmissive);
            builder.ReadTexture(sceneDepth);
            builder.ReadBuffer(lights);
            builder.ReadBuffer(counts);
            builder.ReadBuffer(offsets);
            builder.ReadBuffer(indices);
            builder.ReadTexture(lightingEnvironment);
            builder.ReadBuffer(lightingEnvironmentSH);
            if (shadowEnabled && directionalShadow.IsValid())
                builder.ReadTexture(directionalShadow);
            builder.ReadWriteUAV(hdr);
        },
        [this, gbufferAlbedoSrv, gbufferNormalSrv, gbufferMaterialSrv, gbufferEmissiveSrv, sceneDepthSrv,
         lightingEnvironmentSrv, lightingEnvironmentSHSrv](GpuCommandList& commands, const RenderGraphResources&) {
            commands.SetComputePipeline(m_ClusterLightingPipeline.get());
            auto bindings = AcquireBindGroup(m_ClusterLightingShader);
            if (!bindings)
                return;
            bindings->SetConstants("ClusterConstants", &m_ClusterConstants, sizeof(m_ClusterConstants));
            bindings->SetBuffer("g_Lights", m_GpuScene->GetLightView());
            bindings->SetBuffer("g_ClusterCounts", m_ClusterCountsView);
            bindings->SetBuffer("g_ClusterOffsets", m_ClusterOffsetsView);
            bindings->SetBuffer("g_ClusterLightIndices", m_ClusterLightIndicesView);
            bindings->SetTexture("g_GBufferAlbedo", gbufferAlbedoSrv);
            bindings->SetTexture("g_GBufferNormal", gbufferNormalSrv);
            bindings->SetTexture("g_GBufferMaterial", gbufferMaterialSrv);
            bindings->SetTexture("g_GBufferEmissive", gbufferEmissiveSrv);
            bindings->SetTexture("g_SceneDepth", sceneDepthSrv);
            bindings->SetTexture("g_IBLCubemap", lightingEnvironmentSrv);
            bindings->SetBuffer("g_EnvironmentSH2", lightingEnvironmentSHSrv);
            if (m_ProbeReflectionAtlas)
                bindings->SetTexture("g_LocalReflectionProbes", m_ProbeReflectionAtlas);
            if (m_ProbeReflectionMetadata)
                bindings->SetBuffer("g_LocalReflectionProbeData", m_ProbeReflectionMetadata);
            if (m_ProbeSHVolumeMetadata)
                bindings->SetBuffer("g_LocalSHProbeVolumes", m_ProbeSHVolumeMetadata);
            if (m_ProbeSHCoefficients)
                bindings->SetBuffer("g_LocalSHCoefficients", m_ProbeSHCoefficients);
            bindings->SetTexture("g_ShadowMap", m_DirectionalShadowSrv);
            bindings->SetSampler("g_LinearSampler", m_LinearClampSampler);
            bindings->SetSampler("g_ShadowSampler", m_ShadowSampler);
            bindings->SetStorageTexture("g_HdrOutput", m_HdrUav);
            if (!BindModernPass(commands, "ComputeDeferredLighting", bindings))
                return;
            commands.Dispatch((m_Width + 7u) / 8u, (m_Height + 7u) / 8u, 1);
        });
    if (m_DiagnosticsReadbackThisFrame) {
        graph.AddComputePass(
            "ClusterOverflowReadback", [overflow](RenderGraphBuilder& builder) { builder.ReadCopySource(overflow); },
            [this](GpuCommandList&, const RenderGraphResources&) {
                if (!m_ClusterOverflowReadback)
                    m_ClusterOverflowReadback = m_ReadbackService->ReadbackBufferAsync(m_ClusterOverflow);
            });
    }
    m_ClusterBuffersInShaderState = true;
    m_ClusterOverflowState =
        m_DiagnosticsReadbackThisFrame ? RHIResourceState::CopySource : RHIResourceState::ShaderResource;
    m_HdrInShaderState = true;
    return hdr;
}

RGTextureHandle ModernDeferredPipeline::AddScreenSpaceEffects(
    RenderGraph& graph, RGTextureHandle hdr, const std::shared_ptr<GpuTextureView>& hdrSrv, RGTextureHandle sceneDepth,
    const std::shared_ptr<GpuTextureView>& sceneDepthSrv, RGTextureHandle gbufferAlbedo,
    const std::shared_ptr<GpuTextureView>& gbufferAlbedoSrv, RGTextureHandle gbufferNormal,
    const std::shared_ptr<GpuTextureView>& gbufferNormalSrv, RGTextureHandle gbufferMaterial,
    const std::shared_ptr<GpuTextureView>& gbufferMaterialSrv, RGTextureHandle gbufferVelocity,
    const std::shared_ptr<GpuTextureView>& gbufferVelocitySrv, RGTextureHandle hiz, ScreenSpaceDebugMode debugMode) {
    m_EffectsOutputSrv = hdrSrv;
    m_SSGIDebugOutputSrv.reset();
    m_SSRDebugOutputSrv.reset();
    if (!m_PostSettings.ssgiEnabled && !m_PostSettings.ssrEnabled)
        return hdr;
    const auto import = [&](const char* name, const ComputeTexture& texture, bool initialized) {
        const RHIResourceState initial = initialized ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
        return graph.ImportTexture(name, texture.texture, texture.rtv ? texture.rtv : texture.srv, initial,
                                   RHIResourceState::ShaderResource);
    };
    const uint32_t readHistory = m_HistoryPing;
    const uint32_t writeHistory = 1u - readHistory;
    m_FrameDepthHistoryRead =
        import("PreviousDepthHistory", m_DepthHistory[readHistory], m_GeometryHistoryInShaderState);
    m_FrameNormalHistoryRead =
        import("PreviousNormalHistory", m_NormalHistory[readHistory], m_GeometryHistoryInShaderState);
    RGTextureHandle ssgiTrace, ssgiHistoryRead, ssgiHistoryWrite, ssgiFilter0, ssgiFilter1;
    if (m_PostSettings.ssgiEnabled) {
        ssgiTrace = import("SSGITrace", m_SSGITrace, m_SSGIResourcesInShaderState);
        ssgiHistoryRead = import("SSGIHistoryRead", m_SSGIHistory[readHistory], m_SSGIResourcesInShaderState);
        ssgiHistoryWrite = import("SSGIHistoryWrite", m_SSGIHistory[writeHistory], m_SSGIResourcesInShaderState);
        ssgiFilter0 = import("SSGIFilter0", m_SSGIFilter[0], m_SSGIResourcesInShaderState);
        ssgiFilter1 = import("SSGIFilter1", m_SSGIFilter[1], m_SSGIResourcesInShaderState);
    }
    RGTextureHandle ssrTrace, ssrHistoryRead, ssrHistoryWrite, ssrFilter0, ssrFilter1;
    if (m_PostSettings.ssrEnabled) {
        ssrTrace = import("SSRTrace", m_SSRTrace, m_SSRResourcesInShaderState);
        ssrHistoryRead = import("SSRHistoryRead", m_SSRHistory[readHistory], m_SSRResourcesInShaderState);
        ssrHistoryWrite = import("SSRHistoryWrite", m_SSRHistory[writeHistory], m_SSRResourcesInShaderState);
        ssrFilter0 = import("SSRFilter0", m_SSRFilter[0], m_SSRResourcesInShaderState);
        ssrFilter1 = import("SSRFilter1", m_SSRFilter[1], m_SSRResourcesInShaderState);
    }
    const auto effects = import("ModernEffectsHDR", m_EffectsHdr, m_EffectsResourceInShaderState);
    RGTextureHandle ssgiFinal;
    RGTextureHandle ssrFinal;
    std::shared_ptr<GpuTextureView> ssgiFinalSrv;
    std::shared_ptr<GpuTextureView> ssrFinalSrv;

    ScreenSpaceConstants ssgiTemporalConstants = m_ScreenSpaceConstants;
    ssgiTemporalConstants.effectMode = 1u;
    ScreenSpaceConstants ssrTemporalConstants = m_ScreenSpaceConstants;
    ssrTemporalConstants.effectMode = 2u;
    if (m_PostSettings.ssgiEnabled) {
        ScreenSpaceConstants ssgiTraceConstants = m_ScreenSpaceConstants;
        ssgiTraceConstants.effectMode = 1u;
        graph.AddComputePass(
            "SSGITraceHalfResolution",
            [sceneDepth, gbufferNormal, hdr, hiz, ssgiTrace](RenderGraphBuilder& builder) {
                builder.ReadTexture(sceneDepth);
                builder.ReadTexture(gbufferNormal);
                builder.ReadTexture(hdr);
                builder.ReadTexture(hiz);
                builder.ReadWriteUAV(ssgiTrace);
            },
            [this, sceneDepthSrv, gbufferNormalSrv, hdrSrv, ssgiTraceConstants](GpuCommandList& commands,
                                                                                const RenderGraphResources&) {
                commands.SetComputePipeline(m_SSGITracePipeline.get());
                auto bindings = AcquireBindGroup(m_SSGITraceShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ScreenSpaceConstants", &ssgiTraceConstants, sizeof(ssgiTraceConstants));
                bindings->SetTexture("g_Depth", sceneDepthSrv);
                bindings->SetTexture("g_Normal", gbufferNormalSrv);
                bindings->SetTexture("g_HdrInput", hdrSrv);
                bindings->SetTexture("g_HiZ", m_HiZSrv);
                bindings->SetStorageTexture("g_Output", m_SSGITrace.uav);
                if (!BindModernPass(commands, "SSGITraceHalfResolution", bindings))
                    return;
                commands.Dispatch((ssgiTraceConstants.effectSize[0] + 7u) / 8u,
                                  (ssgiTraceConstants.effectSize[1] + 7u) / 8u, 1);
            });

        graph.AddComputePass(
            "SSGITemporalDenoise",
            [this, ssgiTrace, ssgiHistoryRead, ssgiHistoryWrite, sceneDepth, gbufferNormal,
             gbufferVelocity](RenderGraphBuilder& builder) {
                builder.ReadTexture(ssgiTrace);
                builder.ReadTexture(ssgiHistoryRead);
                builder.ReadTexture(sceneDepth);
                builder.ReadTexture(gbufferNormal);
                builder.ReadTexture(gbufferVelocity);
                builder.ReadTexture(m_FrameDepthHistoryRead);
                builder.ReadTexture(m_FrameNormalHistoryRead);
                builder.ReadWriteUAV(ssgiHistoryWrite);
            },
            [this, sceneDepthSrv, gbufferNormalSrv, gbufferVelocitySrv, readHistory, writeHistory,
             ssgiTemporalConstants](GpuCommandList& commands, const RenderGraphResources&) {
                commands.SetComputePipeline(m_TemporalPipeline.get());
                auto bindings = AcquireBindGroup(m_TemporalShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ScreenSpaceConstants", &ssgiTemporalConstants, sizeof(ssgiTemporalConstants));
                bindings->SetTexture("g_Current", m_SSGITrace.srv);
                bindings->SetTexture("g_History", m_SSGIHistory[readHistory].srv);
                bindings->SetTexture("g_Depth", sceneDepthSrv);
                bindings->SetTexture("g_Normal", gbufferNormalSrv);
                bindings->SetTexture("g_Velocity", gbufferVelocitySrv);
                bindings->SetTexture("g_PreviousDepth", m_DepthHistory[readHistory].srv);
                bindings->SetTexture("g_PreviousNormal", m_NormalHistory[readHistory].srv);
                bindings->SetSampler("g_LinearSampler", m_LinearClampSampler);
                bindings->SetSampler("g_PointSampler", m_PointClampSampler);
                bindings->SetStorageTexture("g_Output", m_SSGIHistory[writeHistory].uav);
                if (!BindModernPass(commands, "SSGITemporalDenoise", bindings))
                    return;
                commands.Dispatch((ssgiTemporalConstants.effectSize[0] + 7u) / 8u,
                                  (ssgiTemporalConstants.effectSize[1] + 7u) / 8u, 1);
            });

        RGTextureHandle ssgiFilterHandles[2] = {ssgiFilter0, ssgiFilter1};
        RGTextureHandle ssgiInput = ssgiHistoryWrite;
        std::shared_ptr<GpuTextureView> ssgiInputSrv = m_SSGIHistory[writeHistory].srv;
        const uint32_t ssgiFilterRounds = m_QualityProfile == QualityProfile::Console
                                              ? (std::min)(m_PostSettings.ssgiFilterRounds, 2u)
                                              : m_PostSettings.ssgiFilterRounds;
        for (uint32_t pass = 0; pass < ssgiFilterRounds * 2u; ++pass) {
            const uint32_t outputIndex = pass & 1u;
            const RGTextureHandle outputHandle = ssgiFilterHandles[outputIndex];
            const std::shared_ptr<GpuTextureView> outputUav = m_SSGIFilter[outputIndex].uav;
            ScreenSpaceConstants filterConstants = m_ScreenSpaceConstants;
            filterConstants.filterStep = pass / 2u;
            filterConstants.effectMode = pass & 1u;
            graph.AddComputePass(
                "SSGIAtrous" + std::to_string(pass),
                [input = ssgiInput, output = outputHandle, sceneDepth, gbufferNormal,
                 gbufferMaterial](RenderGraphBuilder& builder) {
                    builder.ReadTexture(input);
                    builder.ReadTexture(sceneDepth);
                    builder.ReadTexture(gbufferNormal);
                    builder.ReadTexture(gbufferMaterial);
                    builder.ReadWriteUAV(output);
                },
                [this, input = ssgiInputSrv, output = outputUav, sceneDepthSrv, gbufferNormalSrv, gbufferMaterialSrv,
                 filterConstants](GpuCommandList& commands, const RenderGraphResources&) {
                    commands.SetComputePipeline(m_AtrousPipeline.get());
                    auto bindings = AcquireBindGroup(m_AtrousShader);
                    if (!bindings)
                        return;
                    bindings->SetConstants("ScreenSpaceConstants", &filterConstants, sizeof(filterConstants));
                    bindings->SetTexture("g_Current", input);
                    bindings->SetTexture("g_Depth", sceneDepthSrv);
                    bindings->SetTexture("g_Normal", gbufferNormalSrv);
                    bindings->SetTexture("g_Material", gbufferMaterialSrv);
                    bindings->SetStorageTexture("g_Output", output);
                    if (!BindModernPass(commands, "SSGIAtrous", bindings))
                        return;
                    commands.Dispatch((filterConstants.effectSize[0] + 7u) / 8u,
                                      (filterConstants.effectSize[1] + 7u) / 8u, 1);
                });
            ssgiInput = outputHandle;
            ssgiInputSrv = m_SSGIFilter[outputIndex].srv;
        }
        ssgiFinal = ssgiInput;
        ssgiFinalSrv = ssgiInputSrv;
    }

    if (m_PostSettings.ssrEnabled) {
        ScreenSpaceConstants ssrTraceConstants = m_ScreenSpaceConstants;
        ssrTraceConstants.effectMode = 1u;
        graph.AddComputePass(
            "SSRTraceHalfResolution",
            [sceneDepth, gbufferNormal, gbufferMaterial, hdr, hiz, ssrTrace](RenderGraphBuilder& builder) {
                builder.ReadTexture(sceneDepth);
                builder.ReadTexture(gbufferNormal);
                builder.ReadTexture(gbufferMaterial);
                builder.ReadTexture(hdr);
                builder.ReadTexture(hiz);
                builder.ReadWriteUAV(ssrTrace);
            },
            [this, sceneDepthSrv, gbufferNormalSrv, gbufferMaterialSrv, hdrSrv,
             ssrTraceConstants](GpuCommandList& commands, const RenderGraphResources&) {
                commands.SetComputePipeline(m_SSRTracePipeline.get());
                auto bindings = AcquireBindGroup(m_SSRTraceShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ScreenSpaceConstants", &ssrTraceConstants, sizeof(ssrTraceConstants));
                bindings->SetTexture("g_Depth", sceneDepthSrv);
                bindings->SetTexture("g_Normal", gbufferNormalSrv);
                bindings->SetTexture("g_Material", gbufferMaterialSrv);
                bindings->SetTexture("g_HdrInput", hdrSrv);
                bindings->SetTexture("g_HiZ", m_HiZSrv);
                bindings->SetStorageTexture("g_Output", m_SSRTrace.uav);
                if (!BindModernPass(commands, "SSRTraceHalfResolution", bindings))
                    return;
                commands.Dispatch((ssrTraceConstants.effectSize[0] + 7u) / 8u,
                                  (ssrTraceConstants.effectSize[1] + 7u) / 8u, 1);
            });

        graph.AddComputePass(
            "SSRTemporalDenoise",
            [this, ssrTrace, ssrHistoryRead, ssrHistoryWrite, sceneDepth, gbufferNormal,
             gbufferVelocity](RenderGraphBuilder& builder) {
                builder.ReadTexture(ssrTrace);
                builder.ReadTexture(ssrHistoryRead);
                builder.ReadTexture(sceneDepth);
                builder.ReadTexture(gbufferNormal);
                builder.ReadTexture(gbufferVelocity);
                builder.ReadTexture(m_FrameDepthHistoryRead);
                builder.ReadTexture(m_FrameNormalHistoryRead);
                builder.ReadWriteUAV(ssrHistoryWrite);
            },
            [this, sceneDepthSrv, gbufferNormalSrv, gbufferVelocitySrv, readHistory, writeHistory,
             ssrTemporalConstants](GpuCommandList& commands, const RenderGraphResources&) {
                commands.SetComputePipeline(m_TemporalPipeline.get());
                auto bindings = AcquireBindGroup(m_TemporalShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ScreenSpaceConstants", &ssrTemporalConstants, sizeof(ssrTemporalConstants));
                bindings->SetTexture("g_Current", m_SSRTrace.srv);
                bindings->SetTexture("g_History", m_SSRHistory[readHistory].srv);
                bindings->SetTexture("g_Depth", sceneDepthSrv);
                bindings->SetTexture("g_Normal", gbufferNormalSrv);
                bindings->SetTexture("g_Velocity", gbufferVelocitySrv);
                bindings->SetTexture("g_PreviousDepth", m_DepthHistory[readHistory].srv);
                bindings->SetTexture("g_PreviousNormal", m_NormalHistory[readHistory].srv);
                bindings->SetSampler("g_LinearSampler", m_LinearClampSampler);
                bindings->SetSampler("g_PointSampler", m_PointClampSampler);
                bindings->SetStorageTexture("g_Output", m_SSRHistory[writeHistory].uav);
                if (!BindModernPass(commands, "SSRTemporalDenoise", bindings))
                    return;
                commands.Dispatch((ssrTemporalConstants.effectSize[0] + 7u) / 8u,
                                  (ssrTemporalConstants.effectSize[1] + 7u) / 8u, 1);
            });

        RGTextureHandle ssrFilterHandles[2] = {ssrFilter0, ssrFilter1};
        RGTextureHandle ssrInput = ssrHistoryWrite;
        std::shared_ptr<GpuTextureView> ssrInputSrv = m_SSRHistory[writeHistory].srv;
        const uint32_t ssrFilterRounds = m_QualityProfile == QualityProfile::Console
                                             ? (std::min)(m_PostSettings.ssrFilterRounds, 1u)
                                             : m_PostSettings.ssrFilterRounds;
        for (uint32_t pass = 0; pass < ssrFilterRounds * 2u; ++pass) {
            const uint32_t outputIndex = pass & 1u;
            const RGTextureHandle outputHandle = ssrFilterHandles[outputIndex];
            const std::shared_ptr<GpuTextureView> outputUav = m_SSRFilter[outputIndex].uav;
            ScreenSpaceConstants filterConstants = m_ScreenSpaceConstants;
            filterConstants.filterStep = pass / 2u;
            filterConstants.effectMode = (pass & 1u) | 2u;
            graph.AddComputePass(
                "SSRSpatialFilter" + std::to_string(pass),
                [input = ssrInput, output = outputHandle, sceneDepth, gbufferNormal,
                 gbufferMaterial](RenderGraphBuilder& builder) {
                    builder.ReadTexture(input);
                    builder.ReadTexture(sceneDepth);
                    builder.ReadTexture(gbufferNormal);
                    builder.ReadTexture(gbufferMaterial);
                    builder.ReadWriteUAV(output);
                },
                [this, input = ssrInputSrv, output = outputUav, sceneDepthSrv, gbufferNormalSrv, gbufferMaterialSrv,
                 filterConstants](GpuCommandList& commands, const RenderGraphResources&) {
                    commands.SetComputePipeline(m_AtrousPipeline.get());
                    auto bindings = AcquireBindGroup(m_AtrousShader);
                    if (!bindings)
                        return;
                    bindings->SetConstants("ScreenSpaceConstants", &filterConstants, sizeof(filterConstants));
                    bindings->SetTexture("g_Current", input);
                    bindings->SetTexture("g_Depth", sceneDepthSrv);
                    bindings->SetTexture("g_Normal", gbufferNormalSrv);
                    bindings->SetTexture("g_Material", gbufferMaterialSrv);
                    bindings->SetStorageTexture("g_Output", output);
                    if (!BindModernPass(commands, "SSRBilateral", bindings))
                        return;
                    commands.Dispatch((filterConstants.effectSize[0] + 7u) / 8u,
                                      (filterConstants.effectSize[1] + 7u) / 8u, 1);
                });
            ssrInput = outputHandle;
            ssrInputSrv = m_SSRFilter[outputIndex].srv;
        }
        ssrFinal = ssrInput;
        ssrFinalSrv = ssrInputSrv;
    }

    ScreenSpaceConstants compositeConstants = m_ScreenSpaceConstants;
    compositeConstants.effectMode = (m_PostSettings.ssgiEnabled ? 1u : 0u) | (m_PostSettings.ssrEnabled ? 2u : 0u);
    graph.AddComputePass(
        "CompositeSSGIAndSSR",
        [this, hdr, sceneDepth, gbufferAlbedo, gbufferNormal, gbufferMaterial, ssgiFinal, ssrFinal, effects,
         ssgiEnabled = m_PostSettings.ssgiEnabled,
         ssrEnabled = m_PostSettings.ssrEnabled](RenderGraphBuilder& builder) {
            builder.ReadTexture(hdr);
            builder.ReadTexture(sceneDepth);
            builder.ReadTexture(gbufferAlbedo);
            builder.ReadTexture(gbufferNormal);
            builder.ReadTexture(gbufferMaterial);
            if (m_FrameEnvironment.IsValid())
                builder.ReadTexture(m_FrameEnvironment);
            if (ssgiEnabled)
                builder.ReadTexture(ssgiFinal);
            if (ssrEnabled)
                builder.ReadTexture(ssrFinal);
            builder.ReadWriteUAV(effects);
        },
        [this, hdrSrv, sceneDepthSrv, gbufferAlbedoSrv, gbufferNormalSrv, gbufferMaterialSrv, ssgiFinalSrv, ssrFinalSrv,
         compositeConstants](GpuCommandList& commands, const RenderGraphResources&) {
            commands.SetComputePipeline(m_EffectsCompositePipeline.get());
            auto bindings = AcquireBindGroup(m_EffectsCompositeShader);
            if (!bindings)
                return;
            bindings->SetConstants("ScreenSpaceConstants", &compositeConstants, sizeof(compositeConstants));
            bindings->SetTexture("g_HdrInput", hdrSrv);
            bindings->SetTexture("g_Depth", sceneDepthSrv);
            bindings->SetTexture("g_Normal", gbufferNormalSrv);
            bindings->SetTexture("g_Current", gbufferAlbedoSrv);
            bindings->SetTexture("g_Material", gbufferMaterialSrv);
            bindings->SetTexture("g_SSGI", m_PostSettings.ssgiEnabled ? ssgiFinalSrv : hdrSrv);
            bindings->SetTexture("g_SSR", m_PostSettings.ssrEnabled ? ssrFinalSrv : hdrSrv);
            bindings->SetTexture("g_Environment", m_EnvironmentCubeSrv);
            bindings->SetSampler("g_LinearSampler", m_LinearClampSampler);
            bindings->SetStorageTexture("g_Output", m_EffectsHdr.uav);
            if (!BindModernPass(commands, "ModernEffectsComposite", bindings))
                return;
            commands.Dispatch((m_Width + 7u) / 8u, (m_Height + 7u) / 8u, 1);
        });
    m_EffectsOutputSrv = m_EffectsHdr.srv;

    const bool debugSSGI = debugMode == ScreenSpaceDebugMode::SSGI && m_PostSettings.ssgiEnabled;
    const bool debugSSR = debugMode == ScreenSpaceDebugMode::SSRConfidence && m_PostSettings.ssrEnabled;
    if (debugSSGI || debugSSR) {
        if (!m_FrameScreenSpaceDebug.IsValid()) {
            const RHIResourceState debugInitial =
                m_ScreenSpaceDebugInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
            m_FrameScreenSpaceDebug =
                graph.ImportTexture("ModernScreenSpaceDebug", m_ScreenSpaceDebug.texture, m_ScreenSpaceDebug.srv,
                                    debugInitial, RHIResourceState::ShaderResource);
        }
        const RGTextureHandle debugOutput = m_FrameScreenSpaceDebug;
        ScreenSpaceConstants debugConstants = m_ScreenSpaceConstants;
        debugConstants.effectMode = debugSSGI ? 4u : 8u;
        graph.AddComputePass(
            debugSSGI ? "VisualizeSSGI" : "VisualizeSSRConfidence",
            [this, hdr, sceneDepth, gbufferAlbedo, gbufferNormal, gbufferMaterial, ssgiFinal, ssrFinal, debugOutput,
             debugSSGI, debugSSR](RenderGraphBuilder& builder) {
                builder.ReadTexture(hdr);
                builder.ReadTexture(sceneDepth);
                builder.ReadTexture(gbufferAlbedo);
                builder.ReadTexture(gbufferNormal);
                builder.ReadTexture(gbufferMaterial);
                if (m_FrameEnvironment.IsValid())
                    builder.ReadTexture(m_FrameEnvironment);
                if (debugSSGI)
                    builder.ReadTexture(ssgiFinal);
                if (debugSSR)
                    builder.ReadTexture(ssrFinal);
                builder.ReadWriteUAV(debugOutput);
            },
            [this, hdrSrv, sceneDepthSrv, gbufferAlbedoSrv, gbufferNormalSrv, gbufferMaterialSrv, ssgiFinalSrv,
             ssrFinalSrv, debugConstants, debugSSGI](GpuCommandList& commands, const RenderGraphResources&) {
                commands.SetComputePipeline(m_EffectsCompositePipeline.get());
                auto bindings = AcquireBindGroup(m_EffectsCompositeShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ScreenSpaceConstants", &debugConstants, sizeof(debugConstants));
                bindings->SetTexture("g_HdrInput", hdrSrv);
                bindings->SetTexture("g_Depth", sceneDepthSrv);
                bindings->SetTexture("g_Normal", gbufferNormalSrv);
                bindings->SetTexture("g_Current", gbufferAlbedoSrv);
                bindings->SetTexture("g_Material", gbufferMaterialSrv);
                bindings->SetTexture("g_SSGI", debugSSGI ? ssgiFinalSrv : hdrSrv);
                bindings->SetTexture("g_SSR", debugSSGI ? hdrSrv : ssrFinalSrv);
                bindings->SetTexture("g_Environment", m_EnvironmentCubeSrv);
                bindings->SetSampler("g_LinearSampler", m_LinearClampSampler);
                bindings->SetStorageTexture("g_Output", m_ScreenSpaceDebug.uav);
                if (!BindModernPass(commands, debugSSGI ? "VisualizeSSGI" : "VisualizeSSRConfidence", bindings))
                    return;
                commands.Dispatch((m_Width + 7u) / 8u, (m_Height + 7u) / 8u, 1);
            });
        if (debugSSGI)
            m_SSGIDebugOutputSrv = m_ScreenSpaceDebug.srv;
        else
            m_SSRDebugOutputSrv = m_ScreenSpaceDebug.srv;
    }
    return effects;
}

RGTextureHandle ModernDeferredPipeline::AddTemporalPostProcess(
    RenderGraph& graph, RGTextureHandle input, const std::shared_ptr<GpuTextureView>& inputSrv,
    RGTextureHandle sceneDepth, const std::shared_ptr<GpuTextureView>& sceneDepthSrv, RGTextureHandle gbufferNormal,
    const std::shared_ptr<GpuTextureView>& gbufferNormalSrv, RGTextureHandle gbufferVelocity,
    const std::shared_ptr<GpuTextureView>& gbufferVelocitySrv, ScreenSpaceDebugMode debugMode) {
    m_TAAHistoryAgeDebugOutputSrv.reset();
    m_TAARejectReasonDebugOutputSrv.reset();
    const uint32_t readHistory = m_HistoryPing;
    const uint32_t writeHistory = 1u - readHistory;
    const RHIResourceState geometryInitial =
        m_GeometryHistoryInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
    if (!m_FrameDepthHistoryRead.IsValid()) {
        m_FrameDepthHistoryRead =
            graph.ImportTexture("PreviousDepthHistory", m_DepthHistory[readHistory].texture,
                                m_DepthHistory[readHistory].srv, geometryInitial, RHIResourceState::ShaderResource);
        m_FrameNormalHistoryRead =
            graph.ImportTexture("PreviousNormalHistory", m_NormalHistory[readHistory].texture,
                                m_NormalHistory[readHistory].srv, geometryInitial, RHIResourceState::ShaderResource);
    }
    m_FrameDepthHistoryWrite =
        graph.ImportTexture("CurrentDepthHistory", m_DepthHistory[writeHistory].texture,
                            m_DepthHistory[writeHistory].srv, geometryInitial, RHIResourceState::ShaderResource);
    m_FrameNormalHistoryWrite =
        graph.ImportTexture("CurrentNormalHistory", m_NormalHistory[writeHistory].texture,
                            m_NormalHistory[writeHistory].srv, geometryInitial, RHIResourceState::ShaderResource);
    const RHIResourceState postInitial =
        m_PostColorInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
    const auto post = graph.ImportTexture("ModernPostColor", m_PostColor.texture, m_PostColor.rtv, postInitial,
                                          RHIResourceState::ShaderResource);
    RGTextureHandle toneInput = input;
    std::shared_ptr<GpuTextureView> toneInputSrv = inputSrv;
    if (m_PostSettings.taaEnabled) {
        const RHIResourceState taaInitial =
            m_TAAResourcesInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
        const auto taaRead =
            graph.ImportTexture("TAAHistoryRead", m_TAAHistory[readHistory].texture, m_TAAHistory[readHistory].srv,
                                taaInitial, RHIResourceState::ShaderResource);
        const auto taaWrite =
            graph.ImportTexture("TAAHistoryWrite", m_TAAHistory[writeHistory].texture, m_TAAHistory[writeHistory].srv,
                                taaInitial, RHIResourceState::ShaderResource);
        if (!m_FrameScreenSpaceDebug.IsValid()) {
            const RHIResourceState debugInitial =
                m_ScreenSpaceDebugInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
            m_FrameScreenSpaceDebug =
                graph.ImportTexture("ModernScreenSpaceDebug", m_ScreenSpaceDebug.texture, m_ScreenSpaceDebug.srv,
                                    debugInitial, RHIResourceState::ShaderResource);
        }
        ScreenSpaceConstants taaConstants = m_ScreenSpaceConstants;
        taaConstants.effectSize[0] = m_Width;
        taaConstants.effectSize[1] = m_Height;
        if (debugMode == ScreenSpaceDebugMode::TAAHistoryAge)
            taaConstants.effectMode = 16u;
        else if (debugMode == ScreenSpaceDebugMode::TAARejectReason)
            taaConstants.effectMode = 32u;
        graph.AddComputePass(
            "TemporalAntiAliasing",
            [this, input, taaRead, taaWrite, sceneDepth, gbufferNormal, gbufferVelocity](RenderGraphBuilder& builder) {
                builder.ReadTexture(input);
                builder.ReadTexture(taaRead);
                builder.ReadTexture(sceneDepth);
                builder.ReadTexture(gbufferNormal);
                builder.ReadTexture(gbufferVelocity);
                builder.ReadTexture(m_FrameDepthHistoryRead);
                builder.ReadTexture(m_FrameNormalHistoryRead);
                builder.ReadWriteUAV(taaWrite);
                builder.ReadWriteUAV(m_FrameScreenSpaceDebug);
            },
            [this, inputSrv, sceneDepthSrv, gbufferNormalSrv, gbufferVelocitySrv, readHistory, writeHistory,
             taaConstants](GpuCommandList& commands, const RenderGraphResources&) {
                commands.SetComputePipeline(m_TAAPipeline.get());
                auto bindings = AcquireBindGroup(m_TAAShader);
                if (!bindings)
                    return;
                bindings->SetConstants("ScreenSpaceConstants", &taaConstants, sizeof(taaConstants));
                bindings->SetTexture("g_Current", inputSrv);
                bindings->SetTexture("g_History", m_TAAHistory[readHistory].srv);
                bindings->SetTexture("g_Depth", sceneDepthSrv);
                bindings->SetTexture("g_Normal", gbufferNormalSrv);
                bindings->SetTexture("g_Velocity", gbufferVelocitySrv);
                bindings->SetTexture("g_PreviousDepth", m_DepthHistory[readHistory].srv);
                bindings->SetTexture("g_PreviousNormal", m_NormalHistory[readHistory].srv);
                bindings->SetSampler("g_LinearSampler", m_LinearClampSampler);
                bindings->SetSampler("g_PointSampler", m_PointClampSampler);
                bindings->SetStorageTexture("g_Output", m_TAAHistory[writeHistory].uav);
                bindings->SetStorageTexture("g_TAADebugOutput", m_ScreenSpaceDebug.uav);
                if (!BindModernPass(commands, "ModernTAA", bindings))
                    return;
                commands.Dispatch((m_Width + 7u) / 8u, (m_Height + 7u) / 8u, 1);
            });
        toneInput = taaWrite;
        toneInputSrv = m_TAAHistory[writeHistory].srv;
        if (debugMode == ScreenSpaceDebugMode::TAAHistoryAge)
            m_TAAHistoryAgeDebugOutputSrv = m_ScreenSpaceDebug.srv;
        else if (debugMode == ScreenSpaceDebugMode::TAARejectReason)
            m_TAARejectReasonDebugOutputSrv = m_ScreenSpaceDebug.srv;
    }
    const ScreenSpaceConstants toneConstants = m_ScreenSpaceConstants;
    graph.AddComputePass(
        "ComputeBloomAndACESToneMap",
        [this, toneInput, post, sceneDepth, gbufferNormal](RenderGraphBuilder& builder) {
            builder.ReadTexture(toneInput);
            builder.ReadTexture(sceneDepth);
            builder.ReadTexture(gbufferNormal);
            builder.ReadWriteUAV(post);
            builder.ReadWriteUAV(m_FrameDepthHistoryWrite);
            builder.ReadWriteUAV(m_FrameNormalHistoryWrite);
        },
        [this, toneInputSrv, sceneDepthSrv, gbufferNormalSrv, writeHistory,
         toneConstants](GpuCommandList& commands, const RenderGraphResources&) {
            commands.SetComputePipeline(m_BloomTonePipeline.get());
            auto bindings = AcquireBindGroup(m_BloomToneShader);
            if (!bindings)
                return;
            bindings->SetConstants("ScreenSpaceConstants", &toneConstants, sizeof(toneConstants));
            bindings->SetTexture("g_Current", toneInputSrv);
            bindings->SetTexture("g_Depth", sceneDepthSrv);
            bindings->SetTexture("g_Normal", gbufferNormalSrv);
            bindings->SetStorageTexture("g_Output", m_PostColor.uav);
            bindings->SetStorageTexture("g_DepthHistoryOutput", m_DepthHistory[writeHistory].uav);
            bindings->SetStorageTexture("g_NormalHistoryOutput", m_NormalHistory[writeHistory].uav);
            if (!BindModernPass(commands, "ModernBloomTone", bindings))
                return;
            commands.Dispatch((m_Width + 7u) / 8u, (m_Height + 7u) / 8u, 1);
        });
    m_PendingHistoryPing = writeHistory;
    m_TemporalFramePending = true;
    return post;
}
