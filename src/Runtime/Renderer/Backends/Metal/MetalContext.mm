// MetalContext.mm – Metal rendering back-end (Objective-C++)
// Only compiled on macOS (guarded by MYENGINE_PLATFORM_MACOS, and the file is
// only compiled on macOS in xmake.lua).

#include "Core/Platform.h"
#ifdef MYENGINE_PLATFORM_MACOS

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "Renderer/Backends/Metal/MetalContext.h"
#include "Core/Window.h"
#include "Renderer/RHI/RHIResourceStats.h"
#include "Core/Logger.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kMetalBindlessTextureCapacity = 4096;
constexpr uint32_t kMetalMaterialSamplerCount = 8;
constexpr uint32_t kMetalMaterialSamplerBaseIndex = kMetalBindlessTextureCapacity;
constexpr NSUInteger kMetalBindlessBufferIndex = 14;
constexpr NSUInteger kMetalVertexBufferIndex = 15;
constexpr uint64_t kMetalBindlessRetireFrames = 3;
constexpr std::array<const char*, kMetalMaterialSamplerCount> kMetalMaterialSamplerNames = {
    "g_LinearRepeatSampler",       "g_PointRepeatSampler",         "g_LinearClampURepeatVSampler",
    "g_PointClampURepeatVSampler", "g_LinearRepeatUClampVSampler", "g_PointRepeatUClampVSampler",
    "g_LinearClampSampler",        "g_PointClampSampler",
};
} // namespace

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
    bool supportsIndirectCommandBuffers = false;
};

struct MetalGraphicsPipeline : GpuGraphicsPipeline {
    id<MTLRenderPipelineState> pipelineState;
    id<MTLDepthStencilState> depthState;
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;
    MTLCullMode cullMode = MTLCullModeBack;
    MTLWinding frontWinding = MTLWindingClockwise;
    MTLTriangleFillMode fillMode = MTLTriangleFillModeFill;
    MTLDepthClipMode depthClipMode = MTLDepthClipModeClip;
    float depthBias = 0.0f;
    float slopeScaledDepthBias = 0.0f;
    float depthBiasClamp = 0.0f;
};

struct MetalComputePipeline : GpuComputePipeline {
    id<MTLComputePipelineState> pipelineState;
    MTLSize threadsPerThreadgroup = MTLSizeMake(1, 1, 1);
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
    std::function<void(uint32_t)> retireBindless;

    ~MetalGpuTextureView() override {
        if (bindlessIndex != UINT32_MAX && retireBindless)
            retireBindless(bindlessIndex);
    }
    void* GetImGuiTextureId() override { return (__bridge void*)textureView; }
};

struct MetalGpuSampler : GpuSampler {
    id<MTLSamplerState> sampler = nil;
};

uint32_t MetalFormatBytesPerPixel(RHIFormat format) {
    switch (format) {
    case RHIFormat::R8UInt:
    case RHIFormat::R8UNorm:
        return 1;
    case RHIFormat::RG16Float:
    case RHIFormat::R16UInt:
        return 4;
    case RHIFormat::RGBA8UNorm:
    case RHIFormat::BGRA8UNorm:
    case RHIFormat::RGBA8UNormSrgb:
    case RHIFormat::R32UInt:
    case RHIFormat::R32Float:
    case RHIFormat::D24S8:
    case RHIFormat::D32Float:
        return 4;
    case RHIFormat::RGBA16Float:
    case RHIFormat::RG32Float:
        return 8;
    case RHIFormat::RGB32Float:
    case RHIFormat::RGBA32Float:
        return 16;
    default:
        return 0;
    }
}

struct MetalReadbackTicket final : GpuReadbackTicket {
    id<MTLCommandBuffer> commandBuffer = nil;
    id<MTLBuffer> buffer = nil;
    uint32_t size = 0;

    bool IsReady() const override {
        if (!commandBuffer || !buffer)
            return false;
        return commandBuffer.status == MTLCommandBufferStatusCompleted ||
               commandBuffer.status == MTLCommandBufferStatusError;
    }
    bool Read(std::vector<uint8_t>& data) override {
        if (!commandBuffer || commandBuffer.status != MTLCommandBufferStatusCompleted || !buffer)
            return false;
        data.resize(size);
        if (size)
            std::memcpy(data.data(), buffer.contents, size);
        return true;
    }
    uint32_t GetSize() const override { return size; }
};

struct MetalTextureReadbackTicket final : GpuTextureReadbackTicket {
    id<MTLCommandBuffer> commandBuffer = nil;
    id<MTLTexture> texture = nil;
    RHITextureRegion region{};
    RHIFormat format = RHIFormat::Unknown;
    uint32_t rowPitch = 0;
    uint32_t size = 0;

    bool IsReady() const override {
        if (!commandBuffer || !texture)
            return false;
        return commandBuffer.status == MTLCommandBufferStatusCompleted ||
               commandBuffer.status == MTLCommandBufferStatusError;
    }
    bool Read(std::vector<uint8_t>& data) override {
        if (!commandBuffer || commandBuffer.status != MTLCommandBufferStatusCompleted || !texture)
            return false;
        data.resize(size);
        if (size) {
            const MTLRegion nativeRegion = MTLRegionMake2D(region.x, region.y, region.width, region.height);
            [texture getBytes:data.data()
                  bytesPerRow:rowPitch
                bytesPerImage:size
                   fromRegion:nativeRegion
                  mipmapLevel:region.mipLevel
                        slice:region.arrayLayer];
        }
        return true;
    }
    uint32_t GetSize() const override { return size; }
    uint32_t GetRowPitch() const override { return rowPitch; }
    uint32_t GetWidth() const override { return region.width; }
    uint32_t GetHeight() const override { return region.height; }
    RHIFormat GetFormat() const override { return format; }
};

struct MetalIndexedIndirectCommandStream final : GpuIndexedIndirectCommandStream {
    id<MTLIndirectCommandBuffer> commands = nil;
    id<MTLBuffer> executionRange = nil;
    id<MTLArgumentEncoder> commandArgumentEncoder = nil;
    id<MTLBuffer> commandArgumentBuffer = nil;
};

struct MetalBindlessState {
    struct RetiredIndex {
        uint64_t releaseFrame = 0;
        uint32_t index = UINT32_MAX;
    };

    std::mutex mutex;
    id<MTLArgumentEncoder> encoder = nil;
    id<MTLBuffer> argumentBuffer = nil;
    id<MTLTexture> fallbackTexture = nil;
    id<MTLSamplerState> fallbackSampler = nil;
    std::vector<id<MTLTexture>> textures;
    std::array<id<MTLSamplerState>, kMetalMaterialSamplerCount> materialSamplers{};
    std::vector<uint32_t> freeIndices;
    std::vector<RetiredIndex> retired;
    uint32_t nextIndex = 0;
    uint64_t frameSerial = 0;
    bool exhaustedLogged = false;
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
        return MTLPixelFormatInvalid;
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
        const std::string declarationText = (*it)[0].str();
        const std::string typeText = (*it)[1].str();
        const std::string rawName = (*it)[2].str();
        const std::string attr = (*it)[3].str();
        ShaderBindingDesc binding;
        binding.name = NormalizeSlangBindingName(rawName);
        if (binding.name.empty() || binding.name == "g_BindlessTable")
            continue;
        binding.bindPoint = static_cast<uint32_t>(std::stoul((*it)[4].str()));
        binding.bindCount = 1;
        binding.stages = stage;
        if (attr == "texture") {
            binding.type = declarationText.find("access::write") != std::string::npos ||
                                   declarationText.find("access::read_write") != std::string::npos
                               ? ShaderBindingType::StorageTexture
                               : ShaderBindingType::Texture;
        } else if (attr == "sampler") {
            binding.type = ShaderBindingType::Sampler;
        } else {
            binding.type = typeText.find("const device") != std::string::npos
                               ? ShaderBindingType::StructuredBuffer
                               : (typeText.find("device") != std::string::npos ? ShaderBindingType::StorageBuffer
                                                                               : ShaderBindingType::ConstantBuffer);
        }
        AddOrMergeBinding(reflection, binding);
    }
    static const std::regex argumentSamplerRegex(R"(sampler\s+(g_[A-Za-z0-9_]+)\s*\[\[id\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), argumentSamplerRegex), end; it != end; ++it) {
        ShaderBindingDesc binding;
        binding.name = (*it)[1].str();
        binding.type = ShaderBindingType::Sampler;
        binding.bindPoint = static_cast<uint32_t>(std::stoul((*it)[2].str()));
        binding.bindCount = 1;
        binding.stages = stage;
        AddOrMergeBinding(reflection, binding);
    }
}

