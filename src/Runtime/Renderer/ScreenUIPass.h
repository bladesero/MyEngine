#pragma once

#include "Renderer/RenderPass.h"
#include "Renderer/RHI/GpuPipeline.h"
#include "UI/Render/UIDrawList.h"

#include <memory>

class ScreenUIPass final : public RenderPass {
public:
    explicit ScreenUIPass(IRHIDevice* device);

    void Execute(GpuCommandList& commands, const Scene& scene,
                 const Camera& camera) override;
    void Execute(GpuCommandList& commands, const UIDrawList& drawList);
    void Resize(uint32_t width, uint32_t height) override;

private:
    GpuShader* GetOrCreateShader();
    GpuGraphicsPipeline* GetOrCreatePipeline();
    std::shared_ptr<GpuTexture> GetOrCreateWhiteTexture();

    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    std::shared_ptr<GpuShader> m_Shader;
    std::shared_ptr<GpuGraphicsPipeline> m_Pipeline;
    std::shared_ptr<GpuTexture> m_WhiteTexture;
};
