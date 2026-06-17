#include "Renderer/PostProcessPass.h"

#include "Assets/MeshAsset.h"
#include "Camera/Camera.h"
#include "Core/Logger.h"
#include "Math/Mat4Inverse.h"
#include "Renderer/D3D11Context.h"
#include "Renderer/D3D12Context.h"
#include "Renderer/MeshShader.h"
#include "Renderer/PostProcessComponent.h"
#include "Renderer/ShaderManager.h"
#include "Scene/Actor.h"
#include "ShaderBytecodeWindows.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace {

// --------------------------------------------------------------------------
// PostProcess constants (FXAA compositing pass)
// --------------------------------------------------------------------------
struct PostProcessConstants {
    float params[4];      // exposure, gamma, toneMapping, vignette
    float params2[4];     // saturation, contrast, fxaaEnabled, fxaaQuality
    float screenSize[4];  // 1/w, 1/h, w, h
};

PostProcessConstants CollectPostProcessParams(const Scene& scene, uint32_t width, uint32_t height)
{
    PostProcessConstants out{};
    out.params[0]  = 1.0f;
    out.params[1]  = 2.2f;
    out.params[2]  = 1.0f;
    out.params[3]  = 0.0f;
    out.params2[0] = 1.0f;
    out.params2[1] = 1.0f;
    out.params2[2] = 1.0f;   // FXAA on by default
    out.params2[3] = 1.0f;
    out.screenSize[0] = 1.0f / static_cast<float>(width);
    out.screenSize[1] = 1.0f / static_cast<float>(height);
    out.screenSize[2] = static_cast<float>(width);
    out.screenSize[3] = static_cast<float>(height);

    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive()) return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled()) return;
        out.params[0]  = post->GetExposure();
        out.params[1]  = post->GetGamma();
        out.params[2]  = post->IsToneMappingEnabled() ? 1.0f : 0.0f;
        out.params[3]  = post->GetVignette();
        out.params2[0] = post->GetSaturation();
        out.params2[1] = post->GetContrast();
        out.params2[2] = post->GetAntiAliasingStrength() > 0.0f ? 1.0f : 0.0f;
        out.params2[3] = post->GetAntiAliasingStrength();
        found = true;
    });
    return out;
}

// --------------------------------------------------------------------------
// SSAO constants
// --------------------------------------------------------------------------
struct SSAOConstants {
    float projection[16];     // row-major projection
    float invProjection[16];  // row-major inverse projection
    float screenSize[4];      // (1/w, 1/h, w, h)
    float ssaoParams[4];      // (radius, bias, power, intensity)
    float samples[64][4];     // hemisphere kernel (xyz, 0)
};

static constexpr int kSSAOKernelSize = 16;

SSAOConstants BuildSSAOConstants(
    const Scene& scene, const Camera& camera, uint32_t width, uint32_t height)
{
    SSAOConstants out{};

    // Row-major: Mat4::Data() = float[4][4] stored row-major in memory,
    // which maps 1:1 to HLSL row_major float4x4.
    std::memcpy(out.projection, camera.GetProj().Data(), sizeof(out.projection));

    Mat4 invProj;
    Mat4Invert(camera.GetProj(), invProj);
    std::memcpy(out.invProjection, invProj.Data(), sizeof(out.invProjection));

    out.screenSize[0] = 1.0f / static_cast<float>(width);
    out.screenSize[1] = 1.0f / static_cast<float>(height);
    out.screenSize[2] = static_cast<float>(width);
    out.screenSize[3] = static_cast<float>(height);

    out.ssaoParams[0] = 1.2f;   // radius
    out.ssaoParams[1] = 0.025f; // bias
    out.ssaoParams[2] = 1.5f;   // power
    out.ssaoParams[3] = 0.0f;   // intensity

    bool found = false;
    scene.ForEach([&](Actor& actor) {
        if (found || !actor.IsActive()) return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled()) return;
        out.ssaoParams[0] = post->GetSSAORadius();
        out.ssaoParams[1] = post->GetSSAOBias();
        out.ssaoParams[2] = post->GetSSAOPower();
        out.ssaoParams[3] = post->GetSSAOIntensity();
        found = true;
    });

    // Generate hemisphere sample kernel.
    // Orient samples in the hemisphere around +Z (tangent-space normal direction).
    // Use stratified Hammersley-like distribution: more samples near centre.
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < kSSAOKernelSize; ++i) {
        // Cosine-weighted hemisphere sampling
        float u1 = dist(rng);
        float u2 = dist(rng);
        float r  = std::sqrt(u1);
        float theta = 2.0f * 3.14159265f * u2;

        float x = r * std::cos(theta);
        float y = r * std::sin(theta);
        float z = std::sqrt((std::max)(0.0f, 1.0f - u1));

        // Scale: more samples near centre, fewer at edge
        float t   = static_cast<float>(i) / static_cast<float>(kSSAOKernelSize);
        float scl = 0.1f + t * t * 0.9f;

        out.samples[i][0] = x * scl;
        out.samples[i][1] = y * scl;
        out.samples[i][2] = z * scl;
        out.samples[i][3] = 0.0f;
    }

    return out;
}

