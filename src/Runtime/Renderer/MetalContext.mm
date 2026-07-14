// MetalContext.mm – Metal rendering back-end (Objective-C++)
// Only compiled on macOS (guarded by MYENGINE_PLATFORM_MACOS, and the file is
// only compiled on macOS in xmake.lua).

#include "Core/Platform.h"
#ifdef MYENGINE_PLATFORM_MACOS

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "Renderer/MetalContext.h"
#include "Core/Window.h"
#include "Renderer/RHI/RHIResourceStats.h"
#include "Core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <backends/imgui_impl_metal.h>
#include <backends/imgui_impl_sdl3.h>
#endif

namespace {
constexpr NSUInteger kMetalVertexBufferIndex = 15;
}

// ============================================================================
// GPU resource types
// ============================================================================

struct MetalGpuBuffer : GpuBuffer {
    id<MTLBuffer> buffer;
    uint32_t stride = 0;
    uint32_t byteSize = 0;
};

struct MetalGpuShader : GpuShader {
    id<MTLRenderPipelineState> pipelineState;
    id<MTLDepthStencilState> depthState;
    id<MTLFunction> vertexFunction;
    id<MTLFunction> fragmentFunction;
    id<MTLFunction> computeFunction;
    MTLVertexDescriptor* vertexDescriptor = nil;
};

struct MetalGraphicsPipeline : GpuGraphicsPipeline {
    id<MTLRenderPipelineState> pipelineState;
    id<MTLDepthStencilState> depthState;
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;
    MTLCullMode cullMode = MTLCullModeBack;
    MTLWinding frontWinding = MTLWindingClockwise;
    MTLTriangleFillMode fillMode = MTLTriangleFillModeFill;
};

struct MetalComputePipeline : GpuComputePipeline {
    id<MTLComputePipelineState> pipelineState;
};

struct MetalGpuTexture : GpuTexture {
    id<MTLTexture> texture = nil;
    bool isCube = false;

    bool IsCube() const override { return isCube; }
};

struct MetalGpuTextureView : GpuTextureView {
    id<MTLTexture> textureView = nil;
    NSUInteger mipLevel = 0;
    NSUInteger slice = 0;

    void* GetImGuiTextureId() override { return (__bridge void*)textureView; }
};

struct MetalGpuSampler : GpuSampler {
    id<MTLSamplerState> sampler = nil;
};

MTLPixelFormat ToMetalFormat(RHIFormat format) {
    switch (format) {
    case RHIFormat::R8UInt:
        return MTLPixelFormatR8Uint;
    case RHIFormat::RGBA8UNorm:
        return MTLPixelFormatRGBA8Unorm;
    case RHIFormat::BGRA8UNorm:
        return MTLPixelFormatBGRA8Unorm;
    case RHIFormat::RGBA8UNormSrgb:
        return MTLPixelFormatRGBA8Unorm_sRGB;
    case RHIFormat::RG16Float:
        return MTLPixelFormatRG16Float;
    case RHIFormat::RGBA16Float:
        return MTLPixelFormatRGBA16Float;
    case RHIFormat::R8UNorm:
        return MTLPixelFormatR8Unorm;
    case RHIFormat::R16UInt:
        return MTLPixelFormatR16Uint;
    case RHIFormat::R32UInt:
        return MTLPixelFormatR32Uint;
    case RHIFormat::R32Float:
        return MTLPixelFormatR32Float;
    case RHIFormat::RG32Float:
        return MTLPixelFormatRG32Float;
    case RHIFormat::RGB32Float:
        return MTLPixelFormatRGBA32Float;
    case RHIFormat::RGBA32Float:
        return MTLPixelFormatRGBA32Float;
    case RHIFormat::BC1UNorm:
        return MTLPixelFormatInvalid;
    case RHIFormat::BC3UNorm:
        return MTLPixelFormatInvalid;
    case RHIFormat::D24S8:
        return MTLPixelFormatDepth32Float;
    case RHIFormat::D32Float:
        return MTLPixelFormatDepth32Float;
    case RHIFormat::Unknown:
        return MTLPixelFormatInvalid;
    default:
        return MTLPixelFormatRGBA8Unorm;
    }
}

MTLTextureUsage ToMetalUsage(RHIResourceUsage usage) {
    MTLTextureUsage native = MTLTextureUsageUnknown;
    if (HasUsage(usage, RHIResourceUsage::ShaderResource))
        native |= MTLTextureUsageShaderRead;
    if (HasUsage(usage, RHIResourceUsage::RenderTarget) || HasUsage(usage, RHIResourceUsage::DepthStencil))
        native |= MTLTextureUsageRenderTarget;
    if (HasUsage(usage, RHIResourceUsage::UnorderedAccess))
        native |= MTLTextureUsageShaderWrite;
    return native == MTLTextureUsageUnknown ? MTLTextureUsageShaderRead : native;
}

