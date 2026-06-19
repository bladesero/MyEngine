#pragma once

#include <cstdint>
#include <string>

enum class RHIBackend : uint8_t { Unknown, D3D11, D3D12, Metal };

enum class RHIFormat : uint8_t {
    Unknown,
    RGBA8UNorm,
    RGBA16Float,
    R8UNorm,
    R32Float,
    D24S8,
    D32Float,
};

enum class RHIResourceUsage : uint32_t {
    None           = 0,
    VertexBuffer   = 1u << 0,
    IndexBuffer    = 1u << 1,
    ConstantBuffer = 1u << 2,
    ShaderResource = 1u << 3,
    RenderTarget   = 1u << 4,
    DepthStencil   = 1u << 5,
    UnorderedAccess= 1u << 6,
    CopySource     = 1u << 7,
    CopyDestination= 1u << 8,
    Readback       = 1u << 9,
};

inline RHIResourceUsage operator|(RHIResourceUsage a, RHIResourceUsage b) {
    return static_cast<RHIResourceUsage>(static_cast<uint32_t>(a) |
                                         static_cast<uint32_t>(b));
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
    Present,
};

enum class RHILoadOp : uint8_t { Load, Clear, Discard };
enum class RHIStoreOp : uint8_t { Store, Discard };

enum class RHIFilter : uint8_t { Point, Linear, ComparisonLinear };
enum class RHIAddressMode : uint8_t { Repeat, Clamp, Border };

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
    RHIFormat format = RHIFormat::RGBA8UNorm;
    RHIResourceUsage usage = RHIResourceUsage::ShaderResource;
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

struct RHISamplerDesc {
    RHIFilter filter = RHIFilter::Linear;
    RHIAddressMode addressU = RHIAddressMode::Repeat;
    RHIAddressMode addressV = RHIAddressMode::Repeat;
    RHIAddressMode addressW = RHIAddressMode::Repeat;
};

struct ClearColor { float r = 0, g = 0, b = 0, a = 1; };
