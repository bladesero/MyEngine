#pragma once

#include "Renderer/RHI/GpuBuffer.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class RHIAccelerationStructureType : uint8_t { BottomLevel, TopLevel };

enum class RHIAccelerationStructureBuildFlags : uint8_t {
    None = 0,
    AllowUpdate = 1u << 0,
    PreferFastTrace = 1u << 1,
};

inline RHIAccelerationStructureBuildFlags operator|(RHIAccelerationStructureBuildFlags a,
                                                     RHIAccelerationStructureBuildFlags b) {
    return static_cast<RHIAccelerationStructureBuildFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool HasAccelerationStructureBuildFlag(RHIAccelerationStructureBuildFlags value,
                                              RHIAccelerationStructureBuildFlags flag) {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

struct RHIRayTracingGeometryDesc {
    std::shared_ptr<GpuBuffer> vertexBuffer;
    std::shared_ptr<GpuBuffer> indexBuffer;
    uint64_t vertexOffset = 0;
    uint64_t indexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t vertexStride = 0;
    uint32_t indexCount = 0;
    RHIFormat vertexFormat = RHIFormat::RGB32Float;
    RHIFormat indexFormat = RHIFormat::R32UInt;
    bool opaque = false;
};

struct RHIAccelerationStructureDesc {
    RHIAccelerationStructureType type = RHIAccelerationStructureType::BottomLevel;
    std::vector<RHIRayTracingGeometryDesc> geometries;
    uint32_t maxInstances = 0;
    RHIAccelerationStructureBuildFlags flags = RHIAccelerationStructureBuildFlags::PreferFastTrace;
    std::string debugName;
};

struct RHIAccelerationStructureBuildSizes {
    uint64_t resultBytes = 0;
    uint64_t scratchBytes = 0;
    uint64_t updateScratchBytes = 0;
};

struct GpuAccelerationStructure;

struct RHIRayTracingInstanceDesc {
    // Row-major 3x4 object-to-world transform in the API-neutral DXR convention.
    std::array<float, 12> transform = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    uint32_t instanceId = 0;
    uint8_t instanceMask = 0xff;
    bool forceOpaque = false;
    bool forceNonOpaque = false;
    bool disableTriangleCulling = false;
    std::shared_ptr<GpuAccelerationStructure> bottomLevel;
};

struct GpuAccelerationStructure : GpuResource {
    RHIAccelerationStructureDesc desc;
    RHIAccelerationStructureBuildSizes buildSizes;
    virtual ~GpuAccelerationStructure() = default;
};