MTLCompareFunction ToMetalCompare(RHICompareOp op) {
    switch (op) {
    case RHICompareOp::Never:
        return MTLCompareFunctionNever;
    case RHICompareOp::Less:
        return MTLCompareFunctionLess;
    case RHICompareOp::Equal:
        return MTLCompareFunctionEqual;
    case RHICompareOp::LessEqual:
        return MTLCompareFunctionLessEqual;
    case RHICompareOp::Greater:
        return MTLCompareFunctionGreater;
    case RHICompareOp::NotEqual:
        return MTLCompareFunctionNotEqual;
    case RHICompareOp::GreaterEqual:
        return MTLCompareFunctionGreaterEqual;
    case RHICompareOp::Always:
        return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionAlways;
}

MTLBlendFactor ToMetalBlendFactor(RHIBlendFactor factor) {
    switch (factor) {
    case RHIBlendFactor::Zero:
        return MTLBlendFactorZero;
    case RHIBlendFactor::One:
        return MTLBlendFactorOne;
    case RHIBlendFactor::SrcColor:
        return MTLBlendFactorSourceColor;
    case RHIBlendFactor::OneMinusSrcColor:
        return MTLBlendFactorOneMinusSourceColor;
    case RHIBlendFactor::DstColor:
        return MTLBlendFactorDestinationColor;
    case RHIBlendFactor::OneMinusDstColor:
        return MTLBlendFactorOneMinusDestinationColor;
    case RHIBlendFactor::SrcAlpha:
        return MTLBlendFactorSourceAlpha;
    case RHIBlendFactor::OneMinusSrcAlpha:
        return MTLBlendFactorOneMinusSourceAlpha;
    case RHIBlendFactor::DstAlpha:
        return MTLBlendFactorDestinationAlpha;
    case RHIBlendFactor::OneMinusDstAlpha:
        return MTLBlendFactorOneMinusDestinationAlpha;
    case RHIBlendFactor::ConstantColor:
        return MTLBlendFactorBlendColor;
    case RHIBlendFactor::OneMinusConstantColor:
        return MTLBlendFactorOneMinusBlendColor;
    case RHIBlendFactor::SrcAlphaSaturate:
        return MTLBlendFactorSourceAlphaSaturated;
    }
    return MTLBlendFactorOne;
}

MTLBlendOperation ToMetalBlendOp(RHIBlendOp op) {
    switch (op) {
    case RHIBlendOp::Add:
        return MTLBlendOperationAdd;
    case RHIBlendOp::Subtract:
        return MTLBlendOperationSubtract;
    case RHIBlendOp::ReverseSubtract:
        return MTLBlendOperationReverseSubtract;
    case RHIBlendOp::Min:
        return MTLBlendOperationMin;
    case RHIBlendOp::Max:
        return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

MTLColorWriteMask ToMetalColorWriteMask(uint8_t mask) {
    MTLColorWriteMask native = MTLColorWriteMaskNone;
    if (mask & RHIColorWriteRed)
        native |= MTLColorWriteMaskRed;
    if (mask & RHIColorWriteGreen)
        native |= MTLColorWriteMaskGreen;
    if (mask & RHIColorWriteBlue)
        native |= MTLColorWriteMaskBlue;
    if (mask & RHIColorWriteAlpha)
        native |= MTLColorWriteMaskAlpha;
    return native;
}

MTLPrimitiveType ToMetalPrimitiveType(RHIPrimitiveTopology topology) {
    switch (topology) {
    case RHIPrimitiveTopology::PointList:
        return MTLPrimitiveTypePoint;
    case RHIPrimitiveTopology::LineList:
        return MTLPrimitiveTypeLine;
    case RHIPrimitiveTopology::LineStrip:
        return MTLPrimitiveTypeLineStrip;
    case RHIPrimitiveTopology::TriangleStrip:
        return MTLPrimitiveTypeTriangleStrip;
    case RHIPrimitiveTopology::TriangleList:
    default:
        return MTLPrimitiveTypeTriangle;
    }
}

MTLVertexFormat ToMetalVertexFormat(VertexFormat format, uint32_t* byteSize = nullptr) {
    switch (format) {
    case VertexFormat::Float2:
        if (byteSize)
            *byteSize = 8;
        return MTLVertexFormatFloat2;
    case VertexFormat::Float3:
        if (byteSize)
            *byteSize = 12;
        return MTLVertexFormatFloat3;
    case VertexFormat::Float4:
        if (byteSize)
            *byteSize = 16;
        return MTLVertexFormatFloat4;
    }
    if (byteSize)
        *byteSize = 0;
    return MTLVertexFormatInvalid;
}

MTLVertexDescriptor* CreateMetalVertexDescriptor(const VertexElement* layout, uint32_t layoutCount) {
    if (!layout || layoutCount == 0)
        return nil;
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    uint32_t stride = 0;
    for (uint32_t i = 0; i < layoutCount; ++i) {
        const VertexElement& el = layout[i];
        uint32_t elSize = 0;
        vd.attributes[i].format = ToMetalVertexFormat(el.format, &elSize);
        vd.attributes[i].offset = el.offset;
        vd.attributes[i].bufferIndex = kMetalVertexBufferIndex;
        stride = std::max(stride, el.offset + elSize);
    }
    vd.layouts[kMetalVertexBufferIndex].stride = stride;
    vd.layouts[kMetalVertexBufferIndex].stepRate = 1;
    vd.layouts[kMetalVertexBufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;
    return vd;
}

std::string NormalizeSlangBindingName(std::string name) {
    while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back()))) {
        name.pop_back();
    }
    if (!name.empty() && name.back() == '_')
        name.pop_back();
    const std::string prefix = "SLANG_ParameterGroup_";
    if (name.rfind(prefix, 0) == 0)
        name.erase(0, prefix.size());
    const std::string suffix = "_natural";
    const size_t suffixPos = name.find(suffix);
    if (suffixPos != std::string::npos)
        name.erase(suffixPos);
    return name;
}

void AddOrMergeBinding(ShaderReflection& reflection, const ShaderBindingDesc& binding) {
    for (auto& existing : reflection.bindings) {
        if (existing.name == binding.name && existing.type == binding.type) {
            existing.stages |= binding.stages;
            existing.bindPoint = binding.bindPoint;
            existing.bindCount = std::max(existing.bindCount, binding.bindCount);
            return;
        }
    }
    reflection.bindings.push_back(binding);
}

void ParseMetalBindings(const std::string& source, uint8_t stage, ShaderReflection& reflection) {
    static const std::regex bindingRegex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[(buffer|texture|sampler)\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), bindingRegex), end; it != end; ++it) {
        const std::string typeText = (*it)[1].str();
        const std::string rawName = (*it)[2].str();
        const std::string attr = (*it)[3].str();
        ShaderBindingDesc binding;
        binding.name = NormalizeSlangBindingName(rawName);
        if (binding.name.empty())
            continue;
        binding.bindPoint = static_cast<uint32_t>(std::stoul((*it)[4].str()));
        binding.bindCount = 1;
        binding.stages = stage;
        if (attr == "texture") {
            binding.type = ShaderBindingType::Texture;
        } else if (attr == "sampler") {
            binding.type = ShaderBindingType::Sampler;
        } else {
            binding.type = typeText.find("device") != std::string::npos ? ShaderBindingType::StorageBuffer
                                                                        : ShaderBindingType::ConstantBuffer;
        }
        AddOrMergeBinding(reflection, binding);
    }
}

// ============================================================================
// Impl – holds all ObjC/Metal objects
// ============================================================================

struct MetalContext::Impl {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    CAMetalLayer* layer = nil;
    SDL_MetalView metalView = nullptr;

    // Per-frame state
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> cmdBuffer;
    id<MTLRenderCommandEncoder> encoder;
    id<MTLComputeCommandEncoder> computeEncoder;
    MTLRenderPassDescriptor* currentRPD = nil;
    id<MTLTexture> depthTexture;
    bool frameActive = false;
    std::shared_ptr<MetalGpuTexture> currentBackBufferTexture;
    std::shared_ptr<MetalGpuTextureView> currentBackBufferView;
    std::vector<id<MTLBuffer>> transientBuffers;

    // Bound state
    MetalGpuBuffer* boundVB = nullptr;
    MetalGpuBuffer* boundIB = nullptr;
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;

    // Viewport (persistent across frames)
    float vpX = 0, vpY = 0, vpW = 0, vpH = 0;

    // Cached drawable size
    uint32_t drawableW = 0;
    uint32_t drawableH = 0;

    void EnsureDepthTexture(uint32_t w, uint32_t h) {
        if (depthTexture && depthTexture.width == w && depthTexture.height == h) {
            return;
        }
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                        width:w
                                                                                       height:h
                                                                                    mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget;
        depthTexture = [device newTextureWithDescriptor:desc];
    }
};

class MetalImmediateCommandList final : public GpuCommandList {
public:
    explicit MetalImmediateCommandList(MetalContext& owner) : m_Owner(owner) {}

    void BindShader(GpuShader* shader) override { m_Owner.BindShader(shader); }

    void BindVertexBuffer(GpuBuffer* buffer) override { m_Owner.BindVertexBuffer(buffer); }

    void BindIndexBuffer(GpuBuffer* buffer) override { m_Owner.BindIndexBuffer(buffer); }

    void SetVSConstants(const void* data, uint32_t byteSize) override { m_Owner.SetVSConstants(data, byteSize); }

    void Draw(uint32_t vertexCount, uint32_t startVertex) override { m_Owner.Draw(vertexCount, startVertex); }

