// MetalContext.mm – Metal rendering back-end (Objective-C++)
// Only compiled on macOS (guarded by MYENGINE_PLATFORM_MACOS, and the file is
// only compiled on macOS in xmake.lua).

#include "Core/Platform.h"
#ifdef MYENGINE_PLATFORM_MACOS

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "Renderer/MetalContext.h"
#include "Core/Window.h"
#include "Core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <algorithm>
#include <cstring>
#include <vector>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <backends/imgui_impl_metal.h>
#include <backends/imgui_impl_sdl3.h>
#endif

// ============================================================================
// GPU resource types
// ============================================================================

struct MetalGpuBuffer : GpuBuffer {
    id<MTLBuffer> buffer;
    uint32_t      stride   = 0;
    uint32_t      byteSize = 0;
};

struct MetalGpuShader : GpuShader {
    id<MTLRenderPipelineState> pipelineState;
    id<MTLDepthStencilState>   depthState;
};

// ============================================================================
// Impl – holds all ObjC/Metal objects
// ============================================================================

struct MetalContext::Impl {
    id<MTLDevice>               device;
    id<MTLCommandQueue>         queue;
    CAMetalLayer*               layer     = nil;
    SDL_MetalView               metalView = nullptr;

    // Per-frame state
    id<CAMetalDrawable>         drawable;
    id<MTLCommandBuffer>        cmdBuffer;
    id<MTLRenderCommandEncoder> encoder;
    MTLRenderPassDescriptor*    currentRPD = nil;
    id<MTLTexture>              depthTexture;

    // Bound state
    MetalGpuBuffer* boundVB     = nullptr;
    MetalGpuBuffer* boundIB     = nullptr;

    // Viewport (persistent across frames)
    float vpX = 0, vpY = 0, vpW = 0, vpH = 0;

    // Cached drawable size
    uint32_t drawableW = 0;
    uint32_t drawableH = 0;

    void EnsureDepthTexture(uint32_t w, uint32_t h) {
        if (depthTexture &&
            depthTexture.width  == w &&
            depthTexture.height == h) {
            return;
        }
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:w
                                                              height:h
                                                           mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage       = MTLTextureUsageRenderTarget;
        depthTexture = [device newTextureWithDescriptor:desc];
    }
};

class MetalImmediateCommandList final : public GpuCommandList {
public:
    explicit MetalImmediateCommandList(MetalContext& owner)
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
        return m_Owner.GetCommandEncoder();
    }

private:
    MetalContext& m_Owner;
};

class MetalSwapChain final : public GpuSwapChain {
public:
    explicit MetalSwapChain(MetalContext& owner)
        : m_Owner(owner) {}

    void Present(bool vsync) override {
        m_Owner.PresentSwapChain(vsync);
    }

    bool Resize(uint32_t width, uint32_t height) override {
        return m_Owner.ResizeSwapChain(width, height);
    }

    uint32_t GetWidth() const override {
        return m_Owner.m_Impl ? m_Owner.m_Impl->drawableW : 0;
    }

    uint32_t GetHeight() const override {
        return m_Owner.m_Impl ? m_Owner.m_Impl->drawableH : 0;
    }

private:
    MetalContext& m_Owner;
};

// ============================================================================
// MetalContext
// ============================================================================

MetalContext::MetalContext()
    : m_Impl(std::make_unique<Impl>())
    , m_SwapChainInterface(std::make_unique<MetalSwapChain>(*this))
    , m_GraphicsCommandList(std::make_unique<MetalImmediateCommandList>(*this)) {}

MetalContext::~MetalContext() {
    Shutdown();
}

bool MetalContext::Init(IWindow* window) {
    SDL_Window* sdlWin = window ? window->GetSDLWindow() : nullptr;
    if (!sdlWin) {
        Logger::Error("[Metal] Init: no SDL_Window*");
        return false;
    }

    // Create the CAMetalLayer-backed view.
    m_Impl->metalView = SDL_Metal_CreateView(sdlWin);
    if (!m_Impl->metalView) {
        Logger::Error("[Metal] SDL_Metal_CreateView failed");
        return false;
    }

    m_Impl->layer =
        (__bridge CAMetalLayer*)SDL_Metal_GetLayer(m_Impl->metalView);
    if (!m_Impl->layer) {
        Logger::Error("[Metal] SDL_Metal_GetLayer returned null");
        return false;
    }

    // Create the default GPU device.
    m_Impl->device = MTLCreateSystemDefaultDevice();
    if (!m_Impl->device) {
        Logger::Error("[Metal] MTLCreateSystemDefaultDevice failed");
        return false;
    }

    m_Impl->layer.device        = m_Impl->device;
    m_Impl->layer.pixelFormat   = MTLPixelFormatBGRA8Unorm;
    m_Impl->layer.framebufferOnly = YES;

    m_Impl->queue = [m_Impl->device newCommandQueue];
    if (!m_Impl->queue) {
        Logger::Error("[Metal] newCommandQueue failed");
        return false;
    }

    const int w = window->GetWidth();
    const int h = window->GetHeight();
    m_Impl->layer.drawableSize = CGSizeMake(static_cast<CGFloat>(w),
                                            static_cast<CGFloat>(h));
    m_Impl->vpW = static_cast<float>(w);
    m_Impl->vpH = static_cast<float>(h);
    m_Impl->drawableW = static_cast<uint32_t>(w);
    m_Impl->drawableH = static_cast<uint32_t>(h);
    m_Impl->EnsureDepthTexture(m_Impl->drawableW, m_Impl->drawableH);

    Logger::Info("[Metal] Initialized – GPU: ",
                 [[m_Impl->device name] UTF8String]);
    return true;
}

