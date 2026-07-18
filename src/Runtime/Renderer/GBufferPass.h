#pragma once

#include "Renderer/MaterialResourceCache.h"
#include "Renderer/MaterialSystem.h"
#include "Renderer/RenderPass.h"
#include "Renderer/SceneRenderCollector.h"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

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
        std::shared_ptr<GpuTexture> velocity;
        std::shared_ptr<GpuTextureView> velocityRtv;
        std::shared_ptr<GpuTextureView> velocitySrv;
        RHIResourceState initialState = RHIResourceState::Undefined;
    };

    explicit GBufferPass(IRHIDevice* device);

    void Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera) override;
    void ExecuteCompatibilityOnly(GpuCommandList& commands, const Scene& scene, const Camera& camera);
    void ExecuteCompatibilityOnly(GpuCommandList& commands, const Scene& scene, const Camera& camera,
                                  const Mat4& viewProjection, const Mat4& previousViewProjection);
    void Resize(uint32_t width, uint32_t height) override;
    bool PrepareGraphResources();
    GraphResources GetGraphResources() const;
    void MarkGraphResourcesShaderResource();

private:
    bool EnsureResources();
    GpuShader* GetOrCreateShader();
    GpuGraphicsPipeline* GetOrCreatePipeline(const MaterialAsset& material);
    GpuGraphicsPipeline* GetOrCreateGraphPipeline(const ResolvedMaterial& material,
                                                  const std::shared_ptr<ShaderHandle>& shader);
    void ExecuteFiltered(GpuCommandList& commands, const Scene& scene, const Camera& camera, bool compatibilityOnly,
                         const Mat4* viewProjectionOverride = nullptr,
                         const Mat4* previousViewProjectionOverride = nullptr);

    uint32_t m_Width = 1;
    uint32_t m_Height = 1;
    SceneRenderCollector m_Collector;
    MaterialResourceCache m_ResourceCache;
    MaterialSystem m_MaterialSystem;
    std::shared_ptr<ShaderHandle> m_ShaderHandle;
    uint64_t m_ShaderVersion = 0;
    std::array<std::shared_ptr<GpuGraphicsPipeline>, 4> m_Pipelines;
    std::unordered_map<std::string, std::shared_ptr<GpuGraphicsPipeline>> m_GraphPipelines;
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
    std::shared_ptr<GpuTexture> m_Velocity;
    std::shared_ptr<GpuTextureView> m_VelocityRtv;
    std::shared_ptr<GpuTextureView> m_VelocitySrv;
    Mat4 m_PreviousViewProj = Mat4::Identity();
    bool m_HasPreviousViewProj = false;
    std::unordered_map<const Actor*, Mat4> m_PreviousWorld;
    // Pose history is viewport-owned just like view/world history. A component-level previous palette would let
    // Scene View and Game View overwrite each other's last rendered pose when their frame scheduling differs.
    std::unordered_map<const SkinnedMeshRendererComponent*, std::vector<Mat4>> m_PreviousSkinMatrices;
    RHIResourceState m_GBufferState = RHIResourceState::Undefined;
};
