#include "D3D12Context.h"

#include "../Core/Logger.h"
#include "../Core/Window.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <d3dcompiler.h>

#include <windows.h>

#include <cstring>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#if defined(MYENGINE_ENABLE_IMGUI)
#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_sdl3.h>
#endif

// --------------------------------------------------------------------------
// Factory
// --------------------------------------------------------------------------
std::unique_ptr<IRenderContext> CreateD3D12Context() {
    return std::make_unique<D3D12Context>();
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
DXGI_FORMAT D3D12Context::ToDxgiFormat(VertexFormat fmt) {
    switch (fmt) {
    case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::OffsetHandle(D3D12_CPU_DESCRIPTOR_HANDLE h,
                                                        uint32_t index, uint32_t inc) {
    h.ptr += static_cast<SIZE_T>(index) * inc;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Context::OffsetHandle(D3D12_GPU_DESCRIPTOR_HANDLE h,
                                                         uint32_t index, uint32_t inc) {
    h.ptr += static_cast<SIZE_T>(index) * inc;
    return h;
}

class D3D12ImmediateCommandList final : public GpuCommandList {
public:
    explicit D3D12ImmediateCommandList(D3D12Context& owner)
        : m_Owner(owner) {}

    void BindShader(GpuShader* shader) override {
        m_Owner.BindShader(shader);
    }

    void BindVertexBuffer(GpuBuffer* buffer) override {
        m_Owner.BindVertexBuffer(buffer);
    }

    void BindIndexBuffer(GpuBuffer* buffer) override {
        m_Owner.BindIndexBuffer(buffer);
    }

    void SetVSConstants(const void* data, uint32_t byteSize) override {
        m_Owner.SetVSConstants(data, byteSize);
    }

    void Draw(uint32_t vertexCount, uint32_t startVertex) override {
        m_Owner.Draw(vertexCount, startVertex);
    }

    void DrawIndexed(uint32_t indexCount, uint32_t startIndex,
                     uint32_t baseVertex) override {
        m_Owner.DrawIndexed(indexCount, startIndex, baseVertex);
    }

    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                       uint32_t startVertex) override {
        m_Owner.DrawInstanced(vertexCount, instanceCount, startVertex);
    }

    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                              uint32_t startIndex, uint32_t baseVertex) override {
        m_Owner.DrawIndexedInstanced(
            indexCount, instanceCount, startIndex, baseVertex);
    }

    void SetViewport(float x, float y, float w, float h) override {
        m_Owner.SetViewport(x, y, w, h);
    }

    void BindPSTexture(uint32_t slot, GpuTexture* tex) override {
        m_Owner.BindPSTexture(slot, tex);
    }

    void SetBlendMode(GpuBlendMode mode) override {
        m_Owner.SetBlendMode(mode);
    }

    void SetRasterState(bool twoSided, bool wireframe) override {
        m_Owner.SetRasterState(twoSided, wireframe);
    }

    void* GetNativeHandle() const override {
        return m_Owner.GetCommandList();
    }

private:
    D3D12Context& m_Owner;
};

class D3D12SwapChain final : public GpuSwapChain {
public:
    explicit D3D12SwapChain(D3D12Context& owner)
        : m_Owner(owner) {}

    void Present(bool vsync) override {
        m_Owner.PresentSwapChain(vsync);
    }

    bool Resize(uint32_t width, uint32_t height) override {
        return m_Owner.ResizeSwapChain(width, height);
    }

    uint32_t GetWidth() const override {
        return m_Owner.m_SwapChainWidth;
    }

    uint32_t GetHeight() const override {
        return m_Owner.m_SwapChainHeight;
    }

private:
    D3D12Context& m_Owner;
};

// --------------------------------------------------------------------------
// D3D12Context
// --------------------------------------------------------------------------
D3D12Context::D3D12Context()
    : m_SwapChainInterface(std::make_unique<D3D12SwapChain>(*this))
    , m_GraphicsCommandList(std::make_unique<D3D12ImmediateCommandList>(*this)) {}

D3D12Context::~D3D12Context() { Shutdown(); }

static HRESULT CheckHR(HRESULT hr) {
    return hr;
}

bool D3D12Context::Init(IWindow* window) {
    if (!window) return false;

    HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
    if (!hwnd) {
        Logger::Error("D3D12Context::Init – invalid HWND");
        return false;
    }

    const uint32_t w = static_cast<uint32_t>(window->GetWidth());
    const uint32_t h = static_cast<uint32_t>(window->GetHeight());
    m_SwapChainWidth = w;
    m_SwapChainHeight = h;

    // ---- Device / Queue ----------------------------------------------------
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   IID_PPV_ARGS(&m_Device));
    if (FAILED(hr)) {
        Logger::Error("D3D12CreateDevice failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC cqDesc = {};
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = m_Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&m_CommandQueue));
    if (FAILED(hr)) {
        Logger::Error("CreateCommandQueue failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    // ---- SwapChain ---------------------------------------------------------
    ComPtr<IDXGIFactory4> factory;
    {
        UINT flags = 0;
        hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            Logger::Error("CreateDXGIFactory2 failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount = kFrameCount;
    scd.Width = w;
    scd.Height = h;
    scd.Format = m_RtvFormat;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(
        m_CommandQueue.Get(), hwnd, &scd, nullptr, nullptr, &swapChain1);
    if (FAILED(hr)) {
        Logger::Error("CreateSwapChainForHwnd failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    hr = swapChain1.As(&m_SwapChain);
    if (FAILED(hr)) {
        Logger::Error("swapChain1.As<IDXGISwapChain3> failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    // ---- Descriptor heaps --------------------------------------------------
    // RTV heap (swapchain + offscreen)
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kFrameCount + kOffscreenRtvCount;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_RtvHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(RTV) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }

        m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        auto baseHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < kFrameCount; ++i) {
            m_BackBuffers[i].Reset();
            m_RtvHandles[i] = OffsetHandle(baseHandle, i, m_RtvDescriptorSize);
            hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i]));
            if (FAILED(hr)) {
                Logger::Error("GetBuffer failed: 0x",
                              reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
                return false;
            }
            m_Device->CreateRenderTargetView(m_BackBuffers[i].Get(), nullptr,
                                               m_RtvHandles[i]);
        }
    }

    // Offscreen RTV heap (PostProcessPass)
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kOffscreenRtvCount;
        rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_OffscreenRtvHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(offscreen RTV) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_OffscreenRtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    // DSV heap (PostProcessPass depth)
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvDesc.NumDescriptors = kDsvDescriptorCount;
        dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_Device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_DsvHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(DSV) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_DsvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    // SRV heap (shader-visible, required by ImGui dx12 backend for fonts)
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = kDefaultSrvDescriptorCount;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_Device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_SrvHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(SRV) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }

        m_FontSrvCpuHandle = m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        m_FontSrvGpuHandle = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
    }

    m_SrvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Sampler heap (samplers must be in a separate heap type in D3D12)
    {
        D3D12_DESCRIPTOR_HEAP_DESC sampDesc = {};
        sampDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        sampDesc.NumDescriptors = kDefaultSamplerDescriptorCount;
        sampDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_Device->CreateDescriptorHeap(&sampDesc, IID_PPV_ARGS(&m_SamplerHeap));
        if (FAILED(hr)) {
            Logger::Error("CreateDescriptorHeap(Sampler) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_SamplerDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }

    // Upload command allocator + list (one-shot copies for texture uploads)
    {
        hr = m_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_UploadCommandAllocator));
        if (FAILED(hr)) {
            Logger::Error("CreateCommandAllocator(upload) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        hr = m_Device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_UploadCommandAllocator.Get(),
            nullptr, IID_PPV_ARGS(&m_UploadCommandList));
        if (FAILED(hr)) {
            Logger::Error("CreateCommandList(upload) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_UploadCommandList->Close();
    }

    // ---- Command allocators & list (per-frame) ----------------------------
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        hr = m_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Frames[i].commandAllocator));
        if (FAILED(hr)) {
            Logger::Error("CreateCommandAllocator failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
    }

    hr = m_Device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Frames[0].commandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_CommandList));
    if (FAILED(hr)) {
        Logger::Error("CreateCommandList failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }
    m_CommandList->Close();

    // ---- Constant buffers (per-frame) ------------------------------------
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        auto& fr = m_Frames[i];
        fr.constantBufferCapacity = kDefaultConstantBufferCapacity;
        fr.constantBufferOffset = 0;
        fr.fenceValue = 0;

        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = fr.constantBufferCapacity;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        hr = m_Device->CreateCommittedResource(
            &props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&fr.constantBufferUpload));
        if (FAILED(hr)) {
            Logger::Error("CreateCommittedResource(constantBufferUpload) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }

        D3D12_RANGE range = { 0, fr.constantBufferCapacity };
        hr = fr.constantBufferUpload->Map(0, nullptr,
                                           reinterpret_cast<void**>(&fr.constantBufferMapped));
        if (FAILED(hr) || !fr.constantBufferMapped) {
            Logger::Error("Map(constantBufferUpload) failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
    }

    // ---- Fence / event -----------------------------------------------------
    hr = m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
    if (FAILED(hr)) {
        Logger::Error("CreateFence failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_FenceEvent) {
        Logger::Error("CreateEvent failed");
        return false;
    }

    m_RenderFrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    if (!CreateMainDepthBuffer()) {
        Logger::Error("D3D12Context::Init – CreateMainDepthBuffer failed");
        return false;
    }

    Logger::Info("D3D12Context initialised (", w, "x", h, ")");
    return true;
}

void D3D12Context::Shutdown() {
    ShutdownImGui();

    // Wait for all GPU work to finish before releasing resources.
    if (m_Fence && m_FenceEvent) {
        const uint64_t fenceToWait = m_NextFenceValue > 0 ? (m_NextFenceValue - 1) : 0;
        if (fenceToWait > 0) {
            if (m_Fence->GetCompletedValue() < fenceToWait) {
                m_Fence->SetEventOnCompletion(fenceToWait, m_FenceEvent);
                WaitForSingleObject(m_FenceEvent, INFINITE);
            }
        }
    }

    if (m_IsRecording && m_CommandList) {
        m_CommandList->Close();
        m_IsRecording = false;
    }

    m_DefaultTexture.Reset();
    m_MainDepthBuffer.Reset();
    for (auto& backBuffer : m_BackBuffers) {
        backBuffer.Reset();
    }
    m_CurrentRenderTarget = {};
    m_DsvHeap.Reset();
    m_RtvHeap.Reset();
    m_OffscreenRtvHeap.Reset();
    m_SrvHeap.Reset();
    m_UploadCommandList.Reset();
    m_UploadCommandAllocator.Reset();
    for (auto& frame : m_Frames) {
        if (frame.constantBufferUpload && frame.constantBufferMapped) {
            frame.constantBufferUpload->Unmap(0, nullptr);
        }
        frame.constantBufferMapped = nullptr;
        frame.constantBufferUpload.Reset();
        frame.commandAllocator.Reset();
        frame.constantBufferCapacity = 0;
        frame.constantBufferOffset = 0;
        frame.fenceValue = 0;
    }
    m_CommandList.Reset();
    m_SamplerHeap.Reset();
    m_RenderTargetStack.clear();
    m_SwapChain.Reset();
    m_Fence.Reset();
    m_CommandQueue.Reset();
    m_Device.Reset();

    if (m_FenceEvent) {
        CloseHandle(m_FenceEvent);
        m_FenceEvent = nullptr;
    }

    m_SwapChainWidth = 0;
    m_SwapChainHeight = 0;
    m_RenderFrameIndex = 0;
    m_NextFenceValue = 1;
    m_NextRtvSlot = kFrameCount;
    m_NextOffscreenRtvSlot = 0;
    m_NextDsvSlot = 0;
    m_NextSrvSlot = 1;
    m_NextSampSlot = 0;
    m_BoundShader = nullptr;
    m_DeviceLost = false;
    m_LastDeviceError.clear();
}

void D3D12Context::WaitForFrame(uint32_t frameIndex) {
    const uint64_t fenceValue = m_Frames[frameIndex].fenceValue;
    if (!m_Fence || fenceValue == 0) return;

    if (m_Fence->GetCompletedValue() < fenceValue) {
        if (m_FenceEvent) {
            m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent);
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }
    }
}

void D3D12Context::WaitForGpuIdle()
{
    if (!m_CommandQueue || !m_Fence || !m_FenceEvent) return;
    const uint64_t fenceValue = m_NextFenceValue++;
    if (FAILED(m_CommandQueue->Signal(m_Fence.Get(), fenceValue))) return;
    if (m_Fence->GetCompletedValue() < fenceValue) {
        if (SUCCEEDED(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent))) {
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }
    }
    for (auto& frame : m_Frames) {
        frame.fenceValue = fenceValue;
    }
}

void D3D12Context::TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList,
                                              uint32_t frameIndex) {
    auto* resource = m_BackBuffers[frameIndex].Get();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

void D3D12Context::TransitionToPresent(ID3D12GraphicsCommandList* cmdList,
                                        uint32_t frameIndex) {
    auto* resource = m_BackBuffers[frameIndex].Get();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

void D3D12Context::BeginFrame(float r, float g, float b, float a) {
    if (m_IsRecording) {
        // Avoid nested BeginFrame calls.
        return;
    }
    if (!m_Device || !m_SwapChain) return;

    m_RenderFrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
    WaitForFrame(m_RenderFrameIndex);

    auto& fr = m_Frames[m_RenderFrameIndex];
    fr.constantBufferOffset = 0;

    HRESULT hr = fr.commandAllocator->Reset();
    (void)hr;
    m_CommandList->Reset(fr.commandAllocator.Get(), nullptr);
    m_IsRecording = true;

    // Set descriptors for ImGui (fonts SRV) + engine textures.
    EnsureDefaultResources();
    ID3D12DescriptorHeap* heaps[] = { m_SrvHeap.Get(), m_SamplerHeap.Get() };
    m_CommandList->SetDescriptorHeaps(2, heaps);

    TransitionToRenderTarget(m_CommandList.Get(), m_RenderFrameIndex);
    m_RenderTargetStack.clear();
    m_DepthOnlyBound = false;
    m_CullNone = false;
    m_Wireframe = false;

    const D3D12_CPU_DESCRIPTOR_HANDLE colorRtv = GetMainColorRtv();
    const D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = GetMainDsvHandle();
    const D3D12_CPU_DESCRIPTOR_HANDLE* depthDsvPtr =
        depthDsv.ptr != 0 ? &depthDsv : nullptr;
    m_CommandList->OMSetRenderTargets(1, &colorRtv, FALSE, depthDsvPtr);
    m_CurrentRenderTarget.colorRtv = colorRtv;
    m_CurrentRenderTarget.depthDsv = depthDsv;
    m_CurrentRenderTarget.hasColorTarget = true;

    const float color[4] = { r, g, b, a };
    m_CommandList->ClearRenderTargetView(colorRtv, color, 0, nullptr);
    if (depthDsv.ptr != 0) {
        m_CommandList->ClearDepthStencilView(
            depthDsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    }

    // Default pipeline state expectations.
    m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (m_HasViewport) {
        m_CommandList->RSSetViewports(1, &m_Viewport);
        m_CommandList->RSSetScissorRects(1, &m_ScissorRect);
    }
}

void D3D12Context::EndFrame() {
    if (!m_IsRecording) return;

    const uint32_t frameIndex = m_RenderFrameIndex;
    TransitionToPresent(m_CommandList.Get(), frameIndex);

    m_CommandList->Close();

    ID3D12CommandList* cmdLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(1, cmdLists);

    PresentSwapChain(true);

    const uint64_t fenceValue = m_NextFenceValue++;
    m_CommandQueue->Signal(m_Fence.Get(), fenceValue);
    m_Frames[frameIndex].fenceValue = fenceValue;

    m_IsRecording = false;
}

GpuSwapChain* D3D12Context::GetSwapChain() {
    return m_SwapChainInterface.get();
}

GpuCommandList* D3D12Context::GetGraphicsCommandList() {
    return m_GraphicsCommandList.get();
}

bool D3D12Context::InitImGui(IWindow* window) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!window || !window->GetSDLWindow() || !m_Device || !m_SrvHeap) {
        return false;
    }
    if (!SDL_GetCurrentVideoDriver()) {
        Logger::Error("D3D12Context::InitImGui – SDL_GetCurrentVideoDriver() is null");
        return false;
    }

    ShutdownImGui();
    if (!ImGui_ImplSDL3_InitForD3D(window->GetSDLWindow())) {
        return false;
    }
    if (!ImGui_ImplDX12_Init(
            m_Device.Get(),
            GetNumFramesInFlight(),
            m_RtvFormat,
            m_SrvHeap.Get(),
            m_FontSrvCpuHandle,
            m_FontSrvGpuHandle)) {
        ImGui_ImplSDL3_Shutdown();
        return false;
    }

    m_ImGuiInitialized = true;
    return true;
#else
    (void)window;
    return false;
#endif
}

void D3D12Context::ShutdownImGui() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiInitialized) return;
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    m_ImGuiInitialized = false;
#endif
}

void D3D12Context::ProcessImGuiSDLEvent(const SDL_Event& event) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiInitialized) return;
    ImGui_ImplSDL3_ProcessEvent(&event);
#else
    (void)event;
#endif
}

void D3D12Context::BeginImGuiFrame() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiInitialized) return;
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplSDL3_NewFrame();
#endif
}