void MetalContext::Shutdown() {
    ShutdownImGui();

    if (m_Impl->encoder) {
        [m_Impl->encoder endEncoding];
        m_Impl->encoder = nil;
    }
    m_Impl->cmdBuffer    = nil;
    m_Impl->drawable     = nil;
    m_Impl->depthTexture = nil;
    m_Impl->currentRPD   = nil;
    m_Impl->queue        = nil;
    m_Impl->device       = nil;

    if (m_Impl->metalView) {
        SDL_Metal_DestroyView(m_Impl->metalView);
        m_Impl->metalView = nullptr;
    }
    m_Impl->layer = nil;

    Logger::Info("[Metal] Shutdown");
}

void MetalContext::BeginFrame(float r, float g, float b, float a) {
    // Sync drawable size with the layer (handles window resize).
    CGSize sz = m_Impl->layer.drawableSize;
    auto newW = static_cast<uint32_t>(sz.width);
    auto newH = static_cast<uint32_t>(sz.height);
    if (newW != m_Impl->drawableW || newH != m_Impl->drawableH) {
        m_Impl->drawableW = newW;
        m_Impl->drawableH = newH;
        m_Impl->EnsureDepthTexture(newW, newH);
    }

    m_Impl->drawable  = [m_Impl->layer nextDrawable];
    m_Impl->cmdBuffer = [m_Impl->queue commandBuffer];

    MTLRenderPassDescriptor* rpd =
        [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = m_Impl->drawable.texture;
    rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor  = MTLClearColorMake(r, g, b, a);
    rpd.depthAttachment.texture         = m_Impl->depthTexture;
    rpd.depthAttachment.loadAction      = MTLLoadActionClear;
    rpd.depthAttachment.storeAction     = MTLStoreActionDontCare;
    rpd.depthAttachment.clearDepth      = 1.0;

    m_Impl->currentRPD = rpd;

    m_Impl->encoder =
        [m_Impl->cmdBuffer renderCommandEncoderWithDescriptor:rpd];

    // Match D3D convention: no face culling by default.
    [m_Impl->encoder setCullMode:MTLCullModeNone];

    // Apply current viewport.
    float vw = m_Impl->vpW > 0.0f ? m_Impl->vpW
                                   : static_cast<float>(m_Impl->drawableW);
    float vh = m_Impl->vpH > 0.0f ? m_Impl->vpH
                                   : static_cast<float>(m_Impl->drawableH);
    MTLViewport vp = {
        static_cast<double>(m_Impl->vpX), static_cast<double>(m_Impl->vpY),
        static_cast<double>(vw),          static_cast<double>(vh),
        0.0, 1.0
    };
    [m_Impl->encoder setViewport:vp];

    m_Impl->boundVB = nullptr;
    m_Impl->boundIB = nullptr;
}

void MetalContext::EndFrame() {
    if (m_Impl->encoder) {
        [m_Impl->encoder endEncoding];
        m_Impl->encoder = nil;
    }
    m_Impl->currentRPD = nil;

    PresentSwapChain(true);
    m_Impl->cmdBuffer = nil;
    m_Impl->drawable  = nil;
}

GpuSwapChain* MetalContext::GetSwapChain() {
    return m_SwapChainInterface.get();
}

GpuCommandList* MetalContext::GetGraphicsCommandList() {
    return m_GraphicsCommandList.get();
}

bool MetalContext::InitImGui(IWindow* window) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!window || !window->GetSDLWindow() || !m_Impl || !m_Impl->device) {
        return false;
    }

    ShutdownImGui();
    if (!ImGui_ImplSDL3_InitForMetal(window->GetSDLWindow())) {
        return false;
    }
    if (!ImGui_ImplMetal_Init(m_Impl->device)) {
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

void MetalContext::ShutdownImGui() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiInitialized) return;
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    m_ImGuiInitialized = false;
#endif
}