std::string RewriteMetalConstantBufferLayouts(std::string source) {
    // Slang emits HLSL cbuffers as ordinary MSL structs. MSL's natural float2/uint2 alignment and float3 size differ
    // from the packed HLSL/C++ upload ABI used by every RHI backend (for example, uint + uint2 is 12 bytes in HLSL
    // but places the uint2 at offset 8 in an ordinary MSL struct). Restrict the rewrite to Slang parameter-group
    // structs; storage-buffer structs retain their explicit GPU-scene ABI.
    static const std::regex vectorRegex(R"(\b(half|float|int|uint)(2|3)\b)");
    const std::string marker = "struct SLANG_ParameterGroup_";
    size_t search = 0;
    while ((search = source.find(marker, search)) != std::string::npos) {
        const size_t bodyBegin = source.find('{', search + marker.size());
        if (bodyBegin == std::string::npos)
            return {};
        const size_t bodyEnd = source.find("};", bodyBegin + 1);
        if (bodyEnd == std::string::npos)
            return {};
        const std::string body = source.substr(bodyBegin + 1, bodyEnd - bodyBegin - 1);
        const std::string packedBody = std::regex_replace(body, vectorRegex, "packed_$1$2");
        source.replace(bodyBegin + 1, bodyEnd - bodyBegin - 1, packedBody);
        search = bodyBegin + 1 + packedBody.size() + 2;
    }
    return source;
}

std::string RewriteMetalBindlessArgumentBuffer(std::string source) {
    if (source.find("g_BindlessTextures_") == std::string::npos)
        return source;

    constexpr const char* declaration = "\nstruct MyEngineBindlessTextureTable\n"
                                        "{\n"
                                        "    array<texture2d<float, access::sample>, 4096> textures [[id(0)]];\n"
                                        "    sampler g_LinearRepeatSampler [[id(4096)]];\n"
                                        "    sampler g_PointRepeatSampler [[id(4097)]];\n"
                                        "    sampler g_LinearClampURepeatVSampler [[id(4098)]];\n"
                                        "    sampler g_PointClampURepeatVSampler [[id(4099)]];\n"
                                        "    sampler g_LinearRepeatUClampVSampler [[id(4100)]];\n"
                                        "    sampler g_PointRepeatUClampVSampler [[id(4101)]];\n"
                                        "    sampler g_LinearClampSampler [[id(4102)]];\n"
                                        "    sampler g_PointClampSampler [[id(4103)]];\n"
                                        "};\n";
    const std::string marker = "using namespace metal;";
    const size_t markerPosition = source.find(marker);
    if (markerPosition == std::string::npos)
        return {};
    source.insert(markerPosition + marker.size(), declaration);

    const std::regex memberRegex(
        R"(array<texture2d<float,\s*access::sample>,\s*int\(4096\)>\s+g_BindlessTextures_(\d+);)");
    source = std::regex_replace(source, memberRegex, "constant MyEngineBindlessTextureTable* g_BindlessTable_$1;");

    const std::regex parameterRegex(
        R"(array<texture2d<float,\s*access::sample>,\s*int\(4096\)>\s+g_BindlessTextures_(\d+))");
    source = std::regex_replace(source, parameterRegex,
                                "constant MyEngineBindlessTextureTable& g_BindlessTable_$1 [[buffer(14)]]");

    const std::regex assignmentRegex(R"(->g_BindlessTextures_(\d+)\s*=\s*g_BindlessTextures_(\d+);)");
    source = std::regex_replace(source, assignmentRegex, "->g_BindlessTable_$1 = &g_BindlessTable_$2;");

    const std::regex accessRegex(R"(->g_BindlessTextures_(\d+)\[)");
    source = std::regex_replace(source, accessRegex, "->g_BindlessTable_$1->textures[");

    std::smatch tableMember;
    static const std::regex tableMemberRegex(R"(constant\s+MyEngineBindlessTextureTable\*\s+g_BindlessTable_(\d+);)");
    if (!std::regex_search(source, tableMember, tableMemberRegex))
        return {};
    const std::string tableSuffix = tableMember[1].str();
    for (const char* samplerName : kMetalMaterialSamplerNames) {
        const std::string escapedName = samplerName;
        source = std::regex_replace(
            source, std::regex(",\\s*sampler\\s+" + escapedName + "_\\d+\\s*\\[\\[sampler\\(\\d+\\)\\]\\]"), "");
        source = std::regex_replace(source, std::regex("\\s*sampler\\s+" + escapedName + "_\\d+\\s*;"), "");
        source = std::regex_replace(source,
                                    std::regex("\\s*\\(&[A-Za-z_][A-Za-z0-9_]*\\)->" + escapedName + "_\\d+\\s*=\\s*" +
                                               escapedName + "_\\d+\\s*;"),
                                    "");
        source = std::regex_replace(source, std::regex("->" + escapedName + "_\\d+"),
                                    "->g_BindlessTable_" + tableSuffix + "->" + escapedName);
    }
    return source;
}

void CollectMetalBufferBindingNames(const std::string& source, std::vector<std::string>& names) {
    static const std::regex bufferRegex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[buffer\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), bufferRegex), end; it != end; ++it) {
        const std::string normalized = NormalizeSlangBindingName((*it)[2].str());
        if (normalized != "g_BindlessTable" && std::find(names.begin(), names.end(), normalized) == names.end())
            names.push_back(normalized);
    }
}

std::string RemapMetalBufferBindings(const std::string& source,
                                     const std::unordered_map<std::string, uint32_t>& bindings) {
    static const std::regex bufferRegex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[buffer\((\d+)\)\]\])");
    std::string result;
    size_t cursor = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), bufferRegex), end; it != end; ++it) {
        const auto& match = *it;
        result.append(source, cursor, static_cast<size_t>(match.position()) - cursor);
        const std::string rawName = match[2].str();
        const std::string normalized = NormalizeSlangBindingName(rawName);
        const auto mapped = bindings.find(normalized);
        if (normalized == "g_BindlessTable") {
            result += match.str();
        } else if (mapped != bindings.end()) {
            result += match[1].str();
            result += " ";
            result += rawName;
            result += " [[buffer(";
            result += std::to_string(mapped->second);
            result += ")]]";
        } else {
            result += match.str();
        }
        cursor = static_cast<size_t>(match.position() + match.length());
    }
    result.append(source, cursor, std::string::npos);
    return result;
}