void D3D12Context::RenderImGuiDrawData(ImDrawData* drawData) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiInitialized || !drawData || !m_IsRecording || !m_CommandList) {
        return;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE colorRtv = GetMainColorRtv();
    const D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = GetMainDsvHandle();
    const D3D12_CPU_DESCRIPTOR_HANDLE* depthDsvPtr =
        depthDsv.ptr != 0 ? &depthDsv : nullptr;
    m_CommandList->OMSetRenderTargets(1, &colorRtv, FALSE, depthDsvPtr);
    ID3D12DescriptorHeap* heaps[] = { m_SrvHeap.Get() };
    m_CommandList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(drawData, m_CommandList.Get());
#else
    (void)drawData;
#endif
}

void D3D12Context::PresentSwapChain(bool vsync) {
    if (!m_SwapChain) return;
    const HRESULT presentHr = m_SwapChain->Present(vsync ? 1 : 0, 0);
    CheckDeviceResult(presentHr, "D3D12 Present");
}

bool D3D12Context::ResizeSwapChain(uint32_t width, uint32_t height) {
    if (!m_Device || !m_SwapChain || !m_RtvHeap) return false;
    if (width == 0 || height == 0) return false;
    if (m_IsRecording) return false;

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        WaitForFrame(i);
    }

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        m_BackBuffers[i].Reset();
    }
    m_MainDepthBuffer.Reset();
    m_MainDsvHandle = {};
    m_NextDsvSlot = 0;

    HRESULT hr = m_SwapChain->ResizeBuffers(
        kFrameCount, width, height, m_RtvFormat, 0);
    if (FAILED(hr)) {
        Logger::Error("D3D12 ResizeBuffers failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    auto baseHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        m_RtvHandles[i] = OffsetHandle(baseHandle, i, m_RtvDescriptorSize);
        hr = m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i]));
        if (FAILED(hr)) {
            Logger::Error("D3D12 GetBuffer after resize failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return false;
        }
        m_Device->CreateRenderTargetView(m_BackBuffers[i].Get(), nullptr,
                                         m_RtvHandles[i]);
    }

    m_SwapChainWidth = width;
    m_SwapChainHeight = height;
    m_RenderFrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    if (!CreateMainDepthBuffer()) {
        Logger::Error("D3D12 ResizeSwapChain – CreateMainDepthBuffer failed");
        return false;
    }

    return true;
}

