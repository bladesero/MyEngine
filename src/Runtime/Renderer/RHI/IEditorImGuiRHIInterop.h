#pragma once

#include "Renderer/RHI/RHITypes.h"

#include <cstdint>

struct ImGuiBackendHandles {
    RHIBackend backend = RHIBackend::Unknown;
    void* device = nullptr;            // ID3D11Device* / ID3D12Device* / MTLDevice*
    void* deviceContext = nullptr;     // ID3D11DeviceContext* (D3D11 only)
    uint32_t framesInFlight = 0;       // D3D12 only
    void* srvHeap = nullptr;           // D3D12 ID3D12DescriptorHeap*
    uint64_t fontSrvCpuHandle = 0;     // D3D12 font SRV CPU handle
    uint64_t fontSrvGpuHandle = 0;     // D3D12 font SRV GPU handle
    void* backBufferRtvPtr = nullptr;  // D3D11 ID3D11RenderTargetView*
    void* backBufferDsvPtr = nullptr;  // D3D11 ID3D11DepthStencilView*
    void* commandList = nullptr;       // D3D12 ID3D12GraphicsCommandList*
    void* commandBuffer = nullptr;     // Metal id<MTLCommandBuffer>
    void* commandEncoder = nullptr;    // Metal id<MTLRenderCommandEncoder>
    void* renderPassDescriptor = nullptr; // Metal MTLRenderPassDescriptor*
};

class IEditorImGuiRHIInterop {
public:
    using SwapChainResizeCallback = void(*)();

    virtual ~IEditorImGuiRHIInterop() = default;
    virtual ImGuiBackendHandles GetImGuiBackendHandles() { return {}; }
    virtual void SetSwapChainResizeCallback(SwapChainResizeCallback) {}
};