    void DrawIndexed(uint32_t indexCount, uint32_t startIndex, uint32_t baseVertex) override {
        m_Owner.DrawIndexed(indexCount, startIndex, baseVertex);
    }

    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex) override {
        m_Owner.DrawInstanced(vertexCount, instanceCount, startVertex);
    }

    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex,
                              uint32_t baseVertex) override {
        m_Owner.DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex);
    }

    void SetViewport(float x, float y, float w, float h) override { m_Owner.SetViewport(x, y, w, h); }

    void BindPSTexture(uint32_t slot, GpuTexture* tex) override { m_Owner.BindPSTexture(slot, tex); }

    void BeginRendering(const RenderingInfo& info) override { m_Owner.BeginRendering(info); }

    void EndRendering() override { m_Owner.EndRendering(); }

    void SetGraphicsPipeline(GpuGraphicsPipeline* pipeline) override { m_Owner.SetGraphicsPipeline(pipeline); }

    void SetComputePipeline(GpuComputePipeline* pipeline) override { m_Owner.SetComputePipeline(pipeline); }

    void SetDepthOnlyShader(GpuShader* shader) override { m_Owner.BindShader(shader); }

    void SetBindGroup(uint32_t, GpuBindGroup* group) override { m_Owner.SetBindGroup(group); }

    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override { m_Owner.Dispatch(x, y, z); }

    void* GetNativeHandle() const { return m_Owner.GetCommandEncoder(); }

private:
    MetalContext& m_Owner;
};

class MetalSwapChain final : public GpuSwapChain {
public:
    explicit MetalSwapChain(MetalContext& owner) : m_Owner(owner) {}

    void Present(bool vsync) override { m_Owner.PresentSwapChain(vsync); }

    bool Resize(uint32_t width, uint32_t height) override { return m_Owner.ResizeSwapChain(width, height); }

    uint32_t GetWidth() const override { return m_Owner.m_Impl ? m_Owner.m_Impl->drawableW : 0; }

    uint32_t GetHeight() const override { return m_Owner.m_Impl ? m_Owner.m_Impl->drawableH : 0; }

private:
    MetalContext& m_Owner;
};

// ============================================================================
// MetalContext
// ============================================================================

MetalContext::MetalContext()
    : m_Impl(std::make_unique<Impl>()), m_SwapChainInterface(std::make_unique<MetalSwapChain>(*this)),
      m_GraphicsCommandList(std::make_unique<MetalImmediateCommandList>(*this)) {
}

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

    m_Impl->layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(m_Impl->metalView);
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

    m_Impl->layer.device = m_Impl->device;
    m_Impl->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    m_Impl->layer.framebufferOnly = YES;

    m_Impl->queue = [m_Impl->device newCommandQueue];
    if (!m_Impl->queue) {
        Logger::Error("[Metal] newCommandQueue failed");
        return false;
    }

    const int w = window->GetWidth();
    const int h = window->GetHeight();
    m_Impl->layer.drawableSize = CGSizeMake(static_cast<CGFloat>(w), static_cast<CGFloat>(h));
    m_Impl->vpW = static_cast<float>(w);
    m_Impl->vpH = static_cast<float>(h);
    m_Impl->drawableW = static_cast<uint32_t>(w);
    m_Impl->drawableH = static_cast<uint32_t>(h);
    m_Impl->EnsureDepthTexture(m_Impl->drawableW, m_Impl->drawableH);

    Logger::Info("[Metal] Initialized – GPU: ", [[m_Impl->device name] UTF8String]);
    return true;
}

void MetalContext::Shutdown() {
    if (m_Impl->computeEncoder) {
        [m_Impl->computeEncoder endEncoding];
        m_Impl->computeEncoder = nil;
    }
    if (m_Impl->encoder) {
        [m_Impl->encoder endEncoding];
        m_Impl->encoder = nil;
    }
    m_Impl->cmdBuffer = nil;
    m_Impl->drawable = nil;
    m_Impl->depthTexture = nil;
    m_Impl->currentRPD = nil;
    m_Impl->queue = nil;
    m_Impl->device = nil;

    if (m_Impl->metalView) {
        SDL_Metal_DestroyView(m_Impl->metalView);
        m_Impl->metalView = nullptr;
    }
    m_Impl->layer = nil;

    Logger::Info("[Metal] Shutdown");
}

void MetalContext::BeginFrame(float r, float g, float b, float a) {
    if (m_Impl->frameActive) {
        return;
    }

    // Sync drawable size with the layer (handles window resize).
    CGSize sz = m_Impl->layer.drawableSize;
    auto newW = static_cast<uint32_t>(sz.width);
    auto newH = static_cast<uint32_t>(sz.height);
    if (newW != m_Impl->drawableW || newH != m_Impl->drawableH) {
        m_Impl->drawableW = newW;
        m_Impl->drawableH = newH;
        m_Impl->EnsureDepthTexture(newW, newH);
    }

    m_Impl->drawable = [m_Impl->layer nextDrawable];
    m_Impl->cmdBuffer = [m_Impl->queue commandBuffer];
    m_Impl->currentBackBufferTexture = std::make_shared<MetalGpuTexture>();
    m_Impl->currentBackBufferTexture->texture = m_Impl->drawable.texture;
    m_Impl->currentBackBufferTexture->desc.width = m_Impl->drawableW;
    m_Impl->currentBackBufferTexture->desc.height = m_Impl->drawableH;
    m_Impl->currentBackBufferTexture->desc.format = RHIFormat::BGRA8UNorm;
    m_Impl->currentBackBufferTexture->desc.usage = RHIResourceUsage::RenderTarget;
    m_Impl->currentBackBufferView = std::make_shared<MetalGpuTextureView>();
    m_Impl->currentBackBufferView->texture = m_Impl->currentBackBufferTexture;
    m_Impl->currentBackBufferView->textureView = m_Impl->drawable.texture;
    m_Impl->currentBackBufferView->desc.usage = RHIResourceUsage::RenderTarget;

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = m_Impl->drawable.texture;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, a);
    rpd.depthAttachment.texture = m_Impl->depthTexture;
    rpd.depthAttachment.loadAction = MTLLoadActionClear;
    rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
    rpd.depthAttachment.clearDepth = 1.0;

    m_Impl->currentRPD = rpd;

    m_Impl->encoder = [m_Impl->cmdBuffer renderCommandEncoderWithDescriptor:rpd];

    // Match D3D convention: no face culling by default.
    [m_Impl->encoder setCullMode:MTLCullModeNone];

    // Apply current viewport.
    float vw = m_Impl->vpW > 0.0f ? m_Impl->vpW : static_cast<float>(m_Impl->drawableW);
    float vh = m_Impl->vpH > 0.0f ? m_Impl->vpH : static_cast<float>(m_Impl->drawableH);
    MTLViewport vp = {static_cast<double>(m_Impl->vpX),
                      static_cast<double>(m_Impl->vpY),
                      static_cast<double>(vw),
                      static_cast<double>(vh),
                      0.0,
                      1.0};
    [m_Impl->encoder setViewport:vp];

    m_Impl->boundVB = nullptr;
    m_Impl->boundIB = nullptr;
    m_Impl->transientBuffers.clear();
    m_Impl->frameActive = true;
}

void MetalContext::EndFrame() {
    if (!m_Impl->frameActive) {
        return;
    }

    if (m_Impl->computeEncoder) {
        [m_Impl->computeEncoder endEncoding];
        m_Impl->computeEncoder = nil;
    }
    if (m_Impl->encoder) {
        [m_Impl->encoder endEncoding];
        m_Impl->encoder = nil;
    }
    m_Impl->currentRPD = nil;

    PresentSwapChain(true);
    m_Impl->cmdBuffer = nil;
    m_Impl->drawable = nil;
    m_Impl->currentBackBufferView.reset();
    m_Impl->currentBackBufferTexture.reset();
    m_Impl->transientBuffers.clear();
    m_Impl->frameActive = false;
}

