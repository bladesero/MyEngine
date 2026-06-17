#include "Renderer/EnvironmentPass.h"

#include "Core/Logger.h"
#include "Renderer/D3D11Context.h"
#include "Renderer/D3D12Context.h"
#include "Renderer/ShaderManager.h"

#include <cstring>

#ifdef MYENGINE_PLATFORM_WINDOWS
#include "ShaderBytecodeWindows.h"
#endif

namespace {

struct AtmosphereFaceConstants {
    float faceInfo[4];
};

struct SH2Readback {
    float coeffs[9][4];
};

#ifdef MYENGINE_PLATFORM_WINDOWS
void TransitionResource(ID3D12GraphicsCommandList* cmd,
                        ID3D12Resource* resource,
                        D3D12_RESOURCE_STATES before,
                        D3D12_RESOURCE_STATES after)
{
    if (!cmd || !resource || before == after) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);
}

void TransitionSubresource(ID3D12GraphicsCommandList* cmd,
                           ID3D12Resource* resource,
                           UINT subresource,
                           D3D12_RESOURCE_STATES before,
                           D3D12_RESOURCE_STATES after)
{
    if (!cmd || !resource || before == after) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = subresource;
    cmd->ResourceBarrier(1, &barrier);
}
#endif

} // namespace

#ifdef MYENGINE_PLATFORM_WINDOWS
struct EnvironmentPass::D3D11EnvironmentTexture : D3D11Texture {
    bool IsCube() const override { return true; }
};
#endif

EnvironmentPass::EnvironmentPass(IRenderContext* context)
    : RenderPass(context)
{
    m_SH2[0][0] = 0.08f;
    m_SH2[0][1] = 0.10f;
    m_SH2[0][2] = 0.14f;
}

EnvironmentPass::~EnvironmentPass() = default;

void EnvironmentPass::Resize(uint32_t, uint32_t)
{}

GpuTexture* EnvironmentPass::GetEnvironmentCubemap() const
{
#ifdef MYENGINE_PLATFORM_WINDOWS
    if (m_D3D11Environment) return m_D3D11Environment.get();
#endif
    return m_D3D12Environment.get();
}

GpuTexture* EnvironmentPass::GetSH2Buffer() const
{
#ifdef MYENGINE_PLATFORM_WINDOWS
    if (m_D3D11SH2Buffer) return m_D3D11SH2Buffer.get();
#endif
    return m_D3D12SH2Buffer.get();
}

