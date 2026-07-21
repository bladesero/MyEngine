#include "Renderer/EnvironmentPass.h"

#include "Core/Logger.h"
#include "Renderer/EngineShaderCatalog.h"
#include "Renderer/ShaderManager.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {
struct AtmosphereFaceConstants {
    float faceInfo[4];
    float sunDirection[4];
    float skyTint[4];
    float horizonTint[4];
    float groundTint[4];
};

struct EnvironmentMipmapConstants {
    float faceInfo[4];
};
} // namespace

EnvironmentPass::EnvironmentPass(IRHIDevice* device, IRHIReadbackService* readbackService)
    : RenderPass(device, readbackService) {
    m_SH2[0][0] = 0.08f;
    m_SH2[0][1] = 0.10f;
    m_SH2[0][2] = 0.14f;
}

void EnvironmentPass::Resize(uint32_t, uint32_t) {
}

Vec3 EnvironmentPass::DefaultSunDirection() {
    return Vec3{0.35f, 0.72f, 0.25f}.Normalized();
}

void EnvironmentPass::MarkDirty() {
    m_Generated = false;
    m_EnvironmentInShaderState = false;
    m_SHBufferInShaderState = false;
    m_Readback.reset();
}

void EnvironmentPass::SetSunDirection(const Vec3& direction) {
    Vec3 normalized = direction.LengthSq() > 1e-8f ? direction.Normalized() : DefaultSunDirection();
    const Vec3 referenceDirection = m_Generated ? m_GeneratedSunDirection : m_SunDirection;
    if ((normalized - referenceDirection).LengthSq() < 1e-6f) {
        m_SunDirection = normalized;
        return;
    }
    m_SunDirection = normalized;
    if (m_Generated) {
        MarkDirty();
    }
}

void EnvironmentPass::SetEnvironmentSettings(const SceneEnvironmentData& environment) {
    const bool changed = (environment.skyTint - m_SkyTint).LengthSq() >= 1e-6f ||
                         (environment.horizonTint - m_HorizonTint).LengthSq() >= 1e-6f ||
                         (environment.groundTint - m_GroundTint).LengthSq() >= 1e-6f;
    m_SkyTint = environment.skyTint;
    m_HorizonTint = environment.horizonTint;
    m_GroundTint = environment.groundTint;
    if (changed && m_Generated)
        MarkDirty();
}