// --------------------------------------------------------------------------
// SSAO blur constants
// --------------------------------------------------------------------------
struct SSAOBlurConstants {
    float texelSize[4];   // (1/w, isVertical, 1/h, 0)
};

} // namespace

// ==========================================================================
// PostProcessPass
// ==========================================================================

PostProcessPass::PostProcessPass(IRenderContext* context)
    : RenderPass(context)
{}

PostProcessPass::~PostProcessPass()
{
    ReleaseOffscreenRT();
}

void PostProcessPass::ReleaseOffscreenRT()
{
    m_CompositeSRV.Reset();
    m_CompositeRTV.Reset();
    m_CompositeTex.Reset();

    m_OffscreenSRV.Reset();
    m_OffscreenRTV.Reset();
    m_OffscreenTex.Reset();

    m_OffscreenDepthSRV.Reset();
    m_OffscreenDSV.Reset();
    m_OffscreenDepthTex.Reset();

    m_SSAOBlurSRV.Reset();
    m_SSAOBlurRTV.Reset();
    m_SSAOBlurTex.Reset();

    m_SSAOSRV.Reset();
    m_SSAORTV.Reset();
    m_SSAOTex.Reset();

    m_NoiseSRV.Reset();
    m_NoiseTex.Reset();

    m_ComposeSampler.Reset();
    m_PointClampSampler.Reset();
    m_NoiseSampler.Reset();

    m_D3D12OffscreenTex.Reset();
    m_D3D12OffscreenRTV = {};
    m_D3D12OffscreenSRV = {};
    m_D3D12OffscreenSRVGpu = {};
    m_D3D12OffscreenSampler = {};
    m_D3D12OffscreenSamplerGpu = {};
    m_D3D12OffscreenDepth.reset();
    m_D3D12OffscreenState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_D3D12CompositeTex.Reset();
    m_D3D12CompositeRTV = {};
    m_D3D12CompositeSRV = {};
    m_D3D12CompositeSRVGpu = {};
    m_D3D12CompositeState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_D3D12WhiteTex.Reset();
    m_D3D12WhiteSRV = {};
    m_D3D12WhiteSRVGpu = {};
    m_D3D12WhiteTexture.reset();

    m_OffscreenWidth  = 0;
    m_OffscreenHeight = 0;
}