bool EnvironmentPass::EnsureD3D11Resources()
{
#ifndef MYENGINE_PLATFORM_WINDOWS
    return false;
#else
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    if (!d3d11) return false;
    ID3D11Device* device = d3d11->GetDevice();
    if (!device) return false;

    if (!m_D3D11Environment) {
        m_D3D11Environment = std::make_shared<D3D11EnvironmentTexture>();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = kCubeSize;
        desc.Height = kCubeSize;
        desc.MipLevels = 0;
        desc.ArraySize = 6;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &m_D3D11Environment->texture);
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D11 CreateTexture2D cubemap failed");
            m_D3D11Environment.reset();
            return false;
        }

        for (UINT face = 0; face < 6; ++face) {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = desc.Format;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.FirstArraySlice = face;
            rtvDesc.Texture2DArray.ArraySize = 1;
            hr = device->CreateRenderTargetView(
                m_D3D11Environment->texture.Get(), &rtvDesc, &m_D3D11FaceRTV[face]);
            if (FAILED(hr)) {
                Logger::Error("[EnvironmentPass] D3D11 CreateRenderTargetView face failed");
                m_D3D11Environment.reset();
                return false;
            }
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = UINT(-1);
        hr = device->CreateShaderResourceView(
            m_D3D11Environment->texture.Get(), &srvDesc, &m_D3D11Environment->srv);
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D11 CreateShaderResourceView cubemap failed");
            m_D3D11Environment.reset();
            return false;
        }

        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(&samplerDesc, &m_D3D11Environment->sampler);
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D11 CreateSamplerState cubemap failed");
            m_D3D11Environment.reset();
            return false;
        }
        m_D3D11LinearClamp = m_D3D11Environment->sampler;
    }

    if (!m_D3D11SHCompute) {
        HRESULT createHr = device->CreateComputeShader(
            k_AtmosphereSHCsBytecode, k_AtmosphereSHCsBytecodeSize,
            nullptr, &m_D3D11SHCompute);
        if (FAILED(createHr)) {
            Logger::Error("[EnvironmentPass] D3D11 CreateComputeShader from embedded blob failed: 0x",
                reinterpret_cast<void*>(static_cast<uintptr_t>(createHr)));
            return false;
        }
    }

    if (!m_D3D11SHBuffer) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(SH2Readback);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(float) * 4;
        HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_D3D11SHBuffer);
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D11 CreateBuffer SH failed");
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = 9;
        hr = device->CreateUnorderedAccessView(
            m_D3D11SHBuffer.Get(), &uavDesc, &m_D3D11SHUAV);
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D11 CreateUnorderedAccessView SH failed");
            return false;
        }

        m_D3D11SH2Buffer = std::make_shared<D3D11Texture>();
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = 9;
        hr = device->CreateShaderResourceView(
            m_D3D11SHBuffer.Get(), &srvDesc, &m_D3D11SH2Buffer->srv);
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D11 CreateShaderResourceView SH failed");
            return false;
        }
        m_D3D11SH2Buffer->sampler = m_D3D11LinearClamp;

        D3D11_BUFFER_DESC readbackDesc = {};
        readbackDesc.ByteWidth = sizeof(SH2Readback);
        readbackDesc.Usage = D3D11_USAGE_STAGING;
        readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateBuffer(&readbackDesc, nullptr, &m_D3D11SHReadback);
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D11 CreateBuffer SH readback failed");
            return false;
        }
    }

    return true;
#endif
}

