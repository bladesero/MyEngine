#pragma once

#include "Core/Platform.h"

#ifdef MYENGINE_PLATFORM_WINDOWS

#include "Renderer/IRenderContext.h"
#include "Renderer/RHI/IEditorImGuiRHIInterop.h"

#include <cstddef>
#include <memory>
#include <string>

class VulkanContext final : public IRenderContext, public IEditorImGuiRHIInterop {
public:
    VulkanContext();
    ~VulkanContext() override;

    bool Init(IWindow* window) override;
    void Shutdown() override;

    void BeginFrame(float r, float g, float b, float a = 1.0f) override;
    void EndFrame() override;
    bool IsDeviceLost() const override { return m_DeviceLost; }
    const std::string& GetLastDeviceError() const override { return m_LastDeviceError; }
    RHIDeviceLossInfo GetDeviceLossInfo() const override {
        return m_DeviceLost ? RHIDeviceLossInfo{RHIDeviceLossReason::Unknown, -4, m_DeviceGeneration, m_LastDeviceError}
                            : RHIDeviceLossInfo{};
    }
    uint32_t GetFrameIndex() const override { return m_FrameIndex; }
    GpuCommandList* GetGraphicsCommandList() override;
    GpuQueue* GetGraphicsQueue() override { return m_GraphicsQueue.get(); }
    GpuSwapChain* GetSwapChain() override;
    GpuTextureView* GetCurrentBackBufferView() override;
    RHIBackend GetBackend() const override { return RHIBackend::Vulkan; }
    IEditorImGuiRHIInterop* QueryEditorImGuiInterop() override { return this; }
    ImGuiBackendHandles GetImGuiBackendHandles() override;
    void SetImGuiTextureInteropReady(bool ready) override;
    void SetSwapChainResizeCallback(SwapChainResizeCallback cb) override { m_ResizeCallback = cb; }
    bool RecreateSwapchain(uint32_t requestedWidth = 0, uint32_t requestedHeight = 0);

    std::shared_ptr<GpuBuffer> CreateVertexBuffer(const void* data, uint32_t byteSize, uint32_t strideBytes) override;
    std::shared_ptr<GpuBuffer> CreateIndexBuffer(const void* data, uint32_t byteSize) override;
    std::shared_ptr<GpuBuffer> CreateBuffer(const RHIBufferDesc& desc, const void* initialData = nullptr) override;
    std::shared_ptr<GpuBufferView> CreateBufferView(const std::shared_ptr<GpuBuffer>& buffer,
                                                    const RHIBufferViewDesc& desc) override;
    bool UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer, uint64_t offset, const void* data,
                      uint64_t size) override;

    std::shared_ptr<GpuShader> CreateShader(const std::string& source, const std::string& vsEntry,
                                            const std::string& psEntry, const VertexElement* layout,
                                            uint32_t layoutCount) override;
    std::shared_ptr<GpuShader> CreateShaderFromBytecode(const void* vsBytecode, size_t vsSize, const void* psBytecode,
                                                        size_t psSize, const VertexElement* layout,
                                                        uint32_t layoutCount) override;
    std::shared_ptr<GpuShader> CreateComputeShaderFromBytecode(const void* bytecode, size_t byteSize) override;

    std::shared_ptr<GpuTexture> UploadTexture2D(const void* rgba8Data, int width, int height) override;
    std::shared_ptr<GpuTexture> UploadTexture(const RHITextureDesc& desc, const RHITextureSubresourceData* data,
                                              uint32_t subresourceCount) override;
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override;
    std::shared_ptr<GpuTextureView> CreateTextureView(const std::shared_ptr<GpuTexture>& texture,
                                                      const RHITextureViewDesc& desc) override;
    std::shared_ptr<GpuSampler> CreateSampler(const RHISamplerDesc& desc) override;
    std::shared_ptr<GpuGraphicsPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    std::shared_ptr<GpuComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) override;
    std::shared_ptr<GpuBindGroup> CreateBindGroup(const std::shared_ptr<GpuShader>& shader) override;
    RHIDeviceCapabilities GetCapabilities() const override;
    bool IsFormatSupported(RHIFormat format, RHIResourceUsage usage) const override;
    std::shared_ptr<GpuTimestampQueryPool> CreateTimestampQueryPool(uint32_t count) override;
    std::shared_ptr<GpuReadbackTicket> ReadbackBufferAsync(const std::shared_ptr<GpuBuffer>& buffer) override;
    std::shared_ptr<GpuTextureReadbackTicket> ReadbackTextureAsync(const std::shared_ptr<GpuTexture>& texture,
                                                                   const RHITextureRegion& region) override;

private:
    friend class VulkanImmediateCommandList;
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
    std::unique_ptr<GpuCommandList> m_GraphicsCommandList;
    std::unique_ptr<GpuSwapChain> m_SwapChainInterface;
    std::shared_ptr<GpuQueue> m_GraphicsQueue;
    SwapChainResizeCallback m_ResizeCallback = nullptr;
    bool m_DeviceLost = false;
    uint32_t m_FrameIndex = 0;
    std::string m_LastDeviceError;
    uint64_t m_DeviceGeneration = 0;
};

#endif