GpuSwapChain* MetalContext::GetSwapChain() {
    return m_SwapChainInterface.get();
}

GpuTextureView* MetalContext::GetCurrentBackBufferView() {
    return m_Impl->currentBackBufferView.get();
}

GpuCommandList* MetalContext::GetGraphicsCommandList() {
    return m_GraphicsCommandList.get();
}

ImGuiBackendHandles MetalContext::GetImGuiBackendHandles() {
    if (m_Impl->computeEncoder) {
        [m_Impl->computeEncoder endEncoding];
        m_Impl->computeEncoder = nil;
    }
    if (m_Impl->frameActive && !m_Impl->encoder && m_Impl->cmdBuffer && m_Impl->drawable) {
        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = m_Impl->drawable.texture;
        rpd.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpd.depthAttachment.texture = m_Impl->depthTexture;
        rpd.depthAttachment.loadAction = MTLLoadActionLoad;
        rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
        m_Impl->currentRPD = rpd;
        m_Impl->encoder = [m_Impl->cmdBuffer renderCommandEncoderWithDescriptor:rpd];
    }
    ImGuiBackendHandles h;
    h.backend = RHIBackend::Metal;
    h.device = (__bridge void*)m_Impl->device;
    h.commandBuffer = (__bridge void*)m_Impl->cmdBuffer;
    h.commandEncoder = (__bridge void*)m_Impl->encoder;
    h.renderPassDescriptor = (__bridge void*)m_Impl->currentRPD;
    return h;
}

void MetalContext::PresentSwapChain(bool) {
    if (m_Impl->cmdBuffer && m_Impl->drawable) {
        [m_Impl->cmdBuffer presentDrawable:m_Impl->drawable];
        [m_Impl->cmdBuffer commit];
    }
}

bool MetalContext::ResizeSwapChain(uint32_t width, uint32_t height) {
    if (!m_Impl || !m_Impl->layer)
        return false;
    if (width == 0 || height == 0)
        return false;

    m_Impl->layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
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

std::shared_ptr<GpuBuffer> MetalContext::CreateVertexBuffer(const void* data, uint32_t byteSize, uint32_t strideBytes) {
    auto gpuBuf = std::make_shared<MetalGpuBuffer>();
    gpuBuf->buffer = [m_Impl->device newBufferWithBytes:data length:byteSize options:MTLResourceStorageModeShared];
    gpuBuf->stride = strideBytes;
    gpuBuf->byteSize = byteSize;
    gpuBuf->desc = {byteSize, strideBytes, RHIResourceUsage::VertexBuffer, "VertexBuffer"};
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(gpuBuf));
    return gpuBuf;
}

std::shared_ptr<GpuBuffer> MetalContext::CreateIndexBuffer(const void* data, uint32_t byteSize) {
    auto gpuBuf = std::make_shared<MetalGpuBuffer>();
    gpuBuf->buffer = [m_Impl->device newBufferWithBytes:data length:byteSize options:MTLResourceStorageModeShared];
    gpuBuf->byteSize = byteSize;
    gpuBuf->desc = {byteSize, sizeof(uint32_t), RHIResourceUsage::IndexBuffer, "IndexBuffer"};
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(gpuBuf));
    return gpuBuf;
}

std::shared_ptr<GpuShader> MetalContext::CreateShader(const std::string& mslSource, const std::string& vsEntry,
                                                      const std::string& psEntry, const VertexElement* layout,
                                                      uint32_t layoutCount) {
    NSError* err = nil;
    NSString* src = [NSString stringWithUTF8String:mslSource.c_str()];
    id<MTLLibrary> lib = [m_Impl->device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        Logger::Error("[Metal] Shader compile error: ", [[err localizedDescription] UTF8String]);
        return nullptr;
    }

    id<MTLFunction> vsFn = [lib newFunctionWithName:[NSString stringWithUTF8String:vsEntry.c_str()]];
    id<MTLFunction> psFn = [lib newFunctionWithName:[NSString stringWithUTF8String:psEntry.c_str()]];
    if (!vsFn || !psFn) {
        Logger::Error("[Metal] Cannot find shader functions '", vsEntry, "' / '", psEntry, "'");
        return nullptr;
    }

    MTLVertexDescriptor* vd = CreateMetalVertexDescriptor(layout, layoutCount);

    // Render pipeline.
    MTLRenderPipelineDescriptor* rpd = [[MTLRenderPipelineDescriptor alloc] init];
    rpd.vertexFunction = vsFn;
    rpd.fragmentFunction = psFn;
    rpd.vertexDescriptor = vd;
    rpd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    rpd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    id<MTLRenderPipelineState> pso = [m_Impl->device newRenderPipelineStateWithDescriptor:rpd error:&err];
    if (!pso) {
        Logger::Error("[Metal] Pipeline creation failed: ", [[err localizedDescription] UTF8String]);
        return nullptr;
    }

    // Depth-stencil state (matching D3D defaults: depth test less, write enabled).
    MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionLess;
    dsd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> dss = [m_Impl->device newDepthStencilStateWithDescriptor:dsd];

    auto shader = std::make_shared<MetalGpuShader>();
    shader->pipelineState = pso;
    shader->depthState = dss;
    shader->vertexFunction = vsFn;
    shader->fragmentFunction = psFn;
    shader->vertexDescriptor = vd;
    shader->vertexBytecode.assign(mslSource.begin(), mslSource.end());
    shader->pixelBytecode.assign(mslSource.begin(), mslSource.end());
    if (layout && layoutCount)
        shader->vertexLayout.assign(layout, layout + layoutCount);
    ParseMetalBindings(mslSource, ShaderStageVertex | ShaderStagePixel, shader->reflection);
    return shader;
}

