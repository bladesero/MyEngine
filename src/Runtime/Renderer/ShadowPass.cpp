#include "Renderer/ShadowPass.h"

#include "Assets/MaterialAsset.h"
#include "Assets/MeshAsset.h"
#include "Animation/SkinnedMeshRendererComponent.h"
#include "Core/Logger.h"
#include "Renderer/LightComponent.h"
#include "Renderer/ShaderManager.h"
#include "Math/Mat4Inverse.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>


namespace {

const VertexElement k_ShadowVertexLayout[] = {
    { "POSITION", 0, VertexFormat::Float3, offsetof(MeshVertex, position) },
    { "BLENDINDICES", 0, VertexFormat::Float4, offsetof(MeshVertex, boneIndices) },
    { "BLENDWEIGHT", 0, VertexFormat::Float4, offsetof(MeshVertex, boneWeights) },
};

struct ShadowPerDrawConstants {
    float lightMvp[16];
    float boneMatrices[128][16];
    float skinInfo[4];
};

static Vec3 StableUpForDirection(const Vec3& direction)
{
    return std::fabs(direction.Dot(Vec3::Up())) > 0.95f
        ? Vec3{ 0.0f, 0.0f, 1.0f } : Vec3::Up();
}

static void EnsureMeshUploaded(IRHIDevice* device, MeshAsset* mesh)
{
    if (!device || !mesh || mesh->IsUploaded()) return;

    const auto& verts = mesh->GetVertices();
    const auto& idx   = mesh->GetIndices();
    if (verts.empty()) return;

    const uint32_t vbBytes = static_cast<uint32_t>(verts.size() * sizeof(MeshVertex));
    mesh->SetVertexBuffer(device->CreateVertexBuffer(verts.data(), vbBytes, sizeof(MeshVertex)));

    if (!idx.empty()) {
        const uint32_t ibBytes = static_cast<uint32_t>(idx.size() * sizeof(uint32_t));
        mesh->SetIndexBuffer(device->CreateIndexBuffer(idx.data(), ibBytes));
    }
}

static MeshAsset* GetRenderMesh(Actor& actor,
                                SkinnedMeshRendererComponent** outSkin = nullptr,
                                MaterialAsset** outMaterial = nullptr)
{
    if (outSkin) *outSkin = nullptr;
    if (outMaterial) *outMaterial = nullptr;
    if (auto* skinned = actor.GetComponent<SkinnedMeshRendererComponent>()) {
        if (outSkin) *outSkin = skinned;
        if (outMaterial) *outMaterial = skinned->GetMaterial().Get();
        return skinned->IsEnabled() && skinned->IsValid() ? skinned->GetRenderMesh() : nullptr;
    }
    if (auto* renderer = actor.GetComponent<MeshRendererComponent>()) {
        if (outMaterial) *outMaterial = renderer->GetMaterial().Get();
        return renderer->IsEnabled() && renderer->IsValid() ? renderer->GetMesh().Get() : nullptr;
    }
    return nullptr;
}

} // namespace

ShadowPass::ShadowPass(IRHIDevice* device)
    : RenderPass(device)
{}

const Mat4& ShadowPass::GetCascadeViewProj(uint32_t index) const
{
    static const Mat4 kIdentity = Mat4::Identity();
    return index < 4 ? m_LightViewProjCascade[index] : kIdentity;
}

void ShadowPass::Resize(uint32_t width, uint32_t height)
{
    const uint32_t target = 2048u;
    if (target == m_ShadowMapSize) return;

    m_ShadowMapSize = target;
    m_ShadowMapTexture.reset();
    m_SpotShadowMapTexture.reset();
    m_PointShadowMapTexture.reset();
    for (auto& view : m_ShadowCascadeViews) view.reset();
    m_SpotShadowView.reset();
    for (auto& view : m_PointShadowViews) view.reset();
    m_ShadowResourcesInShaderState = false;
}

