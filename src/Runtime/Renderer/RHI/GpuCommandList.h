#pragma once

#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/GpuTexture.h"
#include "Renderer/RHI/GpuPipeline.h"
#include "Renderer/RHI/GpuBindGroup.h"
#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/RHI/GpuSync.h"

#include <cstdint>

enum class GpuBlendMode : uint8_t {
    Opaque,
    Alpha,
};

struct RenderingAttachment {
    GpuTextureView* view = nullptr;
    RHILoadOp loadOp = RHILoadOp::Load;
    RHIStoreOp storeOp = RHIStoreOp::Store;
    ClearColor clearColor{};
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;
};

struct RenderingInfo {
    const RenderingAttachment* colors = nullptr;
    uint32_t colorCount = 0;
    const RenderingAttachment* depth = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
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

    // Backend-independent command surface used by RenderGraph passes.  Default
    // implementations keep non-rendering test contexts source compatible while
    // concrete GPU backends opt into each capability.
    virtual void Transition(GpuResource*, RHIResourceState, RHIResourceState) {}
    virtual void TransitionTexture(GpuTexture* texture, const RHITextureViewDesc&,
                                   RHIResourceState before, RHIResourceState after) {
        Transition(texture, before, after);
    }
    virtual void BeginRendering(const RenderingInfo&) {}
    virtual void EndRendering() {}
    virtual void SetGraphicsPipeline(GpuGraphicsPipeline*) {}
    virtual void SetDepthOnlyShader(GpuShader* shader) { BindShader(shader); }
    virtual void SetComputePipeline(GpuComputePipeline*) {}
    virtual void SetBindGroup(uint32_t, GpuBindGroup*) {}
    virtual void SetVertexBuffer(GpuBuffer* buffer) { BindVertexBuffer(buffer); }
    virtual void SetIndexBuffer(GpuBuffer* buffer) { BindIndexBuffer(buffer); }
    virtual void SetScissor(int32_t, int32_t, uint32_t, uint32_t) {}
    virtual void Dispatch(uint32_t, uint32_t = 1, uint32_t = 1) {}
    virtual void CopyBuffer(GpuBuffer*, uint32_t, GpuBuffer*, uint32_t, uint32_t) {}
    virtual void CopyTexture(GpuTexture*, GpuTexture*) {}
    virtual void CopyTexture(GpuTexture*, const RHITextureRegion&,
                             GpuTexture*, const RHITextureRegion&) {}
    virtual void DrawIndirect(GpuBuffer*, uint64_t = 0) {}
    virtual void DrawIndexedIndirect(GpuBuffer*, uint64_t = 0) {}
    virtual void WriteTimestamp(GpuTimestampQueryPool*, uint32_t) {}
    virtual void ResolveTimestamps(GpuTimestampQueryPool*, uint32_t, uint32_t) {}
    virtual void UAVBarrier(GpuResource*) {}

};