bool EnvironmentPass::EnsureResources() {
    auto* device = Device();
    if (!device)
        return false;
    if (device->GetBackend() == RHIBackend::Unknown)
        return false;
    auto resetResources = [&]() {
        m_Environment.reset();
        m_EnvironmentSrv.reset();
        for (auto& view : m_MipSrvs)
            view.reset();
        for (auto& mip : m_FaceRtvs) {
            for (auto& view : mip)
                view.reset();
        }
        m_LinearClamp.reset();
        m_AtmospherePipeline.reset();
        m_MipmapPipeline.reset();
        m_SHPipeline.reset();
        m_SHBuffer.reset();
        m_SHUav.reset();
        m_SH2Srv.reset();
        m_Readback.reset();
        MarkDirty();
    };
    auto resourcesComplete = [&]() {
        bool valid = m_Environment && m_EnvironmentSrv && m_LinearClamp && m_AtmospherePipeline && m_MipmapPipeline &&
                     m_SHPipeline && m_SHBuffer && m_SHUav && m_SH2Srv;
        for (const auto& view : m_MipSrvs)
            valid = valid && view != nullptr;
        for (const auto& mip : m_FaceRtvs) {
            for (const auto& view : mip)
                valid = valid && view != nullptr;
        }
        return valid;
    };
    if (m_Environment) {
        if (!resourcesComplete()) {
            resetResources();
        } else {
            if ((m_AtmosphereHandle && m_AtmosphereHandle->version != m_AtmosphereVersion) ||
                (m_MipmapHandle && m_MipmapHandle->version != m_MipmapVersion) ||
                (m_SHHandle && m_SHHandle->version != m_SHVersion)) {
                m_AtmosphereShader = m_AtmosphereHandle->shader;
                m_MipmapShader = m_MipmapHandle->shader;
                m_SHShader = m_SHHandle->shader;
                GraphicsPipelineDesc desc;
                desc.colorFormats = {RHIFormat::RGBA16Float};
                desc.depthStencil.depthTestEnable = false;
                desc.depthStencil.depthWriteEnable = false;
                desc.shader = m_AtmosphereShader;
                m_AtmospherePipeline = device->CreateGraphicsPipeline(desc);
                desc.shader = m_MipmapShader;
                m_MipmapPipeline = device->CreateGraphicsPipeline(desc);
                m_AtmosphereVersion = m_AtmosphereHandle->version;
                m_MipmapVersion = m_MipmapHandle->version;
                m_SHVersion = m_SHHandle->version;
                MarkDirty();
            }
            return resourcesComplete();
        }
    }
    RHITextureDesc textureDesc;
    textureDesc.width = kCubeSize;
    textureDesc.height = kCubeSize;
    textureDesc.mipLevels = kCubeMipLevels;
    textureDesc.arrayLayers = 6;
    textureDesc.format = RHIFormat::RGBA16Float;
    textureDesc.usage = RHIResourceUsage::RenderTarget | RHIResourceUsage::ShaderResource;
    textureDesc.cube = true;
    textureDesc.debugName = "EnvironmentCube";
    m_Environment = device->CreateTexture(textureDesc);
    if (!m_Environment)
        return false;

    RHITextureViewDesc srvDesc;
    srvDesc.mipCount = kCubeMipLevels;
    srvDesc.layerCount = 6;
    srvDesc.usage = RHIResourceUsage::ShaderResource;
    m_EnvironmentSrv = device->CreateTextureView(m_Environment, srvDesc);
    if (!m_EnvironmentSrv) {
        resetResources();
        return false;
    }
    for (uint32_t mip = 0; mip < kCubeMipLevels; ++mip) {
        RHITextureViewDesc mipSrvDesc;
        mipSrvDesc.firstMip = mip;
        mipSrvDesc.mipCount = 1;
        mipSrvDesc.layerCount = 6;
        mipSrvDesc.usage = RHIResourceUsage::ShaderResource;
        m_MipSrvs[mip] = device->CreateTextureView(m_Environment, mipSrvDesc);
        if (!m_MipSrvs[mip]) {
            resetResources();
            return false;
        }
    }
    for (uint32_t mip = 0; mip < kCubeMipLevels; ++mip) {
        for (uint32_t face = 0; face < 6; ++face) {
            RHITextureViewDesc rtvDesc;
            rtvDesc.firstMip = mip;
            rtvDesc.firstLayer = face;
            rtvDesc.usage = RHIResourceUsage::RenderTarget;
            m_FaceRtvs[mip][face] = device->CreateTextureView(m_Environment, rtvDesc);
            if (!m_FaceRtvs[mip][face]) {
                resetResources();
                return false;
            }
        }
    }
    RHISamplerDesc samplerDesc;
    samplerDesc.filter = RHIFilter::Linear;
    samplerDesc.addressU = samplerDesc.addressV = samplerDesc.addressW = RHIAddressMode::Clamp;
    m_LinearClamp = device->CreateSampler(samplerDesc);
    if (!m_LinearClamp) {
        resetResources();
        return false;
    }

    m_AtmosphereHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kAtmosphereCubemap, nullptr, 0);
    m_MipmapHandle = ShaderManager::Get().GetOrCreate(EngineShaders::kEnvironmentMipmap, nullptr, 0);
    m_SHHandle = ShaderManager::Get().GetOrCreateCompute(EngineShaders::kAtmosphereSH);
    m_AtmosphereShader = m_AtmosphereHandle ? m_AtmosphereHandle->shader : nullptr;
    m_MipmapShader = m_MipmapHandle ? m_MipmapHandle->shader : nullptr;
    m_AtmosphereVersion = m_AtmosphereHandle ? m_AtmosphereHandle->version : 0;
    m_MipmapVersion = m_MipmapHandle ? m_MipmapHandle->version : 0;
    m_SHShader = m_SHHandle ? m_SHHandle->shader : nullptr;
    m_SHVersion = m_SHHandle ? m_SHHandle->version : 0;
    if (!m_AtmosphereShader || !m_MipmapShader || !m_SHShader) {
        resetResources();
        return false;
    }

    GraphicsPipelineDesc graphicsDesc;
    graphicsDesc.colorFormats = {RHIFormat::RGBA16Float};
    graphicsDesc.depthStencil.depthTestEnable = false;
    graphicsDesc.depthStencil.depthWriteEnable = false;
    graphicsDesc.shader = m_AtmosphereShader;
    m_AtmospherePipeline = device->CreateGraphicsPipeline(graphicsDesc);
    graphicsDesc.shader = m_MipmapShader;
    m_MipmapPipeline = device->CreateGraphicsPipeline(graphicsDesc);
    ComputePipelineDesc computeDesc;
    computeDesc.shader = m_SHShader;
    m_SHPipeline = device->CreateComputePipeline(computeDesc);

    RHIBufferDesc bufferDesc;
    bufferDesc.size = sizeof(m_SH2);
    bufferDesc.stride = sizeof(m_SH2[0]);
    bufferDesc.usage =
        RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess | RHIResourceUsage::CopySource;
    bufferDesc.debugName = "EnvironmentSH2";
    m_SHBuffer = device->CreateBuffer(bufferDesc);
    RHIBufferViewDesc uavDesc;
    uavDesc.elementCount = 9;
    uavDesc.usage = RHIResourceUsage::UnorderedAccess;
    m_SHUav = device->CreateBufferView(m_SHBuffer, uavDesc);
    RHIBufferViewDesc srvBufferDesc;
    srvBufferDesc.elementCount = 9;
    srvBufferDesc.usage = RHIResourceUsage::ShaderResource;
    m_SH2Srv = device->CreateBufferView(m_SHBuffer, srvBufferDesc);
    if (!resourcesComplete()) {
        resetResources();
        return false;
    }
    return true;
}

