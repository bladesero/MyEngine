#pragma once

#include "Renderer/RenderPass.h"
#include "Renderer/RHI/GpuBufferView.h"
#include "Renderer/RHI/GpuReadback.h"

#include <array>
#include <memory>

struct ShaderHandle;

class EnvironmentPass final : public RenderPass {
public:
    explicit EnvironmentPass(IRenderContext* context);

    void Execute(const Scene& scene, const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;

    GpuTexture* GetEnvironmentCubemap() const { return m_Environment.get(); }
    std::shared_ptr<GpuBufferView> GetSH2BufferView() const { return m_SH2Srv; }
    const float* GetSH2Coefficients() const { return &m_SH2[0][0]; }

private:
    bool EnsureResources();
    void RenderCubemap(GpuCommandList& commands);
    void ProjectSH(GpuCommandList& commands);
    void ConsumeReadback();

    static constexpr uint32_t kCubeSize = 64;
    static constexpr uint32_t kCubeMipLevels = 7;
    float m_SH2[9][4] = {};
    bool m_Generated = false;

    std::shared_ptr<GpuTexture> m_Environment;
    std::shared_ptr<GpuTextureView> m_EnvironmentSrv;
    std::array<std::array<std::shared_ptr<GpuTextureView>, 6>, kCubeMipLevels> m_FaceRtvs;
    std::shared_ptr<GpuSampler> m_LinearClamp;
    std::shared_ptr<GpuShader> m_AtmosphereShader;
    std::shared_ptr<GpuShader> m_MipmapShader;
    std::shared_ptr<GpuShader> m_SHShader;
    std::shared_ptr<ShaderHandle> m_AtmosphereHandle;
    std::shared_ptr<ShaderHandle> m_MipmapHandle;
    std::shared_ptr<ShaderHandle> m_SHHandle;
    uint64_t m_AtmosphereVersion = 0;
    uint64_t m_MipmapVersion = 0;
    uint64_t m_SHVersion = 0;
    std::shared_ptr<GpuGraphicsPipeline> m_AtmospherePipeline;
    std::shared_ptr<GpuGraphicsPipeline> m_MipmapPipeline;
    std::shared_ptr<GpuComputePipeline> m_SHPipeline;
    std::shared_ptr<GpuBuffer> m_SHBuffer;
    std::shared_ptr<GpuBufferView> m_SHUav;
    std::shared_ptr<GpuBufferView> m_SH2Srv;
    std::shared_ptr<GpuReadbackTicket> m_Readback;
};
