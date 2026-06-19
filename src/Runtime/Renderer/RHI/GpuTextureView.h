#pragma once

#include "Renderer/RHI/GpuResource.h"
#include "Renderer/RHI/RHITypes.h"

#include <memory>

struct GpuTexture;

struct GpuTextureView : GpuResource {
    std::shared_ptr<GpuTexture> texture;
    RHITextureViewDesc desc;
    virtual void* GetImGuiTextureId() { return nullptr; }
    virtual ~GpuTextureView() = default;
};