void PostProcessPass::EnsureOffscreenRT(uint32_t w, uint32_t h)
{
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    auto* d3d12 = dynamic_cast<D3D12Context*>(Context());
    if (!d3d11 && !d3d12) return;

    if (m_OffscreenWidth == w && m_OffscreenHeight == h &&
        ((d3d11 && m_OffscreenTex) || (d3d12 && m_D3D12OffscreenTex))) {
        return;
    }

    if (d3d12 && m_D3D12OffscreenTex) {
        d3d12->WaitForGpuIdle();
    }

    ReleaseOffscreenRT();

    if (d3d12) {
        ID3D12Device* device = d3d12->GetDevice();
        if (!device) return;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = w;
        texDesc.Height = h;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = texDesc.Format;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 1.0f;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
            IID_PPV_ARGS(&m_D3D12OffscreenTex));
        if (FAILED(hr)) {
            Logger::Error("[PostProcessPass] D3D12 CreateCommittedResource for offscreen RT failed");
            ReleaseOffscreenRT();
            return;
        }

        m_D3D12OffscreenRTV = d3d12->AllocRtvSlot();
        device->CreateRenderTargetView(m_D3D12OffscreenTex.Get(), nullptr, m_D3D12OffscreenRTV);

        m_D3D12OffscreenSRV = d3d12->AllocSrvSlot(m_D3D12OffscreenSRVGpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = texDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_D3D12OffscreenTex.Get(), &srvDesc, m_D3D12OffscreenSRV);

        hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
            IID_PPV_ARGS(&m_D3D12CompositeTex));
        if (FAILED(hr)) {
            Logger::Error("[PostProcessPass] D3D12 CreateCommittedResource for composite RT failed");
            ReleaseOffscreenRT();
            return;
        }
        m_D3D12CompositeRTV = d3d12->AllocRtvSlot();
        device->CreateRenderTargetView(m_D3D12CompositeTex.Get(), nullptr, m_D3D12CompositeRTV);
        m_D3D12CompositeSRV = d3d12->AllocSrvSlot(m_D3D12CompositeSRVGpu);
        device->CreateShaderResourceView(m_D3D12CompositeTex.Get(), &srvDesc, m_D3D12CompositeSRV);

        m_D3D12OffscreenSampler = d3d12->AllocSampSlot(m_D3D12OffscreenSamplerGpu);
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        device->CreateSampler(&samplerDesc, m_D3D12OffscreenSampler);

        const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
        m_D3D12WhiteTexture = d3d12->UploadTexture2D(whitePixel, 1, 1);
        if (!m_D3D12WhiteTexture) {
            Logger::Error("[PostProcessPass] D3D12 white SSAO texture upload failed");
            ReleaseOffscreenRT();
            return;
        }
        if (auto* whiteTex = static_cast<D3D12Texture*>(m_D3D12WhiteTexture.get())) {
            m_D3D12WhiteSRVGpu = whiteTex->srvGpu;
        }

        m_D3D12OffscreenDepth = d3d12->CreateDepthTexture(
            static_cast<int>(w), static_cast<int>(h), false);
        if (!m_D3D12OffscreenDepth) {
            Logger::Error("[PostProcessPass] D3D12 offscreen depth creation failed");
            ReleaseOffscreenRT();
            return;
        }

        m_D3D12OffscreenState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_D3D12CompositeState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_OffscreenWidth = w;
        m_OffscreenHeight = h;
        Logger::Info("[PostProcessPass] D3D12 offscreen RT + depth created: ", w, "x", h);
        return;
    }

    ID3D11Device* device = d3d11->GetDevice();
    if (!device) return;

    // ---- Offscreen color RT (R16G16B16A16_FLOAT) ----
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width              = w;
    texDesc.Height             = h;
    texDesc.MipLevels          = 1;
    texDesc.ArraySize          = 1;
    texDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count   = 1;
    texDesc.Usage              = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr, &m_OffscreenTex);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateTexture2D for offscreen RT failed");
        ReleaseOffscreenRT();
        return;
    }

    hr = device->CreateRenderTargetView(m_OffscreenTex.Get(), nullptr, &m_OffscreenRTV);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateRenderTargetView failed");
        ReleaseOffscreenRT();
        return;
    }

    hr = device->CreateShaderResourceView(m_OffscreenTex.Get(), nullptr, &m_OffscreenSRV);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateShaderResourceView failed");
        ReleaseOffscreenRT();
        return;
    }

    hr = device->CreateTexture2D(&texDesc, nullptr, &m_CompositeTex);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateTexture2D for composite RT failed");
        ReleaseOffscreenRT();
        return;
    }

    hr = device->CreateRenderTargetView(m_CompositeTex.Get(), nullptr, &m_CompositeRTV);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateRenderTargetView for composite RT failed");
        ReleaseOffscreenRT();
        return;
    }

    hr = device->CreateShaderResourceView(m_CompositeTex.Get(), nullptr, &m_CompositeSRV);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateShaderResourceView for composite RT failed");
        ReleaseOffscreenRT();
        return;
    }

    // ---- Offscreen depth (R24G8_TYPELESS, readable as SRV for SSAO) ----
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width              = w;
    depthDesc.Height             = h;
    depthDesc.MipLevels          = 1;
    depthDesc.ArraySize          = 1;
    depthDesc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count   = 1;
    depthDesc.Usage              = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    hr = device->CreateTexture2D(&depthDesc, nullptr, &m_OffscreenDepthTex);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateTexture2D for offscreen depth failed");
        ReleaseOffscreenRT();
        return;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    hr = device->CreateDepthStencilView(m_OffscreenDepthTex.Get(), &dsvDesc, &m_OffscreenDSV);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateDepthStencilView failed");
        ReleaseOffscreenRT();
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format                    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels       = 1;
    depthSrvDesc.Texture2D.MostDetailedMip = 0;
    hr = device->CreateShaderResourceView(m_OffscreenDepthTex.Get(), &depthSrvDesc, &m_OffscreenDepthSRV);
    if (FAILED(hr)) {
        Logger::Error("[PostProcessPass] CreateShaderResourceView for depth failed");
        ReleaseOffscreenRT();
        return;
    }

    // ---- Samplers ----
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MaxAnisotropy  = 1;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampDesc.MaxLOD         = 0.0f;
    device->CreateSamplerState(&sampDesc, &m_ComposeSampler);

    D3D11_SAMPLER_DESC pointDesc = {};
    pointDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
    pointDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    pointDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    pointDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    pointDesc.MaxAnisotropy  = 1;
    pointDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    pointDesc.MaxLOD         = 0.0f;
    device->CreateSamplerState(&pointDesc, &m_PointClampSampler);

    m_OffscreenWidth  = w;
    m_OffscreenHeight = h;

    EnsureSSAOResources(w, h);
    CreateNoiseTexture();

    Logger::Info("[PostProcessPass] Offscreen RT + depth created: ", w, "x", h);
}

