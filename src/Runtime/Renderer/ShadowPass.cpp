#include "Renderer/ShadowPass.h"

#include "Assets/MeshAsset.h"
#include "Core/Logger.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"
#include "Scene/MeshRendererComponent.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

#ifdef MYENGINE_PLATFORM_WINDOWS
#include "Renderer/D3D11Context.h"
#include "ShaderBytecodeWindows.h"
#endif

namespace {

// Position-only layout is enough for depth-only shadow map pass.
const VertexElement k_ShadowVertexLayout[] = {
    { "POSITION", 0, VertexFormat::Float3, offsetof(MeshVertex, position) },
};

struct ShadowPerDrawConstants {
    float lightMvp[16];
};

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

} // namespace

ShadowPass::ShadowPass(IRenderContext* context)
    : RenderPass(context)
{}

void ShadowPass::Resize(uint32_t width, uint32_t height)
{
    const uint32_t maxDim = (width > height) ? width : height;
    const uint32_t target = (maxDim < 1024u) ? 1024u : maxDim;
    if (target == m_ShadowMapSize) return;

    m_ShadowMapSize = target;
#ifdef MYENGINE_PLATFORM_WINDOWS
    m_ShadowDepthTexture.Reset();
    m_ShadowDSV.Reset();
    m_ShadowSRV.Reset();
    m_ShadowSampler.Reset();
    m_ShadowMapTexture.reset();
#endif
}

void ShadowPass::UpdateLightMatrices(const Scene& scene)
{
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
        auto* mr = actor.GetComponent<MeshRendererComponent>();
        if (!mr || !mr->IsValid()) return;

        MeshAsset* mesh = mr->GetMesh().Get();
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
        m_LightViewProj = lightView * lightProj;
        return;
    }

    const Vec3 center = (sceneMin + sceneMax) * 0.5f;
    const Vec3 half = (sceneMax - sceneMin) * 0.5f;
    const float halfMax = (std::max)(half.x, (std::max)(half.y, half.z));
    const float baseExtent = (std::max)(4.0f, halfMax);

    const Vec3 eye = center - m_LightDirection * (baseExtent * 3.0f);
    const Mat4 lightView = Mat4::LookAt(eye, center, Vec3::Up());

    float minX = kInf, minY = kInf, minZ = kInf;
    float maxX = -kInf, maxY = -kInf, maxZ = -kInf;

    const Vec3 corners[8] = {
        { sceneMin.x, sceneMin.y, sceneMin.z },
        { sceneMax.x, sceneMin.y, sceneMin.z },
        { sceneMin.x, sceneMax.y, sceneMin.z },
        { sceneMax.x, sceneMax.y, sceneMin.z },
        { sceneMin.x, sceneMin.y, sceneMax.z },
        { sceneMax.x, sceneMin.y, sceneMax.z },
        { sceneMin.x, sceneMax.y, sceneMax.z },
        { sceneMax.x, sceneMax.y, sceneMax.z },
    };

    for (const Vec3& c : corners) {
        const Vec3 p = lightView.TransformPoint(c);
        if (p.x < minX) minX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.z < minZ) minZ = p.z;
        if (p.x > maxX) maxX = p.x;
        if (p.y > maxY) maxY = p.y;
        if (p.z > maxZ) maxZ = p.z;
    }

    const float xySpan = (std::max)(maxX - minX, maxY - minY);
    const float xyPad = (std::max)(0.5f, xySpan * 0.1f);
    const float zPad = (std::max)(1.0f, (maxZ - minZ) * 0.2f);

    const float left = minX - xyPad;
    const float right = maxX + xyPad;
    const float bottom = minY - xyPad;
    const float top = maxY + xyPad;

    float nearZ = minZ - zPad;
    float farZ  = maxZ + zPad;
    if (nearZ < 0.1f) nearZ = 0.1f;
    if (farZ <= nearZ + 0.1f) farZ = nearZ + 0.1f;

    const Mat4 lightProj = Mat4::Ortho(left, right, bottom, top, nearZ, farZ);
    m_LightViewProj = lightView * lightProj;
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
                k_ShadowVertexLayout, 1);
        }
    } else {
        if (!m_ShadowShaderHandle) {
            m_ShadowShaderHandle = std::make_shared<ShaderHandle>();
            m_ShadowShaderHandle->shader = Context()->CreateShaderFromBytecode(
                k_ShadowDepthVsBytecode, k_ShadowDepthVsBytecodeSize,
                k_ShadowDepthPsBytecode, k_ShadowDepthPsBytecodeSize,
                k_ShadowVertexLayout,
                1);
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
    if (m_ShadowDepthTexture && m_ShadowDSV && m_ShadowSRV &&
        m_ShadowSampler && m_ShadowRasterState && m_ShadowMapTexture) {
        return true;
    }

    ID3D11Device* device = d3d11->GetDevice();
    if (!device) return false;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width              = m_ShadowMapSize;
    texDesc.Height             = m_ShadowMapSize;
    texDesc.MipLevels          = 1;
    texDesc.ArraySize          = 1;
    texDesc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
    texDesc.SampleDesc.Count   = 1;
    texDesc.Usage              = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &m_ShadowDepthTexture);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateTexture2D failed");
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    hr = device->CreateDepthStencilView(m_ShadowDepthTexture.Get(), &dsvDesc, &m_ShadowDSV);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateDepthStencilView failed");
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    hr = device->CreateShaderResourceView(m_ShadowDepthTexture.Get(), &srvDesc, &m_ShadowSRV);
    if (FAILED(hr)) {
        Logger::Error("[ShadowPass] CreateShaderResourceView failed");
        return false;
    }

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter         = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
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

    return true;
}
#endif

