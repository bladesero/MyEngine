#pragma once

#include "IRenderContext.h"

#include <d3d11.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// --------------------------------------------------------------------------
// D3D11 concrete GpuBuffer / GpuShader
// --------------------------------------------------------------------------
struct D3D11Buffer : GpuBuffer {
    ComPtr<ID3D11Buffer> buffer;
    uint32_t             stride = 0;
};

struct D3D11Shader : GpuShader {
    ComPtr<ID3D11VertexShader>  vs;
    ComPtr<ID3D11PixelShader>   ps;
    ComPtr<ID3D11InputLayout>   inputLayout;
};

// --------------------------------------------------------------------------
// D3D11Context
// --------------------------------------------------------------------------
class D3D11Context : public IRenderContext {
public:
    ~D3D11Context() override;

    bool Init(IWindow* window) override;
    void Shutdown()            override;

    void BeginFrame(float r, float g, float b, float a) override;
    void EndFrame()  override;

    std::shared_ptr<GpuBuffer> CreateVertexBuffer(
        const void* data, uint32_t byteSize, uint32_t strideBytes) override;

    std::shared_ptr<GpuShader> CreateShader(
        const std::string& hlslSource,
        const std::string& vsEntry,
        const std::string& psEntry,
        const VertexElement* layout,
        uint32_t layoutCount) override;

    void BindShader(GpuShader* shader)       override;
    void BindVertexBuffer(GpuBuffer* buffer) override;
    void SetVSConstants(const void* data, uint32_t byteSize) override;
    void Draw(uint32_t vertexCount, uint32_t startVertex)    override;
    void SetViewport(float x, float y, float w, float h)     override;

private:
    void CreateConstantBuffer(uint32_t byteSize);

    ComPtr<ID3D11Device>           m_Device;
    ComPtr<ID3D11DeviceContext>    m_Context;
    ComPtr<IDXGISwapChain>         m_SwapChain;
    ComPtr<ID3D11RenderTargetView> m_RTV;
    ComPtr<ID3D11Buffer>           m_CBuffer;      // per-draw VS constants
    uint32_t                       m_CBufferSize = 0;
};