void MetalContext::ProcessImGuiSDLEvent(const SDL_Event& event) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiInitialized) return;
    ImGui_ImplSDL3_ProcessEvent(&event);
#else
    (void)event;
#endif
}

void MetalContext::BeginImGuiFrame() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiInitialized || !m_Impl || !m_Impl->currentRPD) return;
    ImGui_ImplMetal_NewFrame(m_Impl->currentRPD);
    ImGui_ImplSDL3_NewFrame();
#endif
}

void MetalContext::RenderImGuiDrawData(ImDrawData* drawData) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ImGuiInitialized || !drawData || !m_Impl ||
        !m_Impl->cmdBuffer || !m_Impl->encoder) {
        return;
    }
    ImGui_ImplMetal_RenderDrawData(drawData, m_Impl->cmdBuffer, m_Impl->encoder);
#else
    (void)drawData;
#endif
}

void MetalContext::PresentSwapChain(bool)
{
    if (m_Impl->cmdBuffer && m_Impl->drawable) {
        [m_Impl->cmdBuffer presentDrawable:m_Impl->drawable];
        [m_Impl->cmdBuffer commit];
    }
}

bool MetalContext::ResizeSwapChain(uint32_t width, uint32_t height)
{
    if (!m_Impl || !m_Impl->layer) return false;
    if (width == 0 || height == 0) return false;

    m_Impl->layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width),
                                            static_cast<CGFloat>(height));
    m_Impl->drawableW = width;
    m_Impl->drawableH = height;
    m_Impl->vpW = static_cast<float>(width);
    m_Impl->vpH = static_cast<float>(height);
    m_Impl->EnsureDepthTexture(width, height);
    return true;
}

// ============================================================================
// Resource creation
// ============================================================================

std::shared_ptr<GpuBuffer> MetalContext::CreateVertexBuffer(
    const void* data, uint32_t byteSize, uint32_t strideBytes)
{
    auto gpuBuf = std::make_shared<MetalGpuBuffer>();
    gpuBuf->buffer = [m_Impl->device newBufferWithBytes:data
                                                 length:byteSize
                                                options:MTLResourceStorageModeShared];
    gpuBuf->stride   = strideBytes;
    gpuBuf->byteSize = byteSize;
    return gpuBuf;
}

std::shared_ptr<GpuBuffer> MetalContext::CreateIndexBuffer(
    const void* data, uint32_t byteSize)
{
    auto gpuBuf = std::make_shared<MetalGpuBuffer>();
    gpuBuf->buffer = [m_Impl->device newBufferWithBytes:data
                                                 length:byteSize
                                                options:MTLResourceStorageModeShared];
    gpuBuf->byteSize = byteSize;
    return gpuBuf;
}

