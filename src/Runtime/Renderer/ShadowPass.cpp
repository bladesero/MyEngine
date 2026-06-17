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

#ifdef MYENGINE_PLATFORM_WINDOWS
#include "Renderer/D3D11Context.h"
#include "Renderer/D3D12Context.h"
#include "ShaderBytecodeWindows.h"
#endif

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

static void EnsureMeshUploaded(IRenderContext* context, MeshAsset* mesh)
{
    if (!context || !mesh || mesh->IsUploaded()) return;

    const auto& verts = mesh->GetVertices();
    const auto& idx   = mesh->GetIndices();
    if (verts.empty()) return;

    const uint32_t vbBytes = static_cast<uint32_t>(verts.size() * sizeof(MeshVertex));
    mesh->SetVertexBuffer(context->CreateVertexBuffer(verts.data(), vbBytes, sizeof(MeshVertex)));

    if (!idx.empty()) {
        const uint32_t ibBytes = static_cast<uint32_t>(idx.size() * sizeof(uint32_t));
        mesh->SetIndexBuffer(context->CreateIndexBuffer(idx.data(), ibBytes));
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

ShadowPass::ShadowPass(IRenderContext* context)
    : RenderPass(context)
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
#ifdef MYENGINE_PLATFORM_WINDOWS
    m_ShadowDepthTexture.Reset();
    for (auto& dsv : m_ShadowDSV) {
        dsv.Reset();
    }
    m_ShadowSRV.Reset();
    m_SpotShadowDepthTexture.Reset();
    m_SpotShadowDSV.Reset();
    m_SpotShadowSRV.Reset();
    m_PointShadowDepthTexture.Reset();
    for (auto& dsv : m_PointShadowDSV) {
        dsv.Reset();
    }
    m_PointShadowSRV.Reset();
    m_ShadowSampler.Reset();
    m_ShadowMapTexture.reset();
    m_SpotShadowMapTexture.reset();
    m_PointShadowMapTexture.reset();
    m_ShadowDepthResourceD3D12.Reset();
    m_SpotShadowDepthResourceD3D12.Reset();
    m_PointShadowDepthResourceD3D12.Reset();
    for (auto& handle : m_ShadowCascadeDsvD3D12) {
        handle = {};
    }
    m_SpotShadowDsvD3D12 = {};
    for (auto& handle : m_PointShadowDsvD3D12) {
        handle = {};
    }
#endif
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
    if (!Context()) return;

#ifdef MYENGINE_PLATFORM_WINDOWS
    ShaderManager::Get().SetContext(Context());

    if (dynamic_cast<D3D11Context*>(Context()) != nullptr) {
        if (!m_ShadowShaderHandle) {
            m_ShadowShaderHandle = ShaderManager::Get().GetOrCreate(
                "src/Runtime/Renderer/Shaders/ShadowDepth.hlsl",
                "VSMain", "PSMain",
                k_ShadowVertexLayout, 3);
        }
    } else {
        if (!m_ShadowShaderHandle) {
            m_ShadowShaderHandle = ShaderManager::Get().GetOrCreate(
                "src/Runtime/Renderer/Shaders/ShadowDepth.hlsl",
                "VSMain", "PSMain",
                k_ShadowVertexLayout,
                3);
        }
    }
    if (!m_ShadowShaderHandle || !m_ShadowShaderHandle->shader) {
        Logger::Error("[ShadowPass] Failed to create shadow shader");
    }
#endif
}

#ifdef MYENGINE_PLATFORM_WINDOWS
bool ShadowPass::EnsureShadowResourcesD3D11()
{
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    if (!d3d11) return false;
    if (m_ShadowDepthTexture && m_ShadowDSV[0] && m_ShadowDSV[1] && m_ShadowDSV[2] && m_ShadowSRV &&
        m_SpotShadowDepthTexture && m_SpotShadowDSV && m_SpotShadowSRV &&
        m_PointShadowDepthTexture && m_PointShadowSRV &&
        m_PointShadowDSV[0] && m_PointShadowDSV[1] && m_PointShadowDSV[2] &&
        m_PointShadowDSV[3] && m_PointShadowDSV[4] && m_PointShadowDSV[5] &&
        m_ShadowSampler && m_ShadowRasterState &&
        m_ShadowMapTexture && m_SpotShadowMapTexture && m_PointShadowMapTexture) {
        return true;
    }

    ID3D11Device* device = d3d11->GetDevice();
    if (!device) return false;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width              = m_ShadowMapSize;
    texDesc.Height             = m_ShadowMapSize;
    texDesc.MipLevels          = 1;
    texDesc.ArraySize          = kMaxCascades;
    texDesc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
    texDesc.SampleDesc.Count   = 1;
    texDesc.Usage              = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &m_ShadowDepthTexture);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateTexture2D failed");
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC cascadeDsvDesc = {};
    cascadeDsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    cascadeDsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    cascadeDsvDesc.Texture2DArray.MipSlice = 0;
    for (UINT slice = 0; slice < kMaxCascades; ++slice) {
        cascadeDsvDesc.Texture2DArray.FirstArraySlice = slice;
        cascadeDsvDesc.Texture2DArray.ArraySize = 1;
        hr = device->CreateDepthStencilView(
            m_ShadowDepthTexture.Get(), &cascadeDsvDesc, m_ShadowDSV[slice].GetAddressOf());
        if (FAILED(hr)) {
            Logger::Error("[ShadowPass] CreateDepthStencilView cascade failed");
            return false;
        }
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.ArraySize = kMaxCascades;
    hr = device->CreateShaderResourceView(m_ShadowDepthTexture.Get(), &srvDesc, &m_ShadowSRV);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateShaderResourceView failed");
        return false;
    }

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = device->CreateSamplerState(&sampDesc, &m_ShadowSampler);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateSamplerState failed");
        return false;
    }

    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode              = D3D11_FILL_SOLID;
    rasterDesc.CullMode              = D3D11_CULL_NONE;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias             = 1536;
    rasterDesc.DepthBiasClamp        = 0.0f;
    rasterDesc.SlopeScaledDepthBias  = 2.0f;
    rasterDesc.DepthClipEnable       = TRUE;
    hr = device->CreateRasterizerState(&rasterDesc, &m_ShadowRasterState);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateRasterizerState failed");
        return false;
    }

    auto shadowTex = std::make_shared<D3D11Texture>();
    shadowTex->texture = m_ShadowDepthTexture;
    shadowTex->srv     = m_ShadowSRV;
    shadowTex->sampler = m_ShadowSampler;
    m_ShadowMapTexture = shadowTex;

    D3D11_TEXTURE2D_DESC spotDesc = texDesc;
    spotDesc.ArraySize = 1;
    hr = device->CreateTexture2D(&spotDesc, nullptr, &m_SpotShadowDepthTexture);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateTexture2D for spot shadow failed");
        return false;
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC spotDsvDesc = {};
    spotDsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    spotDsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    spotDsvDesc.Texture2D.MipSlice = 0;
    hr = device->CreateDepthStencilView(
        m_SpotShadowDepthTexture.Get(), &spotDsvDesc, &m_SpotShadowDSV);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateDepthStencilView for spot shadow failed");
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC spotSrvDesc = {};
    spotSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    spotSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    spotSrvDesc.Texture2D.MipLevels = 1;
    spotSrvDesc.Texture2D.MostDetailedMip = 0;
    hr = device->CreateShaderResourceView(
        m_SpotShadowDepthTexture.Get(), &spotSrvDesc, &m_SpotShadowSRV);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateShaderResourceView for spot shadow failed");
        return false;
    }

    auto spotShadowTex = std::make_shared<D3D11Texture>();
    spotShadowTex->texture = m_SpotShadowDepthTexture;
    spotShadowTex->srv     = m_SpotShadowSRV;
    spotShadowTex->sampler = m_ShadowSampler;
    m_SpotShadowMapTexture = spotShadowTex;

    D3D11_TEXTURE2D_DESC cubeDesc = texDesc;
    cubeDesc.ArraySize = 6;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    hr = device->CreateTexture2D(&cubeDesc, nullptr, &m_PointShadowDepthTexture);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateTexture2D for point shadow cube failed");
        return false;
    }

    for (UINT face = 0; face < 6; ++face) {
        D3D11_DEPTH_STENCIL_VIEW_DESC faceDsvDesc = {};
        faceDsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        faceDsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        faceDsvDesc.Texture2DArray.MipSlice = 0;
        faceDsvDesc.Texture2DArray.FirstArraySlice = face;
        faceDsvDesc.Texture2DArray.ArraySize = 1;
        hr = device->CreateDepthStencilView(
            m_PointShadowDepthTexture.Get(), &faceDsvDesc, m_PointShadowDSV[face].GetAddressOf());
        if (FAILED(hr)) {
            Logger::Error("[ShadowPass] CreateDepthStencilView for point shadow face failed");
            return false;
        }
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC cubeSrvDesc = {};
    cubeSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    cubeSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    cubeSrvDesc.TextureCube.MipLevels = 1;
    cubeSrvDesc.TextureCube.MostDetailedMip = 0;
    hr = device->CreateShaderResourceView(
        m_PointShadowDepthTexture.Get(), &cubeSrvDesc, &m_PointShadowSRV);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateShaderResourceView for point shadow cube failed");
        return false;
    }

    auto pointShadowTex = std::make_shared<D3D11Texture>();
    pointShadowTex->texture = m_PointShadowDepthTexture;
    pointShadowTex->srv     = m_PointShadowSRV;
    pointShadowTex->sampler = m_ShadowSampler;
    m_PointShadowMapTexture = pointShadowTex;

    return true;
}