bool D3D12Context::CheckDeviceResult(HRESULT hr, const char* operation) {
    if (SUCCEEDED(hr)) return true;
    std::ostringstream stream;
    stream << operation << " failed: 0x" << std::hex << static_cast<unsigned long>(hr);
    m_LastDeviceError = stream.str();
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
        hr == DXGI_ERROR_DEVICE_HUNG) {
        m_DeviceLost = true;
    }
    Logger::Error(m_LastDeviceError);
    return false;
}

std::shared_ptr<GpuBuffer> D3D12Context::CreateVertexBuffer(
    const void* data, uint32_t byteSize, uint32_t strideBytes) {
    if (!data || byteSize == 0 || byteSize % 4 != 0) {
        // Accept any size, but log in case something is clearly wrong.
        // We keep this minimal to avoid over-rejecting.
    }

    auto buf = std::make_shared<D3D12VertexBuffer>();
    buf->byteSize = byteSize;
    buf->stride = strideBytes;

    const uint64_t alignedSize = (static_cast<uint64_t>(byteSize) + 255ull) & ~255ull;

    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = alignedSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_Device->CreateCommittedResource(
        &props, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf->resource));
    if (FAILED(hr)) {
        Logger::Error("CreateVertexBuffer failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }

    void* mapped = nullptr;
    D3D12_RANGE range = { 0, byteSize };
    hr = buf->resource->Map(0, nullptr, &mapped);
    if (FAILED(hr) || !mapped) {
        Logger::Error("VertexBuffer Map failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }
    std::memcpy(mapped, data, byteSize);
    buf->resource->Unmap(0, nullptr);

    return buf;
}

std::shared_ptr<GpuBuffer> D3D12Context::CreateIndexBuffer(
    const void* data, uint32_t byteSize) {
    if (!data || byteSize == 0) return nullptr;

    auto buf = std::make_shared<D3D12IndexBuffer>();
    buf->byteSize = byteSize;
    buf->format = DXGI_FORMAT_R32_UINT;

    const uint64_t alignedSize = (static_cast<uint64_t>(byteSize) + 255ull) & ~255ull;

    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = alignedSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_Device->CreateCommittedResource(
        &props, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf->resource));
    if (FAILED(hr)) {
        Logger::Error("CreateIndexBuffer failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }

    void* mapped = nullptr;
    D3D12_RANGE range = { 0, byteSize };
    hr = buf->resource->Map(0, nullptr, &mapped);
    if (FAILED(hr) || !mapped) {
        Logger::Error("IndexBuffer Map failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }
    std::memcpy(mapped, data, byteSize);
    buf->resource->Unmap(0, nullptr);

    return buf;
}

namespace {

bool CreateTextureRootSignature(ID3D12Device* device, ID3D12RootSignature** outRootSig)
{
    if (!device || !outRootSig) return false;

    D3D12_DESCRIPTOR_RANGE ranges[D3D12Context::kTextureSlotCount * 2] = {};
    for (uint32_t slot = 0; slot < D3D12Context::kTextureSlotCount; ++slot) {
        auto& srv = ranges[slot * 2];
        srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv.NumDescriptors = 1;
        srv.BaseShaderRegister = slot;
        srv.RegisterSpace = 0;

        auto& sampler = ranges[slot * 2 + 1];
        sampler.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampler.NumDescriptors = 1;
        sampler.BaseShaderRegister = slot;
        sampler.RegisterSpace = 0;
    }

    D3D12_ROOT_PARAMETER rootParams[1 + D3D12Context::kTextureSlotCount * 2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    for (uint32_t i = 0; i < D3D12Context::kTextureSlotCount * 2; ++i) {
        rootParams[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[i + 1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
        rootParams[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1 + D3D12Context::kTextureSlotCount * 2;
    rsDesc.pParameters = rootParams;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Error("D3D12SerializeRootSignature failed: ",
                          static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return false;
    }

    hr = device->CreateRootSignature(
        0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(outRootSig));
    return SUCCEEDED(hr);
}

} // namespace

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetMainColorRtv() const
{
    return m_RtvHandles[m_RenderFrameIndex];
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::GetMainDsvHandle() const
{
    return m_MainDsvHandle;
}

bool D3D12Context::CreateMainDepthBuffer()
{
    if (!m_Device || m_SwapChainWidth == 0 || m_SwapChainHeight == 0) {
        return false;
    }

    m_MainDepthBuffer.Reset();
    m_MainDsvHandle = {};

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_SwapChainWidth;
    desc.Height = m_SwapChainHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kDepthTypelessFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = kDepthFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    HRESULT hr = m_Device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
        IID_PPV_ARGS(&m_MainDepthBuffer));
    if (FAILED(hr)) {
        Logger::Error("CreateMainDepthBuffer failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    m_MainDsvHandle = AllocDsvSlot();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = kDepthFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    m_Device->CreateDepthStencilView(m_MainDepthBuffer.Get(), &dsvDesc, m_MainDsvHandle);
    return true;
}

void D3D12Context::PushRenderTarget(const D3D12_CPU_DESCRIPTOR_HANDLE* colorRtv,
                                      D3D12_CPU_DESCRIPTOR_HANDLE depthDsv)
{
    if (!m_IsRecording || !m_CommandList) return;

    m_RenderTargetStack.push_back(m_CurrentRenderTarget);

    const D3D12_CPU_DESCRIPTOR_HANDLE* depthDsvPtr =
        depthDsv.ptr != 0 ? &depthDsv : nullptr;
    if (colorRtv && colorRtv->ptr != 0) {
        m_CommandList->OMSetRenderTargets(1, colorRtv, FALSE, depthDsvPtr);
        m_CurrentRenderTarget.colorRtv = *colorRtv;
        m_CurrentRenderTarget.depthDsv = depthDsv;
        m_CurrentRenderTarget.hasColorTarget = true;
    } else {
        m_CommandList->OMSetRenderTargets(0, nullptr, FALSE, depthDsvPtr);
        m_CurrentRenderTarget.colorRtv = {};
        m_CurrentRenderTarget.depthDsv = depthDsv;
        m_CurrentRenderTarget.hasColorTarget = false;
    }
}

void D3D12Context::PopRenderTarget()
{
    if (!m_IsRecording || !m_CommandList || m_RenderTargetStack.empty()) return;

    const RenderTargetBinding restore = m_RenderTargetStack.back();
    m_RenderTargetStack.pop_back();

    if (restore.hasColorTarget && restore.colorRtv.ptr != 0) {
        const D3D12_CPU_DESCRIPTOR_HANDLE* dsv =
            restore.depthDsv.ptr != 0 ? &restore.depthDsv : nullptr;
        m_CommandList->OMSetRenderTargets(1, &restore.colorRtv, FALSE, dsv);
    } else if (restore.depthDsv.ptr != 0) {
        m_CommandList->OMSetRenderTargets(0, nullptr, FALSE, &restore.depthDsv);
    } else {
        m_CommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    }
    m_CurrentRenderTarget = restore;
}

void D3D12Context::BindDepthOnlyShader(GpuShader* shader)
{
    if (!m_IsRecording || !shader) return;
    auto* s = static_cast<D3D12Shader*>(shader);
    m_BoundShader = s;
    m_DepthOnlyBound = true;
    m_CommandList->SetGraphicsRootSignature(s->rootSignature.Get());
    if (s->depthOnlyPipelineState) {
        m_CommandList->SetPipelineState(s->depthOnlyPipelineState.Get());
    }
}

std::shared_ptr<GpuTexture> D3D12Context::CreateDepthTexture(
    int width, int height, bool cube, uint32_t arraySize)
{
    if (!m_Device || width <= 0 || height <= 0) return nullptr;

    EnsureDefaultResources();

    if (cube) {
        arraySize = 6;
    } else if (arraySize == 0) {
        arraySize = 1;
    }

    auto tex = std::make_shared<D3D12Texture>();
    tex->isCube = cube;
    tex->arraySize = arraySize;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT64>(width);
    desc.Height = static_cast<UINT>(height);
    desc.DepthOrArraySize = arraySize;
    desc.MipLevels = 1;
    desc.Format = kDepthTypelessFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = kDepthFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    HRESULT hr = m_Device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
        IID_PPV_ARGS(&tex->resource));
    if (FAILED(hr)) {
        Logger::Error("CreateDepthTexture failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }

    tex->srvCpu = AllocSrvSlot(tex->srvGpu);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    if (cube) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = 1;
    } else if (arraySize > 1) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.ArraySize = arraySize;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    }
    m_Device->CreateShaderResourceView(tex->resource.Get(), &srvDesc, tex->srvCpu);

    tex->sampCpu = AllocSampSlot(tex->sampGpu);
    D3D12_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
    m_Device->CreateSampler(&sampDesc, tex->sampCpu);

    if (cube) {
        tex->dsvFaces.resize(6);
        for (uint32_t face = 0; face < 6; ++face) {
            tex->dsvFaces[face] = AllocDsvSlot();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = kDepthFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.ArraySize = 1;
            dsvDesc.Texture2DArray.FirstArraySlice = face;
            dsvDesc.Texture2DArray.MipSlice = 0;
            m_Device->CreateDepthStencilView(
                tex->resource.Get(), &dsvDesc, tex->dsvFaces[face]);
        }
        tex->dsvCpu = tex->dsvFaces[0];
    } else if (arraySize > 1) {
        tex->dsvFaces.resize(arraySize);
        for (uint32_t slice = 0; slice < arraySize; ++slice) {
            tex->dsvFaces[slice] = AllocDsvSlot();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = kDepthFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.ArraySize = 1;
            dsvDesc.Texture2DArray.FirstArraySlice = slice;
            dsvDesc.Texture2DArray.MipSlice = 0;
            m_Device->CreateDepthStencilView(
                tex->resource.Get(), &dsvDesc, tex->dsvFaces[slice]);
        }
        tex->dsvCpu = tex->dsvFaces[0];
    } else {
        tex->dsvCpu = AllocDsvSlot();
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = kDepthFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        m_Device->CreateDepthStencilView(tex->resource.Get(), &dsvDesc, tex->dsvCpu);
    }

    return tex;
}

bool D3D12Context::BuildShaderPipelines(D3D12Shader& shader,
                                          const D3D12_SHADER_BYTECODE& vs,
                                          const D3D12_SHADER_BYTECODE& ps,
                                          const VertexElement* layout,
                                          uint32_t layoutCount)
{
    std::vector<D3D12_INPUT_ELEMENT_DESC> descs(layoutCount);
    for (uint32_t i = 0; i < layoutCount; ++i) {
        descs[i] = {
            layout[i].semantic,
            layout[i].index,
            ToDxgiFormat(layout[i].format),
            0,
            layout[i].offset,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        };
    }

    auto initPsoDesc = [&](D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc) {
        psoDesc = {};
        psoDesc.InputLayout = { descs.data(), static_cast<UINT>(layoutCount) };
        psoDesc.pRootSignature = shader.rootSignature.Get();
        psoDesc.VS = vs;
        psoDesc.PS = ps;

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
        psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.RasterizerState.MultisampleEnable = FALSE;
        psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        psoDesc.RasterizerState.ForcedSampleCount = 0;
        psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
        psoDesc.BlendState.IndependentBlendEnable = FALSE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;

        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable = FALSE;

        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = m_RtvFormat;
        psoDesc.DSVFormat = kDepthFormat;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleDesc.Quality = 0;
        psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    initPsoDesc(psoDesc);

    HRESULT hr = m_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&shader.pipelineState));
    if (FAILED(hr)) {
        Logger::Error("CreateGraphicsPipelineState failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return false;
    }

    auto& blendTarget = psoDesc.BlendState.RenderTarget[0];
    blendTarget.BlendEnable = TRUE;
    blendTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendTarget.BlendOp = D3D12_BLEND_OP_ADD;
    blendTarget.SrcBlendAlpha = D3D12_BLEND_ONE;
    blendTarget.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendTarget.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    hr = m_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&shader.alphaPipelineState));
    if (FAILED(hr)) return false;

    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    blendTarget.BlendEnable = FALSE;
    hr = m_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&shader.twoSidedPipelineState));
    if (FAILED(hr)) return false;

    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    hr = m_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&shader.wireframePipelineState));
    if (FAILED(hr)) return false;

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthBias = 1536;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 2.0f;
    psoDesc.NumRenderTargets = 0;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    hr = m_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&shader.depthOnlyPipelineState));
    return SUCCEEDED(hr);
}

std::shared_ptr<GpuShader> D3D12Context::CreateShader(
    const std::string& hlslSource,
    const std::string& vsEntry,
    const std::string& psEntry,
    const VertexElement* layout,
    uint32_t layoutCount) {
    if (layoutCount > 0 && !layout) return nullptr;

    auto sh = std::make_shared<D3D12Shader>();

    auto compileShader = [&](const std::string& entry,
                             const std::string& target,
                             ComPtr<ID3DBlob>& outBlob) -> bool {
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

    if (!CreateTextureRootSignature(m_Device.Get(), &sh->rootSignature)) {
        return nullptr;
    }

    const D3D12_SHADER_BYTECODE vs = {
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    const D3D12_SHADER_BYTECODE ps = {
        psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    if (!BuildShaderPipelines(*sh, vs, ps, layout, layoutCount)) {
        return nullptr;
    }

    return sh;
}

std::shared_ptr<GpuShader> D3D12Context::CreateShaderFromBytecode(
    const void* vsBytecode,
    size_t vsSize,
    const void* psBytecode,
    size_t psSize,
    const VertexElement* layout,
    uint32_t layoutCount) {
    if (!vsBytecode || vsSize == 0 || !psBytecode || psSize == 0 ||
        (layoutCount > 0 && !layout)) {
        return nullptr;
    }

    auto sh = std::make_shared<D3D12Shader>();

    if (!CreateTextureRootSignature(m_Device.Get(), &sh->rootSignature)) {
        return nullptr;
    }

    const D3D12_SHADER_BYTECODE vs = { vsBytecode, vsSize };
    const D3D12_SHADER_BYTECODE ps = { psBytecode, psSize };
    if (!BuildShaderPipelines(*sh, vs, ps, layout, layoutCount)) {
        return nullptr;
    }

    return sh;
}

void D3D12Context::ApplyBoundPipelineState()
{
    if (!m_IsRecording || !m_BoundShader || m_DepthOnlyBound) return;

    ID3D12PipelineState* pso = m_BoundShader->pipelineState.Get();
    if (m_Wireframe && m_BoundShader->wireframePipelineState) {
        pso = m_BoundShader->wireframePipelineState.Get();
    } else if (m_CullNone && m_BoundShader->twoSidedPipelineState) {
        pso = m_BoundShader->twoSidedPipelineState.Get();
    } else if (m_BlendMode == GpuBlendMode::Alpha) {
        pso = m_BoundShader->alphaPipelineState.Get();
    }
    m_CommandList->SetPipelineState(pso);
}

void D3D12Context::SetRasterState(bool twoSided, bool wireframe)
{
    m_CullNone = twoSided;
    m_Wireframe = wireframe;
    ApplyBoundPipelineState();
}

void D3D12Context::BindShader(GpuShader* shader) {
    if (!m_IsRecording || !shader) return;
    auto* s = static_cast<D3D12Shader*>(shader);
    m_BoundShader = s;
    m_DepthOnlyBound = false;
    m_CommandList->SetGraphicsRootSignature(s->rootSignature.Get());
    ApplyBoundPipelineState();
}

void D3D12Context::SetBlendMode(GpuBlendMode mode)
{
    m_BlendMode = mode;
    ApplyBoundPipelineState();
}

void D3D12Context::BindVertexBuffer(GpuBuffer* buffer) {
    if (!m_IsRecording || !buffer) return;
    auto* b = static_cast<D3D12VertexBuffer*>(buffer);

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = b->resource->GetGPUVirtualAddress();
    vbv.SizeInBytes = b->byteSize;
    vbv.StrideInBytes = b->stride;
    m_CommandList->IASetVertexBuffers(0, 1, &vbv);
}

void D3D12Context::BindIndexBuffer(GpuBuffer* buffer) {
    if (!m_IsRecording) return;
    if (!buffer) return;

    auto* b = static_cast<D3D12IndexBuffer*>(buffer);
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = b->resource->GetGPUVirtualAddress();
    ibv.SizeInBytes = b->byteSize;
    ibv.Format = b->format;
    m_CommandList->IASetIndexBuffer(&ibv);
}

void D3D12Context::SetVSConstants(const void* data, uint32_t byteSize) {
    if (!m_IsRecording || !data || byteSize == 0) return;

    auto& fr = m_Frames[m_RenderFrameIndex];

    // D3D12 constant buffer view alignment requirement is 256 bytes.
    const uint32_t alignedSize = (byteSize + 255u) & ~255u;
    uint32_t alignedOffset = (fr.constantBufferOffset + 255u) & ~255u;

    if (alignedOffset + alignedSize > fr.constantBufferCapacity) {
        // Grow upload buffer (rare). We only do this while the frame isn't
        // executing yet; BeginFrame already waited for the frame fence.
        uint32_t newCap = fr.constantBufferCapacity;
        while (newCap < alignedOffset + alignedSize) newCap *= 2;

        D3D12_HEAP_PROPERTIES props = {};
        props.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = newCap;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> newRes;
        HRESULT hr = m_Device->CreateCommittedResource(
            &props, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&newRes));
        if (FAILED(hr) || !newRes) {
            Logger::Error("Grow constant buffer failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return;
        }

        uint8_t* mapped = nullptr;
        hr = newRes->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr) || !mapped) {
            Logger::Error("Grow constant buffer Map failed: 0x",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
            return;
        }

        fr.constantBufferUpload = newRes;
        fr.constantBufferMapped = mapped;
        fr.constantBufferCapacity = newCap;
    }

    std::memcpy(fr.constantBufferMapped + alignedOffset, data, byteSize);

    const D3D12_GPU_VIRTUAL_ADDRESS addr =
        fr.constantBufferUpload->GetGPUVirtualAddress() + alignedOffset;
    m_CommandList->SetGraphicsRootConstantBufferView(0, addr);

    fr.constantBufferOffset = alignedOffset + alignedSize;
}

void D3D12Context::Draw(uint32_t vertexCount, uint32_t startVertex) {
    if (!m_IsRecording) return;
    m_CommandList->DrawInstanced(vertexCount, 1, startVertex, 0);
}

void D3D12Context::DrawIndexed(uint32_t indexCount, uint32_t startIndex,
                               uint32_t baseVertex) {
    if (!m_IsRecording) return;
    m_CommandList->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
}

void D3D12Context::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                                 uint32_t startVertex) {
    if (!m_IsRecording) return;
    m_CommandList->DrawInstanced(vertexCount, instanceCount, startVertex, 0);
}

void D3D12Context::DrawIndexedInstanced(
    uint32_t indexCount, uint32_t instanceCount,
    uint32_t startIndex, uint32_t baseVertex) {
    if (!m_IsRecording) return;
    m_CommandList->DrawIndexedInstanced(
        indexCount, instanceCount, startIndex, baseVertex, 0);
}

void D3D12Context::SetViewport(float x, float y, float w, float h) {
    m_Viewport = {};
    m_Viewport.TopLeftX = x;
    m_Viewport.TopLeftY = y;
    m_Viewport.Width = w;
    m_Viewport.Height = h;
    m_Viewport.MinDepth = 0.0f;
    m_Viewport.MaxDepth = 1.0f;
    m_ScissorRect.left = static_cast<LONG>(x);
    m_ScissorRect.top = static_cast<LONG>(y);
    m_ScissorRect.right = static_cast<LONG>(x + w);
    m_ScissorRect.bottom = static_cast<LONG>(y + h);
    m_HasViewport = true;

    if (m_IsRecording && m_CommandList) {
        m_CommandList->RSSetViewports(1, &m_Viewport);
        m_CommandList->RSSetScissorRects(1, &m_ScissorRect);
    }
}

// --------------------------------------------------------------------------
// Texture upload
// --------------------------------------------------------------------------

void D3D12Context::EnsureDefaultResources()
{
    if (m_DefaultTexture) return;

    // --- Default 1x1 black texture ---
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 1;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = m_Device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&m_DefaultTexture));
        if (FAILED(hr)) {
            Logger::Error("D3D12: EnsureDefaultResources – CreateCommittedResource failed");
            return;
        }
    }

    // Default SRV
    m_DefaultTexSrvCpu = AllocSrvSlot(m_DefaultTexSrvGpu);
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        m_Device->CreateShaderResourceView(m_DefaultTexture.Get(), &srvDesc, m_DefaultTexSrvCpu);
    }

    // Default sampler
    m_DefaultSampCpu = AllocSampSlot(m_DefaultSampGpu);
    {
        D3D12_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampDesc.MaxAnisotropy = 1;
        sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
        m_Device->CreateSampler(&sampDesc, m_DefaultSampCpu);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocSrvSlot(D3D12_GPU_DESCRIPTOR_HANDLE& outGpu)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = OffsetHandle(
        m_SrvHeap->GetCPUDescriptorHandleForHeapStart(), m_NextSrvSlot, m_SrvDescriptorSize);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = OffsetHandle(
        m_SrvHeap->GetGPUDescriptorHandleForHeapStart(), m_NextSrvSlot, m_SrvDescriptorSize);
    ++m_NextSrvSlot;
    outGpu = gpu;
    return cpu;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocRtvSlot()
{
    if (!m_OffscreenRtvHeap) return {};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = OffsetHandle(
        m_OffscreenRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_NextOffscreenRtvSlot,
        m_OffscreenRtvDescriptorSize);
    ++m_NextOffscreenRtvSlot;
    return cpu;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocDsvSlot()
{
    if (!m_DsvHeap) return {};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = OffsetHandle(
        m_DsvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_NextDsvSlot,
        m_DsvDescriptorSize);
    ++m_NextDsvSlot;
    return cpu;
}

void D3D12Context::ResetPostProcessDescriptorAllocators()
{
    m_NextOffscreenRtvSlot = 0;
    m_NextDsvSlot = 0;
    m_NextSrvSlot = 1; // slot 0 reserved for ImGui font
    m_NextSampSlot = 0;
}

void D3D12Context::BindPSTextureDescriptors(
    uint32_t slot,
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu,
    D3D12_GPU_DESCRIPTOR_HANDLE sampGpu)
{
    if (!m_IsRecording || !m_CommandList) return;
    EnsureDefaultResources();

    const D3D12_GPU_DESCRIPTOR_HANDLE srv =
        srvGpu.ptr ? srvGpu : m_DefaultTexSrvGpu;
    const D3D12_GPU_DESCRIPTOR_HANDLE samp =
        sampGpu.ptr ? sampGpu : m_DefaultSampGpu;

    const uint32_t srvParam  = 1 + slot * 2;
    const uint32_t sampParam = 2 + slot * 2;
    m_CommandList->SetGraphicsRootDescriptorTable(srvParam, srv);
    m_CommandList->SetGraphicsRootDescriptorTable(sampParam, samp);
}

std::shared_ptr<GpuShader> D3D12Context::CreateFullscreenShaderFromBytecode(
    const void* vsBytecode,
    size_t vsSize,
    const void* psBytecode,
    size_t psSize,
    DXGI_FORMAT rtvFormat)
{
    if (!vsBytecode || vsSize == 0 || !psBytecode || psSize == 0) {
        return nullptr;
    }
    if (rtvFormat == DXGI_FORMAT_UNKNOWN) {
        rtvFormat = m_RtvFormat;
    }

    auto sh = std::make_shared<D3D12Shader>();

    static constexpr uint32_t kTextureSlotCount = D3D12Context::kTextureSlotCount;
    D3D12_DESCRIPTOR_RANGE ranges[kTextureSlotCount * 2] = {};
    for (uint32_t slot = 0; slot < kTextureSlotCount; ++slot) {
        auto& srv = ranges[slot * 2];
        srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv.NumDescriptors = 1;
        srv.BaseShaderRegister = slot;
        srv.RegisterSpace = 0;

        auto& sampler = ranges[slot * 2 + 1];
        sampler.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        sampler.NumDescriptors = 1;
        sampler.BaseShaderRegister = slot;
        sampler.RegisterSpace = 0;
    }

    D3D12_ROOT_PARAMETER rootParams[1 + kTextureSlotCount * 2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    for (uint32_t i = 0; i < kTextureSlotCount * 2; ++i) {
        rootParams[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[i + 1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
        rootParams[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1 + kTextureSlotCount * 2;
    rsDesc.pParameters = rootParams;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig, &errorBlob);
    if (FAILED(hr)) {
        return nullptr;
    }

    hr = m_Device->CreateRootSignature(
        0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&sh->rootSignature));
    if (FAILED(hr)) {
        return nullptr;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = sh->rootSignature.Get();
    psoDesc.VS = { vsBytecode, vsSize };
    psoDesc.PS = { psBytecode, psSize };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;

    hr = m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&sh->pipelineState));
    if (FAILED(hr)) {
        return nullptr;
    }
    sh->alphaPipelineState = sh->pipelineState;
    return sh;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::AllocSampSlot(D3D12_GPU_DESCRIPTOR_HANDLE& outGpu)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = OffsetHandle(
        m_SamplerHeap->GetCPUDescriptorHandleForHeapStart(), m_NextSampSlot, m_SamplerDescriptorSize);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = OffsetHandle(
        m_SamplerHeap->GetGPUDescriptorHandleForHeapStart(), m_NextSampSlot, m_SamplerDescriptorSize);
    ++m_NextSampSlot;
    outGpu = gpu;
    return cpu;
}

std::shared_ptr<GpuTexture> D3D12Context::UploadTexture2D(
    const void* rgba8Data, int width, int height)
{
    if (!rgba8Data || width <= 0 || height <= 0 || !m_Device) return nullptr;

    EnsureDefaultResources();

    // 1. Create default-heap texture (destination)
    ComPtr<ID3D12Resource> texResource;
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = static_cast<UINT64>(width);
        desc.Height = static_cast<UINT>(height);
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = m_Device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&texResource));
        if (FAILED(hr)) {
            Logger::Error("D3D12: UploadTexture2D – CreateCommittedResource (default) failed");
            return nullptr;
        }
    }

    // 2. Query required upload buffer size and layout
    D3D12_RESOURCE_DESC texDesc = texResource->GetDesc();
    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    m_Device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

    // 3. Create upload buffer and copy pixel rows
    ComPtr<ID3D12Resource> uploadBuffer;
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = uploadSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_Device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuffer));
        if (FAILED(hr)) {
            Logger::Error("D3D12: UploadTexture2D – CreateCommittedResource (upload) failed");
            return nullptr;
        }
    }

    {
        void* mapped = nullptr;
        uploadBuffer->Map(0, nullptr, &mapped);
        if (!mapped) {
            Logger::Error("D3D12: UploadTexture2D – Map failed");
            return nullptr;
        }
        const uint8_t* src = static_cast<const uint8_t*>(rgba8Data);
        uint8_t* dst = static_cast<uint8_t*>(mapped);
        const UINT srcRowPitch = static_cast<UINT>(width) * 4;
        for (int row = 0; row < height; ++row) {
            std::memcpy(dst + row * footprint.Footprint.RowPitch,
                        src + row * srcRowPitch,
                        srcRowPitch);
        }
        uploadBuffer->Unmap(0, nullptr);
    }

    // 4. GPU copy: upload buffer → default texture
    {
        m_UploadCommandAllocator->Reset();
        m_UploadCommandList->Reset(m_UploadCommandAllocator.Get(), nullptr);

        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = texResource.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = uploadBuffer.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprint;

        m_UploadCommandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        // Transition to shader resource
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texResource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_UploadCommandList->ResourceBarrier(1, &barrier);

        m_UploadCommandList->Close();

        ID3D12CommandList* cmdLists[] = { m_UploadCommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, cmdLists);

        // Wait for copy to finish before we let the upload buffer go
        const uint64_t fenceVal = m_NextFenceValue++;
        m_CommandQueue->Signal(m_Fence.Get(), fenceVal);
        if (m_Fence->GetCompletedValue() < fenceVal) {
            m_Fence->SetEventOnCompletion(fenceVal, m_FenceEvent);
            WaitForSingleObject(m_FenceEvent, INFINITE);
        }
    }

    // 5. Create SRV and sampler in descriptor heaps
    auto tex = std::make_shared<D3D12Texture>();
    tex->resource = texResource;

    tex->srvCpu = AllocSrvSlot(tex->srvGpu);
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        m_Device->CreateShaderResourceView(tex->resource.Get(), &srvDesc, tex->srvCpu);
    }

    tex->sampCpu = AllocSampSlot(tex->sampGpu);
    {
        D3D12_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampDesc.MaxAnisotropy = 1;
        sampDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampDesc.MaxLOD = D3D12_FLOAT32_MAX;
        m_Device->CreateSampler(&sampDesc, tex->sampCpu);
    }

    return tex;
}

void D3D12Context::BindPSTexture(uint32_t slot, GpuTexture* tex)
{
    if (!m_IsRecording || !m_CommandList) return;
    EnsureDefaultResources();

    auto* d3dTex = static_cast<D3D12Texture*>(tex);
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = d3dTex ? d3dTex->srvGpu : m_DefaultTexSrvGpu;
    const D3D12_GPU_DESCRIPTOR_HANDLE sampGpu = d3dTex ? d3dTex->sampGpu : m_DefaultSampGpu;

    // Root signature layout: [0]=CBV, [1]=SRV(t0), [2]=Samp(s0), [3]=SRV(t1), [4]=Samp(s1)
    const uint32_t srvParam  = 1 + slot * 2;
    const uint32_t sampParam = 2 + slot * 2;

    m_CommandList->SetGraphicsRootDescriptorTable(srvParam, srvGpu);
    m_CommandList->SetGraphicsRootDescriptorTable(sampParam, sampGpu);
}
