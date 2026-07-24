#include "Renderer/RenderBackendRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>

namespace {
std::mutex g_BackendMutex;
std::array<RenderBackendFactory, 4> g_BackendFactories{};

size_t BackendIndex(RenderBackend backend) {
    return static_cast<size_t>(backend);
}

std::string Normalize(std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}
} // namespace

bool RegisterRenderBackend(RenderBackend backend, RenderBackendFactory factory) {
    if (!IsRenderBackendKnown(backend) || !factory)
        return false;
    std::lock_guard<std::mutex> lock(g_BackendMutex);
    auto& slot = g_BackendFactories[BackendIndex(backend)];
    if (slot && slot != factory)
        return false;
    slot = factory;
    return true;
}

std::optional<RenderBackend> ParseRenderBackend(std::string_view value) {
    const std::string normalized = Normalize(value);
    if (normalized == "d3d11" || normalized == "directx11" || normalized == "dx11" || normalized == "11") {
        return RenderBackend::D3D11;
    }
    if (normalized == "d3d12" || normalized == "directx12" || normalized == "dx12" || normalized == "12") {
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

bool IsRenderBackendKnown(RenderBackend backend) {
    switch (backend) {
    case RenderBackend::D3D11:
    case RenderBackend::D3D12:
    case RenderBackend::Metal:
    case RenderBackend::Vulkan:
        return true;
    }
    return false;
}

bool IsBackendCompiled(RenderBackend backend) {
    if (!IsRenderBackendKnown(backend))
        return false;
    std::lock_guard<std::mutex> lock(g_BackendMutex);
    return g_BackendFactories[BackendIndex(backend)] != nullptr;
}

std::unique_ptr<IRenderContext> CreateRenderContext(RenderBackend backend) {
    if (!IsRenderBackendKnown(backend))
        return {};
    RenderBackendFactory factory = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_BackendMutex);
        factory = g_BackendFactories[BackendIndex(backend)];
    }
    return factory ? factory() : nullptr;
}

const char* RenderBackendToProjectValue(RenderBackend backend) {
    switch (backend) {
    case RenderBackend::D3D11:
        return "d3d11";
    case RenderBackend::D3D12:
        return "d3d12";
    case RenderBackend::Metal:
        return "metal";
    case RenderBackend::Vulkan:
        return "vulkan";
    }
    return "unknown";
}

const char* RenderBackendToLabel(RenderBackend backend) {
    switch (backend) {
    case RenderBackend::D3D11:
        return "DirectX 11";
    case RenderBackend::D3D12:
        return "DirectX 12";
    case RenderBackend::Metal:
        return "Metal";
    case RenderBackend::Vulkan:
        return "Vulkan";
    }
    return "Unknown";
}

std::string AvailableRenderBackendValues() {
    std::string values;
    for (RenderBackend backend :
         {RenderBackend::D3D11, RenderBackend::D3D12, RenderBackend::Metal, RenderBackend::Vulkan}) {
        if (!IsBackendCompiled(backend))
            continue;
        if (!values.empty())
            values += "/";
        values += RenderBackendToProjectValue(backend);
    }
    return values.empty() ? "none" : values;
}