bool ShadowPass::EnsureShadowResourcesD3D12()
{
    auto* d3d12 = dynamic_cast<D3D12Context*>(Context());
    if (!d3d12) return false;
    if (m_ShadowMapTexture && m_SpotShadowMapTexture && m_PointShadowMapTexture &&
        m_ShadowDepthResourceD3D12 && m_ShadowCascadeDsvD3D12[0].ptr != 0 &&
        m_ShadowCascadeDsvD3D12[1].ptr != 0 && m_ShadowCascadeDsvD3D12[2].ptr != 0 && m_SpotShadowDsvD3D12.ptr != 0 &&
        m_PointShadowDsvD3D12[0].ptr != 0) {
        return true;
    }

    const int mapSize = static_cast<int>(m_ShadowMapSize);

    auto directionalShadow = d3d12->CreateDepthTexture(mapSize, mapSize, false, kMaxCascades);
    if (!directionalShadow) {
        Logger::Error("[ShadowPass] D3D12 directional shadow texture failed");
        return false;
    }
    auto* directionalTex = static_cast<D3D12Texture*>(directionalShadow.get());
    m_ShadowDepthResourceD3D12 = directionalTex->resource;
    for (uint32_t cascade = 0; cascade < kMaxCascades; ++cascade) {
        m_ShadowCascadeDsvD3D12[cascade] = directionalTex->dsvFaces[cascade];
    }
    m_ShadowMapTexture = directionalShadow;

    auto spotShadow = d3d12->CreateDepthTexture(mapSize, mapSize, false, 1);
    if (!spotShadow) {
        Logger::Error("[ShadowPass] D3D12 spot shadow texture failed");
        return false;
    }
    auto* spotTex = static_cast<D3D12Texture*>(spotShadow.get());
    m_SpotShadowDepthResourceD3D12 = spotTex->resource;
    m_SpotShadowDsvD3D12 = spotTex->dsvCpu;
    m_SpotShadowMapTexture = spotShadow;

    auto pointShadow = d3d12->CreateDepthTexture(mapSize, mapSize, true, 1);
    if (!pointShadow) {
        Logger::Error("[ShadowPass] D3D12 point shadow texture failed");
        return false;
    }
    auto* pointTex = static_cast<D3D12Texture*>(pointShadow.get());
    m_PointShadowDepthResourceD3D12 = pointTex->resource;
    for (uint32_t face = 0; face < 6; ++face) {
        m_PointShadowDsvD3D12[face] = pointTex->dsvFaces[face];
    }
    m_PointShadowMapTexture = pointShadow;

    return true;
}
#endif