void PostProcessPass::EnsureSSAOResources(uint32_t w, uint32_t h)
{
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    if (!d3d11) return;
    ID3D11Device* device = d3d11->GetDevice();
    if (!device) return;

    // SSAO RT (R8_UNORM)
    if (!m_SSAOTex || !m_SSAORTV || !m_SSAOSRV) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = w;
        desc.Height           = h;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_SSAOTex);
        if (SUCCEEDED(hr)) {
            device->CreateRenderTargetView(m_SSAOTex.Get(), nullptr, &m_SSAORTV);
            device->CreateShaderResourceView(m_SSAOTex.Get(), nullptr, &m_SSAOSRV);
        }
    }

    // SSAO blur RT (R8_UNORM)
    if (!m_SSAOBlurTex || !m_SSAOBlurRTV || !m_SSAOBlurSRV) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = w;
        desc.Height           = h;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_SSAOBlurTex);
        if (SUCCEEDED(hr)) {
            device->CreateRenderTargetView(m_SSAOBlurTex.Get(), nullptr, &m_SSAOBlurRTV);
            device->CreateShaderResourceView(m_SSAOBlurTex.Get(), nullptr, &m_SSAOBlurSRV);
        }
    }
}

void PostProcessPass::CreateNoiseTexture()
{
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    if (!d3d11 || m_NoiseTex) return;
    ID3D11Device* device = d3d11->GetDevice();
    if (!device) return;

    // 4x4 noise texture: random vec3 in [0,1], shader remaps to [-1,1]
    constexpr int kNoiseSize = 4;
    float noiseData[kNoiseSize * kNoiseSize * 4] = {};

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < kNoiseSize * kNoiseSize; ++i) {
        noiseData[i * 4 + 0] = dist(rng);
        noiseData[i * 4 + 1] = dist(rng);
        noiseData[i * 4 + 2] = dist(rng);
        noiseData[i * 4 + 3] = 1.0f;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = kNoiseSize;
    desc.Height           = kNoiseSize;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem     = noiseData;
    sd.SysMemPitch = kNoiseSize * 16; // 4 floats * 4 bytes

    HRESULT hr = device->CreateTexture2D(&desc, &sd, &m_NoiseTex);
    if (SUCCEEDED(hr)) {
        device->CreateShaderResourceView(m_NoiseTex.Get(), nullptr, &m_NoiseSRV);

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.MaxAnisotropy  = 1;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sampDesc.MaxLOD         = 0.0f;
        device->CreateSamplerState(&sampDesc, &m_NoiseSampler);
    }
}

