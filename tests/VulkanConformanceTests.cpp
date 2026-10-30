#include "TestHarness.h"
#include "Assets/MeshAsset.h"
#include "Renderer/IRenderContext.h"
#include "Renderer/RHIConformance.h"
#include "Renderer/ShaderCompilerSlang.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace {

std::string ReadRuntimeSource(const char* relativePath) {
    for (const auto& start : {std::filesystem::current_path(), gExecutableDirectory}) {
        auto directory = start;
        for (uint32_t level = 0; level < 8 && !directory.empty(); ++level, directory = directory.parent_path()) {
            std::ifstream input(directory / relativePath);
            if (input)
                return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }
    }
    return {};
}

// This CPU mock verifies shader routing and real Slang output. It deliberately refuses a pipeline so the test
// cannot claim to verify GPU rendering, presentation, synchronization, or readback.
class VulkanConformanceShaderProbe final : public IRHIContext {
public:
    bool Init(IWindow*) override { return true; }
    void Shutdown() override {}
    RHIBackend GetBackend() const override { return RHIBackend::Vulkan; }
    void BeginFrame(float, float, float, float) override {}
    void EndFrame() override {}
    GpuCommandList* GetGraphicsCommandList() override { return nullptr; }
    RHIDeviceCapabilities GetCapabilities() const override {
        RHIDeviceCapabilities capabilities;
        capabilities.maxTextureDimension2D = 1024;
        capabilities.maxColorAttachments = 4;
        return capabilities;
    }
    std::shared_ptr<GpuBuffer> CreateBuffer(const RHIBufferDesc& desc, const void*) override {
        auto buffer = std::make_shared<GpuBuffer>();
        buffer->desc = desc;
        return buffer;
    }
    std::shared_ptr<GpuBuffer> CreateVertexBuffer(const void*, uint32_t size, uint32_t stride) override {
        RHIBufferDesc desc;
        desc.size = size;
        desc.stride = stride;
        return CreateBuffer(desc, nullptr);
    }
    std::shared_ptr<GpuBuffer> CreateIndexBuffer(const void*, uint32_t size) override {
        return CreateVertexBuffer(nullptr, size, sizeof(uint32_t));
    }
    std::shared_ptr<GpuBufferView> CreateBufferView(const std::shared_ptr<GpuBuffer>& buffer,
                                                    const RHIBufferViewDesc& desc) override {
        auto view = std::make_shared<GpuBufferView>();
        view->buffer = buffer;
        view->desc = desc;
        return view;
    }
    bool UpdateBuffer(const std::shared_ptr<GpuBuffer>&, uint64_t, const void*, uint64_t) override { return true; }
    std::shared_ptr<GpuTexture> CreateTexture(const RHITextureDesc& desc) override {
        auto texture = std::make_shared<GpuTexture>();
        texture->desc = desc;
        return texture;
    }
    std::shared_ptr<GpuTexture> UploadTexture2D(const void*, int width, int height) override {
        RHITextureDesc desc;
        desc.width = width;
        desc.height = height;
        return CreateTexture(desc);
    }
    std::shared_ptr<GpuTexture> UploadTexture(const RHITextureDesc& desc, const RHITextureSubresourceData*,
                                              uint32_t) override {
        return CreateTexture(desc);
    }
    std::shared_ptr<GpuTextureView> CreateTextureView(const std::shared_ptr<GpuTexture>& texture,
                                                      const RHITextureViewDesc& desc) override {
        auto view = std::make_shared<GpuTextureView>();
        view->texture = texture;
        view->desc = desc;
        return view;
    }
    std::shared_ptr<GpuShader> CreateShader(const std::string&, const std::string&, const std::string&,
                                            const VertexElement*, uint32_t) override {
        ++sourceCalls;
        return nullptr;
    }
    std::shared_ptr<GpuShader> CreateShaderFromBytecode(const void* vertex, size_t vertexSize, const void* pixel,
                                                        size_t pixelSize, const VertexElement*,
                                                        uint32_t count) override {
        ++bytecodeCalls;
        const auto isSpirv = [](const void* bytes, size_t size) {
            uint32_t magic = 0;
            if (!bytes || size < 20 || size % sizeof(uint32_t) != 0)
                return false;
            std::memcpy(&magic, bytes, sizeof(magic));
            return magic == 0x07230203u;
        };
        validSpirv = isSpirv(vertex, vertexSize) && isSpirv(pixel, pixelSize) && count == 1;
        return validSpirv ? std::make_shared<GpuShader>() : nullptr;
    }
    std::shared_ptr<GpuGraphicsPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        validInputReflection = desc.shader && desc.shader->reflection.inputs.size() == 1 &&
                               desc.shader->reflection.inputs[0].semantic == "POSITION" &&
                               desc.shader->reflection.inputs[0].semanticIndex == 0 &&
                               desc.shader->reflection.inputs[0].location == 0;
        return nullptr;
    }
    uint32_t sourceCalls = 0;
    uint32_t bytecodeCalls = 0;
    bool validSpirv = false;
    bool validInputReflection = false;
};