bool EnvironmentPass::EnsureD3D12Resources()
{
#ifndef MYENGINE_PLATFORM_WINDOWS
    return false;
#else
    auto* d3d12 = dynamic_cast<D3D12Context*>(Context());
    if (!d3d12) return false;
    ID3D12Device* device = d3d12->GetDevice();
    if (!device) return false;

    if (!m_D3D12Environment) {
        auto texture = std::make_shared<D3D12Texture>();
        texture->isCube = true;
        texture->arraySize = 6;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kCubeSize;
        desc.Height = kCubeSize;
        desc.DepthOrArraySize = 6;
        desc.MipLevels = kCubeMipLevels;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = desc.Format;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
            IID_PPV_ARGS(&texture->resource));
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D12 CreateCommittedResource cubemap failed");
            return false;
        }
        m_D3D12EnvState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        for (UINT mip = 0; mip < kCubeMipLevels; ++mip) {
            for (UINT face = 0; face < 6; ++face) {
                m_D3D12MipFaceRTV[mip][face] = d3d12->AllocRtvSlot();
                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
                rtvDesc.Format = desc.Format;
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.MipSlice = mip;
                rtvDesc.Texture2DArray.FirstArraySlice = face;
                rtvDesc.Texture2DArray.ArraySize = 1;
                device->CreateRenderTargetView(
                    texture->resource.Get(), &rtvDesc, m_D3D12MipFaceRTV[mip][face]);
            }
        }

        for (UINT face = 0; face < 6; ++face) {
            m_D3D12FaceRTV[face] = m_D3D12MipFaceRTV[0][face];
        }

        texture->srvCpu = d3d12->AllocSrvSlot(texture->srvGpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = kCubeMipLevels;
        srvDesc.TextureCube.MostDetailedMip = 0;
        device->CreateShaderResourceView(texture->resource.Get(), &srvDesc, texture->srvCpu);

        texture->sampCpu = d3d12->AllocSampSlot(texture->sampGpu);
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        device->CreateSampler(&samplerDesc, texture->sampCpu);

        m_D3D12Environment = texture;
    }

    if (!m_D3D12SH2Buffer) {
        auto shBuffer = std::make_shared<D3D12Texture>();

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeof(SH2Readback);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&shBuffer->resource));
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D12 CreateCommittedResource SH buffer failed");
            return false;
        }
        m_D3D12SHState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        shBuffer->srvCpu = d3d12->AllocSrvSlot(shBuffer->srvGpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = 9;
        srvDesc.Buffer.StructureByteStride = sizeof(float) * 4;
        device->CreateShaderResourceView(shBuffer->resource.Get(), &srvDesc, shBuffer->srvCpu);

        m_D3D12SHUAVCpu = d3d12->AllocSrvSlot(m_D3D12SHUAVGpu);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = 9;
        uavDesc.Buffer.StructureByteStride = sizeof(float) * 4;
        device->CreateUnorderedAccessView(
            shBuffer->resource.Get(), nullptr, &uavDesc, m_D3D12SHUAVCpu);

        shBuffer->sampCpu = d3d12->AllocSampSlot(shBuffer->sampGpu);
        D3D12_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        device->CreateSampler(&samplerDesc, shBuffer->sampCpu);

        m_D3D12SH2Buffer = shBuffer;
    }

    if (!m_D3D12ComputeRootSignature) {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;

        D3D12_DESCRIPTOR_RANGE samplerRange = {};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = 1;
        samplerRange.BaseShaderRegister = 0;

        D3D12_DESCRIPTOR_RANGE uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &srvRange;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &samplerRange;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &uavRange;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 3;
        rsDesc.pParameters = params;

        Microsoft::WRL::ComPtr<ID3DBlob> rootBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeRootSignature(
            &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob);
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D12SerializeRootSignature compute failed");
            return false;
        }
        hr = device->CreateRootSignature(
            0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
            IID_PPV_ARGS(&m_D3D12ComputeRootSignature));
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D12 CreateRootSignature compute failed");
            return false;
        }
    }

    if (!m_D3D12ComputePSO) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_D3D12ComputeRootSignature.Get();
        psoDesc.CS = { k_AtmosphereSHCsBytecode, k_AtmosphereSHCsBytecodeSize };
        HRESULT hr = device->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(&m_D3D12ComputePSO));
        if (FAILED(hr)) {
            Logger::Error("[EnvironmentPass] D3D12 CreateComputePipelineState failed");
            return false;
        }
    }

    if (!m_D3D12AtmosphereShader) {
        m_D3D12AtmosphereShader = d3d12->CreateFullscreenShaderFromBytecode(
            k_AtmosphereCubemapVsBytecode,
            k_AtmosphereCubemapVsBytecodeSize,
            k_AtmosphereCubemapPsBytecode,
            k_AtmosphereCubemapPsBytecodeSize,
            DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (!m_D3D12AtmosphereShader) {
            Logger::Error("[EnvironmentPass] D3D12 atmosphere cubemap shader failed");
            return false;
        }
    }

    if (!m_D3D12MipmapShader) {
        m_D3D12MipmapShader = d3d12->CreateFullscreenShaderFromBytecode(
            k_EnvironmentMipmapVsBytecode,
            k_EnvironmentMipmapVsBytecodeSize,
            k_EnvironmentMipmapPsBytecode,
            k_EnvironmentMipmapPsBytecodeSize,
            DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (!m_D3D12MipmapShader) {
            Logger::Error("[EnvironmentPass] D3D12 environment mipmap shader failed");
            return false;
        }
    }

    return true;
#endif
}

void EnvironmentPass::ExecuteD3D11()
{
#ifdef MYENGINE_PLATFORM_WINDOWS
    auto* d3d11 = dynamic_cast<D3D11Context*>(Context());
    if (!d3d11 || !EnsureD3D11Resources()) return;
    auto* dc = d3d11->GetDeviceContext();
    if (!dc) return;

    ShaderManager::Get().SetContext(Context());
    auto shaderHandle = ShaderManager::Get().GetOrCreate(
        "src/Runtime/Renderer/Shaders/AtmosphereCubemap.hlsl",
        "VSMain", "PSMain", nullptr, 0);
    if (!shaderHandle || !shaderHandle->shader) {
        Logger::Error("[EnvironmentPass] Atmosphere cubemap shader not available");
        return;
    }

    ID3D11RenderTargetView* prevRTVRaw = nullptr;
    ID3D11DepthStencilView* prevDSVRaw = nullptr;
    dc->OMGetRenderTargets(1, &prevRTVRaw, &prevDSVRaw);
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> prevRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> prevDSV;
    prevRTV.Attach(prevRTVRaw);
    prevDSV.Attach(prevDSVRaw);

    UINT prevViewportCount = 1;
    D3D11_VIEWPORT prevViewport = {};
    dc->RSGetViewports(&prevViewportCount, &prevViewport);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(kCubeSize);
    viewport.Height = static_cast<float>(kCubeSize);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &viewport);

    ID3D11ShaderResourceView* nullPsSrv = nullptr;
    dc->PSSetShaderResources(8, 1, &nullPsSrv);
    dc->PSSetShaderResources(9, 1, &nullPsSrv);

    GpuCommandList* cmd = Context()->GetGraphicsCommandList();
    for (uint32_t face = 0; face < 6; ++face) {
        ID3D11RenderTargetView* rtv = m_D3D11FaceRTV[face].Get();
        dc->OMSetRenderTargets(1, &rtv, nullptr);
        const float clear[4] = {};
        dc->ClearRenderTargetView(rtv, clear);

        AtmosphereFaceConstants constants{};
        constants.faceInfo[0] = static_cast<float>(face);
        cmd->BindShader(shaderHandle->shader.get());
        cmd->SetBlendMode(GpuBlendMode::Opaque);
        cmd->BindVertexBuffer(nullptr);
        cmd->BindIndexBuffer(nullptr);
        cmd->SetVSConstants(&constants, sizeof(constants));
        cmd->Draw(3);
    }

    ID3D11RenderTargetView* restoreRTV = prevRTV.Get();
    dc->OMSetRenderTargets(1, &restoreRTV, prevDSV.Get());
    if (prevViewportCount > 0) {
        dc->RSSetViewports(1, &prevViewport);
    }

    dc->GenerateMips(m_D3D11Environment->srv.Get());

    ID3D11ShaderResourceView* cubeSrv = m_D3D11Environment->srv.Get();
    ID3D11SamplerState* sampler = m_D3D11LinearClamp.Get();
    ID3D11UnorderedAccessView* uav = m_D3D11SHUAV.Get();
    dc->CSSetShader(m_D3D11SHCompute.Get(), nullptr, 0);
    dc->CSSetShaderResources(0, 1, &cubeSrv);
    dc->CSSetSamplers(0, 1, &sampler);
    dc->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    dc->Dispatch(1, 1, 1);

    ID3D11UnorderedAccessView* nullUav = nullptr;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ID3D11SamplerState* nullSampler = nullptr;
    dc->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
    dc->CSSetShaderResources(0, 1, &nullSrv);
    dc->CSSetSamplers(0, 1, &nullSampler);
    dc->CSSetShader(nullptr, nullptr, 0);

    dc->CopyResource(m_D3D11SHReadback.Get(), m_D3D11SHBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(dc->Map(m_D3D11SHReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        std::memcpy(m_SH2, mapped.pData, sizeof(m_SH2));
        dc->Unmap(m_D3D11SHReadback.Get(), 0);
    }
#endif
}

void EnvironmentPass::ExecuteD3D12()
{
#ifdef MYENGINE_PLATFORM_WINDOWS
    auto* d3d12 = dynamic_cast<D3D12Context*>(Context());
    if (!d3d12 || !EnsureD3D12Resources()) return;

    ID3D12GraphicsCommandList* cmdList = d3d12->GetCommandList();
    if (!cmdList) return;

    auto* env = static_cast<D3D12Texture*>(m_D3D12Environment.get());
    auto* sh = static_cast<D3D12Texture*>(m_D3D12SH2Buffer.get());
    if (!env || !env->resource || !sh || !sh->resource) return;

    TransitionResource(cmdList, env->resource.Get(), m_D3D12EnvState,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_D3D12EnvState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    GpuCommandList* cmd = Context()->GetGraphicsCommandList();
    for (uint32_t face = 0; face < 6; ++face) {
        d3d12->PushRenderTarget(&m_D3D12FaceRTV[face], {});
        d3d12->SetViewport(0.0f, 0.0f,
                           static_cast<float>(kCubeSize),
                           static_cast<float>(kCubeSize));
        const float clear[4] = {};
        cmdList->ClearRenderTargetView(m_D3D12FaceRTV[face], clear, 0, nullptr);

        AtmosphereFaceConstants constants{};
        constants.faceInfo[0] = static_cast<float>(face);
        cmd->BindShader(m_D3D12AtmosphereShader.get());
        cmd->SetBlendMode(GpuBlendMode::Opaque);
        cmd->BindVertexBuffer(nullptr);
        cmd->BindIndexBuffer(nullptr);
        cmd->SetVSConstants(&constants, sizeof(constants));
        cmd->Draw(3);
        d3d12->PopRenderTarget();
    }

    TransitionResource(cmdList, env->resource.Get(), m_D3D12EnvState,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_D3D12EnvState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    for (uint32_t mip = 1; mip < kCubeMipLevels; ++mip) {
        const float size = static_cast<float>(kCubeSize >> mip);
        for (uint32_t face = 0; face < 6; ++face) {
            const UINT subresource = mip + face * kCubeMipLevels;
            TransitionSubresource(cmdList, env->resource.Get(), subresource,
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET);

            d3d12->PushRenderTarget(&m_D3D12MipFaceRTV[mip][face], {});
            d3d12->SetViewport(0.0f, 0.0f, size, size);
            const float clear[4] = {};
            cmdList->ClearRenderTargetView(m_D3D12MipFaceRTV[mip][face], clear, 0, nullptr);

            AtmosphereFaceConstants constants{};
            constants.faceInfo[0] = static_cast<float>(face);
            constants.faceInfo[1] = static_cast<float>(mip - 1);
            cmd->BindShader(m_D3D12MipmapShader.get());
            cmd->SetBlendMode(GpuBlendMode::Opaque);
            cmd->BindVertexBuffer(nullptr);
            cmd->BindIndexBuffer(nullptr);
            cmd->SetVSConstants(&constants, sizeof(constants));
            d3d12->BindPSTextureDescriptors(0, env->srvGpu, env->sampGpu);
            cmd->Draw(3);
            d3d12->PopRenderTarget();

            TransitionSubresource(cmdList, env->resource.Get(), subresource,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
    }

    TransitionResource(cmdList, sh->resource.Get(), m_D3D12SHState,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_D3D12SHState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    cmdList->SetComputeRootSignature(m_D3D12ComputeRootSignature.Get());
    cmdList->SetPipelineState(m_D3D12ComputePSO.Get());
    cmdList->SetComputeRootDescriptorTable(0, env->srvGpu);
    cmdList->SetComputeRootDescriptorTable(1, env->sampGpu);
    cmdList->SetComputeRootDescriptorTable(2, m_D3D12SHUAVGpu);
    cmdList->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = sh->resource.Get();
    cmdList->ResourceBarrier(1, &uavBarrier);

    TransitionResource(cmdList, sh->resource.Get(), m_D3D12SHState,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_D3D12SHState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
#endif
}

void EnvironmentPass::Execute(const Scene&, const Camera&)
{
    ExecuteD3D11();
    ExecuteD3D12();
}
