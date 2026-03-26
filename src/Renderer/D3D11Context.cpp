#include "D3D11Context.h"
#include "../Core/Logger.h"
#include "../Core/Window.h"

#include <d3dcompiler.h>
#include <dxgi.h>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// --------------------------------------------------------------------------
// Factory
// --------------------------------------------------------------------------
std::unique_ptr<IRenderContext> CreateD3D11Context() {
    return std::make_unique<D3D11Context>();
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
static DXGI_FORMAT ToDxgiFormat(VertexFormat fmt) {
    switch (fmt) {
    case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

// --------------------------------------------------------------------------
// D3D11Context
// --------------------------------------------------------------------------

D3D11Context::~D3D11Context() { Shutdown(); }

bool D3D11Context::Init(IWindow* window) {
    HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
    if (!hwnd) {
        Logger::Error("D3D11Context::Init – invalid HWND");
        return false;
    }

    const int w = window->GetWidth();
    const int h = window->GetHeight();

    // ---- SwapChain + Device ------------------------------------------------
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount                        = 2;
    scd.BufferDesc.Width                   = static_cast<UINT>(w);
    scd.BufferDesc.Height                  = static_cast<UINT>(h);
    scd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator   = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow                       = hwnd;
    scd.SampleDesc.Count                   = 1;
    scd.Windowed                           = TRUE;
    scd.SwapEffect                         = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, levels, 1,
        D3D11_SDK_VERSION,
        &scd,
        &m_SwapChain, &m_Device, &featureLevel, &m_Context);

    if (FAILED(hr)) {
        Logger::Error("D3D11CreateDeviceAndSwapChain failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    // ---- Render target + depth buffer --------------------------------------
    ComPtr<ID3D11Texture2D> backBuffer;
    m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RTV);

    // Create depth stencil buffer
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width              = static_cast<UINT>(w);
    depthDesc.Height             = static_cast<UINT>(h);
    depthDesc.MipLevels          = 1;
    depthDesc.ArraySize          = 1;
    depthDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count   = 1;
    depthDesc.Usage              = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
    m_Device->CreateTexture2D(&depthDesc, nullptr, &m_Depth);
    m_Device->CreateDepthStencilView(m_Depth.Get(), nullptr, &m_DSV);

    m_Context->OMSetRenderTargets(1, m_RTV.GetAddressOf(), m_DSV.Get());

    // ---- Default viewport --------------------------------------------------
    SetViewport(0, 0, static_cast<float>(w), static_cast<float>(h));

    Logger::Info("D3D11Context initialised (", w, "x", h, ")");
    return true;
}

void D3D11Context::Shutdown() {
    if (m_Context) { m_Context->ClearState(); }
    m_DSV.Reset();
    m_Depth.Reset();
    m_CBuffer.Reset();
    m_RTV.Reset();
    m_SwapChain.Reset();
    m_Context.Reset();
    m_Device.Reset();
}

void D3D11Context::BeginFrame(float r, float g, float b, float a) {
    const float color[4] = { r, g, b, a };
    m_Context->ClearRenderTargetView(m_RTV.Get(), color);
    if (m_DSV) {
        m_Context->ClearDepthStencilView(m_DSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        m_Context->OMSetRenderTargets(1, m_RTV.GetAddressOf(), m_DSV.Get());
    } else {
        m_Context->OMSetRenderTargets(1, m_RTV.GetAddressOf(), nullptr);
    }
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void D3D11Context::EndFrame() {
    m_SwapChain->Present(1, 0);
}

// --------------------------------------------------------------------------
// Resource creation
// --------------------------------------------------------------------------

std::shared_ptr<GpuBuffer> D3D11Context::CreateVertexBuffer(
    const void* data, uint32_t byteSize, uint32_t strideBytes)
{
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = byteSize;
    bd.Usage          = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = data;

    auto buf = std::make_shared<D3D11Buffer>();
    buf->stride = strideBytes;
    HRESULT hr = m_Device->CreateBuffer(&bd, &sd, &buf->buffer);
    if (FAILED(hr)) {
        Logger::Error("CreateVertexBuffer failed");
        return nullptr;
    }
    return buf;
}

std::shared_ptr<GpuBuffer> D3D11Context::CreateIndexBuffer(
    const void* data, uint32_t byteSize)
{
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = byteSize;
    bd.Usage          = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags      = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = data;

    auto buf = std::make_shared<D3D11IndexBuffer>();
    buf->format = DXGI_FORMAT_R32_UINT;
    HRESULT hr = m_Device->CreateBuffer(&bd, &sd, &buf->buffer);
    if (FAILED(hr)) {
        Logger::Error("CreateIndexBuffer failed");
        return nullptr;
    }
    return buf;
}

std::shared_ptr<GpuShader> D3D11Context::CreateShader(
    const std::string& hlslSource,
    const std::string& vsEntry,
    const std::string& psEntry,
    const VertexElement* layout,
    uint32_t layoutCount)
{
    auto sh = std::make_shared<D3D11Shader>();

    auto compileShader = [&](const std::string& entry,
                             const std::string& target,
                             ComPtr<ID3DBlob>& outBlob) -> bool
    {
        ComPtr<ID3DBlob> errBlob;
        HRESULT hr = D3DCompile(
            hlslSource.c_str(), hlslSource.size(),
            nullptr, nullptr, nullptr,
            entry.c_str(), target.c_str(),
            D3DCOMPILE_ENABLE_STRICTNESS, 0,
            &outBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) {
                Logger::Error("Shader compile error: ",
                    static_cast<const char*>(errBlob->GetBufferPointer()));
            }
            return false;
        }
        return true;
    };

    ComPtr<ID3DBlob> vsBlob, psBlob;
    if (!compileShader(vsEntry, "vs_5_0", vsBlob)) return nullptr;
    if (!compileShader(psEntry, "ps_5_0", psBlob)) return nullptr;

    m_Device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &sh->vs);
    m_Device->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &sh->ps);

    // Build input layout from VertexElement descriptors.
    std::vector<D3D11_INPUT_ELEMENT_DESC> descs(layoutCount);
    for (uint32_t i = 0; i < layoutCount; ++i) {
        descs[i] = {
            layout[i].semantic,
            layout[i].index,
            ToDxgiFormat(layout[i].format),
            0,
            layout[i].offset,
            D3D11_INPUT_PER_VERTEX_DATA, 0
        };
    }
    m_Device->CreateInputLayout(
        descs.data(), layoutCount,
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        &sh->inputLayout);

    return sh;
}

// --------------------------------------------------------------------------
// Per-draw constant buffer helper
// --------------------------------------------------------------------------
void D3D11Context::CreateConstantBuffer(uint32_t byteSize) {
    // Round up to 16-byte boundary.
    byteSize = (byteSize + 15u) & ~15u;
    if (m_CBuffer && m_CBufferSize >= byteSize) return;

    m_CBuffer.Reset();
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = byteSize;
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_Device->CreateBuffer(&bd, nullptr, &m_CBuffer);
    m_CBufferSize = byteSize;
}

// --------------------------------------------------------------------------
// Binding / draw
// --------------------------------------------------------------------------
void D3D11Context::BindShader(GpuShader* shader) {
    auto* s = static_cast<D3D11Shader*>(shader);
    m_Context->VSSetShader(s->vs.Get(), nullptr, 0);
    m_Context->PSSetShader(s->ps.Get(), nullptr, 0);
    m_Context->IASetInputLayout(s->inputLayout.Get());
}

void D3D11Context::BindVertexBuffer(GpuBuffer* buffer) {
    auto* b = static_cast<D3D11Buffer*>(buffer);
    UINT offset = 0;
    m_Context->IASetVertexBuffers(0, 1, b->buffer.GetAddressOf(), &b->stride, &offset);
}

void D3D11Context::BindIndexBuffer(GpuBuffer* buffer) {
    if (!buffer) {
        m_Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
        return;
    }
    auto* b = static_cast<D3D11IndexBuffer*>(buffer);
    m_Context->IASetIndexBuffer(b->buffer.Get(), b->format, 0);
}

void D3D11Context::SetVSConstants(const void* data, uint32_t byteSize) {
    CreateConstantBuffer(byteSize);

    D3D11_MAPPED_SUBRESOURCE ms = {};
    m_Context->Map(m_CBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    memcpy(ms.pData, data, byteSize);
    m_Context->Unmap(m_CBuffer.Get(), 0);
    m_Context->VSSetConstantBuffers(0, 1, m_CBuffer.GetAddressOf());
    m_Context->PSSetConstantBuffers(0, 1, m_CBuffer.GetAddressOf());
}

void D3D11Context::Draw(uint32_t vertexCount, uint32_t startVertex) {
    m_Context->Draw(vertexCount, startVertex);
}

void D3D11Context::DrawIndexed(uint32_t indexCount, uint32_t startIndex,
                               uint32_t baseVertex) {
    m_Context->DrawIndexed(indexCount, startIndex, baseVertex);
}

void D3D11Context::SetViewport(float x, float y, float w, float h) {
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = x;  vp.TopLeftY = y;
    vp.Width    = w;  vp.Height   = h;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    m_Context->RSSetViewports(1, &vp);
}

// --------------------------------------------------------------------------
// Texture upload / bind
// --------------------------------------------------------------------------

std::shared_ptr<GpuTexture> D3D11Context::UploadTexture2D(
    const void* rgba8Data, int width, int height)
{
    if (!rgba8Data || width <= 0 || height <= 0) return nullptr;

    auto tex = std::make_shared<D3D11Texture>();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = static_cast<UINT>(width);
    desc.Height           = static_cast<UINT>(height);
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem     = rgba8Data;
    sd.SysMemPitch = static_cast<UINT>(width * 4);

    HRESULT hr = m_Device->CreateTexture2D(&desc, &sd, &tex->texture);
    if (FAILED(hr)) {
        Logger::Error("D3D11: UploadTexture2D – CreateTexture2D failed");
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = m_Device->CreateShaderResourceView(tex->texture.Get(), &srvDesc, &tex->srv);
    if (FAILED(hr)) {
        Logger::Error("D3D11: UploadTexture2D – CreateShaderResourceView failed");
        return nullptr;
    }

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy  = 1;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;

    hr = m_Device->CreateSamplerState(&sampDesc, &tex->sampler);
    if (FAILED(hr)) {
        Logger::Error("D3D11: UploadTexture2D – CreateSamplerState failed");
        return nullptr;
    }

    return tex;
}

void D3D11Context::BindPSTexture(uint32_t slot, GpuTexture* tex)
{
    if (!tex) {
        ID3D11ShaderResourceView* nullSrv    = nullptr;
        ID3D11SamplerState*       nullSampler = nullptr;
        m_Context->PSSetShaderResources(slot, 1, &nullSrv);
        m_Context->PSSetSamplers(slot, 1, &nullSampler);
        return;
    }
    auto* d3dTex = static_cast<D3D11Texture*>(tex);
    m_Context->PSSetShaderResources(slot, 1, d3dTex->srv.GetAddressOf());
    m_Context->PSSetSamplers(slot, 1, d3dTex->sampler.GetAddressOf());
}