void ShadowPass::UpdateLightMatrices(const Scene& scene, const Camera& camera)
{
    bool foundDirectional = false;
    bool foundSpot = false;
    bool foundPoint = false;
    int spotIndex = 0;
    int pointIndex = 0;
    m_DirectionalShadowEnabled = false;
    m_SpotShadowIndex = -1;
    m_PointShadowIndex = -1;

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* light = actor.GetComponent<LightComponent>();
        if (!light || !light->IsEnabled()) return;
        if (light->GetLightType() == LightType::Directional) {
            if (!foundDirectional) {
                m_LightDirection = light->GetDirection();
                m_DirectionalShadowEnabled = light->CastsShadows();
                foundDirectional = true;
            }
            return;
        }
        if (light->GetLightType() == LightType::Spot) {
            if (!foundSpot && light->CastsShadows() && spotIndex < 4) {
                const Vec3 position = actor.GetWorldPosition();
                const Vec3 direction = light->GetDirection();
                const Mat4 lightView = Mat4::LookAt(
                    position, position + direction, StableUpForDirection(direction));
                const Mat4 lightProj = Mat4::Perspective(
                    light->GetOuterConeAngle() * 2.0f * kDeg2Rad,
                    1.0f, 0.05f, light->GetRange());
                m_SpotLightViewProj = lightView * lightProj;
                m_SpotShadowIndex = spotIndex;
                foundSpot = true;
            }
            ++spotIndex;
            return;
        }
        if (light->GetLightType() == LightType::Point) {
            if (!foundPoint && light->CastsShadows() && pointIndex < 4) {
                m_PointShadowPosition = actor.GetWorldPosition();
                m_PointShadowRange = light->GetRange();
                m_PointShadowIndex = pointIndex;
                foundPoint = true;

                const Vec3 position = m_PointShadowPosition;
                const Vec3 dirs[6] = {
                    { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
                    { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
                    { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
                };
                const Vec3 ups[6] = {
                    { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
                    { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f },
                    { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
                };
                const Mat4 lightProj = Mat4::Perspective(
                    90.0f * kDeg2Rad, 1.0f, 0.05f, m_PointShadowRange);
                for (int face = 0; face < 6; ++face) {
                    const Mat4 lightView = Mat4::LookAt(
                        position, position + dirs[face], ups[face]);
                    m_PointLightViewProj[face] = lightView * lightProj;
                }
            }
            ++pointIndex;
        }
    });

    const float kInf = std::numeric_limits<float>::infinity();
    Vec3 sceneMin = { kInf, kInf, kInf };
    Vec3 sceneMax = { -kInf, -kInf, -kInf };
    bool hasBounds = false;

    auto expandBounds = [&](const Vec3& p) {
        if (p.x < sceneMin.x) sceneMin.x = p.x;
        if (p.y < sceneMin.y) sceneMin.y = p.y;
        if (p.z < sceneMin.z) sceneMin.z = p.z;
        if (p.x > sceneMax.x) sceneMax.x = p.x;
        if (p.y > sceneMax.y) sceneMax.y = p.y;
        if (p.z > sceneMax.z) sceneMax.z = p.z;
    };

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        MeshAsset* mesh = GetRenderMesh(actor);
        if (!mesh || mesh->GetVertices().empty()) return;

        const AABB& aabb = mesh->GetAABB();
        const Vec3 corners[8] = {
            { aabb.min.x, aabb.min.y, aabb.min.z },
            { aabb.max.x, aabb.min.y, aabb.min.z },
            { aabb.min.x, aabb.max.y, aabb.min.z },
            { aabb.max.x, aabb.max.y, aabb.min.z },
            { aabb.min.x, aabb.min.y, aabb.max.z },
            { aabb.max.x, aabb.min.y, aabb.max.z },
            { aabb.min.x, aabb.max.y, aabb.max.z },
            { aabb.max.x, aabb.max.y, aabb.max.z },
        };

        const Mat4 world = actor.GetWorldMatrix();
        for (const Vec3& c : corners) {
            expandBounds(world.TransformPoint(c));
        }
        hasBounds = true;
    });

    if (!hasBounds) {
        const float extent = 8.0f;
        const Vec3 center = Vec3::Zero();
        const Vec3 eye = center - m_LightDirection * (extent * 2.5f);
        const Mat4 lightView = Mat4::LookAt(eye, center, Vec3::Up());
        const Mat4 lightProj = Mat4::Ortho(-extent, extent, -extent, extent, 0.1f, extent * 8.0f);
        m_LightViewProjCascade[0] = lightView * lightProj;
        m_LightViewProj = m_LightViewProjCascade[0];
        m_CascadeCount = 1;
        m_CascadeSplits[0] = camera.GetNear() + (camera.GetFar() - camera.GetNear()) * 0.5f;
        m_CascadeSplits[1] = camera.GetFar();
        return;
    }

    // ----- CSM: camera-frustum-based cascade projections -----
    const float camNear = camera.GetNear();

    // Hardcoded cascade splits: cascade 0 = near..15, cascade 1 = 15..75,
    // cascade 2 = 75..150.
    const uint32_t cascadeCount = kMaxCascades;
    const float splits[3] = { 15.0f, 75.0f, 150.0f };

    m_CascadeCount = cascadeCount;
    m_CascadeSplits[0] = splits[0];
    m_CascadeSplits[1] = splits[1];
    m_CascadeSplits[2] = splits[2];
    m_CascadeSplits[3] = 0.0f;

    // Inverse view to transform view-space corners to world space.
    Mat4 invView;
    const Vec3 center = (sceneMin + sceneMax) * 0.5f;
    const Vec3 half = (sceneMax - sceneMin) * 0.5f;
    const float halfMax = (std::max)(half.x, (std::max)(half.y, half.z));

    if (!Mat4Invert(camera.GetView(), invView)) {
        // Fallback on degenerate view matrix.
        const float extent = (std::max)(4.0f, halfMax);
        const Vec3 eyeFb = center - m_LightDirection * (extent * 3.0f);
        const Mat4 lightViewFb = Mat4::LookAt(eyeFb, center, Vec3::Up());
        const Mat4 lightProjFb = Mat4::Ortho(-extent, extent, -extent, extent, 0.1f, extent * 8.0f);
        m_LightViewProjCascade[0] = lightViewFb * lightProjFb;
        m_LightViewProj = m_LightViewProjCascade[0];
        m_CascadeCount = 1;
        return;
    }

    const float fovYRad = camera.GetFovY() * kDeg2Rad;
    const float aspect = camera.GetAspect();
    const float tanHalfFov = std::tan(fovYRad * 0.5f);

    // Light view: look at scene center from far enough away.
    const float lightDist = (std::max)(4.0f, halfMax) * 3.0f;
    const Vec3 eye = center - m_LightDirection * lightDist;
    const Mat4 lightView = Mat4::LookAt(eye, center, Vec3::Up());

    for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade) {
        const float splitNear = (cascade == 0) ? camNear : splits[cascade - 1];
        const float splitFar  = splits[cascade];

        // Sub-frustum corners in view space. The engine uses a left-handed
        // camera/projection convention, so visible camera-space depth is +Z.
        const float nearHalfH = tanHalfFov * splitNear;
        const float nearHalfW = nearHalfH * aspect;
        const float farHalfH  = tanHalfFov * splitFar;
        const float farHalfW  = farHalfH * aspect;

        const Vec3 frustumCornersVS[8] = {
            { -nearHalfW,  nearHalfH, splitNear },
            {  nearHalfW,  nearHalfH, splitNear },
            { -nearHalfW, -nearHalfH, splitNear },
            {  nearHalfW, -nearHalfH, splitNear },
            { -farHalfW,   farHalfH,  splitFar  },
            {  farHalfW,   farHalfH,  splitFar  },
            { -farHalfW,  -farHalfH,  splitFar  },
            {  farHalfW,  -farHalfH,  splitFar  },
        };

        // --- Sphere bounding: tight XY from frustum, extended Z from scene ---
        Vec3 wsMin = { kInf, kInf, kInf };
        Vec3 wsMax = { -kInf, -kInf, -kInf };

        for (const Vec3& cornerVS : frustumCornersVS) {
            const Vec3 ws = invView.TransformPoint(cornerVS);
            if (ws.x < wsMin.x) wsMin.x = ws.x;
            if (ws.y < wsMin.y) wsMin.y = ws.y;
            if (ws.z < wsMin.z) wsMin.z = ws.z;
            if (ws.x > wsMax.x) wsMax.x = ws.x;
            if (ws.y > wsMax.y) wsMax.y = ws.y;
            if (ws.z > wsMax.z) wsMax.z = ws.z;
        }

        // Fit bounding sphere from frustum-only AABB (XY tightness).
        const Vec3 sphereCenterWS = (wsMin + wsMax) * 0.5f;
        const Vec3 sphereExtent  = (wsMax - wsMin) * 0.5f;
        const float sphereRadius = std::sqrt(
            sphereExtent.x * sphereExtent.x +
            sphereExtent.y * sphereExtent.y +
            sphereExtent.z * sphereExtent.z);

        // Transform sphere center to light-view space.
        // lightView is orthonormal, radius is invariant.
        const Vec3 sphereCenterLS = lightView.TransformPoint(sphereCenterWS);

        // --- XY bounds: tight around frustum sphere ---
        // --- Texel alignment: snap XY to shadow-map texel grid ---
        const float shadowMapSizeF = static_cast<float>(m_ShadowMapSize);
        const float worldUnitsPerTexel = (sphereRadius * 2.0f) / shadowMapSizeF;

        float left   = sphereCenterLS.x - sphereRadius;
        float right  = sphereCenterLS.x + sphereRadius;
        float bottom = sphereCenterLS.y - sphereRadius;
        float top    = sphereCenterLS.y + sphereRadius;

        left   = std::floor(left   / worldUnitsPerTexel) * worldUnitsPerTexel;
        right  = std::ceil (right  / worldUnitsPerTexel) * worldUnitsPerTexel;
        bottom = std::floor(bottom / worldUnitsPerTexel) * worldUnitsPerTexel;
        top    = std::ceil (top    / worldUnitsPerTexel) * worldUnitsPerTexel;

        // --- Z range: extend with scene AABB in light space to catch all occluders ---
        float minZ = sphereCenterLS.z - sphereRadius;
        float maxZ = sphereCenterLS.z + sphereRadius;

        const Vec3 sceneCorners[8] = {
            { sceneMin.x, sceneMin.y, sceneMin.z },
            { sceneMax.x, sceneMin.y, sceneMin.z },
            { sceneMin.x, sceneMax.y, sceneMin.z },
            { sceneMax.x, sceneMax.y, sceneMin.z },
            { sceneMin.x, sceneMin.y, sceneMax.z },
            { sceneMax.x, sceneMin.y, sceneMax.z },
            { sceneMin.x, sceneMax.y, sceneMax.z },
            { sceneMax.x, sceneMax.y, sceneMax.z },
        };
        for (const Vec3& sc : sceneCorners) {
            const Vec3 ls = lightView.TransformPoint(sc);
            if (ls.z < minZ) minZ = ls.z;
            if (ls.z > maxZ) maxZ = ls.z;
        }

        const float zPad = (std::max)(1.0f, (maxZ - minZ) * 0.2f);
        const float nearZ = minZ - zPad;
        const float farZ  = maxZ + zPad;

        const Mat4 lightProj = Mat4::Ortho(left, right, bottom, top, nearZ, farZ);
        m_LightViewProjCascade[cascade] = lightView * lightProj;
    }

    m_LightViewProj = m_LightViewProjCascade[0];
}

