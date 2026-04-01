#include "D3D12Context.h"

#include "../Core/Logger.h"
#include "../Core/Window.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <d3dcompiler.h>

#include <windows.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#if defined(MYENGINE_ENABLE_IMGUI)
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

    void SetViewport(float x, float y, float w, float h) override {
        m_Owner.SetViewport(x, y, w, h);
    }

    void BindPSTexture(uint32_t slot, GpuTexture* tex) override {
        m_Owner.BindPSTexture(slot, tex);
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
    // RTV heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kFrameCount;
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

    // ---- Command allocators & list ----------------------------------------
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

    Logger::Info("D3D12Context initialised (", w, "x", h, ")");
    return true;
}

void D3D12Context::Shutdown() {
    ShutdownImGui();

    if (m_IsRecording) {
        // Best effort: close command list before shutdown.
        if (m_CommandList) {
            m_CommandList->Close();
        }
        m_IsRecording = false;
    }

    if (m_Fence && m_FenceEvent) {
        const uint64_t fenceToWait = m_NextFenceValue > 0 ? (m_NextFenceValue - 1) : 0;
        if (fenceToWait > 0) {
            if (m_Fence->GetCompletedValue() < fenceToWait) {
                m_Fence->SetEventOnCompletion(fenceToWait, m_FenceEvent);
                WaitForSingleObject(m_FenceEvent, INFINITE);
            }
        }
    }

    if (m_FenceEvent) {
        CloseHandle(m_FenceEvent);
        m_FenceEvent = nullptr;
    }

    m_SwapChainWidth = 0;
    m_SwapChainHeight = 0;
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

    // Set descriptors for ImGui (fonts SRV).
    ID3D12DescriptorHeap* heaps[] = { m_SrvHeap.Get() };
    m_CommandList->SetDescriptorHeaps(1, heaps);

    TransitionToRenderTarget(m_CommandList.Get(), m_RenderFrameIndex);
    m_CommandList->OMSetRenderTargets(1, &m_RtvHandles[m_RenderFrameIndex],
                                      FALSE, nullptr);

    const float color[4] = { r, g, b, a };
    m_CommandList->ClearRenderTargetView(m_RtvHandles[m_RenderFrameIndex],
                                          color, 0, nullptr);

    // Default pipeline state expectations.
    m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (m_HasViewport) {
        m_CommandList->RSSetViewports(1, &m_Viewport);
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
    ImGui_ImplDX12_RenderDrawData(drawData, m_CommandList.Get());
#else
    (void)drawData;
#endif
}

void D3D12Context::PresentSwapChain(bool vsync) {
    if (!m_SwapChain) return;
    const HRESULT presentHr = m_SwapChain->Present(vsync ? 1 : 0, 0);
    (void)presentHr;
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

    return true;
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

std::shared_ptr<GpuShader> D3D12Context::CreateShader(
    const std::string& hlslSource,
    const std::string& vsEntry,
    const std::string& psEntry,
    const VertexElement* layout,
    uint32_t layoutCount) {
    if (!layout || layoutCount == 0) return nullptr;

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

    // ---- Root signature (cbuffer PerDraw : register(b0)) -------------------
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &rootParam;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
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
        return nullptr;
    }

    hr = m_Device->CreateRootSignature(
        0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&sh->rootSignature));
    if (FAILED(hr)) {
        Logger::Error("CreateRootSignature failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }

    // ---- Input layout ------------------------------------------------------
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

    // ---- Pipeline state ---------------------------------------------------
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { descs.data(), static_cast<UINT>(layoutCount) };
    psoDesc.pRootSignature = sh->rootSignature.Get();

    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    psoDesc.RasterizerState = {};
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

    psoDesc.BlendState = {};
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.DepthStencilState = {};
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_RtvFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;

    hr = m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&sh->pipelineState));
    if (FAILED(hr)) {
        Logger::Error("CreateGraphicsPipelineState failed: 0x",
                      reinterpret_cast<void*>(static_cast<uintptr_t>(hr)));
        return nullptr;
    }

    return sh;
}

void D3D12Context::BindShader(GpuShader* shader) {
    if (!m_IsRecording || !shader) return;
    auto* s = static_cast<D3D12Shader*>(shader);
    m_CommandList->SetGraphicsRootSignature(s->rootSignature.Get());
    m_CommandList->SetPipelineState(s->pipelineState.Get());
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

void D3D12Context::SetViewport(float x, float y, float w, float h) {
    m_Viewport = {};
    m_Viewport.TopLeftX = x;
    m_Viewport.TopLeftY = y;
    m_Viewport.Width = w;
    m_Viewport.Height = h;
    m_Viewport.MinDepth = 0.0f;
    m_Viewport.MaxDepth = 1.0f;
    m_HasViewport = true;

    if (m_IsRecording && m_CommandList) {
        m_CommandList->RSSetViewports(1, &m_Viewport);
    }
}

// Texture upload – not yet implemented for D3D12; returns nullptr.
std::shared_ptr<GpuTexture> D3D12Context::UploadTexture2D(
    const void*, int, int)
{
    return nullptr;
}

void D3D12Context::BindPSTexture(uint32_t, GpuTexture*)
{
    // No-op for D3D12 (not yet implemented).
}