bool TestVulkanConformanceUsesSlangBytecode() {
    if (!Check(ShaderCompilerSlang::IsAvailable(), "Slang is required to test the Vulkan conformance shader path"))
        return false;
    VulkanConformanceShaderProbe context;
    const auto report = RunRHIConformance(context);
    return Check(context.sourceCalls == 0 && context.bytecodeCalls == 1 && context.validSpirv &&
                     context.validInputReflection,
                 "Vulkan conformance must pass real SPIR-V to CreateShaderFromBytecode: " + report.failure) &&
           Check(!report.passed && report.failure.find("pipeline-create:") == 0,
                 "CPU shader-routing test must stop at the deliberately rejected mock pipeline");
}

bool TestVulkanStaticShadowVertexStride() {
    const size_t fullStride = offsetof(MeshVertex, boneWeights) + sizeof(MeshVertex::boneWeights);
    if (!Check(fullStride == sizeof(MeshVertex), "The shadow stride sentinel must reach the end of MeshVertex"))
        return false;
    const std::string source = ReadRuntimeSource("src/Runtime/Renderer/ShadowPass.cpp");
    const size_t begin = source.find("const VertexElement k_StaticShadowVertexLayout[]");
    const size_t end = begin == std::string::npos ? begin : source.find("};", begin);
    if (!Check(begin != std::string::npos && end != std::string::npos, "Static shadow layout source was not found"))
        return false;
    const std::string layout = source.substr(begin, end - begin);
    return Check(layout.find("defined(MYENGINE_PLATFORM_LINUX)") != std::string::npos &&
                     layout.find("offsetof(MeshVertex, boneWeights)") != std::string::npos,
                 "Linux static shadow layout must retain MeshVertex's full interleaved stride");
}

bool TestVulkanDeviceIdentitySourceContract() {
    const std::string header = ReadRuntimeSource("src/Runtime/Renderer/Backends/Vulkan/VulkanContext.h");
    const std::string source = ReadRuntimeSource("src/Runtime/Renderer/Backends/Vulkan/VulkanContext.cpp");
    const size_t begin = source.find("RHIDeviceIdentity VulkanContext::GetDeviceIdentity() const");
    const size_t end = source.find("RHIDeviceCapabilities VulkanContext::GetCapabilities() const", begin);
    if (!Check(header.find("RHIDeviceIdentity GetDeviceIdentity() const override;") != std::string::npos &&
                   begin != std::string::npos && end != std::string::npos,
               "Vulkan must override the empty default device identity"))
        return false;
    const std::string identity = source.substr(begin, end - begin);
    return Check(identity.find("vkGetPhysicalDeviceProperties2") != std::string::npos &&
                     identity.find("identity.adapterName = properties.properties.deviceName") != std::string::npos &&
                     identity.find("identity.softwareAdapter = properties.properties.deviceType == "
                                   "VK_PHYSICAL_DEVICE_TYPE_CPU") != std::string::npos &&
                     identity.find("VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU") != std::string::npos &&
                     identity.find("VK_MEMORY_HEAP_DEVICE_LOCAL_BIT") != std::string::npos,
                 "Vulkan identity must report real properties, CPU/software type, and discrete-only dedicated VRAM");
}

MYENGINE_REGISTER_TEST("Renderer", "TestVulkanConformanceUsesSlangBytecode", TestVulkanConformanceUsesSlangBytecode);
MYENGINE_REGISTER_TEST("Renderer", "TestVulkanStaticShadowVertexStride", TestVulkanStaticShadowVertexStride);
MYENGINE_REGISTER_TEST("Renderer", "TestVulkanDeviceIdentitySourceContract", TestVulkanDeviceIdentitySourceContract);

} // namespace