void ShadowPass::EnsureShadowShader()
{
    if (!Device()) return;
    if (!m_ShadowShaderHandle)
        m_ShadowShaderHandle = ShaderManager::Get().GetOrCreate(
            "Content/Engine/Shaders/ShadowDepth.shader", k_ShadowVertexLayout, 3);
    if (!m_ShadowShaderHandle || !m_ShadowShaderHandle->shader) {
        Logger::Error("[ShadowPass] Failed to create shadow shader");
        m_ShadowPipeline.reset();
        return;
    }
    if (!m_ShadowPipeline || m_ShadowShaderVersion != m_ShadowShaderHandle->version) {
        GraphicsPipelineDesc desc;
        desc.shader = m_ShadowShaderHandle->shader;
        desc.colorFormats.clear();
        desc.depthFormat = RHIFormat::D24S8;
        desc.rasterizer.cullMode = RHICullMode::None;
        desc.rasterizer.depthBias = 1536;
        desc.rasterizer.slopeScaledDepthBias = 2.0f;
        desc.depthStencil.depthTestEnable = true;
        desc.depthStencil.depthWriteEnable = true;
        desc.depthStencil.depthCompareOp = RHICompareOp::Less;
        m_ShadowPipeline = Device()->CreateGraphicsPipeline(desc);
        m_ShadowShaderVersion = m_ShadowShaderHandle->version;
        if (!m_ShadowPipeline)
            Logger::Error("[ShadowPass] Failed to create shadow pipeline");
    }
}


