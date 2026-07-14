#pragma once

#include "Renderer/RHI/GpuBindGroup.h"
#include "Renderer/RHI/GpuBuffer.h"
#include "Renderer/RHI/GpuBufferView.h"
#include "Renderer/RHI/GpuSampler.h"
#include "Renderer/RHI/GpuPipeline.h"
#include "Renderer/RHI/GpuShader.h"
#include "Renderer/RHI/GpuTexture.h"
#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/RHI/GpuSync.h"
#include "Renderer/RHI/VertexLayout.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;
    virtual RHIBackend GetBackend() const = 0;
    virtual std::shared_ptr<GpuBuffer> CreateVertexBuffer(const void* data, uint32_t byteSize,
                                                          uint32_t strideBytes) = 0;
    virtual std::shared_ptr<GpuBuffer> CreateIndexBuffer(const void* data, uint32_t byteSize) = 0;
    virtual std::shared_ptr<GpuBuffer> CreateBuffer(const RHIBufferDesc&, const void* initialData = nullptr) {
        return nullptr;
    }
    virtual std::shared_ptr<GpuBufferView> CreateBufferView(const std::shared_ptr<GpuBuffer>&,
                                                            const RHIBufferViewDesc&) {
        return nullptr;
    }
    virtual std::shared_ptr<GpuShader> CreateShader(const std::string& source, const std::string& vsEntry,
                                                    const std::string& psEntry, const VertexElement* layout,
                                                    uint32_t layoutCount) = 0;
    virtual std::shared_ptr<GpuShader> CreateShaderFromBytecode(const void*, size_t, const void*, size_t,
                                                                const VertexElement*, uint32_t) {
        return nullptr;
    }
    virtual std::shared_ptr<GpuShader> CreateComputeShaderFromBytecode(const void*, size_t) { return nullptr; }
    virtual std::shared_ptr<GpuTexture> UploadTexture2D(const void* rgba8Data, int width, int height) = 0;
    virtual bool UpdateBuffer(const std::shared_ptr<GpuBuffer>&, uint64_t, const void*, uint64_t) { return false; }
    virtual std::shared_ptr<GpuTexture> UploadTexture(const RHITextureDesc&, const RHITextureSubresourceData*,
                                                      uint32_t) {
        return nullptr;
    }
    virtual RHIDeviceCapabilities GetCapabilities() const { return {}; }
    virtual bool IsFormatSupported(RHIFormat, RHIResourceUsage) const { return false; }
    virtual std::shared_ptr<GpuFence> CreateFence(uint64_t = 0) { return nullptr; }
    virtual std::shared_ptr<GpuTimestampQueryPool> CreateTimestampQueryPool(uint32_t) { return nullptr; }
    virtual uint32_t GetBindlessIndex(const std::shared_ptr<GpuTextureView>& view) const {
        return view ? view->bindlessIndex : UINT32_MAX;
    }
    virtual std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc&) { return nullptr; }
    virtual std::shared_ptr<GpuTextureView> CreateTextureView(const std::shared_ptr<GpuTexture>&,
                                                              const RHITextureViewDesc&) {
        return nullptr;
    }
    virtual std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc&) { return nullptr; }
    virtual std::shared_ptr<GpuBindGroup> CreateBindGroup(const std::shared_ptr<GpuShader>& shader) {
        return shader ? std::make_shared<GpuBindGroup>(shader) : nullptr;
    }
    virtual std::shared_ptr<GpuGraphicsPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
        if (!desc.shader)
            return nullptr;
        auto pipeline = std::make_shared<GpuGraphicsPipeline>();
        pipeline->desc = desc;
        return pipeline;
    }
    virtual std::shared_ptr<GpuComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) {
        if (!desc.shader)
            return nullptr;
        auto pipeline = std::make_shared<GpuComputePipeline>();
        pipeline->desc = desc;
        return pipeline;
    }
};