bool RewriteMetalBufferBindings(std::string& first, std::string* second = nullptr) {
    std::vector<std::string> names;
    CollectMetalBufferBindingNames(first, names);
    if (second)
        CollectMetalBufferBindingNames(*second, names);
    std::sort(names.begin(), names.end());
    if (names.size() > kMetalBindlessBufferIndex) {
        Logger::Error("[Metal] Shader requires ", names.size(),
                      " buffer slots; only 14 are available with the bindless table");
        return false;
    }
    std::unordered_map<std::string, uint32_t> bindings;
    for (uint32_t index = 0; index < names.size(); ++index)
        bindings[names[index]] = index;
    first = RemapMetalBufferBindings(first, bindings);
    if (second)
        *second = RemapMetalBufferBindings(*second, bindings);
    return true;
}

void CollectMetalTextureBindingNames(const std::string& source, std::vector<std::string>& names) {
    static const std::regex textureRegex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[texture\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), textureRegex), end; it != end; ++it) {
        const std::string normalized = NormalizeSlangBindingName((*it)[2].str());
        if (std::find(names.begin(), names.end(), normalized) == names.end())
            names.push_back(normalized);
    }
}

std::string RemapMetalTextureBindings(const std::string& source,
                                      const std::unordered_map<std::string, uint32_t>& bindings) {
    static const std::regex textureRegex(
        R"(([A-Za-z_][A-Za-z0-9_:<>, \*&]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\[texture\((\d+)\)\]\])");
    std::string result;
    size_t cursor = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), textureRegex), end; it != end; ++it) {
        const auto& match = *it;
        result.append(source, cursor, static_cast<size_t>(match.position()) - cursor);
        const std::string rawName = match[2].str();
        const auto mapped = bindings.find(NormalizeSlangBindingName(rawName));
        if (mapped == bindings.end()) {
            result += match.str();
        } else {
            result += match[1].str();
            result += " ";
            result += rawName;
            result += " [[texture(";
            result += std::to_string(mapped->second);
            result += ")]]";
        }
        cursor = static_cast<size_t>(match.position() + match.length());
    }
    result.append(source, cursor, std::string::npos);
    return result;
}