bool ShadowPass::EnsureShadowResources()
{
    if (m_ShadowMapTexture && m_SpotShadowMapTexture && m_PointShadowMapTexture)
        return true;
    RHITextureDesc directional;
    directional.width = directional.height = m_ShadowMapSize;
    directional.arrayLayers = kMaxCascades;
    directional.format = RHIFormat::D24S8;
    directional.usage = RHIResourceUsage::DepthStencil | RHIResourceUsage::ShaderResource;
    directional.debugName = "DirectionalShadow";
    m_ShadowMapTexture = Device()->CreateTexture(directional);

    RHITextureDesc spot = directional;
    spot.arrayLayers = 1; spot.debugName = "SpotShadow";
    m_SpotShadowMapTexture = Device()->CreateTexture(spot);

    RHITextureDesc point = directional;
    point.arrayLayers = 6; point.cube = true; point.debugName = "PointShadow";
    m_PointShadowMapTexture = Device()->CreateTexture(point);
    if (!m_ShadowMapTexture || !m_SpotShadowMapTexture || !m_PointShadowMapTexture) {
        Logger::Error("[ShadowPass] RHI shadow texture creation failed");
        return false;
    }
    for (uint32_t cascade = 0; cascade < kMaxCascades; ++cascade) {
        RHITextureViewDesc view;
        view.firstLayer = cascade; view.layerCount = 1;
        view.usage = RHIResourceUsage::DepthStencil;
        m_ShadowCascadeViews[cascade] = Device()->CreateTextureView(m_ShadowMapTexture, view);
    }
    RHITextureViewDesc spotView; spotView.usage = RHIResourceUsage::DepthStencil;
    m_SpotShadowView = Device()->CreateTextureView(m_SpotShadowMapTexture, spotView);
    for (uint32_t face = 0; face < 6; ++face) {
        RHITextureViewDesc view;
        view.firstLayer = face; view.layerCount = 1;
        view.usage = RHIResourceUsage::DepthStencil;
        m_PointShadowViews[face] = Device()->CreateTextureView(m_PointShadowMapTexture, view);
    }
    bool valid = m_SpotShadowView != nullptr;
    for (const auto& view : m_ShadowCascadeViews) valid = valid && view != nullptr;
    for (const auto& view : m_PointShadowViews) valid = valid && view != nullptr;
    if (!valid) Logger::Error("[ShadowPass] RHI shadow view creation failed");
    return valid;
}

