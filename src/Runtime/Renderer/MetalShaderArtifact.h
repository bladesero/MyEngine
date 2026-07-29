#pragma once

#include "API/RuntimeApi.h"
#include "Assets/ShaderAsset.h"
#include "Core/Platform.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MetalShaderArtifact {

#if defined(MYENGINE_PLATFORM_MACOS)
#define MYENGINE_METAL_ARTIFACT_API MYENGINE_RUNTIME_API
#else
#define MYENGINE_METAL_ARTIFACT_API
#endif

enum class PayloadKind : uint8_t {
    MSLSource = 1,
    Metallib = 2,
};

struct DecodedPayload {
    PayloadKind kind = PayloadKind::MSLSource;
    std::string entryPoint;
    bool supportsIndirectCommandBuffers = false;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

inline constexpr uint32_t kContainerVersion = 1;
inline constexpr uint32_t kTransformationAbi = 2;

MYENGINE_METAL_ARTIFACT_API bool IsContainer(const void* data, size_t size);
MYENGINE_METAL_ARTIFACT_API bool Encode(PayloadKind kind, const std::string& entryPoint,
                                        bool supportsIndirectCommandBuffers, const std::vector<uint8_t>& payload,
                                        std::vector<uint8_t>& output, std::string* error = nullptr);
MYENGINE_METAL_ARTIFACT_API bool Decode(const void* data, size_t size, DecodedPayload& output,
                                        std::string* error = nullptr);

MYENGINE_METAL_ARTIFACT_API bool TransformComputeSource(std::string& source,
                                                        CookedShaderStageReflection& reflection,
                                                        bool& supportsIndirectCommandBuffers,
                                                        std::string* error = nullptr);
MYENGINE_METAL_ARTIFACT_API bool TransformGraphicsSources(std::string& vertexSource, std::string& fragmentSource,
                                                          CookedShaderStageReflection& vertexReflection,
                                                          CookedShaderStageReflection& fragmentReflection,
                                                          bool& supportsIndirectCommandBuffers,
                                                          std::string* error = nullptr);

// Produces a native Metal library when the optional Xcode Metal Toolchain is available. Otherwise it wraps the
// already-transformed MSL source so the runtime can retain its compatibility path without repeating ABI rewrites.
MYENGINE_METAL_ARTIFACT_API bool BuildCookedPayload(const std::string& transformedSource,
                                                    const std::string& entryPoint,
                                                    bool supportsIndirectCommandBuffers,
                                                    std::vector<uint8_t>& output,
                                                    bool* usedSourceFallback = nullptr,
                                                    std::string* error = nullptr,
                                                    const std::function<bool()>& cancellationRequested = {});

MYENGINE_METAL_ARTIFACT_API bool IsNativeCompilerAvailable();
MYENGINE_METAL_ARTIFACT_API std::string GetToolchainFingerprint();

} // namespace MetalShaderArtifact

#undef MYENGINE_METAL_ARTIFACT_API