bool RewriteMetalTextureBindings(std::string& first, std::string* second = nullptr) {
    std::vector<std::string> names;
    CollectMetalTextureBindingNames(first, names);
    if (second)
        CollectMetalTextureBindingNames(*second, names);
    std::sort(names.begin(), names.end());
    if (names.size() > 128) {
        Logger::Error("[Metal] Shader requires ", names.size(), " direct texture slots; device maximum is 128");
        return false;
    }
    std::unordered_map<std::string, uint32_t> bindings;
    for (uint32_t index = 0; index < names.size(); ++index)
        bindings[names[index]] = index;
    first = RemapMetalTextureBindings(first, bindings);
    if (second)
        *second = RemapMetalTextureBindings(*second, bindings);
    return true;
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
    std::shared_ptr<MetalBindlessState> bindless = std::make_shared<MetalBindlessState>();
    id<MTLComputePipelineState> indirectCommandBuildPipeline = nil;
    id<MTLComputePipelineState> clearStorageBufferPipeline = nil;
    MetalComputePipeline* boundComputePipeline = nullptr;
    bool modernDeviceBaseline = false;
    bool indirectCommandBuffersSupported = false;
    bool bindlessSupported = false;
    bool modernFormatsSupported = false;

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
    void DispatchIndirect(GpuBuffer* args, uint64_t offset) override { m_Owner.DispatchIndirect(args, offset); }
    void ClearStorageBuffer(GpuBufferView* view, uint32_t value) override { m_Owner.ClearStorageBuffer(view, value); }
    void UAVBarrier(GpuResource* resource) override { m_Owner.UAVBarrier(resource); }
    void BuildIndexedIndirectCommandStream(GpuIndexedIndirectCommandStream* stream, GpuBuffer* arguments,
                                           uint64_t argumentOffset, GpuBuffer* countBuffer, uint64_t countOffset,
                                           GpuBuffer* indexBuffer, uint32_t maxDrawCount, uint32_t stride) override {
        m_Owner.BuildIndexedIndirectCommandStream(stream, arguments, argumentOffset, countBuffer, countOffset,
                                                  indexBuffer, maxDrawCount, stride);
    }
    void ExecuteIndexedIndirectCommandStream(GpuIndexedIndirectCommandStream* stream) override {
        m_Owner.ExecuteIndexedIndirectCommandStream(stream);
    }

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

    const bool macOS14OrNewer =
        [[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:NSOperatingSystemVersion{14, 0, 0}];
    const bool apple7OrNewer = [m_Impl->device supportsFamily:MTLGPUFamilyApple7];
    const bool argumentBuffersTier2 = m_Impl->device.argumentBuffersSupport == MTLArgumentBuffersTier2;
    m_Impl->modernDeviceBaseline = macOS14OrNewer && apple7OrNewer;
    m_Impl->indirectCommandBuffersSupported = m_Impl->modernDeviceBaseline;

    if (m_Impl->modernDeviceBaseline && argumentBuffersTier2) {
        auto& bindless = *m_Impl->bindless;
        MTLArgumentDescriptor* textures = [[MTLArgumentDescriptor alloc] init];
        textures.dataType = MTLDataTypeTexture;
        textures.index = 0;
        textures.arrayLength = kMetalBindlessTextureCapacity;
        textures.access = MTLBindingAccessReadOnly;
        textures.textureType = MTLTextureType2D;
        NSMutableArray<MTLArgumentDescriptor*>* bindlessArguments = [NSMutableArray arrayWithObject:textures];
        for (uint32_t samplerIndex = 0; samplerIndex < kMetalMaterialSamplerCount; ++samplerIndex) {
            MTLArgumentDescriptor* sampler = [[MTLArgumentDescriptor alloc] init];
            sampler.dataType = MTLDataTypeSampler;
            sampler.index = kMetalMaterialSamplerBaseIndex + samplerIndex;
            sampler.arrayLength = 1;
            sampler.access = MTLBindingAccessReadOnly;
            [bindlessArguments addObject:sampler];
        }
        bindless.encoder = [m_Impl->device newArgumentEncoderWithArguments:bindlessArguments];
        bindless.argumentBuffer = [m_Impl->device newBufferWithLength:bindless.encoder.encodedLength
                                                              options:MTLResourceStorageModeShared];
        [bindless.encoder setArgumentBuffer:bindless.argumentBuffer offset:0];

        MTLTextureDescriptor* fallbackDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:1
                                                              height:1
                                                           mipmapped:NO];
        fallbackDesc.usage = MTLTextureUsageShaderRead;
        fallbackDesc.storageMode = MTLStorageModeShared;
        bindless.fallbackTexture = [m_Impl->device newTextureWithDescriptor:fallbackDesc];
        const uint32_t white = 0xffffffffu;
        [bindless.fallbackTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                                    mipmapLevel:0
                                      withBytes:&white
                                    bytesPerRow:sizeof(white)];
        bindless.textures.assign(kMetalBindlessTextureCapacity, bindless.fallbackTexture);
        for (uint32_t i = 0; i < kMetalBindlessTextureCapacity; ++i)
            [bindless.encoder setTexture:bindless.fallbackTexture atIndex:i];
        MTLSamplerDescriptor* fallbackSamplerDesc = [[MTLSamplerDescriptor alloc] init];
        fallbackSamplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
        fallbackSamplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
        fallbackSamplerDesc.mipFilter = MTLSamplerMipFilterLinear;
        fallbackSamplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
        fallbackSamplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;
        fallbackSamplerDesc.rAddressMode = MTLSamplerAddressModeRepeat;
        fallbackSamplerDesc.supportArgumentBuffers = YES;
        bindless.fallbackSampler = [m_Impl->device newSamplerStateWithDescriptor:fallbackSamplerDesc];
        bindless.materialSamplers.fill(bindless.fallbackSampler);
        for (uint32_t i = 0; i < kMetalMaterialSamplerCount; ++i)
            [bindless.encoder setSamplerState:bindless.fallbackSampler atIndex:kMetalMaterialSamplerBaseIndex + i];
        m_Impl->bindlessSupported =
            bindless.encoder && bindless.argumentBuffer && bindless.fallbackTexture && bindless.fallbackSampler;

        static constexpr const char* kBuildIndirectCommandsMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct MyEngineIndexedDraw
{
    uint objectIndex;
    uint indexCount;
    uint instanceCount;
    uint startIndex;
    int baseVertex;
    uint startInstance;
};

struct MyEngineIndirectCommandContainer
{
    command_buffer commands [[id(0)]];
};

kernel void BuildIndexedCommands(device const MyEngineIndexedDraw* draws [[buffer(0)]],
                                 device const uint* drawCount [[buffer(1)]],
                                 device const uint* indexBuffer [[buffer(2)]],
                                 constant uint2& limits [[buffer(3)]],
                                 constant MyEngineIndirectCommandContainer& commandContainer [[buffer(4)]],
                                 device uint2* executionRange [[buffer(5)]],
                                 uint commandIndex [[thread_position_in_grid]])
{
    const uint count = min(drawCount[0], limits.x);
    if (commandIndex == 0)
        executionRange[0] = uint2(0, count);
    if (commandIndex >= count)
        return;
    const MyEngineIndexedDraw draw = draws[commandIndex];
    render_command command(commandContainer.commands, commandIndex);
    command.draw_indexed_primitives(primitive_type::triangle, draw.indexCount,
                                    indexBuffer + draw.startIndex, draw.instanceCount,
                                    draw.baseVertex, draw.startInstance);
}

kernel void ClearStorageBuffer(device uint* destination [[buffer(0)]],
                               constant uint& clearValue [[buffer(1)]],
                               uint wordIndex [[thread_position_in_grid]])
{
    destination[wordIndex] = clearValue;
}
)MSL";
        NSError* indirectError = nil;
        id<MTLLibrary> indirectLibrary =
            [m_Impl->device newLibraryWithSource:[NSString stringWithUTF8String:kBuildIndirectCommandsMSL]
                                         options:nil
                                           error:&indirectError];
        id<MTLFunction> indirectFunction =
            indirectLibrary ? [indirectLibrary newFunctionWithName:@"BuildIndexedCommands"] : nil;
        id<MTLFunction> clearFunction =
            indirectLibrary ? [indirectLibrary newFunctionWithName:@"ClearStorageBuffer"] : nil;
        if (indirectFunction) {
            m_Impl->indirectCommandBuildPipeline = [m_Impl->device newComputePipelineStateWithFunction:indirectFunction
                                                                                                 error:&indirectError];
        }
        if (clearFunction) {
            m_Impl->clearStorageBufferPipeline = [m_Impl->device newComputePipelineStateWithFunction:clearFunction
                                                                                               error:&indirectError];
        }
        if (!m_Impl->indirectCommandBuildPipeline || !m_Impl->clearStorageBufferPipeline) {
            m_Impl->indirectCommandBuffersSupported = false;
            Logger::Warn("[Metal] Modern command infrastructure unavailable: ",
                         indirectError ? [[indirectError localizedDescription] UTF8String] : "unknown");
        }
    } else if (m_Impl->modernDeviceBaseline) {
        Logger::Warn("[Metal] Modern Deferred requires argument-buffer tier 2");
    }

    m_Impl->layer.device = m_Impl->device;
    m_Impl->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    m_Impl->layer.framebufferOnly = YES;

    m_Impl->queue = [m_Impl->device newCommandQueue];
    if (!m_Impl->queue) {
        Logger::Error("[Metal] newCommandQueue failed");
        return false;
    }

    const int w = (std::max)(1, window->GetPixelWidth());
    const int h = (std::max)(1, window->GetPixelHeight());
    m_Impl->layer.drawableSize = CGSizeMake(static_cast<CGFloat>(w), static_cast<CGFloat>(h));
    m_Impl->vpW = static_cast<float>(w);
    m_Impl->vpH = static_cast<float>(h);
    m_Impl->drawableW = static_cast<uint32_t>(w);
    m_Impl->drawableH = static_cast<uint32_t>(h);
    m_Impl->EnsureDepthTexture(m_Impl->drawableW, m_Impl->drawableH);
    m_Impl->modernFormatsSupported =
        IsFormatSupported(RHIFormat::RGBA16Float, RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess |
                                                      RHIResourceUsage::RenderTarget) &&
        IsFormatSupported(RHIFormat::RG32Float, RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess) &&
        IsFormatSupported(RHIFormat::RG16Float, RHIResourceUsage::ShaderResource | RHIResourceUsage::RenderTarget) &&
        IsFormatSupported(RHIFormat::R8UNorm, RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess);

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
    m_Impl->indirectCommandBuildPipeline = nil;
    m_Impl->clearStorageBufferPipeline = nil;
    if (m_Impl->bindless) {
        std::lock_guard<std::mutex> lock(m_Impl->bindless->mutex);
        m_Impl->bindless->encoder = nil;
        m_Impl->bindless->argumentBuffer = nil;
        m_Impl->bindless->fallbackTexture = nil;
        m_Impl->bindless->fallbackSampler = nil;
        m_Impl->bindless->materialSamplers.fill(nil);
        m_Impl->bindless->textures.clear();
        m_Impl->bindless->freeIndices.clear();
        m_Impl->bindless->retired.clear();
        m_Impl->bindless->nextIndex = 0;
        m_Impl->bindless->frameSerial = 0;
        m_Impl->bindless->exhaustedLogged = false;
    }
    m_Impl->modernDeviceBaseline = false;
    m_Impl->indirectCommandBuffersSupported = false;
    m_Impl->bindlessSupported = false;
    m_Impl->modernFormatsSupported = false;
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
    if (m_Impl->bindless) {
        auto& bindless = *m_Impl->bindless;
        std::lock_guard<std::mutex> lock(bindless.mutex);
        ++bindless.frameSerial;
        auto retired = bindless.retired.begin();
        while (retired != bindless.retired.end()) {
            if (retired->releaseFrame > bindless.frameSerial) {
                ++retired;
                continue;
            }
            if (retired->index < bindless.textures.size()) {
                bindless.textures[retired->index] = bindless.fallbackTexture;
                [bindless.encoder setTexture:bindless.fallbackTexture atIndex:retired->index];
                bindless.freeIndices.push_back(retired->index);
            }
            retired = bindless.retired.erase(retired);
        }
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

RHIDeviceCapabilities MetalContext::GetCapabilities() const {
    RHIDeviceCapabilities capabilities;
    capabilities.maxTextureDimension2D = 16384;
    capabilities.maxTextureArrayLayers = 2048;
    // Metal exposes eight color attachment slots on supported macOS GPUs. Leaving the
    // IRHIDevice default of one here prevents the classic deferred GBuffer pass from
    // reaching the backend even though BeginRendering and pipeline creation support MRT.
    capabilities.maxColorAttachments = 8;
    capabilities.maxSamples = 1;
    capabilities.computeShaders = true;
    capabilities.storageTextures = true;
    bool bindlessReady = false;
    if (m_Impl && m_Impl->bindless) {
        std::lock_guard<std::mutex> lock(m_Impl->bindless->mutex);
        bindlessReady = m_Impl->bindlessSupported && !m_Impl->bindless->exhaustedLogged;
    }
    const bool modern = m_Impl && m_Impl->modernDeviceBaseline && m_Impl->indirectCommandBuffersSupported &&
                        m_Impl->indirectCommandBuildPipeline && bindlessReady && m_Impl->modernFormatsSupported;
    capabilities.indirectDraw = modern;
    capabilities.indirectDrawCount = modern;
    capabilities.indirectDispatch = modern;
    capabilities.bindlessResources = modern;
    capabilities.shaderDrawParameters = modern;
    capabilities.maxBindlessResources = modern ? kMetalBindlessTextureCapacity : 0;
    capabilities.modernDeferredFormats = modern;
    return capabilities;
}

bool MetalContext::IsFormatSupported(RHIFormat format, RHIResourceUsage usage) const {
    if (!m_Impl || !m_Impl->device)
        return false;
    const MTLPixelFormat pixelFormat = ToMetalFormat(format);
    if (pixelFormat == MTLPixelFormatInvalid)
        return false;
    if (HasUsage(usage, RHIResourceUsage::DepthStencil) && format != RHIFormat::D24S8 && format != RHIFormat::D32Float)
        return false;
    if (HasUsage(usage, RHIResourceUsage::UnorderedAccess) &&
        (format == RHIFormat::D24S8 || format == RHIFormat::D32Float || format == RHIFormat::RGBA8UNormSrgb ||
         format == RHIFormat::BGRA8UNorm))
        return false;
    if (HasUsage(usage, RHIResourceUsage::RenderTarget) &&
        (format == RHIFormat::R8UInt || format == RHIFormat::R16UInt || format == RHIFormat::R32UInt))
        return false;
    MTLTextureDescriptor* probe = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                                     width:4
                                                                                    height:4
                                                                                 mipmapped:NO];
    probe.storageMode = MTLStorageModePrivate;
    probe.usage = ToMetalUsage(usage);
    return [m_Impl->device newTextureWithDescriptor:probe] != nil;
}

