#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <memory>

struct GpuTexture;

struct ImGuiNativeTextureInfo {
    RHIBackend backend = RHIBackend::Unknown;
    void* imageView = nullptr;
    void* sampler = nullptr;
    uint32_t imageLayout = 0;
};

struct GpuTextureView : GpuResource {
    GpuTextureView() { CommitAccounting(GpuResourceAccountingClass::Descriptor, 0, 1); }
    std::shared_ptr<GpuTexture> texture;
    RHITextureViewDesc desc;
    uint32_t bindlessIndex = UINT32_MAX;
    virtual void* GetImGuiTextureId() { return nullptr; }
    virtual ImGuiNativeTextureInfo GetImGuiNativeTextureInfo() const { return {}; }
    virtual ~GpuTextureView() = default;
};
