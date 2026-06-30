#pragma once

#include "Renderer/MaterialResourceCache.h"
#include "Renderer/RenderPass.h"
#include "Renderer/SceneRenderCollector.h"

#include <array>
#include <memory>
#include <unordered_map>

struct ShaderHandle;
class MaterialAsset;

class GBufferPass final : public RenderPass {
public:
    struct GraphResources {
        std::shared_ptr<GpuTexture> albedo;
        std::shared_ptr<GpuTextureView> albedoRtv;
        std::shared_ptr<GpuTextureView> albedoSrv;
        std::shared_ptr<GpuTexture> normal;
        std::shared_ptr<GpuTextureView> normalRtv;
        std::shared_ptr<GpuTextureView> normalSrv;
        std::shared_ptr<GpuTexture> material;
        std::shared_ptr<GpuTextureView> materialRtv;
        std::shared_ptr<GpuTextureView> materialSrv;
        std::shared_ptr<GpuTexture> emissive;
        std::shared_ptr<GpuTextureView> emissiveRtv;
        std::shared_ptr<GpuTextureView> emissiveSrv;
        RHIResourceState initialState = RHIResourceState::Undefined;
    };

    explicit GBufferPass(IRHIDevice* device);

    void Execute(GpuCommandList& commands, const Scene& scene,
                 const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;
    bool PrepareGraphResources();
    GraphResources GetGraphResources() const;
    void MarkGraphResourcesShaderResource();

private:
    bool EnsureResources();
    GpuShader* GetOrCreateShader();
    GpuGraphicsPipeline* GetOrCreatePipeline(const MaterialAsset& material);

    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    SceneRenderCollector m_Collector;
    MaterialResourceCache m_ResourceCache;
    std::shared_ptr<ShaderHandle> m_ShaderHandle;
    uint64_t m_ShaderVersion = 0;
    std::array<std::shared_ptr<GpuGraphicsPipeline>, 4> m_Pipelines;
    std::shared_ptr<GpuTexture> m_Albedo;
    std::shared_ptr<GpuTextureView> m_AlbedoRtv;
    std::shared_ptr<GpuTextureView> m_AlbedoSrv;
    std::shared_ptr<GpuTexture> m_Normal;
    std::shared_ptr<GpuTextureView> m_NormalRtv;
    std::shared_ptr<GpuTextureView> m_NormalSrv;
    std::shared_ptr<GpuTexture> m_Material;
    std::shared_ptr<GpuTextureView> m_MaterialRtv;
    std::shared_ptr<GpuTextureView> m_MaterialSrv;
    std::shared_ptr<GpuTexture> m_Emissive;
    std::shared_ptr<GpuTextureView> m_EmissiveRtv;
    std::shared_ptr<GpuTextureView> m_EmissiveSrv;
    RHIResourceState m_GBufferState = RHIResourceState::Undefined;
};