std::shared_ptr<GpuReadbackTicket> MetalContext::ReadbackBufferAsync(const std::shared_ptr<GpuBuffer>& buffer) {
    auto native = std::dynamic_pointer_cast<MetalGpuBuffer>(buffer);
    if (!m_Impl || !m_Impl->queue || !native || !native->buffer || native->desc.size == 0)
        return nullptr;
    auto ticket = std::make_shared<MetalReadbackTicket>();
    ticket->buffer = native->buffer;
    ticket->size = native->desc.size;
    ticket->commandBuffer = m_Impl->cmdBuffer;
    if (!ticket->commandBuffer) {
        ticket->commandBuffer = [m_Impl->queue commandBuffer];
        [ticket->commandBuffer commit];
    }
    return ticket;
}

std::shared_ptr<GpuTextureReadbackTicket> MetalContext::ReadbackTextureAsync(const std::shared_ptr<GpuTexture>& texture,
                                                                             const RHITextureRegion& region) {
    auto native = std::dynamic_pointer_cast<MetalGpuTexture>(texture);
    const uint32_t bytesPerPixel = native ? MetalFormatBytesPerPixel(native->desc.format) : 0;
    if (!m_Impl || !m_Impl->queue || !native || !native->texture || bytesPerPixel == 0 || region.width == 0 ||
        region.height == 0 || region.depth != 1 || region.mipLevel >= native->desc.mipLevels ||
        region.arrayLayer >= native->desc.arrayLayers) {
        return nullptr;
    }
    const uint32_t mipWidth = (std::max)(1u, native->desc.width >> region.mipLevel);
    const uint32_t mipHeight = (std::max)(1u, native->desc.height >> region.mipLevel);
    if (region.x > mipWidth || region.y > mipHeight || region.width > mipWidth - region.x ||
        region.height > mipHeight - region.y) {
        return nullptr;
    }
    const uint64_t rowPitch = static_cast<uint64_t>(region.width) * bytesPerPixel;
    const uint64_t size = rowPitch * region.height;
    if (size > UINT32_MAX)
        return nullptr;
    auto ticket = std::make_shared<MetalTextureReadbackTicket>();
    ticket->texture = native->texture;
    ticket->region = region;
    ticket->format = native->desc.format;
    ticket->rowPitch = static_cast<uint32_t>(rowPitch);
    ticket->size = static_cast<uint32_t>(size);
    ticket->commandBuffer = m_Impl->cmdBuffer;
    if (!ticket->commandBuffer) {
        ticket->commandBuffer = [m_Impl->queue commandBuffer];
        [ticket->commandBuffer commit];
    }
    return ticket;
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

std::shared_ptr<GpuBuffer> MetalContext::CreateBuffer(const RHIBufferDesc& desc, const void* initialData) {
    if (!m_Impl || !m_Impl->device || desc.size == 0)
        return nullptr;

    auto buffer = std::make_shared<MetalGpuBuffer>();
    buffer->desc = desc;
    buffer->stride = desc.stride;
    buffer->byteSize = desc.size;
    buffer->buffer = [m_Impl->device newBufferWithLength:desc.size options:MTLResourceStorageModeShared];
    if (!buffer->buffer)
        return nullptr;
    if (initialData)
        std::memcpy(buffer->buffer.contents, initialData, desc.size);
    if (!desc.debugName.empty())
        buffer->buffer.label = [NSString stringWithUTF8String:desc.debugName.c_str()];
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(buffer));
    return buffer;
}

std::shared_ptr<GpuBufferView> MetalContext::CreateBufferView(const std::shared_ptr<GpuBuffer>& buffer,
                                                              const RHIBufferViewDesc& desc) {
    auto native = std::dynamic_pointer_cast<MetalGpuBuffer>(buffer);
    if (!native || !native->buffer || native->desc.stride == 0)
        return nullptr;
    const uint32_t totalElements = native->desc.size / native->desc.stride;
    if (desc.firstElement > totalElements)
        return nullptr;
    const uint32_t elementCount = desc.elementCount ? desc.elementCount : totalElements - desc.firstElement;
    if (elementCount > totalElements - desc.firstElement)
        return nullptr;

    auto view = std::make_shared<GpuBufferView>();
    view->buffer = buffer;
    view->desc = desc;
    view->desc.elementCount = elementCount;
    return view;
}

std::shared_ptr<GpuIndexedIndirectCommandStream> MetalContext::CreateIndexedIndirectCommandStream(uint32_t capacity) {
    if (!m_Impl || !m_Impl->device || !m_Impl->indirectCommandBuffersSupported || capacity == 0)
        return nullptr;
    MTLIndirectCommandBufferDescriptor* descriptor = [[MTLIndirectCommandBufferDescriptor alloc] init];
    descriptor.commandTypes = MTLIndirectCommandTypeDrawIndexed;
    descriptor.inheritPipelineState = YES;
    descriptor.inheritBuffers = YES;
    descriptor.maxVertexBufferBindCount = 0;
    descriptor.maxFragmentBufferBindCount = 0;
    auto stream = std::make_shared<MetalIndexedIndirectCommandStream>();
    stream->capacity = capacity;
    stream->commands = [m_Impl->device newIndirectCommandBufferWithDescriptor:descriptor
                                                              maxCommandCount:capacity
                                                                      options:0];
    stream->executionRange = [m_Impl->device newBufferWithLength:sizeof(MTLIndirectCommandBufferExecutionRange)
                                                         options:MTLResourceStorageModeShared];
    MTLArgumentDescriptor* commandArgument = [[MTLArgumentDescriptor alloc] init];
    commandArgument.dataType = MTLDataTypeIndirectCommandBuffer;
    commandArgument.index = 0;
    commandArgument.access = MTLBindingAccessReadWrite;
    stream->commandArgumentEncoder = [m_Impl->device newArgumentEncoderWithArguments:@[ commandArgument ]];
    if (!stream->commands || !stream->executionRange || !stream->commandArgumentEncoder)
        return nullptr;
    stream->commandArgumentBuffer = [m_Impl->device newBufferWithLength:stream->commandArgumentEncoder.encodedLength
                                                                options:MTLResourceStorageModeShared];
    if (!stream->commandArgumentBuffer)
        return nullptr;
    [stream->commandArgumentEncoder setArgumentBuffer:stream->commandArgumentBuffer offset:0];
    [stream->commandArgumentEncoder setIndirectCommandBuffer:stream->commands atIndex:0];
    return stream;
}