void ShadowPass::Execute(const Scene& scene, const Camera& camera)
{
    if (!Context()) return;
    EnsureShadowShader();
    if (!m_ShadowShaderHandle || !m_ShadowShaderHandle->shader) return;
    m_ShadowShaderVersion = m_ShadowShaderHandle->version;

    UpdateLightMatrices(scene, camera);

#ifdef MYENGINE_PLATFORM_WINDOWS
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    auto* d3d12 = dynamic_cast<D3D12Context*>(Context());
    if (!d3d11 && !d3d12) return;

    GpuCommandList* cmd = Context()->GetGraphicsCommandList();
    if (!cmd) return;

    const float shadowMapSize = static_cast<float>(m_ShadowMapSize);

    auto drawShadowScene = [&](const Mat4& lightViewProj) {
        scene.ForEach([&](Actor& actor) {
            if (!actor.IsActive()) return;
            SkinnedMeshRendererComponent* skin = nullptr;
            MaterialAsset* material = nullptr;
            MeshAsset* mesh = GetRenderMesh(actor, &skin, &material);
            if (!mesh) return;
            if (material && material->GetBlendMode() == BlendMode::Transparent) return;
            EnsureMeshUploaded(Context(), mesh);
            if (!mesh->GetVertexBuffer()) return;

            const Mat4 world = actor.GetWorldMatrix();
            const Mat4 lightMvp = world * lightViewProj;

            ShadowPerDrawConstants constants{};
            std::memcpy(constants.lightMvp, lightMvp.Data(), sizeof(constants.lightMvp));
            if (skin && skin->UsesGpuSkinning()) {
                const auto& matrices = skin->GetSkinMatrices();
                const size_t boneCount = (std::min)(matrices.size(), size_t{128});
                constants.skinInfo[0] = static_cast<float>(boneCount);
                for (size_t bone = 0; bone < boneCount; ++bone) {
                    std::memcpy(constants.boneMatrices[bone], matrices[bone].Data(),
                                sizeof(constants.boneMatrices[bone]));
                }
            }

            cmd->BindShader(m_ShadowShaderHandle->shader.get());
            cmd->BindVertexBuffer(mesh->GetVertexBuffer());

            if (mesh->GetIndexBuffer()) {
                cmd->BindIndexBuffer(mesh->GetIndexBuffer());
                for (const auto& sm : mesh->GetSubMeshes()) {
                    cmd->SetVSConstants(&constants, sizeof(constants));
                    cmd->DrawIndexed(sm.indexCount, sm.indexOffset,
                                     static_cast<uint32_t>(sm.vertexOffset));
                }
            } else {
                cmd->BindIndexBuffer(nullptr);
                for (const auto& sm : mesh->GetSubMeshes()) {
                    cmd->SetVSConstants(&constants, sizeof(constants));
                    cmd->Draw(sm.indexCount, sm.vertexOffset);
                }
            }
        });
    };

    if (d3d11) {
        if (!EnsureShadowResourcesD3D11()) return;

        ID3D11DeviceContext* dc = d3d11->GetDeviceContext();
        if (!dc) return;

        ID3D11RenderTargetView* prevRTVRaw = nullptr;
    ID3D11DepthStencilView* prevDSVRaw = nullptr;
        dc->OMGetRenderTargets(1, &prevRTVRaw, &prevDSVRaw);
        ComPtr<ID3D11RenderTargetView> prevRTV;
    ComPtr<ID3D11DepthStencilView> prevDSV;
    prevRTV.Attach(prevRTVRaw);
    prevDSV.Attach(prevDSVRaw);

        D3D11_VIEWPORT prevVP = {};
    UINT vpCount = 1;
        dc->RSGetViewports(&vpCount, &prevVP);

        ID3D11RasterizerState* prevRasterStateRaw = nullptr;
        dc->RSGetState(&prevRasterStateRaw);
        ComPtr<ID3D11RasterizerState> prevRasterState;
    prevRasterState.Attach(prevRasterStateRaw);

        D3D11_VIEWPORT shadowVP = {};
        shadowVP.TopLeftX = 0.0f;
        shadowVP.TopLeftY = 0.0f;
        shadowVP.Width    = static_cast<float>(m_ShadowMapSize);
        shadowVP.Height   = static_cast<float>(m_ShadowMapSize);
        shadowVP.MinDepth = 0.0f;
        shadowVP.MaxDepth = 1.0f;

        dc->RSSetState(m_ShadowRasterState.Get());
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11ShaderResourceView* nullSrvs[8] = {};
        dc->PSSetShaderResources(0, 8, nullSrvs);

        if (m_DirectionalShadowEnabled) {
            const uint32_t cascadeCount = (std::max)(1u, m_CascadeCount);
            for (uint32_t cascade = 0; cascade < cascadeCount && cascade < kMaxCascades; ++cascade) {
                dc->OMSetRenderTargets(0, nullptr, m_ShadowDSV[cascade].Get());
                dc->ClearDepthStencilView(
                    m_ShadowDSV[cascade].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
                dc->RSSetViewports(1, &shadowVP);
                drawShadowScene(m_LightViewProjCascade[cascade]);
            }
        }
        if (m_SpotShadowIndex >= 0) {
            dc->OMSetRenderTargets(0, nullptr, m_SpotShadowDSV.Get());
            dc->ClearDepthStencilView(m_SpotShadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
            dc->RSSetViewports(1, &shadowVP);
            drawShadowScene(m_SpotLightViewProj);
        }
        if (m_PointShadowIndex >= 0) {
            for (int face = 0; face < 6; ++face) {
                dc->OMSetRenderTargets(0, nullptr, m_PointShadowDSV[face].Get());
                dc->ClearDepthStencilView(
                    m_PointShadowDSV[face].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
                dc->RSSetViewports(1, &shadowVP);
                drawShadowScene(m_PointLightViewProj[face]);
            }
        }

        ID3D11RenderTargetView* prevRTVPtr = prevRTV.Get();
    if (prevRTVPtr) {
        dc->OMSetRenderTargets(1, &prevRTVPtr, prevDSV.Get());
    } else {
        dc->OMSetRenderTargets(0, nullptr, prevDSV.Get());
    }
    if (vpCount > 0) {
        dc->RSSetViewports(1, &prevVP);
    }
        dc->RSSetState(prevRasterState.Get());
        return;
    }

    if (!EnsureShadowResourcesD3D12()) return;

    d3d12->BindDepthOnlyShader(m_ShadowShaderHandle->shader.get());
    d3d12->SetRasterState(true, false);
    cmd->SetViewport(0.0f, 0.0f, shadowMapSize, shadowMapSize);

    if (m_DirectionalShadowEnabled) {
        for (uint32_t cascade = 0; cascade < m_CascadeCount; ++cascade) {
            d3d12->PushRenderTarget(nullptr, m_ShadowCascadeDsvD3D12[cascade]);
            d3d12->GetCommandList()->ClearDepthStencilView(
                m_ShadowCascadeDsvD3D12[cascade],
                D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            drawShadowScene(m_LightViewProjCascade[cascade]);
            d3d12->PopRenderTarget();
        }
    }
    if (m_SpotShadowIndex >= 0) {
        d3d12->PushRenderTarget(nullptr, m_SpotShadowDsvD3D12);
        d3d12->GetCommandList()->ClearDepthStencilView(
            m_SpotShadowDsvD3D12, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        drawShadowScene(m_SpotLightViewProj);
        d3d12->PopRenderTarget();
    }
    if (m_PointShadowIndex >= 0) {
        for (int face = 0; face < 6; ++face) {
            d3d12->PushRenderTarget(nullptr, m_PointShadowDsvD3D12[face]);
            d3d12->GetCommandList()->ClearDepthStencilView(
                m_PointShadowDsvD3D12[face],
                D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            drawShadowScene(m_PointLightViewProj[face]);
            d3d12->PopRenderTarget();
        }
    }

    d3d12->BindShader(m_ShadowShaderHandle->shader.get());
    d3d12->SetRasterState(false, false);
#else
    (void)scene;
    (void)camera;
#endif
}