bool ShadowPass::PrepareGraphResources(const Scene& scene, const Camera& camera)
{
    if (!Device()) return false;
    EnsureShadowShader();
    if (!m_ShadowShaderHandle || !m_ShadowShaderHandle->shader || !m_ShadowPipeline) return false;
    UpdateLightMatrices(scene, camera);
    return EnsureShadowResources();
}

ShadowPass::GraphResources ShadowPass::GetGraphResources() const
{
    GraphResources out;
    out.directional = m_ShadowMapTexture;
    for (uint32_t i = 0; i < kMaxCascades; ++i) {
        out.directionalCascadeViews[i] = m_ShadowCascadeViews[i];
    }
    out.spot = m_SpotShadowMapTexture;
    out.spotView = m_SpotShadowView;
    out.point = m_PointShadowMapTexture;
    for (uint32_t i = 0; i < 6; ++i) {
        out.pointViews[i] = m_PointShadowViews[i];
    }
    out.initialState = m_ShadowResourcesInShaderState
        ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
    return out;
}

void ShadowPass::DrawShadowScene(GpuCommandList& commands, const Scene& scene,
                                 const Mat4& lightViewProj)
{
    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        SkinnedMeshRendererComponent* skin = nullptr;
        MaterialAsset* material = nullptr;
        MeshAsset* mesh = GetRenderMesh(actor, &skin, &material);
        if (!mesh || (material && material->GetBlendMode() == BlendMode::Transparent)) return;
        EnsureMeshUploaded(Device(), mesh);
        if (!mesh->GetVertexBuffer()) return;
        ShadowPerDrawConstants constants{};
        const Mat4 lightMvp = actor.GetWorldMatrix() * lightViewProj;
        std::memcpy(constants.lightMvp, lightMvp.Data(), sizeof(constants.lightMvp));
        if (skin && skin->UsesGpuSkinning()) {
            const auto& matrices = skin->GetSkinMatrices();
            const size_t count = (std::min)(matrices.size(), size_t{128});
            constants.skinInfo[0] = static_cast<float>(count);
            for (size_t bone = 0; bone < count; ++bone)
                std::memcpy(constants.boneMatrices[bone], matrices[bone].Data(),
                            sizeof(constants.boneMatrices[bone]));
        }
        commands.SetVertexBuffer(mesh->GetVertexBuffer());
        commands.SetIndexBuffer(mesh->GetIndexBuffer());
        auto bindings = Device()->CreateBindGroup(m_ShadowShaderHandle->shader);
        if (bindings && bindings->SetConstants(
                "ShadowPerDraw", &constants, sizeof(constants)))
            commands.SetBindGroup(0, bindings.get());
        for (const auto& sm : mesh->GetSubMeshes()) {
            if (mesh->GetIndexBuffer())
                commands.DrawIndexed(sm.indexCount, sm.indexOffset, static_cast<uint32_t>(sm.vertexOffset));
            else
                commands.Draw(sm.indexCount, sm.vertexOffset);
        }
    });
}