std::shared_ptr<GpuShader> MetalContext::CreateShaderFromBytecode(const void* vsBytecode, size_t vsSize,
                                                                  const void* psBytecode, size_t psSize,
                                                                  const VertexElement* layout, uint32_t layoutCount) {
    if (!vsBytecode || vsSize == 0 || !psBytecode || psSize == 0 || (layoutCount > 0 && !layout)) {
        return nullptr;
    }

    NSError* err = nil;
    NSString* vsSource = [[NSString alloc] initWithBytes:vsBytecode length:vsSize encoding:NSUTF8StringEncoding];
    NSString* psSource = [[NSString alloc] initWithBytes:psBytecode length:psSize encoding:NSUTF8StringEncoding];
    if (!vsSource || !psSource) {
        Logger::Error("[Metal] Cooked Metal shader blob is not UTF-8 MSL");
        return nullptr;
    }

    id<MTLLibrary> vsLib = [m_Impl->device newLibraryWithSource:vsSource options:nil error:&err];
    if (!vsLib) {
        Logger::Error("[Metal] Vertex MSL compile error: ", [[err localizedDescription] UTF8String]);
        return nullptr;
    }
    err = nil;
    id<MTLLibrary> psLib = [m_Impl->device newLibraryWithSource:psSource options:nil error:&err];
    if (!psLib) {
        Logger::Error("[Metal] Fragment MSL compile error: ", [[err localizedDescription] UTF8String]);
        return nullptr;
    }

    id<MTLFunction> vsFn = [vsLib newFunctionWithName:@"VSMain"];
    id<MTLFunction> psFn = [psLib newFunctionWithName:@"PSMain"];
    if (!vsFn || !psFn) {
        Logger::Error("[Metal] Cannot find cooked shader functions VSMain / PSMain");
        return nullptr;
    }

    MTLVertexDescriptor* vd = CreateMetalVertexDescriptor(layout, layoutCount);

    MTLRenderPipelineDescriptor* rpd = [[MTLRenderPipelineDescriptor alloc] init];
    rpd.vertexFunction = vsFn;
    rpd.fragmentFunction = psFn;
    rpd.vertexDescriptor = vd;
    rpd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    rpd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    err = nil;
    id<MTLRenderPipelineState> pso = [m_Impl->device newRenderPipelineStateWithDescriptor:rpd error:&err];
    if (!pso) {
        Logger::Error("[Metal] Cooked pipeline creation failed: ", [[err localizedDescription] UTF8String]);
        return nullptr;
    }

    MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionLess;
    dsd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> dss = [m_Impl->device newDepthStencilStateWithDescriptor:dsd];

    auto shader = std::make_shared<MetalGpuShader>();
    shader->pipelineState = pso;
    shader->depthState = dss;
    shader->vertexFunction = vsFn;
    shader->fragmentFunction = psFn;
    shader->vertexDescriptor = vd;
    shader->vertexBytecode.assign(static_cast<const uint8_t*>(vsBytecode),
                                  static_cast<const uint8_t*>(vsBytecode) + vsSize);
    shader->pixelBytecode.assign(static_cast<const uint8_t*>(psBytecode),
                                 static_cast<const uint8_t*>(psBytecode) + psSize);
    if (layout && layoutCount)
        shader->vertexLayout.assign(layout, layout + layoutCount);
    std::string vsText(static_cast<const char*>(vsBytecode), vsSize);
    std::string psText(static_cast<const char*>(psBytecode), psSize);
    ParseMetalBindings(vsText, ShaderStageVertex, shader->reflection);
    ParseMetalBindings(psText, ShaderStagePixel, shader->reflection);
    if (shader->reflection.bindings.empty()) {
        Logger::Error("[Metal] Cooked Metal shader has no binding reflection");
        return nullptr;
    }
    return shader;
}

std::shared_ptr<GpuShader> MetalContext::CreateComputeShaderFromBytecode(const void* bytecode, size_t byteSize) {
    if (!bytecode || byteSize == 0 || !m_Impl || !m_Impl->device)
        return nullptr;
    std::string source(static_cast<const char*>(bytecode), byteSize);
    NSString* src = [[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding];
    if (!src) {
        Logger::Error("[Metal] Cooked Metal compute shader blob is not UTF-8 MSL");
        return nullptr;
    }
    NSError* err = nil;
    id<MTLLibrary> lib = [m_Impl->device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        Logger::Error("[Metal] Compute MSL compile error: ", err ? [[err localizedDescription] UTF8String] : "unknown");
        return nullptr;
    }
    id<MTLFunction> fn = [lib newFunctionWithName:@"CSMain"];
    if (!fn) {
        Logger::Error("[Metal] Cannot find cooked compute shader function CSMain");
        return nullptr;
    }

    auto shader = std::make_shared<MetalGpuShader>();
    shader->computeFunction = fn;
    shader->computeBytecode.assign(static_cast<const uint8_t*>(bytecode),
                                   static_cast<const uint8_t*>(bytecode) + byteSize);
    ParseMetalBindings(source, ShaderStageCompute, shader->reflection);
    if (shader->reflection.bindings.empty()) {
        Logger::Error("[Metal] Cooked Metal compute shader has no binding reflection");
        return nullptr;
    }
    return shader;
}

// ============================================================================
// Draw state / draw calls
// ============================================================================

void MetalContext::BeginRendering(const RenderingInfo& info) {
    if (!m_Impl || !m_Impl->cmdBuffer)
        return;
    if (m_Impl->computeEncoder) {
        [m_Impl->computeEncoder endEncoding];
        m_Impl->computeEncoder = nil;
    }
    if (m_Impl->encoder) {
        [m_Impl->encoder endEncoding];
        m_Impl->encoder = nil;
    }

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    for (uint32_t i = 0; i < info.colorCount && i < 8; ++i) {
        auto* view = dynamic_cast<MetalGpuTextureView*>(info.colors[i].view);
        if (!view || !view->textureView)
            continue;
        auto nativeTexture = std::dynamic_pointer_cast<MetalGpuTexture>(view->texture);
        auto attachment = rpd.colorAttachments[i];
        attachment.texture = nativeTexture && nativeTexture->texture ? nativeTexture->texture : view->textureView;
        attachment.level = view->mipLevel;
        attachment.slice = view->slice;
        attachment.loadAction =
            info.colors[i].loadOp == RHILoadOp::Clear
                ? MTLLoadActionClear
                : (info.colors[i].loadOp == RHILoadOp::Discard ? MTLLoadActionDontCare : MTLLoadActionLoad);
        attachment.storeAction =
            info.colors[i].storeOp == RHIStoreOp::Discard ? MTLStoreActionDontCare : MTLStoreActionStore;
        const ClearColor& c = info.colors[i].clearColor;
        attachment.clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a);
    }
    if (info.depth && info.depth->view) {
        auto* view = dynamic_cast<MetalGpuTextureView*>(info.depth->view);
        if (view && view->textureView) {
            auto nativeTexture = std::dynamic_pointer_cast<MetalGpuTexture>(view->texture);
            rpd.depthAttachment.texture =
                nativeTexture && nativeTexture->texture ? nativeTexture->texture : view->textureView;
            rpd.depthAttachment.level = view->mipLevel;
            rpd.depthAttachment.slice = view->slice;
            rpd.depthAttachment.loadAction =
                info.depth->loadOp == RHILoadOp::Clear
                    ? MTLLoadActionClear
                    : (info.depth->loadOp == RHILoadOp::Discard ? MTLLoadActionDontCare : MTLLoadActionLoad);
            rpd.depthAttachment.storeAction =
                info.depth->storeOp == RHIStoreOp::Discard ? MTLStoreActionDontCare : MTLStoreActionStore;
            rpd.depthAttachment.clearDepth = info.depth->clearDepth;
        }
    }
    m_Impl->currentRPD = rpd;
    m_Impl->encoder = [m_Impl->cmdBuffer renderCommandEncoderWithDescriptor:rpd];
    if (!m_Impl->encoder)
        return;
    [m_Impl->encoder setCullMode:MTLCullModeNone];
    SetViewport(0.0f, 0.0f, static_cast<float>(info.width), static_cast<float>(info.height));
}

void MetalContext::EndRendering() {
    if (!m_Impl || !m_Impl->encoder)
        return;
    [m_Impl->encoder endEncoding];
    m_Impl->encoder = nil;
}

