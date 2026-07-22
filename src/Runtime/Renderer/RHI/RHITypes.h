#pragma once

#include <cstdint>
#include <string>

enum class RHIBackend : uint8_t { Unknown, D3D11, D3D12, Metal, Vulkan };

enum class RHIFormat : uint8_t {
    Unknown,
    R8UInt,
    RGBA8UNorm,
    BGRA8UNorm,
    RGBA8UNormSrgb,
    RG16Float,
    RGBA16Float,
    R8UNorm,
    R16UInt,
    R32UInt,
    R32Float,
    RG32Float,
    RGB32Float,
    RGBA32Float,
    BC1UNorm,
    BC3UNorm,
    D24S8,
    D32Float,
};

enum class RHIResourceUsage : uint32_t {
    None = 0,
    VertexBuffer = 1u << 0,
    IndexBuffer = 1u << 1,
    ConstantBuffer = 1u << 2,
    ShaderResource = 1u << 3,
    RenderTarget = 1u << 4,
    DepthStencil = 1u << 5,
    UnorderedAccess = 1u << 6,
    CopySource = 1u << 7,
    CopyDestination = 1u << 8,
    Readback = 1u << 9,
    IndirectArguments = 1u << 10,
    AccelerationStructureBuildInput = 1u << 11,
    AccelerationStructureStorage = 1u << 12,
};

inline RHIResourceUsage operator|(RHIResourceUsage a, RHIResourceUsage b) {
    return static_cast<RHIResourceUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool HasUsage(RHIResourceUsage value, RHIResourceUsage flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

enum class RHIResourceState : uint8_t {
    Undefined,
    ShaderResource,
    RenderTarget,
    DepthRead,
    DepthWrite,
    UnorderedAccess,
    CopySource,
    CopyDestination,
    IndirectArgument,
    AccelerationStructure,
    Present,
};

enum class RHILoadOp : uint8_t { Load, Clear, Discard };
enum class RHIStoreOp : uint8_t { Store, Discard };

enum class RHIFilter : uint8_t { Point, Linear, ComparisonLinear };
enum class RHIAddressMode : uint8_t { Repeat, Clamp, Border };

enum class RHIPrimitiveTopology : uint8_t {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
};

enum class RHICompareOp : uint8_t {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class RHIStencilOp : uint8_t {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
};

enum class RHIBlendFactor : uint8_t {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    SrcAlphaSaturate,
};

enum class RHIBlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
enum class RHICullMode : uint8_t { None, Front, Back };
enum class RHIFrontFace : uint8_t { Clockwise, CounterClockwise };
enum class RHIFillMode : uint8_t { Solid, Wireframe };

enum RHIColorWriteMask : uint8_t {
    RHIColorWriteNone = 0,
    RHIColorWriteRed = 1u << 0,
    RHIColorWriteGreen = 1u << 1,
    RHIColorWriteBlue = 1u << 2,
    RHIColorWriteAlpha = 1u << 3,
    RHIColorWriteAll = RHIColorWriteRed | RHIColorWriteGreen | RHIColorWriteBlue | RHIColorWriteAlpha,
};

struct RHIBufferDesc {
    uint32_t size = 0;
    uint32_t stride = 0;
    RHIResourceUsage usage = RHIResourceUsage::None;
    std::string debugName;
};

struct RHIBufferViewDesc {
    uint32_t firstElement = 0;
    uint32_t elementCount = 0;
    RHIResourceUsage usage = RHIResourceUsage::ShaderResource;
};

struct RHITextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    uint32_t sampleCount = 1;
    uint32_t sampleQuality = 0;
    RHIFormat format = RHIFormat::RGBA8UNorm;
    RHIResourceUsage usage = RHIResourceUsage::ShaderResource;
    bool array = false;
    bool cube = false;
    std::string debugName;
};

struct RHITextureViewDesc {
    uint32_t firstMip = 0;
    uint32_t mipCount = 1;
    uint32_t firstLayer = 0;
    uint32_t layerCount = 1;
    RHIResourceUsage usage = RHIResourceUsage::ShaderResource;
};

struct RHIBufferRegion {
    uint64_t offset = 0;
    uint64_t size = 0;
};

struct RHITextureRegion {
    uint32_t x = 0, y = 0, z = 0;
    uint32_t width = 0, height = 0, depth = 1;
    uint32_t mipLevel = 0, arrayLayer = 0;
};

struct RHITextureSubresourceData {
    const void* data = nullptr;
    uint32_t rowPitch = 0;
    uint32_t slicePitch = 0;
    uint32_t mipLevel = 0;
    uint32_t arrayLayer = 0;
};

enum class RHIRayTracingTier : uint8_t { None, Tier10, Tier11 };

struct RHIDeviceCapabilities {
    uint32_t maxTextureDimension2D = 0;
    uint32_t maxTextureArrayLayers = 0;
    uint32_t maxColorAttachments = 1;
    uint32_t maxSamples = 1;
    uint32_t maxBindlessResources = 0;
    bool computeShaders = false;
    bool storageTextures = false;
    bool timestampQueries = false;
    bool indirectDraw = false;
    bool indirectDrawCount = false;
    bool indirectDispatch = false;
    bool bindlessResources = false;
    bool shaderDrawParameters = false;
    bool modernDeferredFormats = false;
    bool accelerationStructures = false;
    bool inlineRayQueries = false;
    RHIRayTracingTier rayTracingTier = RHIRayTracingTier::None;
};

struct RHIDrawIndirectArgs {
    uint32_t vertexCount = 0, instanceCount = 0, startVertex = 0, startInstance = 0;
};

struct RHIDrawIndexedIndirectArgs {
    uint32_t indexCount = 0, instanceCount = 0, startIndex = 0;
    int32_t baseVertex = 0;
    uint32_t startInstance = 0;
};

// Modern GPU-driven draws need an explicit object index on D3D12 because SV_InstanceID starts at zero for every
// draw there. Vulkan consumes the native draw fields at byte offset 4 and exposes firstInstance through Slang's raw
// SV_VulkanInstanceID semantic.
struct RHIObjectDrawIndexedIndirectArgs {
    uint32_t objectIndex = 0;
    uint32_t indexCount = 0, instanceCount = 0, startIndex = 0;
    int32_t baseVertex = 0;
    uint32_t startInstance = 0;
};

static_assert(sizeof(RHIDrawIndexedIndirectArgs) == 20, "native indexed indirect ABI changed");
static_assert(sizeof(RHIObjectDrawIndexedIndirectArgs) == 24, "object indexed indirect ABI changed");
static_assert(offsetof(RHIObjectDrawIndexedIndirectArgs, indexCount) == sizeof(uint32_t),
              "Vulkan draw arguments must immediately follow objectIndex");

struct RHIDispatchIndirectArgs {
    uint32_t groupCountX = 0, groupCountY = 0, groupCountZ = 0;
};

struct RHISamplerDesc {
    RHIFilter filter = RHIFilter::Linear;
    RHIAddressMode addressU = RHIAddressMode::Repeat;
    RHIAddressMode addressV = RHIAddressMode::Repeat;
    RHIAddressMode addressW = RHIAddressMode::Repeat;
};

struct ClearColor {
    float r = 0, g = 0, b = 0, a = 1;
};