std::shared_ptr<GpuShader> MetalContext::CreateShader(
    const std::string& mslSource,
    const std::string& vsEntry,
    const std::string& psEntry,
    const VertexElement* layout,
    uint32_t            layoutCount)
{
    NSError* err = nil;
    NSString* src = [NSString stringWithUTF8String:mslSource.c_str()];
    id<MTLLibrary> lib =
        [m_Impl->device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        Logger::Error("[Metal] Shader compile error: ",
                      [[err localizedDescription] UTF8String]);
        return nullptr;
    }

    id<MTLFunction> vsFn =
        [lib newFunctionWithName:[NSString stringWithUTF8String:vsEntry.c_str()]];
    id<MTLFunction> psFn =
        [lib newFunctionWithName:[NSString stringWithUTF8String:psEntry.c_str()]];
    if (!vsFn || !psFn) {
        Logger::Error("[Metal] Cannot find shader functions '",
                      vsEntry, "' / '", psEntry, "'");
        return nullptr;
    }

    // Build vertex descriptor from the layout (attribute index == element index).
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    uint32_t stride = 0;
    for (uint32_t i = 0; i < layoutCount; ++i) {
        const VertexElement& el = layout[i];
        uint32_t elSize = 0;
        MTLVertexFormat fmt = MTLVertexFormatInvalid;
        switch (el.format) {
            case VertexFormat::Float2: fmt = MTLVertexFormatFloat2; elSize =  8; break;
            case VertexFormat::Float3: fmt = MTLVertexFormatFloat3; elSize = 12; break;
            case VertexFormat::Float4: fmt = MTLVertexFormatFloat4; elSize = 16; break;
        }
        vd.attributes[i].format      = fmt;
        vd.attributes[i].offset      = el.offset;
        vd.attributes[i].bufferIndex = 0;          // VB always at index 0
        stride = std::max(stride, el.offset + elSize);
    }
    vd.layouts[0].stride       = stride;
    vd.layouts[0].stepRate     = 1;
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    // Render pipeline.
    MTLRenderPipelineDescriptor* rpd =
        [[MTLRenderPipelineDescriptor alloc] init];
    rpd.vertexFunction                  = vsFn;
    rpd.fragmentFunction                = psFn;
    rpd.vertexDescriptor                = vd;
    rpd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    rpd.depthAttachmentPixelFormat      = MTLPixelFormatDepth32Float;

    id<MTLRenderPipelineState> pso =
        [m_Impl->device newRenderPipelineStateWithDescriptor:rpd error:&err];
    if (!pso) {
        Logger::Error("[Metal] Pipeline creation failed: ",
                      [[err localizedDescription] UTF8String]);
        return nullptr;
    }

    // Depth-stencil state (matching D3D defaults: depth test less, write enabled).
    MTLDepthStencilDescriptor* dsd =
        [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionLess;
    dsd.depthWriteEnabled    = YES;
    id<MTLDepthStencilState> dss =
        [m_Impl->device newDepthStencilStateWithDescriptor:dsd];

    auto shader = std::make_shared<MetalGpuShader>();
    shader->pipelineState = pso;
    shader->depthState    = dss;
    return shader;
}

// ============================================================================
// Draw state / draw calls
// ============================================================================

void MetalContext::BindShader(GpuShader* shader) {
    auto* ms = dynamic_cast<MetalGpuShader*>(shader);
    if (!ms || !m_Impl->encoder) return;
    [m_Impl->encoder setRenderPipelineState:ms->pipelineState];
    [m_Impl->encoder setDepthStencilState:ms->depthState];
}

void MetalContext::BindVertexBuffer(GpuBuffer* buffer) {
    auto* mb = dynamic_cast<MetalGpuBuffer*>(buffer);
    m_Impl->boundVB = mb;
    if (mb && m_Impl->encoder) {
        [m_Impl->encoder setVertexBuffer:mb->buffer offset:0 atIndex:0];
    }
}

void MetalContext::BindIndexBuffer(GpuBuffer* buffer) {
    m_Impl->boundIB = dynamic_cast<MetalGpuBuffer*>(buffer);
}

void MetalContext::SetVSConstants(const void* data, uint32_t byteSize) {
    if (!m_Impl->encoder) return;
    // Constants at buffer index 1 (index 0 is the vertex buffer).
    [m_Impl->encoder setVertexBytes:data length:byteSize atIndex:1];
}

void MetalContext::Draw(uint32_t vertexCount, uint32_t startVertex) {
    if (!m_Impl->encoder) return;
    [m_Impl->encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:startVertex
                        vertexCount:vertexCount];
}

void MetalContext::DrawIndexed(uint32_t indexCount,
                               uint32_t startIndex,
                               uint32_t baseVertex)
{
    if (!m_Impl->encoder || !m_Impl->boundIB) return;
    NSUInteger byteOffset = static_cast<NSUInteger>(startIndex) * sizeof(uint32_t);
    [m_Impl->encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:indexCount
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:m_Impl->boundIB->buffer
                         indexBufferOffset:byteOffset
                             instanceCount:1
                                baseVertex:static_cast<NSInteger>(baseVertex)
                              baseInstance:0];
}

void MetalContext::SetViewport(float x, float y, float w, float h) {
    m_Impl->vpX = x;
    m_Impl->vpY = y;
    m_Impl->vpW = w;
    m_Impl->vpH = h;

    // Update drawable size hint so the swapchain tracks the window.
    m_Impl->layer.drawableSize = CGSizeMake(static_cast<CGFloat>(w),
                                            static_cast<CGFloat>(h));

    if (m_Impl->encoder) {
        MTLViewport vp = { x, y, w, h, 0.0, 1.0 };
        [m_Impl->encoder setViewport:vp];
    }
}

std::shared_ptr<GpuTexture> MetalContext::UploadTexture2D(
    const void*, int, int)
{
    return nullptr;
}

void MetalContext::BindPSTexture(uint32_t, GpuTexture*)
{
    // Not implemented in current Metal path.
}

// ============================================================================
// Metal-specific accessors (for ImGui Metal back-end)
// ============================================================================

void* MetalContext::GetDevice() const {
    return (__bridge void*)m_Impl->device;
}

void* MetalContext::GetCommandBuffer() const {
    return (__bridge void*)m_Impl->cmdBuffer;
}

void* MetalContext::GetCommandEncoder() const {
    return (__bridge void*)m_Impl->encoder;
}

void* MetalContext::GetRenderPassDescriptor() const {
    return (__bridge void*)m_Impl->currentRPD;
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<IRenderContext> CreateMetalContext() {
    return std::make_unique<MetalContext>();
}

#endif // MYENGINE_PLATFORM_MACOS