// --------------------------------------------------------------------------
// Shader creation helpers
// --------------------------------------------------------------------------

GpuShader* PostProcessPass::GetOrCreateFXAAShader()
{
    auto handle = ShaderManager::Get().GetOrCreate(
        "src/Runtime/Renderer/Shaders/PostProcessFXAA.hlsl",
        "VSMain", "PSMain",
        nullptr, 0);
    m_FXAAHandle = handle;
    return handle ? handle->shader.get() : nullptr;
}

GpuShader* PostProcessPass::GetOrCreateSSAOShader()
{
    auto handle = ShaderManager::Get().GetOrCreate(
        "src/Runtime/Renderer/Shaders/PostProcessSSAO.hlsl",
        "VSMain", "PSMain",
        nullptr, 0);
    m_SSAOHandle = handle;
    return handle ? handle->shader.get() : nullptr;
}

GpuShader* PostProcessPass::GetOrCreateSSAOBlurShader()
{
    auto handle = ShaderManager::Get().GetOrCreate(
        "src/Runtime/Renderer/Shaders/PostProcessSSAOBlur.hlsl",
        "VSMain", "PSMain",
        nullptr, 0);
    m_SSAOBlurHandle = handle;
    return handle ? handle->shader.get() : nullptr;
}

// --------------------------------------------------------------------------
// Offscreen begin
// --------------------------------------------------------------------------

