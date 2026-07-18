#pragma once

#include "Renderer/RenderPass.h"
#include "Renderer/RHI/GpuPipeline.h"
#include "UI/Render/UIDrawList.h"

#include <memory>
#include <unordered_map>

struct GpuSampler;
struct GpuTextureView;
struct ShaderHandle;

class ScreenUIPass final : public RenderPass {
public:
    explicit ScreenUIPass(IRHIDevice* device);

    void Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera) override;
    bool Prepare(const UIDrawList& drawList, RHIFormat colorFormat);
    void Execute(GpuCommandList& commands, const UIDrawList& drawList, RHIFormat colorFormat);
    void Resize(uint32_t width, uint32_t height) override;

private:
    GpuShader* GetOrCreateShader();
    GpuGraphicsPipeline* GetOrCreatePipeline(RHIFormat colorFormat);
    std::shared_ptr<GpuTexture> GetOrCreateWhiteTexture();
    std::shared_ptr<GpuTextureView> GetOrCreateTextureView(const std::shared_ptr<GpuTexture>& texture);
    std::shared_ptr<GpuSampler> GetOrCreateSampler();

    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    bool m_ShaderCreationAttempted = false;
    bool m_LoggedBindingFailure = false;
    std::shared_ptr<ShaderHandle> m_ShaderHandle;
    std::shared_ptr<GpuShader> m_Shader;
    std::unordered_map<uint32_t, std::shared_ptr<GpuGraphicsPipeline>> m_Pipelines;
    std::shared_ptr<GpuTexture> m_WhiteTexture;
    std::shared_ptr<GpuSampler> m_LinearSampler;
    std::unordered_map<GpuTexture*, std::shared_ptr<GpuTextureView>> m_TextureViews;
};