void EnvironmentPass::RenderCubemap(GpuCommandList& commands) {
    for (uint32_t face = 0; face < 6; ++face) {
        RHITextureViewDesc range;
        range.firstLayer = face;
        range.layerCount = 1;
        commands.TransitionTexture(m_Environment.get(), range, RHIResourceState::Undefined,
                                   RHIResourceState::RenderTarget);
        RenderingAttachment color;
        color.view = m_FaceRtvs[0][face].get();
        color.loadOp = RHILoadOp::Clear;
        RenderingInfo rendering{&color, 1, nullptr, kCubeSize, kCubeSize};
        commands.BeginRendering(rendering);
        commands.SetGraphicsPipeline(m_AtmospherePipeline.get());
        auto bindings = Device()->CreateBindGroup(m_AtmosphereShader);
        if (!bindings) {
            Logger::Error("[EnvironmentPass] Failed to create atmosphere bind group");
            commands.EndRendering();
            return;
        }
        AtmosphereFaceConstants constants{
            {static_cast<float>(face), 0, 0, 0},
            {m_SunDirection.x, m_SunDirection.y, m_SunDirection.z, 0.0f},
            {m_SkyTint.x, m_SkyTint.y, m_SkyTint.z, 0.0f},
            {m_HorizonTint.x, m_HorizonTint.y, m_HorizonTint.z, 0.0f},
            {m_GroundTint.x, m_GroundTint.y, m_GroundTint.z, 0.0f},
        };
        bindings->SetConstants("AtmosphereFaceConstants", &constants, sizeof(constants));
        commands.SetBindGroup(0, bindings.get());
        commands.Draw(3);
        commands.EndRendering();
        commands.TransitionTexture(m_Environment.get(), range, RHIResourceState::RenderTarget,
                                   RHIResourceState::ShaderResource);
    }

    for (uint32_t mip = 1; mip < kCubeMipLevels; ++mip) {
        const uint32_t size = (std::max)(1u, kCubeSize >> mip);
        for (uint32_t face = 0; face < 6; ++face) {
            RHITextureViewDesc range;
            range.firstMip = mip;
            range.firstLayer = face;
            commands.TransitionTexture(m_Environment.get(), range, RHIResourceState::Undefined,
                                       RHIResourceState::RenderTarget);
            RenderingAttachment color;
            color.view = m_FaceRtvs[mip][face].get();
            color.loadOp = RHILoadOp::Clear;
            RenderingInfo rendering{&color, 1, nullptr, size, size};
            commands.BeginRendering(rendering);
            commands.SetGraphicsPipeline(m_MipmapPipeline.get());
            auto bindings = Device()->CreateBindGroup(m_MipmapShader);
            if (!bindings) {
                Logger::Error("[EnvironmentPass] Failed to create mipmap bind group");
                commands.EndRendering();
                return;
            }
            EnvironmentMipmapConstants constants{{static_cast<float>(face), static_cast<float>(mip - 1), 0, 0}};
            bindings->SetConstants("EnvironmentMipmapConstants", &constants, sizeof(constants));
            bindings->SetTexture("g_SourceCube", m_MipSrvs[mip - 1]);
            bindings->SetSampler("g_SourceSampler", m_LinearClamp);
            commands.SetBindGroup(0, bindings.get());
            commands.Draw(3);
            commands.EndRendering();
            commands.TransitionTexture(m_Environment.get(), range, RHIResourceState::RenderTarget,
                                       RHIResourceState::ShaderResource);
        }
    }
}