void ShadowPass::ExecuteGraphManaged(GpuCommandList& commands, const Scene& scene)
{
    if (!Device() || !m_ShadowPipeline) return;
    commands.SetGraphicsPipeline(m_ShadowPipeline.get());
    commands.SetViewport(0.0f, 0.0f, static_cast<float>(m_ShadowMapSize),
                         static_cast<float>(m_ShadowMapSize));

    auto renderDepth = [&](GpuTextureView* view, const Mat4& matrix) {
        RenderingAttachment depth; depth.view = view; depth.loadOp = RHILoadOp::Clear;
        depth.storeOp = RHIStoreOp::Store; depth.clearDepth = 1.0f;
        RenderingInfo info; info.depth = &depth; info.width = m_ShadowMapSize; info.height = m_ShadowMapSize;
        commands.BeginRendering(info);
        DrawShadowScene(commands, scene, matrix);
        commands.EndRendering();
    };
    const RHIResourceState before = m_ShadowResourcesInShaderState
        ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
    if (m_DirectionalShadowEnabled) {
        commands.Transition(m_ShadowMapTexture.get(), before, RHIResourceState::DepthWrite);
        for (uint32_t cascade = 0; cascade < m_CascadeCount; ++cascade)
            renderDepth(m_ShadowCascadeViews[cascade].get(), m_LightViewProjCascade[cascade]);
        commands.Transition(m_ShadowMapTexture.get(), RHIResourceState::DepthWrite,
                            RHIResourceState::ShaderResource);
    }
    if (m_SpotShadowIndex >= 0) {
        commands.Transition(m_SpotShadowMapTexture.get(), before, RHIResourceState::DepthWrite);
        renderDepth(m_SpotShadowView.get(), m_SpotLightViewProj);
        commands.Transition(m_SpotShadowMapTexture.get(), RHIResourceState::DepthWrite,
                            RHIResourceState::ShaderResource);
    }
    if (m_PointShadowIndex >= 0) {
        commands.Transition(m_PointShadowMapTexture.get(), before, RHIResourceState::DepthWrite);
        for (uint32_t face = 0; face < 6; ++face)
            renderDepth(m_PointShadowViews[face].get(), m_PointLightViewProj[face]);
        commands.Transition(m_PointShadowMapTexture.get(), RHIResourceState::DepthWrite,
                            RHIResourceState::ShaderResource);
    }
    m_ShadowResourcesInShaderState = true;
}