void ShadowPass::Execute(const Scene& scene, const Camera&)
{
    if (!Context()) return;
    EnsureShadowShader();
    if (!m_ShadowShaderHandle || !m_ShadowShaderHandle->shader) return;
    m_ShadowShaderVersion = m_ShadowShaderHandle->version;

    UpdateLightMatrices(scene);

#ifdef MYENGINE_PLATFORM_WINDOWS
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    if (!d3d11) return;
    if (!EnsureShadowResourcesD3D11()) return;

    ID3D11DeviceContext* dc = d3d11->GetDeviceContext();
    if (!dc) return;
    GpuCommandList* cmd = Context()->GetGraphicsCommandList();
    if (!cmd) return;

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

    // A texture cannot stay bound as SRV while being rebound as the shadow DSV.
    ID3D11ShaderResourceView* nullSrv = nullptr;
    dc->PSSetShaderResources(1, 1, &nullSrv);

    dc->OMSetRenderTargets(0, nullptr, m_ShadowDSV.Get());
    dc->ClearDepthStencilView(m_ShadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT shadowVP = {};
    shadowVP.TopLeftX = 0.0f;
    shadowVP.TopLeftY = 0.0f;
    shadowVP.Width    = static_cast<float>(m_ShadowMapSize);
    shadowVP.Height   = static_cast<float>(m_ShadowMapSize);
    shadowVP.MinDepth = 0.0f;
    shadowVP.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &shadowVP);
    dc->RSSetState(m_ShadowRasterState.Get());
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    scene.ForEach([&](Actor& actor) {
        if (!actor.IsActive()) return;
        auto* mr = actor.GetComponent<MeshRendererComponent>();
        if (!mr || !mr->IsValid()) return;

        MeshAsset* mesh = mr->GetMesh().Get();
        if (!mesh) return;
        EnsureMeshUploaded(Context(), mesh);
        if (!mesh->GetVertexBuffer()) return;

        const Mat4 world = actor.GetWorldMatrix();
        const Mat4 lightMvp = world * m_LightViewProj;

        ShadowPerDrawConstants constants{};
        std::memcpy(constants.lightMvp, lightMvp.Data(), sizeof(constants.lightMvp));

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
#else
    (void)scene;
#endif
}