void PostProcessPass::BeginOffscreen()
{
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    auto* d3d12 = dynamic_cast<D3D12Context*>(Context());
    if (!d3d11 && !d3d12) return;

    if (d3d12) {
        EnsureOffscreenRT(m_OffscreenWidth, m_OffscreenHeight);
        if (!m_D3D12OffscreenTex || !m_D3D12OffscreenDepth) return;

        ID3D12GraphicsCommandList* cmd = d3d12->GetCommandList();
        if (!cmd) return;

        if (m_D3D12OffscreenState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_D3D12OffscreenTex.Get();
            barrier.Transition.StateBefore = m_D3D12OffscreenState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            m_D3D12OffscreenState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        auto* depth = static_cast<D3D12Texture*>(m_D3D12OffscreenDepth.get());
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = depth ? depth->dsvCpu : D3D12_CPU_DESCRIPTOR_HANDLE{};
        d3d12->PushRenderTarget(&m_D3D12OffscreenRTV, dsv);
        d3d12->SetViewport(0.0f, 0.0f,
                           static_cast<float>(m_OffscreenWidth),
                           static_cast<float>(m_OffscreenHeight));

        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        cmd->ClearRenderTargetView(m_D3D12OffscreenRTV, clearColor, 0, nullptr);
        if (dsv.ptr != 0) {
            cmd->ClearDepthStencilView(
                dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        }
        return;
    }

    ID3D11DeviceContext* dc = d3d11->GetDeviceContext();
    if (!dc) return;

    // Save backbuffer render target state
    m_SavedRTV.Reset();
    m_SavedDSV.Reset();
    dc->OMGetRenderTargets(1, m_SavedRTV.GetAddressOf(), m_SavedDSV.GetAddressOf());

    // Save viewport
    UINT vpCount = 1;
    dc->RSGetViewports(&vpCount, &m_SavedViewport);
    m_ViewportSaved = (vpCount > 0);

    // Set offscreen viewport
    D3D11_VIEWPORT offscreenVP = {};
    offscreenVP.TopLeftX = 0.0f;
    offscreenVP.TopLeftY = 0.0f;
    offscreenVP.Width    = static_cast<float>(m_OffscreenWidth);
    offscreenVP.Height   = static_cast<float>(m_OffscreenHeight);
    offscreenVP.MinDepth = 0.0f;
    offscreenVP.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &offscreenVP);

    // Bind offscreen RT with own depth DSV
    ID3D11RenderTargetView* rtv = m_OffscreenRTV.Get();
    dc->OMSetRenderTargets(1, &rtv, m_OffscreenDSV.Get());

    // Clear offscreen RT and depth
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    dc->ClearRenderTargetView(rtv, clearColor);
    dc->ClearDepthStencilView(m_OffscreenDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

// --------------------------------------------------------------------------
// SSAO
// --------------------------------------------------------------------------

void PostProcessPass::RenderSSAO(const Scene& scene, const Camera& camera)
{
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    if (!d3d11) return;

    ID3D11DeviceContext* dc = d3d11->GetDeviceContext();
    if (!dc) return;

    // Bind SSAO RT
    ID3D11RenderTargetView* rtv = m_SSAORTV.Get();
    if (!rtv) return;
    dc->OMSetRenderTargets(1, &rtv, nullptr);

    const float aoClear[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    dc->ClearRenderTargetView(rtv, aoClear);

    GpuShader* ssaoShader = GetOrCreateSSAOShader();
    if (!ssaoShader || !m_OffscreenDepthSRV || !m_NoiseSRV ||
        !m_PointClampSampler || !m_NoiseSampler) {
        if (!ssaoShader) {
            Logger::Error("[PostProcessPass] SSAO shader not available");
        }
        return;
    }

    // Build SSAO constants
    SSAOConstants ssaoConsts = BuildSSAOConstants(
        scene, camera, m_OffscreenWidth, m_OffscreenHeight);

    // Keep viewport from BeginOffscreen (0, 0, offscreenW, offscreenH)

    d3d11->BindShader(ssaoShader);

    // Bind depth SRV + noise SRV
    ID3D11ShaderResourceView* srvs[2] = {
        m_OffscreenDepthSRV.Get(),
        m_NoiseSRV.Get()
    };
    dc->PSSetShaderResources(0, 2, srvs);

    ID3D11SamplerState* samplers[2] = {
        m_PointClampSampler.Get(),
        m_NoiseSampler.Get()
    };
    dc->PSSetSamplers(0, 2, samplers);

    d3d11->SetVSConstants(&ssaoConsts, sizeof(ssaoConsts));

    // Fullscreen triangle
    dc->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    dc->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->Draw(3, 0);

    // Unbind SRVs
    ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
    dc->PSSetShaderResources(0, 2, nullSrvs);

    // ---- SSAO blur pass (horizontal + vertical ping-pong) ----
    GpuShader* blurShader = GetOrCreateSSAOBlurShader();
    if (!blurShader) return;

    // Horizontal blur: read SSAO RT, write blur RT
    ID3D11RenderTargetView* blurRtv = m_SSAOBlurRTV.Get();
    dc->OMSetRenderTargets(1, &blurRtv, nullptr);

    d3d11->BindShader(blurShader);

    ID3D11ShaderResourceView* blurSrvs[1] = { m_SSAOSRV.Get() };
    dc->PSSetShaderResources(0, 1, blurSrvs);
    dc->PSSetSamplers(0, 1, &samplers[0]); // point-clamp

    SSAOBlurConstants blurConsts = {};
    blurConsts.texelSize[0] = 1.0f / static_cast<float>(m_OffscreenWidth);
    blurConsts.texelSize[2] = 1.0f / static_cast<float>(m_OffscreenHeight);
    blurConsts.texelSize[1] = 0.0f; // horizontal
    d3d11->SetVSConstants(&blurConsts, sizeof(blurConsts));

    dc->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv[1] = { nullptr };
    dc->PSSetShaderResources(0, 1, nullSrv);

    // Vertical blur: read blur RT, write back to SSAO RT
    dc->OMSetRenderTargets(1, &rtv, nullptr);

    ID3D11ShaderResourceView* blurSrvs2[1] = { m_SSAOBlurSRV.Get() };
    dc->PSSetShaderResources(0, 1, blurSrvs2);

    blurConsts.texelSize[1] = 1.0f; // vertical
    d3d11->SetVSConstants(&blurConsts, sizeof(blurConsts));

    dc->Draw(3, 0);

    // Unbind SRV
    dc->PSSetShaderResources(0, 1, nullSrv);
}

void PostProcessPass::RenderBloom(const Scene& scene)
{
    bool enabled = false;
    scene.ForEach([&](Actor& actor) {
        if (enabled || !actor.IsActive()) return;
        auto* post = actor.GetComponent<PostProcessComponent>();
        if (!post || !post->IsEnabled()) return;
        enabled = post->IsBloomEnabled() && post->GetBloomIntensity() > 0.0f;
    });

    if (!enabled) return;

    // Bloom settings are serialized and exposed on the component, but the
    // render resources/shader pass are not present in this update yet.
}

// --------------------------------------------------------------------------
// Composite back to backbuffer
// --------------------------------------------------------------------------

void PostProcessPass::EndOffscreenAndComposite(const Scene& scene)
{
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    auto* d3d12 = dynamic_cast<D3D12Context*>(Context());
    if (!d3d11 && !d3d12) return;

    if (d3d12) {
        d3d12->PopRenderTarget();
        ID3D12GraphicsCommandList* cmd = d3d12->GetCommandList();
        if (!cmd || !m_D3D12OffscreenTex || !m_D3D12CompositeTex) return;

        if (cmd && m_D3D12OffscreenTex &&
            m_D3D12OffscreenState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_D3D12OffscreenTex.Get();
            barrier.Transition.StateBefore = m_D3D12OffscreenState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            m_D3D12OffscreenState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_D3D12CompositeState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_D3D12CompositeTex.Get();
            barrier.Transition.StateBefore = m_D3D12CompositeState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            m_D3D12CompositeState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        if (!m_D3D12FXAAShader) {
            m_D3D12FXAAShader = d3d12->CreateFullscreenShaderFromBytecode(
                k_PostProcessFXAAVsBytecode,
                k_PostProcessFXAAVsBytecodeSize,
                k_PostProcessFXAAPsBytecode,
                k_PostProcessFXAAPsBytecodeSize,
                DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
        if (!m_D3D12FXAAShader) {
            Logger::Error("[PostProcessPass] D3D12 FXAA shader not available");
            return;
        }

        d3d12->PushRenderTarget(&m_D3D12CompositeRTV, {});
        d3d12->SetViewport(0.0f, 0.0f,
                           static_cast<float>(m_OffscreenWidth),
                           static_cast<float>(m_OffscreenHeight));
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        cmd->ClearRenderTargetView(m_D3D12CompositeRTV, clearColor, 0, nullptr);

        d3d12->BindShader(m_D3D12FXAAShader.get());
        d3d12->BindPSTextureDescriptors(
            0, m_D3D12OffscreenSRVGpu, m_D3D12OffscreenSamplerGpu);
        d3d12->BindPSTextureDescriptors(
            1, m_D3D12WhiteSRVGpu, m_D3D12OffscreenSamplerGpu);

        PostProcessConstants constants = CollectPostProcessParams(
            scene, m_OffscreenWidth, m_OffscreenHeight);
        d3d12->SetVSConstants(&constants, sizeof(constants));
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);

        d3d12->PopRenderTarget();

        if (m_D3D12CompositeState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_D3D12CompositeTex.Get();
            barrier.Transition.StateBefore = m_D3D12CompositeState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            m_D3D12CompositeState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        return;
    }

    ID3D11DeviceContext* dc = d3d11->GetDeviceContext();
    if (!dc) return;

    auto restoreSavedViewport = [&]() {
        if (m_ViewportSaved) {
            dc->RSSetViewports(1, &m_SavedViewport);
            m_ViewportSaved = false;
        }
    };
    auto clearSavedTargets = [&]() {
        m_SavedRTV.Reset();
        m_SavedDSV.Reset();
    };

    if (m_CompositeToBackbuffer) {
        // Restore backbuffer RT + DSV
        if (m_SavedRTV) {
            ID3D11RenderTargetView* rtv = m_SavedRTV.Get();
            dc->OMSetRenderTargets(1, &rtv, m_SavedDSV.Get());
        }

        // Restore viewport (scene sub-rect or full window)
        restoreSavedViewport();
    } else {
        if (!m_CompositeRTV) {
            Logger::Error("[PostProcessPass] Composite RT not available");
            restoreSavedViewport();
            clearSavedTargets();
            return;
        }
        D3D11_VIEWPORT compositeVP = {};
        compositeVP.TopLeftX = 0.0f;
        compositeVP.TopLeftY = 0.0f;
        compositeVP.Width    = static_cast<float>(m_OffscreenWidth);
        compositeVP.Height   = static_cast<float>(m_OffscreenHeight);
        compositeVP.MinDepth = 0.0f;
        compositeVP.MaxDepth = 1.0f;
        dc->RSSetViewports(1, &compositeVP);
        ID3D11RenderTargetView* rtv = m_CompositeRTV.Get();
        dc->OMSetRenderTargets(1, &rtv, nullptr);
    }

    GpuShader* shader = GetOrCreateFXAAShader();
    if (!shader) {
        Logger::Error("[PostProcessPass] FXAA shader not available");
        if (!m_CompositeToBackbuffer) {
            restoreSavedViewport();
        }
        clearSavedTargets();
        return;
    }

    d3d11->BindShader(shader);

    // Bind offscreen color SRV (t0) + SSAO SRV (t1)
    ID3D11ShaderResourceView* srvs[2] = {
        m_OffscreenSRV.Get(),
        m_SSAOSRV ? m_SSAOSRV.Get() : m_OffscreenSRV.Get()
    };
    dc->PSSetShaderResources(0, 2, srvs);

    ID3D11SamplerState* samplers[2] = {
        m_ComposeSampler.Get(),
        m_PointClampSampler.Get()
    };
    dc->PSSetSamplers(0, 2, samplers);

    // Build post-process constants
    PostProcessConstants constants = CollectPostProcessParams(
        scene, m_OffscreenWidth, m_OffscreenHeight);

    d3d11->SetVSConstants(&constants, sizeof(constants));

    // Fullscreen triangle
    dc->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    dc->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->Draw(3, 0);

    // Unbind SRVs
    ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
    dc->PSSetShaderResources(0, 2, nullSrvs);

    if (!m_CompositeToBackbuffer) {
        if (m_SavedRTV) {
            ID3D11RenderTargetView* rtv = m_SavedRTV.Get();
            dc->OMSetRenderTargets(1, &rtv, m_SavedDSV.Get());
        } else {
            ID3D11RenderTargetView* nullRtv = nullptr;
            dc->OMSetRenderTargets(1, &nullRtv, nullptr);
        }
        restoreSavedViewport();
    }

    clearSavedTargets();
}

void* PostProcessPass::GetSceneColorTextureHandle() const
{
    if (m_D3D12CompositeSRVGpu.ptr != 0) {
        return reinterpret_cast<void*>(m_D3D12CompositeSRVGpu.ptr);
    }
    if (m_D3D12OffscreenSRVGpu.ptr != 0) {
        return reinterpret_cast<void*>(m_D3D12OffscreenSRVGpu.ptr);
    }
    return GetSceneColorSrv();
}

void PostProcessPass::Execute(const Scene& scene, const Camera& camera)
{
    (void)scene;
    (void)camera;
    // PostProcessPass uses BeginOffscreen / RenderSSAO / EndOffscreenAndComposite
    // directly from Renderer::RenderScene instead of the Execute() interface.
}

void PostProcessPass::Resize(uint32_t width, uint32_t height)
{
    EnsureOffscreenRT(width, height);
}
