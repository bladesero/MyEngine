#pragma once

#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/GpuTexture.h"

#include <cstdint>

enum class GpuBlendMode : uint8_t {
    Opaque,
    Alpha,
};

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
    virtual void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                               uint32_t startVertex = 0) {
        for (uint32_t i = 0; i < instanceCount; ++i) Draw(vertexCount, startVertex);
    }
    virtual void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                                      uint32_t startIndex = 0,
                                      uint32_t baseVertex = 0) {
        for (uint32_t i = 0; i < instanceCount; ++i) {
            DrawIndexed(indexCount, startIndex, baseVertex);
        }
    }
    virtual void SetViewport(float x, float y, float w, float h) = 0;
    virtual void BindPSTexture(uint32_t slot, GpuTexture* tex) = 0;
    virtual void SetBlendMode(GpuBlendMode mode) { (void)mode; }
    virtual void SetRasterState(bool twoSided, bool wireframe) {
        (void)twoSided;
        (void)wireframe;
    }

    // Optional native handle for backend interop.
    virtual void* GetNativeHandle() const { return nullptr; }
};