void ShadowPass::Execute(GpuCommandList& commands, const Scene& scene, const Camera& camera)
{
    if (!Device()) return;
    EnsureShadowShader();
    if (!m_ShadowShaderHandle || !m_ShadowShaderHandle->shader || !m_ShadowPipeline) return;

    UpdateLightMatrices(scene, camera);

    if (!EnsureShadowResources()) return;
    commands.SetGraphicsPipeline(m_ShadowPipeline.get());
    commands.SetViewport(0.0f, 0.0f, static_cast<float>(m_ShadowMapSize),
                         static_cast<float>(m_ShadowMapSize));

    auto renderDepth = [&](GpuTextureView* view, const Mat4& matrix) {
        RenderingAttachment depth; depth.view = view; depth.loadOp = RHILoadOp::Clear;
        depth.storeOp = RHIStoreOp::Store; depth.clearDepth = 1.0f;
        RenderingInfo info; info.depth = &depth; info.width = m_ShadowMapSize; info.height = m_ShadowMapSize;
        commands.BeginRendering(info); DrawShadowScene(commands, scene, matrix); commands.EndRendering();
    };
    const RHIResourceState before = m_ShadowResourcesInShaderState
        ? RHIResourceState::ShaderResource : RHIResourceState::Undefined;
    if (m_DirectionalShadowEnabled) {
        commands.Transition(m_ShadowMapTexture.get(), before, RHIResourceState::DepthWrite);
        for (uint32_t cascade = 0; cascade < m_CascadeCount; ++cascade)
            renderDepth(m_ShadowCascadeViews[cascade].get(), m_LightViewProjCascade[cascade]);
        commands.Transition(m_ShadowMapTexture.get(), RHIResourceState::DepthWrite, RHIResourceState::ShaderResource);
    }
    if (m_SpotShadowIndex >= 0) {
        commands.Transition(m_SpotShadowMapTexture.get(), before, RHIResourceState::DepthWrite);
        renderDepth(m_SpotShadowView.get(), m_SpotLightViewProj);
        commands.Transition(m_SpotShadowMapTexture.get(), RHIResourceState::DepthWrite, RHIResourceState::ShaderResource);
    }
    if (m_PointShadowIndex >= 0) {
        commands.Transition(m_PointShadowMapTexture.get(), before, RHIResourceState::DepthWrite);
        for (uint32_t face = 0; face < 6; ++face)
            renderDepth(m_PointShadowViews[face].get(), m_PointLightViewProj[face]);
        commands.Transition(m_PointShadowMapTexture.get(), RHIResourceState::DepthWrite, RHIResourceState::ShaderResource);
    }
    m_ShadowResourcesInShaderState = true;
    return;

}