std::shared_ptr<GpuGraphicsPipeline> MetalContext::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    auto shader = std::dynamic_pointer_cast<MetalGpuShader>(desc.shader);
    if (!m_Impl || !m_Impl->device || !shader || !shader->vertexFunction || !shader->fragmentFunction) {
        Logger::Error("[Metal] CreateGraphicsPipeline failed: invalid shader/functions");
        return nullptr;
    }

    MTLRenderPipelineDescriptor* native = [[MTLRenderPipelineDescriptor alloc] init];
    native.vertexFunction = shader->vertexFunction;
    native.vertexDescriptor = shader->vertexDescriptor;
    native.rasterSampleCount = std::max(desc.multisample.sampleCount, 1u);
    native.alphaToCoverageEnabled = desc.blend.alphaToCoverageEnable;

    const size_t colorCount = std::min(desc.colorFormats.size(), size_t{8});
    native.fragmentFunction = colorCount > 0 ? shader->fragmentFunction : nil;
    for (size_t i = 0; i < colorCount; ++i) {
        MTLPixelFormat pixelFormat = ToMetalFormat(desc.colorFormats[i]);
        if (pixelFormat == MTLPixelFormatInvalid) {
            Logger::Error("[Metal] CreateGraphicsPipeline failed: invalid color format at slot ", i);
            return nullptr;
        }
        auto attachment = native.colorAttachments[i];
        attachment.pixelFormat = pixelFormat;
        const size_t blendCount = desc.blend.attachments.size();
        const RHIBlendAttachmentState blend =
            blendCount ? desc.blend.attachments[std::min(i, blendCount - 1)] : RHIBlendAttachmentState{};
        attachment.blendingEnabled = blend.blendEnable;
        attachment.sourceRGBBlendFactor = ToMetalBlendFactor(blend.srcColorFactor);
        attachment.destinationRGBBlendFactor = ToMetalBlendFactor(blend.dstColorFactor);
        attachment.rgbBlendOperation = ToMetalBlendOp(blend.colorOp);
        attachment.sourceAlphaBlendFactor = ToMetalBlendFactor(blend.srcAlphaFactor);
        attachment.destinationAlphaBlendFactor = ToMetalBlendFactor(blend.dstAlphaFactor);
        attachment.alphaBlendOperation = ToMetalBlendOp(blend.alphaOp);
        attachment.writeMask = ToMetalColorWriteMask(blend.colorWriteMask);
    }

    if (desc.depthFormat != RHIFormat::Unknown) {
        native.depthAttachmentPixelFormat = ToMetalFormat(desc.depthFormat);
    }

    NSError* err = nil;
    id<MTLRenderPipelineState> pso = [m_Impl->device newRenderPipelineStateWithDescriptor:native error:&err];
    if (!pso) {
        Logger::Error("[Metal] CreateGraphicsPipeline failed: ",
                      err ? [[err localizedDescription] UTF8String] : "unknown error");
        return nullptr;
    }

    MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction =
        desc.depthStencil.depthTestEnable ? ToMetalCompare(desc.depthStencil.depthCompareOp) : MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = desc.depthStencil.depthWriteEnable ? YES : NO;
    id<MTLDepthStencilState> dss = [m_Impl->device newDepthStencilStateWithDescriptor:dsd];

    auto pipeline = std::make_shared<MetalGraphicsPipeline>();
    pipeline->desc = desc;
    pipeline->pipelineState = pso;
    pipeline->depthState = dss;
    pipeline->primitiveType = ToMetalPrimitiveType(desc.topology);
    pipeline->cullMode = desc.rasterizer.cullMode == RHICullMode::None
                             ? MTLCullModeNone
                             : (desc.rasterizer.cullMode == RHICullMode::Front ? MTLCullModeFront : MTLCullModeBack);
    pipeline->frontWinding =
        desc.rasterizer.frontFace == RHIFrontFace::CounterClockwise ? MTLWindingCounterClockwise : MTLWindingClockwise;
    pipeline->fillMode =
        desc.rasterizer.fillMode == RHIFillMode::Wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
    return pipeline;
}

std::shared_ptr<GpuComputePipeline> MetalContext::CreateComputePipeline(const ComputePipelineDesc& desc) {
    auto shader = std::dynamic_pointer_cast<MetalGpuShader>(desc.shader);
    if (!shader || !shader->computeFunction || !m_Impl || !m_Impl->device) {
        Logger::Error("[Metal] CreateComputePipeline failed: invalid shader/function");
        return nullptr;
    }
    NSError* err = nil;
    id<MTLComputePipelineState> pso = [m_Impl->device newComputePipelineStateWithFunction:shader->computeFunction
                                                                                    error:&err];
    if (!pso) {
        Logger::Error("[Metal] Compute pipeline creation failed: ",
                      err ? [[err localizedDescription] UTF8String] : "unknown");
        return nullptr;
    }
    auto pipeline = std::make_shared<MetalComputePipeline>();
    pipeline->desc = desc;
    pipeline->pipelineState = pso;
    return pipeline;
}

void MetalContext::SetGraphicsPipeline(GpuGraphicsPipeline* pipeline) {
    if (m_Impl && m_Impl->computeEncoder) {
        [m_Impl->computeEncoder endEncoding];
        m_Impl->computeEncoder = nil;
    }
    auto* native = dynamic_cast<MetalGraphicsPipeline*>(pipeline);
    if (!native || !native->pipelineState || !m_Impl->encoder)
        return;
    [m_Impl->encoder setRenderPipelineState:native->pipelineState];
    [m_Impl->encoder setDepthStencilState:native->depthState];
    [m_Impl->encoder setCullMode:native->cullMode];
    [m_Impl->encoder setFrontFacingWinding:native->frontWinding];
    [m_Impl->encoder setTriangleFillMode:native->fillMode];
    m_Impl->primitiveType = native->primitiveType;
}

void MetalContext::SetComputePipeline(GpuComputePipeline* pipeline) {
    if (!m_Impl || !m_Impl->cmdBuffer)
        return;
    if (m_Impl->encoder) {
        [m_Impl->encoder endEncoding];
        m_Impl->encoder = nil;
    }
    auto* native = dynamic_cast<MetalComputePipeline*>(pipeline);
    if (!native || !native->pipelineState)
        return;
    if (!m_Impl->computeEncoder) {
        m_Impl->computeEncoder = [m_Impl->cmdBuffer computeCommandEncoder];
    }
    [m_Impl->computeEncoder setComputePipelineState:native->pipelineState];
}

void MetalContext::BindShader(GpuShader* shader) {
    auto* ms = dynamic_cast<MetalGpuShader*>(shader);
    if (!ms || !ms->pipelineState || !m_Impl->encoder)
        return;
    [m_Impl->encoder setRenderPipelineState:ms->pipelineState];
    [m_Impl->encoder setDepthStencilState:ms->depthState];
}

void MetalContext::BindVertexBuffer(GpuBuffer* buffer) {
    auto* mb = dynamic_cast<MetalGpuBuffer*>(buffer);
    m_Impl->boundVB = mb;
    if (!m_Impl->encoder)
        return;
    [m_Impl->encoder setVertexBuffer:mb ? mb->buffer : nil offset:0 atIndex:kMetalVertexBufferIndex];
}

void MetalContext::BindIndexBuffer(GpuBuffer* buffer) {
    m_Impl->boundIB = dynamic_cast<MetalGpuBuffer*>(buffer);
}

void MetalContext::SetVSConstants(const void* data, uint32_t byteSize) {
    if (!m_Impl->encoder)
        return;
    if (byteSize <= 4096) {
        [m_Impl->encoder setVertexBytes:data length:byteSize atIndex:0];
    } else {
        id<MTLBuffer> buffer = [m_Impl->device newBufferWithBytes:data
                                                           length:byteSize
                                                          options:MTLResourceStorageModeShared];
        if (!buffer)
            return;
        m_Impl->transientBuffers.push_back(buffer);
        [m_Impl->encoder setVertexBuffer:buffer offset:0 atIndex:0];
    }
}

