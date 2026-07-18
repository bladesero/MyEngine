#pragma once

#include "Project/GraphicsDeviceProfile.h"
#include "Renderer/RHI/RHITypes.h"

#include <string>

enum class RenderPath {
    Forward,
    Deferred,
};

enum class ResolvedRenderPipeline {
    Forward,
    ClassicDeferred,
    ModernDeferred,
};

struct RenderPipelineDiagnostics {
    RenderPath requestedPath = RenderPath::Forward;
    GraphicsDeviceProfile requestedDeviceProfile = GraphicsDeviceProfile::Desktop;
    ResolvedRenderPipeline resolvedPipeline = ResolvedRenderPipeline::Forward;
    bool modernSupported = false;
    bool usedFallback = false;
    std::string fallbackReason;
};

inline constexpr const char* RenderPathName(RenderPath path) {
    return path == RenderPath::Deferred ? "deferred" : "forward";
}

inline constexpr const char* ResolvedRenderPipelineName(ResolvedRenderPipeline pipeline) {
    switch (pipeline) {
    case ResolvedRenderPipeline::ClassicDeferred:
        return "classic_deferred";
    case ResolvedRenderPipeline::ModernDeferred:
        return "modern_deferred";
    default:
        return "forward";
    }
}

inline bool HasModernDeferredCapabilities(RHIBackend backend, const RHIDeviceCapabilities& capabilities) {
    const bool supportedBackend = backend == RHIBackend::D3D12 || backend == RHIBackend::Vulkan;
    return supportedBackend && capabilities.computeShaders && capabilities.storageTextures &&
           capabilities.indirectDraw && capabilities.indirectDrawCount && capabilities.indirectDispatch &&
           capabilities.bindlessResources && capabilities.shaderDrawParameters && capabilities.modernDeferredFormats &&
           capabilities.maxBindlessResources >= 4096;
}

inline RenderPipelineDiagnostics ResolveRenderPipeline(RenderPath path, GraphicsDeviceProfile profile,
                                                       RHIBackend backend, const RHIDeviceCapabilities& capabilities,
                                                       bool modernImplementationReady) {
    RenderPipelineDiagnostics diagnostics;
    diagnostics.requestedPath = path;
    diagnostics.requestedDeviceProfile = profile;
    if (path == RenderPath::Forward) {
        diagnostics.resolvedPipeline = ResolvedRenderPipeline::Forward;
        return diagnostics;
    }
    if (profile == GraphicsDeviceProfile::Mobile) {
        diagnostics.resolvedPipeline = ResolvedRenderPipeline::ClassicDeferred;
        diagnostics.fallbackReason = "mobile device profile selects classic deferred";
        return diagnostics;
    }

    diagnostics.modernSupported = modernImplementationReady && HasModernDeferredCapabilities(backend, capabilities);
    if (diagnostics.modernSupported) {
        diagnostics.resolvedPipeline = ResolvedRenderPipeline::ModernDeferred;
        return diagnostics;
    }

    diagnostics.resolvedPipeline = ResolvedRenderPipeline::ClassicDeferred;
    diagnostics.usedFallback = profile == GraphicsDeviceProfile::Console;
    diagnostics.fallbackReason = modernImplementationReady
                                     ? "backend capabilities do not satisfy the modern deferred contract"
                                     : "modern deferred implementation is not initialized";
    return diagnostics;
}