bool MetalContext::UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer, uint64_t offset, const void* data,
                                uint64_t size) {
    auto native = std::dynamic_pointer_cast<MetalGpuBuffer>(buffer);
    if (!native || !native->buffer || !data || size == 0 || offset > native->desc.size ||
        size > static_cast<uint64_t>(native->desc.size) - offset)
        return false;
    std::memcpy(static_cast<uint8_t*>(native->buffer.contents) + offset, data, static_cast<size_t>(size));
    return true;
}

std::shared_ptr<GpuShader> MetalContext::CreateShader(const std::string& mslSource, const std::string& vsEntry,
                                                      const std::string& psEntry, const VertexElement* layout,
                                                      uint32_t layoutCount) {
    std::string rewrittenSource = RewriteMetalConstantBufferLayouts(mslSource);
    rewrittenSource = RewriteMetalBindlessArgumentBuffer(std::move(rewrittenSource));
    if (rewrittenSource.empty() || !RewriteMetalBufferBindings(rewrittenSource) ||
        !RewriteMetalTextureBindings(rewrittenSource))
        return nullptr;
    NSError* err = nil;
    NSString* src = [NSString stringWithUTF8String:rewrittenSource.c_str()];
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
    shader->supportsIndirectCommandBuffers = rewrittenSource.find("[[texture(") == std::string::npos &&
                                             rewrittenSource.find("[[sampler(") == std::string::npos;
    shader->vertexBytecode.assign(rewrittenSource.begin(), rewrittenSource.end());
    shader->pixelBytecode.assign(rewrittenSource.begin(), rewrittenSource.end());
    if (layout && layoutCount)
        shader->vertexLayout.assign(layout, layout + layoutCount);
    ParseMetalBindings(rewrittenSource, ShaderStageVertex | ShaderStagePixel, shader->reflection);
    return shader;
}

