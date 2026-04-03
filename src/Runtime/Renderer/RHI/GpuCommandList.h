#pragma once

#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/GpuTexture.h"

#include <cstdint>

// Draw command recording/dispatch abstraction.
class GpuCommandList {
public:
    virtual ~GpuCommandList() = default;

    virtual void BindShader(GpuShader* shader) = 0;
    virtual void BindVertexBuffer(GpuBuffer* buffer) = 0;
    virtual void BindIndexBuffer(GpuBuffer* buffer) = 0;
    virtual void SetVSConstants(const void* data, uint32_t byteSize) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t startVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0,
                             uint32_t baseVertex = 0) = 0;
    virtual void SetViewport(float x, float y, float w, float h) = 0;
    virtual void BindPSTexture(uint32_t slot, GpuTexture* tex) = 0;

    // Optional native handle for backend interop.
    virtual void* GetNativeHandle() const { return nullptr; }
};