void MetalContext::SetBindGroup(GpuBindGroup* group) {
    if (!group || !group->GetShader() || !m_Impl || (!m_Impl->encoder && !m_Impl->computeEncoder))
        return;
    const auto& reflection = group->GetShader()->reflection;
    static std::unordered_map<std::string, bool> warnedMissingBindings;
    auto warnMissing = [&](const std::string& name) {
        if (!warnedMissingBindings[name]) {
            warnedMissingBindings[name] = true;
            Logger::Error("[Metal] Missing shader reflection binding for '", name, "'");
        }
    };
    for (const auto& value : group->GetConstants()) {
        const auto* binding = reflection.Find(value.first);
        if (!binding || binding->type != ShaderBindingType::ConstantBuffer) {
            warnMissing(value.first);
            continue;
        }
        const uint32_t slot = binding->bindPoint;
        const auto* bytes = value.second.data();
        const auto size = static_cast<NSUInteger>(value.second.size());
        if (size <= 4096) {
            if (binding->stages == 0 || (binding->stages & ShaderStageVertex))
                [m_Impl->encoder setVertexBytes:bytes length:size atIndex:slot];
            if (binding->stages == 0 || (binding->stages & ShaderStagePixel))
                [m_Impl->encoder setFragmentBytes:bytes length:size atIndex:slot];
            if (m_Impl->computeEncoder && (binding->stages == 0 || (binding->stages & ShaderStageCompute)))
                [m_Impl->computeEncoder setBytes:bytes length:size atIndex:slot];
        } else {
            id<MTLBuffer> buffer = [m_Impl->device newBufferWithBytes:bytes
                                                               length:size
                                                              options:MTLResourceStorageModeShared];
            if (!buffer)
                continue;
            m_Impl->transientBuffers.push_back(buffer);
            if (binding->stages == 0 || (binding->stages & ShaderStageVertex))
                [m_Impl->encoder setVertexBuffer:buffer offset:0 atIndex:slot];
            if (binding->stages == 0 || (binding->stages & ShaderStagePixel))
                [m_Impl->encoder setFragmentBuffer:buffer offset:0 atIndex:slot];
            if (m_Impl->computeEncoder && (binding->stages == 0 || (binding->stages & ShaderStageCompute)))
                [m_Impl->computeEncoder setBuffer:buffer offset:0 atIndex:slot];
        }
    }
    for (const auto& value : group->GetTextures()) {
        const auto* binding = reflection.Find(value.first);
        if (!binding || binding->type != ShaderBindingType::Texture) {
            warnMissing(value.first);
            continue;
        }
        const uint32_t slot = binding->bindPoint;
        auto* view = dynamic_cast<MetalGpuTextureView*>(value.second.get());
        if (!view || !view->textureView)
            continue;
        if (binding->stages == 0 || (binding->stages & ShaderStageVertex))
            [m_Impl->encoder setVertexTexture:view->textureView atIndex:slot];
        if (binding->stages == 0 || (binding->stages & ShaderStagePixel))
            [m_Impl->encoder setFragmentTexture:view->textureView atIndex:slot];
        if (m_Impl->computeEncoder && (binding->stages == 0 || (binding->stages & ShaderStageCompute)))
            [m_Impl->computeEncoder setTexture:view->textureView atIndex:slot];
    }
    for (const auto& value : group->GetSamplers()) {
        const auto* binding = reflection.Find(value.first);
        if (!binding || binding->type != ShaderBindingType::Sampler) {
            warnMissing(value.first);
            continue;
        }
        const uint32_t slot = binding->bindPoint;
        auto* sampler = dynamic_cast<MetalGpuSampler*>(value.second.get());
        if (!sampler || !sampler->sampler)
            continue;
        if (binding->stages == 0 || (binding->stages & ShaderStageVertex))
            [m_Impl->encoder setVertexSamplerState:sampler->sampler atIndex:slot];
        if (binding->stages == 0 || (binding->stages & ShaderStagePixel))
            [m_Impl->encoder setFragmentSamplerState:sampler->sampler atIndex:slot];
        if (m_Impl->computeEncoder && (binding->stages == 0 || (binding->stages & ShaderStageCompute)))
            [m_Impl->computeEncoder setSamplerState:sampler->sampler atIndex:slot];
    }
    for (const auto& value : group->GetStorageBuffers()) {
        const auto* binding = reflection.Find(value.first);
        if (!binding || binding->type != ShaderBindingType::StorageBuffer) {
            warnMissing(value.first);
            continue;
        }
        auto* view = value.second ? dynamic_cast<MetalGpuBuffer*>(value.second->buffer.get()) : nullptr;
        if (!view || !view->buffer)
            continue;
        if (binding->stages == 0 || (binding->stages & ShaderStageVertex))
            [m_Impl->encoder setVertexBuffer:view->buffer offset:0 atIndex:binding->bindPoint];
        if (binding->stages == 0 || (binding->stages & ShaderStagePixel))
            [m_Impl->encoder setFragmentBuffer:view->buffer offset:0 atIndex:binding->bindPoint];
        if (m_Impl->computeEncoder && (binding->stages == 0 || (binding->stages & ShaderStageCompute)))
            [m_Impl->computeEncoder setBuffer:view->buffer offset:0 atIndex:binding->bindPoint];
    }
}

void MetalContext::Draw(uint32_t vertexCount, uint32_t startVertex) {
    if (!m_Impl->encoder)
        return;
    [m_Impl->encoder drawPrimitives:m_Impl->primitiveType vertexStart:startVertex vertexCount:vertexCount];
}

void MetalContext::DrawIndexed(uint32_t indexCount, uint32_t startIndex, uint32_t baseVertex) {
    if (!m_Impl->encoder || !m_Impl->boundIB)
        return;
    NSUInteger byteOffset = static_cast<NSUInteger>(startIndex) * sizeof(uint32_t);
    [m_Impl->encoder drawIndexedPrimitives:m_Impl->primitiveType
                                indexCount:indexCount
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:m_Impl->boundIB->buffer
                         indexBufferOffset:byteOffset
                             instanceCount:1
                                baseVertex:static_cast<NSInteger>(baseVertex)
                              baseInstance:0];
}

void MetalContext::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex) {
    if (!m_Impl->encoder)
        return;
    [m_Impl->encoder drawPrimitives:m_Impl->primitiveType
                        vertexStart:startVertex
                        vertexCount:vertexCount
                      instanceCount:instanceCount];
}

void MetalContext::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex,
                                        uint32_t baseVertex) {
    if (!m_Impl->encoder || !m_Impl->boundIB)
        return;
    NSUInteger byteOffset = static_cast<NSUInteger>(startIndex) * sizeof(uint32_t);
    [m_Impl->encoder drawIndexedPrimitives:m_Impl->primitiveType
                                indexCount:indexCount
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:m_Impl->boundIB->buffer
                         indexBufferOffset:byteOffset
                             instanceCount:instanceCount
                                baseVertex:static_cast<NSInteger>(baseVertex)
                              baseInstance:0];
}