std::shared_ptr<GpuShader> MetalContext::CreateShaderFromBytecode(const void* vsBytecode, size_t vsSize,
                                                                  const void* psBytecode, size_t psSize,
                                                                  const VertexElement* layout, uint32_t layoutCount) {
    if (!vsBytecode || vsSize == 0 || !psBytecode || psSize == 0 || (layoutCount > 0 && !layout)) {
        return nullptr;
    }

    NSError* err = nil;
    std::string vsText(static_cast<const char*>(vsBytecode), vsSize);
    std::string psText(static_cast<const char*>(psBytecode), psSize);
    vsText = RewriteMetalConstantBufferLayouts(std::move(vsText));
    psText = RewriteMetalConstantBufferLayouts(std::move(psText));
    vsText = RewriteMetalBindlessArgumentBuffer(std::move(vsText));
    psText = RewriteMetalBindlessArgumentBuffer(std::move(psText));
    if (vsText.empty() || psText.empty() || !RewriteMetalBufferBindings(vsText, &psText) ||
        !RewriteMetalTextureBindings(vsText, &psText))
        return nullptr;
    NSString* vsSource = [[NSString alloc] initWithBytes:vsText.data()
                                                  length:vsText.size()
                                                encoding:NSUTF8StringEncoding];
    NSString* psSource = [[NSString alloc] initWithBytes:psText.data()
                                                  length:psText.size()
                                                encoding:NSUTF8StringEncoding];
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
    shader->supportsIndirectCommandBuffers =
        vsText.find("[[texture(") == std::string::npos && psText.find("[[texture(") == std::string::npos &&
        vsText.find("[[sampler(") == std::string::npos && psText.find("[[sampler(") == std::string::npos;
    shader->vertexBytecode.assign(vsText.begin(), vsText.end());
    shader->pixelBytecode.assign(psText.begin(), psText.end());
    if (layout && layoutCount)
        shader->vertexLayout.assign(layout, layout + layoutCount);
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
    source = RewriteMetalConstantBufferLayouts(std::move(source));
    source = RewriteMetalBindlessArgumentBuffer(std::move(source));
    if (source.empty() || !RewriteMetalBufferBindings(source) || !RewriteMetalTextureBindings(source))
        return nullptr;
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
    std::smatch entryMatch;
    static const std::regex entryRegex(
        R"(\[\[kernel\]\]\s+[A-Za-z_][A-Za-z0-9_:<>, \*&]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()");
    if (!std::regex_search(source, entryMatch, entryRegex)) {
        Logger::Error("[Metal] Cannot discover cooked compute shader entry point");
        return nullptr;
    }
    const std::string entryPoint = entryMatch[1].str();
    id<MTLFunction> fn = [lib newFunctionWithName:[NSString stringWithUTF8String:entryPoint.c_str()]];
    if (!fn) {
        Logger::Error("[Metal] Cannot find cooked compute shader function ", entryPoint);
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
    native.supportIndirectCommandBuffers = shader->supportsIndirectCommandBuffers ? YES : NO;
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
    pipeline->depthClipMode = desc.rasterizer.depthClipEnable ? MTLDepthClipModeClip : MTLDepthClipModeClamp;
    // The cross-backend raster state stores constant bias as an integer for D3D/Vulkan.
    // Metal expects a normalized floating-point adjustment; preserve the engine's authored
    // shadow value (1536) as approximately 0.015, matching Metal shadow-map practice.
    pipeline->depthBias = static_cast<float>(desc.rasterizer.depthBias) * 0.00001f;
    pipeline->slopeScaledDepthBias = desc.rasterizer.slopeScaledDepthBias;
    pipeline->depthBiasClamp = desc.rasterizer.depthBiasClamp;
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
    const uint32_t threadX = desc.shader->threadGroupSize[0];
    const uint32_t threadY = desc.shader->threadGroupSize[1];
    const uint32_t threadZ = desc.shader->threadGroupSize[2];
    const uint64_t totalThreads = static_cast<uint64_t>(threadX) * threadY * threadZ;
    if (threadX == 0 || threadY == 0 || threadZ == 0 || totalThreads > pso.maxTotalThreadsPerThreadgroup) {
        Logger::Error("[Metal] Invalid compute thread-group size ", threadX, "x", threadY, "x", threadZ,
                      " (device maximum ", pso.maxTotalThreadsPerThreadgroup, ")");
        return nullptr;
    }
    pipeline->threadsPerThreadgroup = MTLSizeMake(threadX, threadY, threadZ);
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
    [m_Impl->encoder setDepthClipMode:native->depthClipMode];
    [m_Impl->encoder setDepthBias:native->depthBias
                       slopeScale:native->slopeScaledDepthBias
                            clamp:native->depthBiasClamp];
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
    if (!native || !native->pipelineState) {
        m_Impl->boundComputePipeline = nullptr;
        return;
    }
    if (!m_Impl->computeEncoder) {
        m_Impl->computeEncoder = [m_Impl->cmdBuffer computeCommandEncoder];
    }
    [m_Impl->computeEncoder setComputePipelineState:native->pipelineState];
    m_Impl->boundComputePipeline = native;
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
    if (m_Impl->bindlessSupported && m_Impl->bindless && m_Impl->bindless->argumentBuffer) {
        auto& bindless = *m_Impl->bindless;
        std::lock_guard<std::mutex> lock(bindless.mutex);
        if (m_Impl->encoder) {
            [m_Impl->encoder setVertexBuffer:bindless.argumentBuffer offset:0 atIndex:kMetalBindlessBufferIndex];
            [m_Impl->encoder setFragmentBuffer:bindless.argumentBuffer offset:0 atIndex:kMetalBindlessBufferIndex];
            [m_Impl->encoder useResource:bindless.fallbackTexture
                                   usage:MTLResourceUsageRead
                                  stages:MTLRenderStageVertex | MTLRenderStageFragment];
            if (bindless.nextIndex > 0) {
                [m_Impl->encoder useResources:bindless.textures.data()
                                        count:bindless.nextIndex
                                        usage:MTLResourceUsageRead
                                       stages:MTLRenderStageVertex | MTLRenderStageFragment];
            }
        }
        if (m_Impl->computeEncoder) {
            [m_Impl->computeEncoder setBuffer:bindless.argumentBuffer offset:0 atIndex:kMetalBindlessBufferIndex];
            [m_Impl->computeEncoder useResource:bindless.fallbackTexture usage:MTLResourceUsageRead];
            if (bindless.nextIndex > 0) {
                [m_Impl->computeEncoder useResources:bindless.textures.data()
                                               count:bindless.nextIndex
                                               usage:MTLResourceUsageRead];
            }
        }
    }
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
    for (const auto& value : group->GetStorageTextures()) {
        const auto* binding = reflection.Find(value.first);
        if (!binding || binding->type != ShaderBindingType::StorageTexture) {
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
        if (slot >= kMetalMaterialSamplerBaseIndex &&
            slot < kMetalMaterialSamplerBaseIndex + kMetalMaterialSamplerCount && m_Impl->bindless) {
            auto& bindless = *m_Impl->bindless;
            std::lock_guard<std::mutex> lock(bindless.mutex);
            const uint32_t samplerIndex = slot - kMetalMaterialSamplerBaseIndex;
            if (bindless.materialSamplers[samplerIndex] != sampler->sampler) {
                bindless.materialSamplers[samplerIndex] = sampler->sampler;
                [bindless.encoder setSamplerState:sampler->sampler atIndex:slot];
            }
            continue;
        }
        if (binding->stages == 0 || (binding->stages & ShaderStageVertex))
            [m_Impl->encoder setVertexSamplerState:sampler->sampler atIndex:slot];
        if (binding->stages == 0 || (binding->stages & ShaderStagePixel))
            [m_Impl->encoder setFragmentSamplerState:sampler->sampler atIndex:slot];
        if (m_Impl->computeEncoder && (binding->stages == 0 || (binding->stages & ShaderStageCompute)))
            [m_Impl->computeEncoder setSamplerState:sampler->sampler atIndex:slot];
    }
    const auto bindBufferMap = [&](const auto& buffers, ShaderBindingType expected) {
        for (const auto& value : buffers) {
            const auto* binding = reflection.Find(value.first);
            if (!binding || binding->type != expected) {
                warnMissing(value.first);
                continue;
            }
            auto* buffer = value.second ? dynamic_cast<MetalGpuBuffer*>(value.second->buffer.get()) : nullptr;
            if (!buffer || !buffer->buffer)
                continue;
            const NSUInteger offset = static_cast<NSUInteger>(value.second->desc.firstElement) * buffer->desc.stride;
            if (binding->stages == 0 || (binding->stages & ShaderStageVertex))
                [m_Impl->encoder setVertexBuffer:buffer->buffer offset:offset atIndex:binding->bindPoint];
            if (binding->stages == 0 || (binding->stages & ShaderStagePixel))
                [m_Impl->encoder setFragmentBuffer:buffer->buffer offset:offset atIndex:binding->bindPoint];
            if (m_Impl->computeEncoder && (binding->stages == 0 || (binding->stages & ShaderStageCompute)))
                [m_Impl->computeEncoder setBuffer:buffer->buffer offset:offset atIndex:binding->bindPoint];
        }
    };
    bindBufferMap(group->GetBuffers(), ShaderBindingType::StructuredBuffer);
    bindBufferMap(group->GetStorageBuffers(), ShaderBindingType::StorageBuffer);
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
    if (!m_Impl || !m_Impl->computeEncoder || !m_Impl->boundComputePipeline || x == 0 || y == 0 || z == 0)
        return;
    MTLSize threadgroups = MTLSizeMake(x, y, z);
    [m_Impl->computeEncoder dispatchThreadgroups:threadgroups
                           threadsPerThreadgroup:m_Impl->boundComputePipeline->threadsPerThreadgroup];
}

void MetalContext::DispatchIndirect(GpuBuffer* arguments, uint64_t offset) {
    auto* buffer = dynamic_cast<MetalGpuBuffer*>(arguments);
    if (!m_Impl || !m_Impl->computeEncoder || !m_Impl->boundComputePipeline || !buffer || !buffer->buffer ||
        offset + sizeof(RHIDispatchIndirectArgs) > buffer->byteSize)
        return;
    [m_Impl->computeEncoder dispatchThreadgroupsWithIndirectBuffer:buffer->buffer
                                              indirectBufferOffset:offset
                                             threadsPerThreadgroup:m_Impl->boundComputePipeline->threadsPerThreadgroup];
}

void MetalContext::ClearStorageBuffer(GpuBufferView* bufferView, uint32_t value) {
    auto* view = bufferView;
    auto* buffer = view && view->buffer ? dynamic_cast<MetalGpuBuffer*>(view->buffer.get()) : nullptr;
    if (!m_Impl || !m_Impl->cmdBuffer || !view || !buffer || !buffer->buffer)
        return;
    if (m_Impl->computeEncoder) {
        [m_Impl->computeEncoder endEncoding];
        m_Impl->computeEncoder = nil;
        m_Impl->boundComputePipeline = nullptr;
    }
    if (m_Impl->encoder) {
        [m_Impl->encoder endEncoding];
        m_Impl->encoder = nil;
    }
    const NSUInteger offset = static_cast<NSUInteger>(view->desc.firstElement) * buffer->desc.stride;
    const NSUInteger size = static_cast<NSUInteger>(view->desc.elementCount) * buffer->desc.stride;
    if (size == 0)
        return;
    const uint8_t fillByte = static_cast<uint8_t>(value & 0xffu);
    const uint32_t repeatedByte = static_cast<uint32_t>(fillByte) * 0x01010101u;
    if (value == repeatedByte) {
        id<MTLBlitCommandEncoder> blit = [m_Impl->cmdBuffer blitCommandEncoder];
        [blit fillBuffer:buffer->buffer range:NSMakeRange(offset, size) value:fillByte];
        [blit endEncoding];
        return;
    }
    if (!m_Impl->clearStorageBufferPipeline || (offset & 3u) != 0 || (size & 3u) != 0) {
        Logger::Error("[Metal] ClearStorageBuffer requires a 4-byte-aligned storage view");
        return;
    }
    id<MTLComputeCommandEncoder> clear = [m_Impl->cmdBuffer computeCommandEncoder];
    [clear setComputePipelineState:m_Impl->clearStorageBufferPipeline];
    [clear setBuffer:buffer->buffer offset:offset atIndex:0];
    [clear setBytes:&value length:sizeof(value) atIndex:1];
    const NSUInteger wordCount = size / sizeof(uint32_t);
    [clear dispatchThreads:MTLSizeMake(wordCount, 1, 1)
        threadsPerThreadgroup:MTLSizeMake((std::min)(wordCount, NSUInteger{64}), 1, 1)];
    [clear endEncoding];
}

void MetalContext::UAVBarrier(GpuResource*) {
    if (!m_Impl || !m_Impl->computeEncoder)
        return;
    [m_Impl->computeEncoder memoryBarrierWithScope:MTLBarrierScopeBuffers | MTLBarrierScopeTextures];
}

void MetalContext::BuildIndexedIndirectCommandStream(GpuIndexedIndirectCommandStream* stream, GpuBuffer* arguments,
                                                     uint64_t argumentOffset, GpuBuffer* countBuffer,
                                                     uint64_t countOffset, GpuBuffer* indexBuffer,
                                                     uint32_t maxDrawCount, uint32_t stride) {
    auto* nativeStream = dynamic_cast<MetalIndexedIndirectCommandStream*>(stream);
    auto* nativeArguments = dynamic_cast<MetalGpuBuffer*>(arguments);
    auto* nativeCount = dynamic_cast<MetalGpuBuffer*>(countBuffer);
    auto* nativeIndex = dynamic_cast<MetalGpuBuffer*>(indexBuffer);
    if (!m_Impl || !m_Impl->cmdBuffer || !m_Impl->indirectCommandBuildPipeline || !nativeStream ||
        !nativeStream->commands || !nativeStream->executionRange || !nativeArguments || !nativeArguments->buffer ||
        !nativeCount || !nativeCount->buffer || maxDrawCount == 0 || maxDrawCount > nativeStream->capacity ||
        stride != sizeof(RHIObjectDrawIndexedIndirectArgs) || argumentOffset > nativeArguments->byteSize ||
        static_cast<uint64_t>(maxDrawCount) * stride > nativeArguments->byteSize - argumentOffset ||
        countOffset > nativeCount->byteSize || sizeof(uint32_t) > nativeCount->byteSize - countOffset)
        return;

    stream->arguments = arguments;
    stream->count = countBuffer;
    stream->indexBuffer = indexBuffer;
    stream->argumentOffset = argumentOffset;
    stream->countOffset = countOffset;
    stream->maxDrawCount = maxDrawCount;
    stream->stride = stride;
    if (m_Impl->computeEncoder) {
        [m_Impl->computeEncoder endEncoding];
        m_Impl->computeEncoder = nil;
    }
    if (m_Impl->encoder) {
        [m_Impl->encoder endEncoding];
        m_Impl->encoder = nil;
    }
    m_Impl->boundComputePipeline = nullptr;

    id<MTLBlitCommandEncoder> reset = [m_Impl->cmdBuffer blitCommandEncoder];
    [reset resetCommandsInBuffer:nativeStream->commands withRange:NSMakeRange(0, maxDrawCount)];
    [reset endEncoding];

    id<MTLComputeCommandEncoder> compute = [m_Impl->cmdBuffer computeCommandEncoder];
    [compute setComputePipelineState:m_Impl->indirectCommandBuildPipeline];
    [compute setBuffer:nativeArguments->buffer offset:argumentOffset atIndex:0];
    [compute setBuffer:nativeCount->buffer offset:countOffset atIndex:1];
    // An empty scene legitimately has no geometry arena. Still dispatch thread zero so it writes an empty execution
    // range and prevents commands from the previous frame from being replayed.
    id<MTLBuffer> commandIndexBuffer =
        nativeIndex && nativeIndex->buffer ? nativeIndex->buffer : nativeArguments->buffer;
    [compute setBuffer:commandIndexBuffer offset:0 atIndex:2];
    const uint32_t limits[2] = {nativeIndex && nativeIndex->buffer ? maxDrawCount : 0u, stride};
    [compute setBytes:limits length:sizeof(limits) atIndex:3];
    [compute setBuffer:nativeStream->commandArgumentBuffer offset:0 atIndex:4];
    [compute setBuffer:nativeStream->executionRange offset:0 atIndex:5];
    [compute useResource:commandIndexBuffer usage:MTLResourceUsageRead];
    [compute useResource:nativeStream->commands usage:MTLResourceUsageWrite];
    constexpr NSUInteger kThreads = 64;
    [compute dispatchThreads:MTLSizeMake(maxDrawCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(kThreads, 1, 1)];
    [compute endEncoding];
}

void MetalContext::ExecuteIndexedIndirectCommandStream(GpuIndexedIndirectCommandStream* stream) {
    auto* native = dynamic_cast<MetalIndexedIndirectCommandStream*>(stream);
    auto* indexBuffer = stream ? dynamic_cast<MetalGpuBuffer*>(stream->indexBuffer) : nullptr;
    if (!m_Impl || !m_Impl->encoder || !native || !native->commands || !native->executionRange)
        return;
    if (indexBuffer && indexBuffer->buffer) {
        [m_Impl->encoder useResource:indexBuffer->buffer usage:MTLResourceUsageRead stages:MTLRenderStageVertex];
    }
    [m_Impl->encoder useResource:native->commands
                           usage:MTLResourceUsageRead
                          stages:MTLRenderStageVertex | MTLRenderStageFragment];
    [m_Impl->encoder executeCommandsInBuffer:native->commands
                              indirectBuffer:native->executionRange
                        indirectBufferOffset:0];
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
    const uint32_t bytesPerPixel = desc.format == RHIFormat::RGBA8UNorm                                         ? 4u
                                   : desc.format == RHIFormat::RGBA16Float                                      ? 8u
                                   : (desc.format == RHIFormat::D32Float || desc.format == RHIFormat::R32Float) ? 4u
                                                                                                                : 0u;
    if (!data || subresourceCount == 0 || !m_Impl || !m_Impl->device || bytesPerPixel == 0 ||
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
        const uint32_t rowPitch = src.rowPitch ? src.rowPitch : mipWidth * bytesPerPixel;
        if (rowPitch < mipWidth * bytesPerPixel)
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
    else if (desc.array || desc.arrayLayers > 1)
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
    } else if (nativeTexture->desc.array || sliceCount > 1) {
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
    if (m_Impl->bindlessSupported && HasUsage(desc.usage, RHIResourceUsage::ShaderResource) &&
        viewType == MTLTextureType2D && m_Impl->bindless) {
        auto state = m_Impl->bindless;
        std::lock_guard<std::mutex> lock(state->mutex);
        uint32_t index = UINT32_MAX;
        if (!state->freeIndices.empty()) {
            index = state->freeIndices.back();
            state->freeIndices.pop_back();
        } else if (state->nextIndex < kMetalBindlessTextureCapacity) {
            index = state->nextIndex++;
        }
        if (index != UINT32_MAX) {
            view->bindlessIndex = index;
            state->textures[index] = view->textureView;
            [state->encoder setTexture:view->textureView atIndex:index];
            std::weak_ptr<MetalBindlessState> weakState = state;
            view->retireBindless = [weakState](uint32_t retiredIndex) {
                if (auto locked = weakState.lock()) {
                    std::lock_guard<std::mutex> retireLock(locked->mutex);
                    locked->retired.push_back({locked->frameSerial + kMetalBindlessRetireFrames, retiredIndex});
                }
            };
        } else if (!state->exhaustedLogged) {
            state->exhaustedLogged = true;
            Logger::Error("[Metal] Bindless texture table exhausted (capacity ", kMetalBindlessTextureCapacity, ")");
        }
    }
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
    native.supportArgumentBuffers = YES;
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
