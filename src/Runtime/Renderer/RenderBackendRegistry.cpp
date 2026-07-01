#include "Renderer/RenderBackendRegistry.h"

#include <algorithm>
#include <cctype>

namespace {
std::string Normalize(std::string_view value)
{
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}
}

std::optional<RenderBackend> ParseRenderBackend(std::string_view value)
{
    const std::string normalized = Normalize(value);
    if (normalized == "d3d11" || normalized == "directx11" ||
        normalized == "dx11" || normalized == "11") {
        return RenderBackend::D3D11;
    }
    if (normalized == "d3d12" || normalized == "directx12" ||
        normalized == "dx12" || normalized == "12") {
        return RenderBackend::D3D12;
    }
    if (normalized == "metal" || normalized == "mtl") {
        return RenderBackend::Metal;
    }
    if (normalized == "vulkan" || normalized == "vk") {
        return RenderBackend::Vulkan;
    }
    return std::nullopt;
}

bool IsRenderBackendKnown(RenderBackend backend)
{
    switch (backend) {
    case RenderBackend::D3D11:
    case RenderBackend::D3D12:
    case RenderBackend::Metal:
    case RenderBackend::Vulkan:
        return true;
    }
    return false;
}

bool IsBackendCompiled(RenderBackend backend)
{
    switch (backend) {
    case RenderBackend::D3D11:
    case RenderBackend::D3D12:
#if defined(MYENGINE_PLATFORM_WINDOWS)
        return true;
#else
        return false;
#endif
    case RenderBackend::Metal:
#if defined(MYENGINE_PLATFORM_MACOS)
        return true;
#else
        return false;
#endif
    case RenderBackend::Vulkan:
#if defined(MYENGINE_ENABLE_VULKAN)
        return true;
#else
        return false;
#endif
    }
    return false;
}

std::unique_ptr<IRenderContext> CreateRenderContext(RenderBackend backend)
{
    switch (backend) {
    case RenderBackend::D3D11:
#if defined(MYENGINE_PLATFORM_WINDOWS)
        return CreateD3D11Context();
#else
        break;
#endif
    case RenderBackend::D3D12:
#if defined(MYENGINE_PLATFORM_WINDOWS)
        return CreateD3D12Context();
#else
        break;
#endif
    case RenderBackend::Metal:
#if defined(MYENGINE_PLATFORM_MACOS)
        return CreateMetalContext();
#else
        break;
#endif
    case RenderBackend::Vulkan:
#if defined(MYENGINE_ENABLE_VULKAN)
        return CreateVulkanContext();
#else
        break;
#endif
    }
    return {};
}

const char* RenderBackendToProjectValue(RenderBackend backend)
{
    switch (backend) {
    case RenderBackend::D3D11: return "d3d11";
    case RenderBackend::D3D12: return "d3d12";
    case RenderBackend::Metal: return "metal";
    case RenderBackend::Vulkan: return "vulkan";
    }
    return "unknown";
}

const char* RenderBackendToLabel(RenderBackend backend)
{
    switch (backend) {
    case RenderBackend::D3D11: return "DirectX 11";
    case RenderBackend::D3D12: return "DirectX 12";
    case RenderBackend::Metal: return "Metal";
    case RenderBackend::Vulkan: return "Vulkan";
    }
    return "Unknown";
}

std::string AvailableRenderBackendValues()
{
    std::string values;
    for (RenderBackend backend : {RenderBackend::D3D11, RenderBackend::D3D12,
                                  RenderBackend::Metal, RenderBackend::Vulkan}) {
        if (!IsBackendCompiled(backend)) continue;
        if (!values.empty()) values += "/";
        values += RenderBackendToProjectValue(backend);
    }
    return values.empty() ? "none" : values;
}