void MetalContext::Dispatch(uint32_t x, uint32_t y, uint32_t z) {
    if (!m_Impl || !m_Impl->computeEncoder || x == 0 || y == 0 || z == 0)
        return;
    MTLSize threadgroups = MTLSizeMake(x, y, z);
    MTLSize threadsPerThreadgroup = MTLSizeMake(9, 1, 1);
    [m_Impl->computeEncoder dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerThreadgroup];
}

void MetalContext::SetViewport(float x, float y, float w, float h) {
    m_Impl->vpX = x;
    m_Impl->vpY = y;
    m_Impl->vpW = w;
    m_Impl->vpH = h;

    if (m_Impl->encoder) {
        MTLViewport vp = {x, y, w, h, 0.0, 1.0};
        [m_Impl->encoder setViewport:vp];
    }
}

std::shared_ptr<GpuTexture> MetalContext::UploadTexture2D(const void* rgba8Data, int width, int height) {
    if (!rgba8Data || width <= 0 || height <= 0 || !m_Impl || !m_Impl->device)
        return nullptr;
    RHITextureDesc desc;
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.format = RHIFormat::RGBA8UNorm;
    desc.usage = RHIResourceUsage::ShaderResource;
    RHITextureSubresourceData data;
    data.data = rgba8Data;
    data.rowPitch = desc.width * 4;
    data.slicePitch = data.rowPitch * desc.height;
    return UploadTexture(desc, &data, 1);
}

std::shared_ptr<GpuTexture> MetalContext::UploadTexture(const RHITextureDesc& desc,
                                                        const RHITextureSubresourceData* data,
                                                        uint32_t subresourceCount) {
    if (!data || subresourceCount == 0 || !m_Impl || !m_Impl->device || desc.format != RHIFormat::RGBA8UNorm ||
        subresourceCount != desc.mipLevels * desc.arrayLayers) {
        return nullptr;
    }
    auto texture = std::dynamic_pointer_cast<MetalGpuTexture>(CreateTexture(desc));
    if (!texture || !texture->texture)
        return nullptr;
    for (uint32_t i = 0; i < subresourceCount; ++i) {
        const auto& src = data[i];
        if (!src.data || src.mipLevel >= desc.mipLevels || src.arrayLayer >= desc.arrayLayers) {
            return nullptr;
        }
        const uint32_t mipWidth = (std::max)(1u, desc.width >> src.mipLevel);
        const uint32_t mipHeight = (std::max)(1u, desc.height >> src.mipLevel);
        const uint32_t rowPitch = src.rowPitch ? src.rowPitch : mipWidth * 4;
        if (rowPitch < mipWidth * 4)
            return nullptr;
        MTLRegion region = MTLRegionMake2D(0, 0, mipWidth, mipHeight);
        [texture->texture
            replaceRegion:region
              mipmapLevel:src.mipLevel
                    slice:src.arrayLayer
                withBytes:src.data
              bytesPerRow:static_cast<NSUInteger>(rowPitch)
            bytesPerImage:static_cast<NSUInteger>(src.slicePitch ? src.slicePitch : rowPitch * mipHeight)];
    }
    return texture;
}

std::shared_ptr<GpuTexture> MetalContext::CreateTexture(const RHITextureDesc& desc) {
    if (!m_Impl || !m_Impl->device || desc.width == 0 || desc.height == 0)
        return nullptr;
    MTLTextureDescriptor* native = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:ToMetalFormat(desc.format)
                                                                                      width:desc.width
                                                                                     height:desc.height
                                                                                  mipmapped:desc.mipLevels > 1];
    native.mipmapLevelCount = desc.mipLevels;
    native.arrayLength = desc.cube ? 1 : desc.arrayLayers;
    native.usage = ToMetalUsage(desc.usage);
    native.storageMode = MTLStorageModeShared;
    if (desc.cube)
        native.textureType = MTLTextureTypeCube;
    else if (desc.arrayLayers > 1)
        native.textureType = MTLTextureType2DArray;

    auto result = std::make_shared<MetalGpuTexture>();
    result->desc = desc;
    result->isCube = desc.cube;
    result->texture = [m_Impl->device newTextureWithDescriptor:native];
    if (result->texture)
        CommitRHIResourceAccounting(std::static_pointer_cast<GpuTexture>(result));
    return result->texture ? result : nullptr;
}

std::shared_ptr<GpuTextureView> MetalContext::CreateTextureView(const std::shared_ptr<GpuTexture>& texture,
                                                                const RHITextureViewDesc& desc) {
    auto nativeTexture = std::dynamic_pointer_cast<MetalGpuTexture>(texture);
    if (!nativeTexture || !nativeTexture->texture)
        return nullptr;
    auto view = std::make_shared<MetalGpuTextureView>();
    view->texture = texture;
    view->desc = desc;
    view->mipLevel = desc.firstMip;
    view->slice = desc.firstLayer;
    const bool renderTargetView =
        HasUsage(desc.usage, RHIResourceUsage::RenderTarget) || HasUsage(desc.usage, RHIResourceUsage::DepthStencil);
    MTLTextureType viewType = nativeTexture->texture.textureType;
    NSUInteger firstSlice = desc.firstLayer;
    NSUInteger sliceCount = desc.layerCount;
    if (nativeTexture->isCube && renderTargetView) {
        viewType = MTLTextureType2D;
        sliceCount = 1;
    } else if (nativeTexture->isCube) {
        viewType = MTLTextureTypeCube;
        firstSlice = 0;
        sliceCount = 6;
    } else if (sliceCount > 1) {
        viewType = MTLTextureType2DArray;
    } else {
        viewType = MTLTextureType2D;
    }
    view->textureView = [nativeTexture->texture newTextureViewWithPixelFormat:nativeTexture->texture.pixelFormat
                                                                  textureType:viewType
                                                                       levels:NSMakeRange(desc.firstMip, desc.mipCount)
                                                                       slices:NSMakeRange(firstSlice, sliceCount)];
    if (!view->textureView)
        view->textureView = nativeTexture->texture;
    return view;
}

std::shared_ptr<GpuSampler> MetalContext::CreateSampler(const RHISamplerDesc& desc) {
    if (!m_Impl || !m_Impl->device)
        return nullptr;
    MTLSamplerDescriptor* native = [[MTLSamplerDescriptor alloc] init];
    native.minFilter = desc.filter == RHIFilter::Point ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    native.magFilter = desc.filter == RHIFilter::Point ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
    if (desc.filter == RHIFilter::ComparisonLinear) {
        native.compareFunction = MTLCompareFunctionLessEqual;
    }
    native.sAddressMode =
        desc.addressU == RHIAddressMode::Repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
    native.tAddressMode =
        desc.addressV == RHIAddressMode::Repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
    native.rAddressMode =
        desc.addressW == RHIAddressMode::Repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
    auto sampler = std::make_shared<MetalGpuSampler>();
    sampler->desc = desc;
    sampler->sampler = [m_Impl->device newSamplerStateWithDescriptor:native];
    return sampler->sampler ? sampler : nullptr;
}

void MetalContext::BindPSTexture(uint32_t slot, GpuTexture* tex) {
    if (!m_Impl || !m_Impl->encoder)
        return;
    auto* native = dynamic_cast<MetalGpuTexture*>(tex);
    if (!native || !native->texture)
        return;
    [m_Impl->encoder setFragmentTexture:native->texture atIndex:slot];
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