void EnvironmentPass::ProjectSH(GpuCommandList& commands) {
    commands.Transition(m_SHBuffer.get(), RHIResourceState::Undefined, RHIResourceState::UnorderedAccess);
    commands.SetComputePipeline(m_SHPipeline.get());
    auto bindings = Device()->CreateBindGroup(m_SHShader);
    if (!bindings) {
        Logger::Error("[EnvironmentPass] Failed to create SH bind group");
        return;
    }
    bindings->SetTexture("g_EnvironmentCube", m_EnvironmentSrv);
    bindings->SetSampler("g_EnvironmentSampler", m_LinearClamp);
    bindings->SetStorageBuffer("g_SH2Out", m_SHUav);
    std::string error;
    if (!bindings->Validate(&error)) {
        Logger::Error("[EnvironmentPass] Invalid SH bind group: ", error);
        return;
    }
    commands.SetBindGroup(0, bindings.get());
    commands.Dispatch(1, 1, 1);
    commands.UAVBarrier(m_SHBuffer.get());
    commands.Transition(m_SHBuffer.get(), RHIResourceState::UnorderedAccess, RHIResourceState::CopySource);
    m_Readback = ReadbackService() ? ReadbackService()->ReadbackBufferAsync(m_SHBuffer) : nullptr;
    commands.Transition(m_SHBuffer.get(), RHIResourceState::CopySource, RHIResourceState::ShaderResource);
}

void EnvironmentPass::ConsumeReadback() {
    if (!m_Readback || !m_Readback->IsReady())
        return;
    std::vector<uint8_t> bytes;
    if (m_Readback->Read(bytes) && bytes.size() >= sizeof(m_SH2)) {
        std::memcpy(m_SH2, bytes.data(), sizeof(m_SH2));
        bool allZero = true;
        const float* values = &m_SH2[0][0];
        for (size_t i = 0; i < 9 * 4; ++i) {
            if (values[i] != 0.0f) {
                allZero = false;
                break;
            }
        }
        if (allZero && !m_LoggedZeroSHReadback) {
            Logger::Warn("[EnvironmentPass] SH readback returned all zero coefficients");
            m_LoggedZeroSHReadback = true;
        }
    }
    m_Readback.reset();
}

bool EnvironmentPass::PrepareGraphResources() {
    ConsumeReadback();
    return EnsureResources();
}

EnvironmentPass::GraphResources EnvironmentPass::GetGraphResources() const {
    GraphResources out;
    out.environment = m_Environment;
    out.environmentView = m_EnvironmentSrv;
    out.shBuffer = m_SHBuffer;
    out.shBufferView = m_SH2Srv;
    out.environmentInitialState =
        m_EnvironmentInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
    out.shInitialState = m_SHBufferInShaderState ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
    out.generated = m_Generated;
    return out;
}

void EnvironmentPass::ExecuteGraphManaged(GpuCommandList& commands) {
    if (m_Generated)
        return;
    RenderCubemap(commands);
    ProjectSH(commands);
    m_Generated = true;
    m_GeneratedSunDirection = m_SunDirection;
}

void EnvironmentPass::MarkGraphResourcesShaderResource() {
    if (!m_Generated)
        return;
    m_EnvironmentInShaderState = true;
    m_SHBufferInShaderState = true;
}

void EnvironmentPass::Execute(GpuCommandList& commands, const Scene&, const Camera&) {
    ConsumeReadback();
    if (!EnsureResources() || m_Generated)
        return;
    RenderCubemap(commands);
    ProjectSH(commands);
    m_Generated = true;
    m_GeneratedSunDirection = m_SunDirection;
    m_EnvironmentInShaderState = true;
    m_SHBufferInShaderState = true;
}
