#pragma once

#include "Renderer/RenderPass.h"
#include "Core/Math.h"

#include <memory>
#include <unordered_map>

class TextureAsset;
class MeshAsset;

class MainPass final : public RenderPass {
public:
    explicit MainPass(IRenderContext* context);

    void Execute(const Scene& scene, const Camera& camera) override;
    void Resize(uint32_t width, uint32_t height) override;

    void SetPresentEnabled(bool present) { m_PresentEnabled = present; }
    void SetShadowInput(const Mat4& lightViewProj, const Vec3& lightDirection, GpuTexture* shadowMap);

private:
    enum class ShaderMode {
        Unknown,
        Legacy,
        ShadowedD3D11,
    };

    void EnsureMeshUploaded(MeshAsset* mesh);
    void EnsureTextureUploaded(TextureAsset* tex);
    GpuShader* GetOrCreateShader();

private:
    ShaderMode m_ShaderMode = ShaderMode::Unknown;
    bool m_PresentEnabled = true;

    std::shared_ptr<GpuShader> m_MainShader;
    std::unordered_map<TextureAsset*, std::shared_ptr<GpuTexture>> m_TexCache;
    GpuTexture* m_ShadowMap = nullptr;
    Mat4 m_LightViewProj = Mat4::Identity();
    Vec3 m_LightDirection = Vec3{ -0.55f, -1.0f, -0.45f }.Normalized();
};
