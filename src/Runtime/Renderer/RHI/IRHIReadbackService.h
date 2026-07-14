#pragma once

#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuReadback.h"
#include "Renderer/RHI/GpuTexture.h"

#include <memory>

class IRHIReadbackService {
public:
    virtual ~IRHIReadbackService() = default;

    virtual std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(const std::shared_ptr<GpuBuffer>&) {
        return nullptr;
    }
    virtual std::shared_ptr<GpuTextureReadbackTicket> ReadbackTextureAsync(const std::shared_ptr<GpuTexture>&,
                                                                           const RHITextureRegion&) {
        return nullptr;
    }
};
