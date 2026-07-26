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
    const bool supportedBackend =
        backend == RHIBackend::D3D12 || backend == RHIBackend::Vulkan || backend == RHIBackend::Metal;
    return supportedBackend && capabilities.computeShaders && capabilities.storageTextures &&
           capabilities.indirectDraw && capabilities.indirectDrawCount && capabilities.indirectDispatch &&
           capabilities.bindlessResources && capabilities.shaderDrawParameters && capabilities.modernDeferredFormats &&
           capabilities.maxBindlessResources >= 4096;
}

inline std::string DescribeModernDeferredCapabilityFailure(RHIBackend backend,
                                                           const RHIDeviceCapabilities& capabilities) {
    if (backend != RHIBackend::D3D12 && backend != RHIBackend::Vulkan && backend != RHIBackend::Metal)
        return "backend has no modern deferred implementation";
    std::string missing;
    const auto append = [&missing](const char* capability) {
        if (!missing.empty())
            missing += ", ";
        missing += capability;
    };
    if (!capabilities.computeShaders)
        append("compute shaders");
    if (!capabilities.storageTextures)
        append("storage textures");
    if (!capabilities.indirectDraw)
        append("indirect draw");
    if (!capabilities.indirectDrawCount)
        append("counted indirect draw");
    if (!capabilities.indirectDispatch)
        append("indirect dispatch");
    if (!capabilities.bindlessResources)
        append("bindless resources");
    if (!capabilities.shaderDrawParameters)
        append("shader draw parameters");
    if (!capabilities.modernDeferredFormats)
        append("required HDR/velocity/UAV formats");
    if (capabilities.maxBindlessResources < 4096)
        append("4096 bindless texture slots");
    return missing.empty() ? std::string{} : "missing modern deferred capabilities: " + missing;
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
                                     ? DescribeModernDeferredCapabilityFailure(backend, capabilities)
                                     : "modern deferred implementation is not initialized";
    return diagnostics;
}
