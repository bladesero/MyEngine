#include "Renderer/VulkanContext.h"

#ifdef MYENGINE_PLATFORM_WINDOWS

#include "Core/Logger.h"
#include "Core/Window.h"
#include "Renderer/RHI/RHIResourceStats.h"

#include <SDL3/SDL.h>
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kFramesInFlight = 2;
constexpr uint32_t kBindlessTextureCapacity = 16384;
constexpr uint64_t kMaxVulkanPipelineCacheBytes = 128ull * 1024ull * 1024ull;
constexpr uint32_t kConstantUploadChunkBytes = 1024u * 1024u;

std::filesystem::path GetVulkanPipelineCachePath(VkPhysicalDevice physicalDevice) {
    if (!physicalDevice)
        return {};
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required <= 1)
        return {};
    std::wstring localAppData(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(), required);
    if (written == 0 || written >= required)
        return {};
    localAppData.resize(written);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    std::ostringstream name;
    name << "v1_" << std::hex << std::setfill('0') << std::setw(8) << properties.vendorID << '_' << std::setw(8)
         << properties.deviceID << '_' << std::setw(8) << properties.driverVersion << '_';
    for (uint8_t byte : properties.pipelineCacheUUID)
        name << std::setw(2) << static_cast<uint32_t>(byte);
    name << ".bin";
    return std::filesystem::path(localAppData) / L"MyEngine" / L"PipelineCache" / L"Vulkan" / name.str();
}

bool ReadVulkanPipelineCache(const std::filesystem::path& path, std::vector<uint8_t>& data) {
    if (path.empty())
        return false;
    std::error_code error;
    const uint64_t size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kMaxVulkanPipelineCacheBytes)
        return false;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    data.resize(static_cast<size_t>(size));
    return static_cast<bool>(input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)));
}

bool WriteVulkanPipelineCache(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    if (path.empty() || data.empty() || data.size() > kMaxVulkanPipelineCacheBytes)
        return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    std::filesystem::path temporary = path;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output ||
            !output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

const char* VkResultName(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN:
        return "VK_ERROR_UNKNOWN";
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:
        return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_FRAGMENTATION:
        return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
        return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_PIPELINE_COMPILE_REQUIRED:
        return "VK_PIPELINE_COMPILE_REQUIRED";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "VK_ERROR_UNRECOGNIZED";
    }
}

struct VulkanDeferredReleaseQueue {
    struct RetiredResource {
        uint64_t releaseFrame = 0;
        std::function<void(VkDevice)> destroy;
    };

    void Attach(VkDevice newDevice) {
        std::lock_guard<std::mutex> lock(mutex);
        device = newDevice;
    }

    void Retire(std::function<void(VkDevice)> destroy) {
        if (!destroy)
            return;
        std::lock_guard<std::mutex> lock(mutex);
        if (!device)
            return;
        retired.push_back({frameSerial + kFramesInFlight, std::move(destroy)});
    }

    void AdvanceFrame() {
        std::vector<std::function<void(VkDevice)>> ready;
        VkDevice currentDevice = VK_NULL_HANDLE;
        {
            std::lock_guard<std::mutex> lock(mutex);
            currentDevice = device;
            ++frameSerial;
            retired.erase(std::remove_if(retired.begin(), retired.end(),
                                         [&](RetiredResource& resource) {
                                             if (resource.releaseFrame > frameSerial)
                                                 return false;
                                             ready.push_back(std::move(resource.destroy));
                                             return true;
                                         }),
                          retired.end());
        }
        if (currentDevice) {
            for (auto& destroy : ready)
                destroy(currentDevice);
        }
    }

    void FlushAndDetach(bool detach) {
        std::vector<std::function<void(VkDevice)>> ready;
        VkDevice currentDevice = VK_NULL_HANDLE;
        {
            std::lock_guard<std::mutex> lock(mutex);
            currentDevice = device;
            ready.reserve(retired.size());
            for (auto& resource : retired)
                ready.push_back(std::move(resource.destroy));
            retired.clear();
            if (detach)
                device = VK_NULL_HANDLE;
        }
        if (currentDevice) {
            for (auto& destroy : ready)
                destroy(currentDevice);
        }
    }

    std::mutex mutex;
    VkDevice device = VK_NULL_HANDLE;
    uint64_t frameSerial = 0;
    std::vector<RetiredResource> retired;
};

struct VulkanBuffer final : GpuBuffer {
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocationSize = 0;
    VkMemoryPropertyFlags memoryProperties = 0;
    uint32_t stride = 0;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanBuffer() override {
        const VkBuffer retiredBuffer = buffer;
        const VkDeviceMemory retiredMemory = memory;
        if (deferredReleases) {
            deferredReleases->Retire([retiredBuffer, retiredMemory](VkDevice currentDevice) {
                if (retiredBuffer)
                    vkDestroyBuffer(currentDevice, retiredBuffer, nullptr);
                if (retiredMemory)
                    vkFreeMemory(currentDevice, retiredMemory, nullptr);
            });
        } else {
            if (device && retiredBuffer)
                vkDestroyBuffer(device, retiredBuffer, nullptr);
            if (device && retiredMemory)
                vkFreeMemory(device, retiredMemory, nullptr);
        }
    }
};

struct VulkanConstantUploadChunk {
    std::shared_ptr<VulkanBuffer> buffer;
    void* mapped = nullptr;
    VkDeviceSize writeOffset = 0;
};

struct VulkanBufferView final : GpuBufferView {
    std::shared_ptr<VulkanBuffer> nativeBuffer;
};

struct VulkanTexture final : GpuTexture {
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool ownsImage = true;
    bool isCube = false;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanTexture() override {
        if (!ownsImage)
            return;
        const VkImage retiredImage = image;
        const VkDeviceMemory retiredMemory = memory;
        if (deferredReleases) {
            deferredReleases->Retire([retiredImage, retiredMemory](VkDevice currentDevice) {
                if (retiredImage)
                    vkDestroyImage(currentDevice, retiredImage, nullptr);
                if (retiredMemory)
                    vkFreeMemory(currentDevice, retiredMemory, nullptr);
            });
        } else {
            if (device && retiredImage)
                vkDestroyImage(device, retiredImage, nullptr);
            if (device && retiredMemory)
                vkFreeMemory(device, retiredMemory, nullptr);
        }
    }

    bool IsCube() const override { return isCube; }
};

struct VulkanBindlessIndexAllocator {
    uint32_t Allocate() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!freeIndices.empty()) {
            const uint32_t index = freeIndices.back();
            freeIndices.pop_back();
            return index;
        }
        return nextIndex < kBindlessTextureCapacity ? nextIndex++ : UINT32_MAX;
    }
    void Retire(uint32_t index) {
        if (index == UINT32_MAX)
            return;
        std::lock_guard<std::mutex> lock(mutex);
        retired.push_back({index, frameSerial + kFramesInFlight});
    }
    void AdvanceFrame() {
        std::lock_guard<std::mutex> lock(mutex);
        ++frameSerial;
        retired.erase(std::remove_if(retired.begin(), retired.end(),
                                     [&](const Retired& item) {
                                         if (item.releaseFrame > frameSerial)
                                             return false;
                                         freeIndices.push_back(item.index);
                                         return true;
                                     }),
                      retired.end());
    }
    struct Retired {
        uint32_t index = UINT32_MAX;
        uint64_t releaseFrame = 0;
    };
    std::mutex mutex;
    uint32_t nextIndex = 0;
    uint64_t frameSerial = 0;
    std::vector<uint32_t> freeIndices;
    std::vector<Retired> retired;
};

struct VulkanTextureView final : GpuTextureView {
    VkDevice device = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSampler imguiSampler = VK_NULL_HANDLE;
    std::shared_ptr<VulkanBindlessIndexAllocator> bindlessAllocator;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanTextureView() override {
        if (bindlessAllocator)
            bindlessAllocator->Retire(bindlessIndex);
        const VkImageView retiredView = imageView;
        if (deferredReleases) {
            deferredReleases->Retire([retiredView](VkDevice currentDevice) {
                if (retiredView)
                    vkDestroyImageView(currentDevice, retiredView, nullptr);
            });
        } else if (device && retiredView) {
            vkDestroyImageView(device, retiredView, nullptr);
        }
    }

    ImGuiNativeTextureInfo GetImGuiNativeTextureInfo() const override {
        ImGuiNativeTextureInfo info{};
        info.backend = RHIBackend::Vulkan;
        info.imageView = imageView;
        info.sampler = imguiSampler;
        info.imageLayout = static_cast<uint32_t>(imageLayout);
        return info;
    }
};

struct VulkanSampler final : GpuSampler {
    VkDevice device = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanSampler() override {
        const VkSampler retiredSampler = sampler;
        if (deferredReleases) {
            deferredReleases->Retire([retiredSampler](VkDevice currentDevice) {
                if (retiredSampler)
                    vkDestroySampler(currentDevice, retiredSampler, nullptr);
            });
        } else if (device && retiredSampler) {
            vkDestroySampler(device, retiredSampler, nullptr);
        }
    }
};

struct VulkanReadbackTicket final : GpuReadbackTicket {
    VkDevice device = VK_NULL_HANDLE;
    std::shared_ptr<VulkanBuffer> staging;
    VkFence fence = VK_NULL_HANDLE;
    bool ownsFence = false;
    uint32_t size = 0;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanReadbackTicket() override {
        const VkFence retiredFence = ownsFence ? fence : VK_NULL_HANDLE;
        if (deferredReleases) {
            deferredReleases->Retire([retiredFence](VkDevice currentDevice) {
                if (retiredFence)
                    vkDestroyFence(currentDevice, retiredFence, nullptr);
            });
        } else if (device && retiredFence) {
            vkDestroyFence(device, retiredFence, nullptr);
        }
    }

    bool IsReady() const override {
        if (!device || !staging || !staging->memory || !fence)
            return false;
        return vkGetFenceStatus(device, fence) == VK_SUCCESS;
    }

    bool Read(std::vector<uint8_t>& data) override {
        if (!IsReady())
            return false;
        data.resize(size);
        if (size == 0)
            return true;
        void* mapped = nullptr;
        if (vkMapMemory(device, staging->memory, 0, size, 0, &mapped) != VK_SUCCESS)
            return false;
        std::memcpy(data.data(), mapped, size);
        vkUnmapMemory(device, staging->memory);
        return true;
    }

    uint32_t GetSize() const override { return size; }
};

struct VulkanTimestampPool final : GpuTimestampQueryPool {
    VkDevice device = VK_NULL_HANDLE;
    VkQueryPool pool = VK_NULL_HANDLE;
    uint32_t count = 0;
    double timestampPeriod = 1.0;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanTimestampPool() override {
        const VkQueryPool retiredPool = pool;
        if (deferredReleases) {
            deferredReleases->Retire([retiredPool](VkDevice currentDevice) {
                if (retiredPool)
                    vkDestroyQueryPool(currentDevice, retiredPool, nullptr);
            });
        } else if (device && retiredPool) {
            vkDestroyQueryPool(device, retiredPool, nullptr);
        }
    }
    uint32_t GetCount() const override { return count; }
    uint64_t GetFrequency() const override {
        return timestampPeriod > 0.0 ? static_cast<uint64_t>(1000000000.0 / timestampPeriod) : 0;
    }
    bool ReadResults(uint32_t first, uint32_t resultCount, std::vector<uint64_t>& ticks) override {
        if (!device || !pool || first + resultCount > count)
            return false;
        ticks.resize(resultCount);
        const VkResult result = vkGetQueryPoolResults(device, pool, first, resultCount, resultCount * sizeof(uint64_t),
                                                      ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (result != VK_SUCCESS) {
            ticks.clear();
            return false;
        }
        return true;
    }
};

struct VulkanShader final : GpuShader {
    VkDevice device = VK_NULL_HANDLE;
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule pixelModule = VK_NULL_HANDLE;
    VkShaderModule computeModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::vector<VertexElement> vertexLayout;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanShader() override {
        const VkPipelineLayout retiredPipelineLayout = pipelineLayout;
        const VkDescriptorSetLayout retiredDescriptorSetLayout = descriptorSetLayout;
        const VkShaderModule retiredVertex = vertexModule;
        const VkShaderModule retiredPixel = pixelModule;
        const VkShaderModule retiredCompute = computeModule;
        auto destroy = [retiredPipelineLayout, retiredDescriptorSetLayout, retiredVertex, retiredPixel,
                        retiredCompute](VkDevice currentDevice) {
            if (retiredPipelineLayout)
                vkDestroyPipelineLayout(currentDevice, retiredPipelineLayout, nullptr);
            if (retiredDescriptorSetLayout)
                vkDestroyDescriptorSetLayout(currentDevice, retiredDescriptorSetLayout, nullptr);
            if (retiredVertex)
                vkDestroyShaderModule(currentDevice, retiredVertex, nullptr);
            if (retiredPixel)
                vkDestroyShaderModule(currentDevice, retiredPixel, nullptr);
            if (retiredCompute)
                vkDestroyShaderModule(currentDevice, retiredCompute, nullptr);
        };
        if (deferredReleases)
            deferredReleases->Retire(std::move(destroy));
        else if (device)
            destroy(device);
    }
};

struct VulkanGraphicsPipeline final : GpuGraphicsPipeline {
    VkDevice device = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::vector<VkFormat> colorFormats;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanGraphicsPipeline() override {
        const VkPipeline retiredPipeline = pipeline;
        if (deferredReleases) {
            deferredReleases->Retire([retiredPipeline](VkDevice currentDevice) {
                if (retiredPipeline)
                    vkDestroyPipeline(currentDevice, retiredPipeline, nullptr);
            });
        } else if (device && retiredPipeline) {
            vkDestroyPipeline(device, retiredPipeline, nullptr);
        }
    }
};

struct VulkanComputePipeline final : GpuComputePipeline {
    VkDevice device = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases;

    ~VulkanComputePipeline() override {
        const VkPipeline retiredPipeline = pipeline;
        if (deferredReleases) {
            deferredReleases->Retire([retiredPipeline](VkDevice currentDevice) {
                if (retiredPipeline)
                    vkDestroyPipeline(currentDevice, retiredPipeline, nullptr);
            });
        } else if (device && retiredPipeline) {
            vkDestroyPipeline(device, retiredPipeline, nullptr);
        }
    }
};

struct VulkanBindGroup final : GpuBindGroup {
    explicit VulkanBindGroup(std::shared_ptr<GpuShader> shader) : GpuBindGroup(std::move(shader)) {}

    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    std::unordered_map<std::string, std::shared_ptr<VulkanBuffer>> constantBuffers;
    std::vector<std::shared_ptr<GpuResource>> keepAlive;
    bool descriptorsDirty = true;

    ~VulkanBindGroup() override = default;
};

VkFormat ToVulkanFormat(RHIFormat format) {
    switch (format) {
    case RHIFormat::R8UInt:
        return VK_FORMAT_R8_UINT;
    case RHIFormat::RGBA8UNorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::BGRA8UNorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case RHIFormat::RGBA8UNormSrgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case RHIFormat::RG16Float:
        return VK_FORMAT_R16G16_SFLOAT;
    case RHIFormat::RGBA16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case RHIFormat::R8UNorm:
        return VK_FORMAT_R8_UNORM;
    case RHIFormat::R16UInt:
        return VK_FORMAT_R16_UINT;
    case RHIFormat::R32UInt:
        return VK_FORMAT_R32_UINT;
    case RHIFormat::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case RHIFormat::RG32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    case RHIFormat::RGB32Float:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case RHIFormat::RGBA32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RHIFormat::BC1UNorm:
        // RHI BC1 follows DXT1 semantics, including the optional one-bit alpha mode used by masked materials.
        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case RHIFormat::BC3UNorm:
        return VK_FORMAT_BC3_UNORM_BLOCK;
    case RHIFormat::D24S8:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::D32Float:
        return VK_FORMAT_D32_SFLOAT;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

RHIFormat FromVulkanFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return RHIFormat::BGRA8UNorm;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return RHIFormat::RGBA8UNorm;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return RHIFormat::RGBA8UNormSrgb;
    default:
        return RHIFormat::BGRA8UNorm;
    }
}

VkImageAspectFlags AspectMask(RHIFormat format) {
    switch (format) {
    case RHIFormat::D24S8:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    case RHIFormat::D32Float:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

struct TextureBlockLayout {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t bytes = 0;
};

std::optional<TextureBlockLayout> GetTextureBlockLayout(RHIFormat format) {
    switch (format) {
    case RHIFormat::RGBA8UNorm:
        return TextureBlockLayout{1, 1, 4};
    case RHIFormat::D32Float:
        return TextureBlockLayout{1, 1, sizeof(float)};
    case RHIFormat::BC1UNorm:
        return TextureBlockLayout{4, 4, 8};
    case RHIFormat::BC3UNorm:
        return TextureBlockLayout{4, 4, 16};
    default:
        return std::nullopt;
    }
}

VkImageUsageFlags ToVulkanImageUsage(RHIResourceUsage usage) {
    VkImageUsageFlags result = 0;
    if (HasUsage(usage, RHIResourceUsage::ShaderResource))
        result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasUsage(usage, RHIResourceUsage::RenderTarget))
        result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasUsage(usage, RHIResourceUsage::DepthStencil))
        result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasUsage(usage, RHIResourceUsage::UnorderedAccess))
        result |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasUsage(usage, RHIResourceUsage::CopySource))
        result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (HasUsage(usage, RHIResourceUsage::CopyDestination))
        result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return result;
}

VkImageAspectFlags ViewAspectMask(RHIFormat format, RHIResourceUsage usage) {
    const VkImageAspectFlags aspect = AspectMask(format);
    if ((aspect & VK_IMAGE_ASPECT_DEPTH_BIT) == 0)
        return aspect;
    if (HasUsage(usage, RHIResourceUsage::ShaderResource))
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    return aspect;
}

bool IsDepthStencilFormat(RHIFormat format) {
    const VkImageAspectFlags aspect = AspectMask(format);
    return (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;
}

VkImageLayout ToLayout(RHIResourceState state, RHIFormat format = RHIFormat::Unknown) {
    const bool depthStencil = IsDepthStencilFormat(format);
    switch (state) {
    case RHIResourceState::ShaderResource:
        return depthStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case RHIResourceState::RenderTarget:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case RHIResourceState::DepthRead:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case RHIResourceState::DepthWrite:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case RHIResourceState::UnorderedAccess:
        return VK_IMAGE_LAYOUT_GENERAL;
    case RHIResourceState::CopySource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case RHIResourceState::CopyDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case RHIResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

VkAccessFlags ToAccessMask(RHIResourceState state, RHIFormat format = RHIFormat::Unknown) {
    switch (state) {
    case RHIResourceState::ShaderResource:
        return VK_ACCESS_SHADER_READ_BIT;
    case RHIResourceState::RenderTarget:
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case RHIResourceState::DepthRead:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    case RHIResourceState::DepthWrite:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case RHIResourceState::UnorderedAccess:
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    case RHIResourceState::CopySource:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case RHIResourceState::CopyDestination:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    case RHIResourceState::IndirectArgument:
        return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    case RHIResourceState::Present:
    case RHIResourceState::Undefined:
    default:
        (void)format;
        return 0;
    }
}

VkPipelineStageFlags ToPipelineStage(RHIResourceState state, RHIFormat format = RHIFormat::Unknown) {
    switch (state) {
    case RHIResourceState::ShaderResource:
        return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case RHIResourceState::RenderTarget:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case RHIResourceState::DepthRead:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case RHIResourceState::DepthWrite:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case RHIResourceState::UnorderedAccess:
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case RHIResourceState::CopySource:
    case RHIResourceState::CopyDestination:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case RHIResourceState::IndirectArgument:
        return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    case RHIResourceState::Present:
        return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    case RHIResourceState::Undefined:
    default:
        (void)format;
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

VkAttachmentLoadOp ToLoadOp(RHILoadOp op) {
    switch (op) {
    case RHILoadOp::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case RHILoadOp::Discard:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    default:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    }
}

VkAttachmentStoreOp ToStoreOp(RHIStoreOp op) {
    return op == RHIStoreOp::Discard ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
}

uint32_t VertexFormatSize(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float2:
        return 8;
    case VertexFormat::Float3:
        return 12;
    case VertexFormat::Float4:
        return 16;
    }
    return 0;
}

VkFormat ToVulkanVertexFormat(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float2:
        return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3:
        return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkPrimitiveTopology ToVulkanTopology(RHIPrimitiveTopology topology) {
    switch (topology) {
    case RHIPrimitiveTopology::PointList:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case RHIPrimitiveTopology::LineList:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case RHIPrimitiveTopology::LineStrip:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case RHIPrimitiveTopology::TriangleStrip:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case RHIPrimitiveTopology::TriangleList:
    default:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkCompareOp ToVulkanCompare(RHICompareOp op) {
    switch (op) {
    case RHICompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case RHICompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case RHICompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case RHICompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RHICompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case RHICompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case RHICompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RHICompareOp::Always:
    default:
        return VK_COMPARE_OP_ALWAYS;
    }
}

VkStencilOp ToVulkanStencilOp(RHIStencilOp op) {
    switch (op) {
    case RHIStencilOp::Zero:
        return VK_STENCIL_OP_ZERO;
    case RHIStencilOp::Replace:
        return VK_STENCIL_OP_REPLACE;
    case RHIStencilOp::IncrementClamp:
        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case RHIStencilOp::DecrementClamp:
        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case RHIStencilOp::Invert:
        return VK_STENCIL_OP_INVERT;
    case RHIStencilOp::IncrementWrap:
        return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case RHIStencilOp::DecrementWrap:
        return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    case RHIStencilOp::Keep:
    default:
        return VK_STENCIL_OP_KEEP;
    }
}

VkStencilOpState ToVulkanStencilFace(const RHIStencilFaceState& face, const RHIDepthStencilState& state) {
    VkStencilOpState native{};
    native.failOp = ToVulkanStencilOp(face.failOp);
    native.passOp = ToVulkanStencilOp(face.passOp);
    native.depthFailOp = ToVulkanStencilOp(face.depthFailOp);
    native.compareOp = ToVulkanCompare(face.compareOp);
    native.compareMask = state.stencilReadMask;
    native.writeMask = state.stencilWriteMask;
    native.reference = state.stencilReference;
    return native;
}

VkBlendFactor ToVulkanBlendFactor(RHIBlendFactor factor) {
    switch (factor) {
    case RHIBlendFactor::Zero:
        return VK_BLEND_FACTOR_ZERO;
    case RHIBlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case RHIBlendFactor::SrcColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case RHIBlendFactor::OneMinusSrcColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case RHIBlendFactor::DstColor:
        return VK_BLEND_FACTOR_DST_COLOR;
    case RHIBlendFactor::OneMinusDstColor:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case RHIBlendFactor::SrcAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case RHIBlendFactor::OneMinusSrcAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case RHIBlendFactor::DstAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case RHIBlendFactor::OneMinusDstAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case RHIBlendFactor::ConstantColor:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case RHIBlendFactor::OneMinusConstantColor:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case RHIBlendFactor::SrcAlphaSaturate:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp ToVulkanBlendOp(RHIBlendOp op) {
    switch (op) {
    case RHIBlendOp::Subtract:
        return VK_BLEND_OP_SUBTRACT;
    case RHIBlendOp::ReverseSubtract:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case RHIBlendOp::Min:
        return VK_BLEND_OP_MIN;
    case RHIBlendOp::Max:
        return VK_BLEND_OP_MAX;
    case RHIBlendOp::Add:
    default:
        return VK_BLEND_OP_ADD;
    }
}

VkColorComponentFlags ToVulkanColorMask(uint8_t mask) {
    VkColorComponentFlags flags = 0;
    if (mask & RHIColorWriteRed)
        flags |= VK_COLOR_COMPONENT_R_BIT;
    if (mask & RHIColorWriteGreen)
        flags |= VK_COLOR_COMPONENT_G_BIT;
    if (mask & RHIColorWriteBlue)
        flags |= VK_COLOR_COMPONENT_B_BIT;
    if (mask & RHIColorWriteAlpha)
        flags |= VK_COLOR_COMPONENT_A_BIT;
    return flags;
}

VkFrontFace ToVulkanFrontFace(RHIFrontFace frontFace) {
    return frontFace == RHIFrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
}

VkDescriptorType ToVulkanDescriptorType(ShaderBindingType type) {
    switch (type) {
    case ShaderBindingType::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case ShaderBindingType::Texture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case ShaderBindingType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case ShaderBindingType::StructuredBuffer:
    case ShaderBindingType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ShaderBindingType::StorageTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
}

VkShaderStageFlags ToVulkanShaderStages(uint8_t stages) {
    VkShaderStageFlags flags = 0;
    if (stages & ShaderStageVertex)
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (stages & ShaderStagePixel)
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stages & ShaderStageCompute)
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    return flags ? flags : VK_SHADER_STAGE_ALL;
}

void AddOrMergeBinding(ShaderReflection& reflection, const ShaderBindingDesc& binding) {
    if (binding.name.empty())
        return;
    for (auto& existing : reflection.bindings) {
        if (existing.name == binding.name) {
            existing.stages |= binding.stages;
            existing.type = binding.type;
            existing.bindPoint = binding.bindPoint;
            existing.bindCount = (std::max)(existing.bindCount, binding.bindCount);
            if (binding.byteSize != 0)
                existing.byteSize = binding.byteSize;
            return;
        }
    }
    reflection.bindings.push_back(binding);
}

std::string NormalizeSpirvName(std::string name) {
    const std::string prefix = "SLANG_ParameterGroup_";
    const bool slangParameterGroup = name.rfind(prefix, 0) == 0;
    if (slangParameterGroup)
        name.erase(0, prefix.size());
    const std::string suffix = "_natural";
    if (const size_t pos = name.find(suffix); pos != std::string::npos)
        name.erase(pos);
    if (slangParameterGroup) {
        while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())))
            name.pop_back();
        if (!name.empty() && name.back() == '_')
            name.pop_back();
    }
    return name;
}

VkImageViewType ToVulkanImageViewType(const RHITextureDesc& texture, const RHITextureViewDesc& view) {
    const bool wholeCube = texture.cube && HasUsage(view.usage, RHIResourceUsage::ShaderResource) &&
                           view.firstLayer == 0 && view.layerCount == 6;
    if (wholeCube)
        return VK_IMAGE_VIEW_TYPE_CUBE;
    return view.layerCount > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
}

uint32_t SpirvStringWordCount(const uint32_t* words, uint32_t wordCount, std::string& out) {
    out.clear();
    uint32_t consumed = 0;
    for (; consumed < wordCount; ++consumed) {
        uint32_t word = words[consumed];
        for (uint32_t byte = 0; byte < 4; ++byte) {
            const char c = static_cast<char>((word >> (byte * 8)) & 0xff);
            if (c == '\0')
                return consumed + 1;
            out.push_back(c);
        }
    }
    return consumed;
}

void ReflectSpirvStage(const void* bytecode, size_t byteSize, uint8_t stage, ShaderReflection& reflection) {
    if (!bytecode || byteSize < 20 || (byteSize % sizeof(uint32_t)) != 0)
        return;
    const auto* words = static_cast<const uint32_t*>(bytecode);
    const uint32_t wordCount = static_cast<uint32_t>(byteSize / sizeof(uint32_t));
    if (words[0] != 0x07230203u)
        return;

    struct TypeInfo {
        uint32_t op = 0;
        uint32_t storageClass = UINT32_MAX;
        uint32_t pointeeType = 0;
        uint32_t imageSampled = 1;
    };
    struct VariableInfo {
        uint32_t pointerType = 0;
        uint32_t storageClass = UINT32_MAX;
    };
    std::unordered_map<uint32_t, std::string> names;
    std::unordered_map<uint32_t, uint32_t> bindings;
    std::unordered_map<uint32_t, uint32_t> sets;
    std::unordered_map<uint32_t, TypeInfo> types;
    std::unordered_map<uint32_t, VariableInfo> variables;

    for (uint32_t offset = 5; offset < wordCount;) {
        const uint32_t instruction = words[offset];
        const uint16_t op = static_cast<uint16_t>(instruction & 0xffffu);
        const uint16_t count = static_cast<uint16_t>(instruction >> 16);
        if (count == 0 || offset + count > wordCount)
            break;
        const uint32_t* operands = words + offset + 1;
        const uint32_t operandCount = count - 1;

        switch (op) {
        case 5: { // OpName
            if (operandCount >= 2) {
                std::string name;
                SpirvStringWordCount(operands + 1, operandCount - 1, name);
                names[operands[0]] = NormalizeSpirvName(std::move(name));
            }
            break;
        }
        case 25: // OpTypeImage
            if (operandCount >= 7) {
                types[operands[0]].op = op;
                types[operands[0]].imageSampled = operands[6];
            }
            break;
        case 26: // OpTypeSampler
        case 27: // OpTypeSampledImage
        case 30: // OpTypeStruct
            if (operandCount >= 1)
                types[operands[0]].op = op;
            break;
        case 32: // OpTypePointer
            if (operandCount >= 3) {
                TypeInfo info{};
                info.op = op;
                info.storageClass = operands[1];
                info.pointeeType = operands[2];
                types[operands[0]] = info;
            }
            break;
        case 59: // OpVariable
            if (operandCount >= 3)
                variables[operands[1]] = {operands[0], operands[2]};
            break;
        case 71: { // OpDecorate
            if (operandCount >= 3) {
                constexpr uint32_t kDecorationBinding = 33;
                constexpr uint32_t kDecorationDescriptorSet = 34;
                if (operands[1] == kDecorationBinding)
                    bindings[operands[0]] = operands[2];
                if (operands[1] == kDecorationDescriptorSet)
                    sets[operands[0]] = operands[2];
            }
            break;
        }
        default:
            break;
        }
        offset += count;
    }

    for (const auto& value : variables) {
        const uint32_t id = value.first;
        auto bindingIt = bindings.find(id);
        if (bindingIt == bindings.end())
            continue;
        if (auto setIt = sets.find(id); setIt != sets.end() && setIt->second != 0)
            continue;
        auto ptrIt = types.find(value.second.pointerType);
        if (ptrIt == types.end())
            continue;
        auto pointeeIt = types.find(ptrIt->second.pointeeType);
        if (pointeeIt == types.end())
            continue;

        ShaderBindingDesc binding{};
        binding.name = names.count(id) ? names[id] : ("binding" + std::to_string(bindingIt->second));
        binding.bindPoint = bindingIt->second;
        binding.bindCount = 1;
        binding.stages = stage;

        constexpr uint32_t kStorageUniformConstant = 0;
        constexpr uint32_t kStorageUniform = 2;
        constexpr uint32_t kStorageStorageBuffer = 12;
        if (value.second.storageClass == kStorageUniform || value.second.storageClass == kStorageStorageBuffer) {
            if (value.second.storageClass == kStorageUniform)
                binding.type = ShaderBindingType::ConstantBuffer;
            else
                binding.type =
                    binding.bindPoint >= 128 ? ShaderBindingType::StorageBuffer : ShaderBindingType::StructuredBuffer;
        } else if (value.second.storageClass == kStorageUniformConstant) {
            if (pointeeIt->second.op == 26)
                binding.type = ShaderBindingType::Sampler;
            else if (pointeeIt->second.op == 25 && pointeeIt->second.imageSampled == 2)
                binding.type = ShaderBindingType::StorageTexture;
            else
                binding.type = ShaderBindingType::Texture;
        } else {
            continue;
        }
        AddOrMergeBinding(reflection, binding);
    }
}

bool CreateShaderLayouts(VkDevice device, VkDescriptorSetLayout bindlessLayout, VulkanShader& shader) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(shader.reflection.bindings.size());
    for (const auto& binding : shader.reflection.bindings) {
        VkDescriptorSetLayoutBinding native{};
        native.binding = binding.bindPoint;
        native.descriptorType = ToVulkanDescriptorType(binding.type);
        native.descriptorCount = (std::max)(binding.bindCount, 1u);
        native.stageFlags = ToVulkanShaderStages(binding.stages);
        bindings.push_back(native);
    }
    std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) { return a.binding < b.binding; });
    bindings.erase(std::unique(bindings.begin(), bindings.end(),
                               [](const auto& a, const auto& b) {
                                   return a.binding == b.binding && a.descriptorType == b.descriptorType;
                               }),
                   bindings.end());

    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    setInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &shader.descriptorSetLayout) != VK_SUCCESS)
        return false;

    const VkDescriptorSetLayout layouts[] = {shader.descriptorSetLayout, bindlessLayout};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = bindlessLayout ? 2u : 1u;
    layoutInfo.pSetLayouts = layouts;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shader.pipelineLayout) != VK_SUCCESS)
        return false;
    return true;
}

void ImageBarrier(VkCommandBuffer cmd, VkImage image, RHIFormat format, RHIResourceState before, RHIResourceState after,
                  VkImageSubresourceRange range) {
    if (before == after)
        return;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = ToAccessMask(before, format);
    barrier.dstAccessMask = ToAccessMask(after, format);
    barrier.oldLayout = ToLayout(before, format);
    barrier.newLayout = ToLayout(after, format);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = range;
    vkCmdPipelineBarrier(cmd, ToPipelineStage(before, format), ToPipelineStage(after, format), 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

void ImageBarrier(VkCommandBuffer cmd, VkImage image, RHIFormat format, RHIResourceState before,
                  RHIResourceState after) {
    VkImageSubresourceRange range{};
    range.aspectMask = AspectMask(format);
    range.baseMipLevel = 0;
    range.levelCount = VK_REMAINING_MIP_LEVELS;
    range.baseArrayLayer = 0;
    range.layerCount = VK_REMAINING_ARRAY_LAYERS;
    ImageBarrier(cmd, image, format, before, after, range);
}

const char* ShaderBindingTypeName(ShaderBindingType type) {
    switch (type) {
    case ShaderBindingType::ConstantBuffer:
        return "constant-buffer";
    case ShaderBindingType::Texture:
        return "texture";
    case ShaderBindingType::Sampler:
        return "sampler";
    case ShaderBindingType::StructuredBuffer:
        return "structured-buffer";
    case ShaderBindingType::StorageBuffer:
        return "storage-buffer";
    case ShaderBindingType::StorageTexture:
        return "storage-texture";
    }
    return "unknown";
}

void WarnOnce(const std::string& key, const std::string& message) {
    static std::set<std::string> warned;
    if (warned.insert(key).second)
        Logger::Warn(message);
}

void BufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, RHIResourceState before, RHIResourceState after) {
    if (before == after)
        return;
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = ToAccessMask(before);
    barrier.dstAccessMask = ToAccessMask(after);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, ToPipelineStage(before), ToPipelineStage(after), 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void UAVMemoryBarrier(VkCommandBuffer cmd, VkBuffer buffer) {
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void UAVMemoryBarrier(VkCommandBuffer cmd, VkImage image, RHIFormat format) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = AspectMask(format);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}

class VulkanSwapChain final : public GpuSwapChain {
public:
    explicit VulkanSwapChain(VulkanContext& owner) : m_Owner(owner) {}
    void Present(bool vsync) override {
        (void)vsync;
        m_Owner.EndFrame();
    }
    bool Resize(uint32_t width, uint32_t height) override { return m_Owner.RecreateSwapchain(width, height); }
    uint32_t GetWidth() const override;
    uint32_t GetHeight() const override;

private:
    VulkanContext& m_Owner;
};

class VulkanQueue final : public GpuQueue {
public:
    bool Submit(GpuCommandList*, GpuFence*, uint64_t) override { return true; }
    bool Wait(GpuFence* fence, uint64_t value) override { return !fence || fence->Wait(value); }
};
} // namespace

struct VulkanContext::Impl {
    IWindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::filesystem::path pipelineCachePath;
    bool pipelineCacheDirty = false;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<std::shared_ptr<VulkanTexture>> backBufferTextures;
    std::vector<std::shared_ptr<VulkanTextureView>> backBufferViews;
    std::vector<VkSemaphore> imageRenderFinished;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    std::array<VkSemaphore, kFramesInFlight> renderFinished{};
    std::array<VkFence, kFramesInFlight> inFlight{};
    std::array<VkDescriptorPool, kFramesInFlight> rhiDescriptorPools{};
    std::array<std::vector<std::shared_ptr<GpuResource>>, kFramesInFlight> frameKeepAlive{};
    // Descriptor sets snapshot offsets into these persistently mapped chunks. A slot is rewound only after its fence,
    // so later passes can share an arena without overwriting constants referenced by earlier recorded commands.
    std::array<std::vector<VulkanConstantUploadChunk>, kFramesInFlight> constantUploadChunks{};
    VkDeviceSize uniformBufferOffsetAlignment = 256;
    VkDeviceSize nonCoherentAtomSize = 256;
    VkDescriptorSetLayout bindlessDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool bindlessDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet bindlessDescriptorSet = VK_NULL_HANDLE;
    std::shared_ptr<VulkanBindlessIndexAllocator> bindlessAllocator = std::make_shared<VulkanBindlessIndexAllocator>();
    bool descriptorIndexingSupported = false;
    bool shaderDrawParametersSupported = false;
    bool indirectCountSupported = false;
    uint32_t imageIndex = 0;
    uint32_t frameIndex = 0;
    bool frameOpen = false;
    bool swapchainOutOfDate = false;
    bool swapchainSuboptimal = false;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    VkSampler defaultSampler = VK_NULL_HANDLE;
    std::shared_ptr<VulkanDeferredReleaseQueue> deferredReleases = std::make_shared<VulkanDeferredReleaseQueue>();
};

namespace {
uint32_t FindMemoryType(VkPhysicalDevice gpu, uint32_t typeFilter, VkMemoryPropertyFlags properties,
                        VkMemoryPropertyFlags* selectedProperties = nullptr) {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            if (selectedProperties)
                *selectedProperties = memory.memoryTypes[i].propertyFlags;
            return i;
        }
    }
    return UINT32_MAX;
}

std::shared_ptr<VulkanBuffer> CreateVulkanBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                                                 const RHIBufferDesc& desc, VkBufferUsageFlags usage,
                                                 VkMemoryPropertyFlags properties,
                                                 const std::shared_ptr<VulkanDeferredReleaseQueue>& deferredReleases,
                                                 const void* initialData = nullptr) {
    if (!device || !physicalDevice || desc.size == 0)
        return nullptr;
    auto result = std::make_shared<VulkanBuffer>();
    result->device = device;
    result->deferredReleases = deferredReleases;
    result->desc = desc;
    result->stride = desc.stride;
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = desc.size;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &create, nullptr, &result->buffer) != VK_SUCCESS)
        return nullptr;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, result->buffer, &req);
    VkMemoryPropertyFlags selectedProperties = 0;
    const uint32_t memoryType = FindMemoryType(physicalDevice, req.memoryTypeBits, properties, &selectedProperties);
    if (memoryType == UINT32_MAX)
        return nullptr;
    result->allocationSize = req.size;
    result->memoryProperties = selectedProperties;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device, &alloc, nullptr, &result->memory) != VK_SUCCESS)
        return nullptr;
    vkBindBufferMemory(device, result->buffer, result->memory, 0);
    if (initialData) {
        void* mapped = nullptr;
        if (vkMapMemory(device, result->memory, 0, desc.size, 0, &mapped) == VK_SUCCESS) {
            std::memcpy(mapped, initialData, desc.size);
            vkUnmapMemory(device, result->memory);
        }
    }
    return result;
}

VkDescriptorPool CreateVulkanDescriptorPool(VkDevice device, uint32_t maxSets, VkDescriptorPoolCreateFlags flags = 0) {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSets},          {VK_DESCRIPTOR_TYPE_SAMPLER, maxSets},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets},         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets},
    };
    VkDescriptorPoolCreateInfo descriptorPool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPool.flags = flags;
    descriptorPool.maxSets = maxSets;
    descriptorPool.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    descriptorPool.pPoolSizes = poolSizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &descriptorPool, nullptr, &pool) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return pool;
}

bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& item) {
        return std::strcmp(item.extensionName, name) == 0;
    });
}

std::shared_ptr<VulkanShader> MakeShader(VkDevice device,
                                         const std::shared_ptr<VulkanDeferredReleaseQueue>& deferredReleases,
                                         const void* bytes, size_t size, VkShaderModule VulkanShader::* field) {
    if (!bytes || size == 0 || (size % sizeof(uint32_t)) != 0)
        return {};
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = size;
    createInfo.pCode = static_cast<const uint32_t*>(bytes);
    auto shader = std::make_shared<VulkanShader>();
    shader->device = device;
    shader->deferredReleases = deferredReleases;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &(shader.get()->*field)) != VK_SUCCESS) {
        return {};
    }
    return shader;
}

VkExtent2D ChooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& caps, IWindow* window, uint32_t requestedWidth,
                                 uint32_t requestedHeight) {
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;
    uint32_t width = requestedWidth
                         ? requestedWidth
                         : static_cast<uint32_t>(window && window->GetPixelWidth() > 0 ? window->GetPixelWidth() : 1);
    uint32_t height =
        requestedHeight ? requestedHeight
                        : static_cast<uint32_t>(window && window->GetPixelHeight() > 0 ? window->GetPixelHeight() : 1);
    width = (std::max)(caps.minImageExtent.width, (std::min)(width, caps.maxImageExtent.width));
    height = (std::max)(caps.minImageExtent.height, (std::min)(height, caps.maxImageExtent.height));
    return {width, height};
}
} // namespace

class VulkanImmediateCommandList final : public GpuCommandList {
public:
    explicit VulkanImmediateCommandList(VulkanContext::Impl& impl) : m_Impl(impl) {}

    void BindShader(GpuShader*) override {}
    void BindVertexBuffer(GpuBuffer* buffer) override { SetVertexBuffer(buffer); }
    void BindIndexBuffer(GpuBuffer* buffer) override { SetIndexBuffer(buffer); }
    void SetVSConstants(const void*, uint32_t) override {}
    void Draw(uint32_t vertexCount, uint32_t startVertex = 0) override {
        if (!m_Impl.frameOpen || !m_CurrentPipeline)
            return;
        BindPendingDescriptorSet();
        vkCmdDraw(CommandBuffer(), vertexCount, 1, startVertex, 0);
    }
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, uint32_t baseVertex = 0) override {
        if (!m_Impl.frameOpen || !m_CurrentPipeline)
            return;
        BindPendingDescriptorSet();
        vkCmdDrawIndexed(CommandBuffer(), indexCount, 1, startIndex, static_cast<int32_t>(baseVertex), 0);
    }
    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex = 0) override {
        if (!m_Impl.frameOpen || !m_CurrentPipeline)
            return;
        BindPendingDescriptorSet();
        vkCmdDraw(CommandBuffer(), vertexCount, instanceCount, startVertex, 0);
    }
    void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex = 0,
                              uint32_t baseVertex = 0) override {
        if (!m_Impl.frameOpen || !m_CurrentPipeline)
            return;
        BindPendingDescriptorSet();
        vkCmdDrawIndexed(CommandBuffer(), indexCount, instanceCount, startIndex, static_cast<int32_t>(baseVertex), 0);
    }
    void SetViewport(float x, float y, float w, float h) override {
        if (!m_Impl.frameOpen)
            return;
        VkViewport viewport{x, y + h, w, -h, 0.0f, 1.0f};
        vkCmdSetViewport(m_Impl.commandBuffers[m_Impl.frameIndex], 0, 1, &viewport);
    }
    void BindPSTexture(uint32_t, GpuTexture*) override {}
    void SetBlendMode(GpuBlendMode) override {}
    void SetRasterState(bool, bool) override {}

    void Transition(GpuResource* resource, RHIResourceState before, RHIResourceState after) override {
        if (!m_Impl.frameOpen)
            return;
        auto* texture = dynamic_cast<VulkanTexture*>(resource);
        if (texture && texture->image) {
            ImageBarrier(m_Impl.commandBuffers[m_Impl.frameIndex], texture->image, texture->desc.format, before, after);
            texture->layout = ToLayout(after, texture->desc.format);
            return;
        }
        auto* buffer = dynamic_cast<VulkanBuffer*>(resource);
        if (buffer && buffer->buffer) {
            BufferBarrier(m_Impl.commandBuffers[m_Impl.frameIndex], buffer->buffer, before, after);
        }
    }

    void TransitionTexture(GpuTexture* texture, const RHITextureViewDesc& range, RHIResourceState before,
                           RHIResourceState after) override {
        auto* native = dynamic_cast<VulkanTexture*>(texture);
        if (!native || !native->image || !m_Impl.frameOpen)
            return;
        VkImageSubresourceRange nativeRange{};
        nativeRange.aspectMask = ViewAspectMask(native->desc.format, range.usage);
        nativeRange.baseMipLevel = range.firstMip;
        nativeRange.levelCount = range.mipCount;
        nativeRange.baseArrayLayer = range.firstLayer;
        nativeRange.layerCount = range.layerCount;
        ImageBarrier(m_Impl.commandBuffers[m_Impl.frameIndex], native->image, native->desc.format, before, after,
                     nativeRange);
        native->layout = ToLayout(after, native->desc.format);
    }

    void BeginRendering(const RenderingInfo& info) override {
        if (!m_Impl.frameOpen)
            return;
        std::array<VkRenderingAttachmentInfo, 8> colors{};
        for (uint32_t i = 0; i < info.colorCount && i < colors.size(); ++i) {
            auto* view = dynamic_cast<VulkanTextureView*>(info.colors[i].view);
            if (!view || !view->imageView)
                continue;
            colors[i] = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colors[i].imageView = view->imageView;
            colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colors[i].loadOp = ToLoadOp(info.colors[i].loadOp);
            colors[i].storeOp = ToStoreOp(info.colors[i].storeOp);
            colors[i].clearValue.color = {{info.colors[i].clearColor.r, info.colors[i].clearColor.g,
                                           info.colors[i].clearColor.b, info.colors[i].clearColor.a}};
        }
        VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        VkRenderingAttachmentInfo* depthPtr = nullptr;
        VkRenderingAttachmentInfo* stencilPtr = nullptr;
        if (info.depth && info.depth->view) {
            auto* view = dynamic_cast<VulkanTextureView*>(info.depth->view);
            if (view && view->imageView) {
                depth.imageView = view->imageView;
                depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depth.loadOp = ToLoadOp(info.depth->loadOp);
                depth.storeOp = ToStoreOp(info.depth->storeOp);
                depth.clearValue.depthStencil = {info.depth->clearDepth, info.depth->clearStencil};
                depthPtr = &depth;
                if (view->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
                    stencilPtr = &depth;
            }
        }
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = {info.width ? info.width : m_Impl.swapchainExtent.width,
                                       info.height ? info.height : m_Impl.swapchainExtent.height};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = info.colorCount;
        rendering.pColorAttachments = colors.data();
        rendering.pDepthAttachment = depthPtr;
        rendering.pStencilAttachment = stencilPtr;
        vkCmdBeginRendering(m_Impl.commandBuffers[m_Impl.frameIndex], &rendering);
        const float width = static_cast<float>(rendering.renderArea.extent.width);
        const float height = static_cast<float>(rendering.renderArea.extent.height);
        VkViewport viewport{0.0f, height, width, -height, 0.0f, 1.0f};
        vkCmdSetViewport(m_Impl.commandBuffers[m_Impl.frameIndex], 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, rendering.renderArea.extent};
        vkCmdSetScissor(m_Impl.commandBuffers[m_Impl.frameIndex], 0, 1, &scissor);
        m_RenderingOpen = true;
    }

    void EndRendering() override {
        if (m_Impl.frameOpen && m_RenderingOpen) {
            vkCmdEndRendering(m_Impl.commandBuffers[m_Impl.frameIndex]);
            m_RenderingOpen = false;
        }
    }

    void SetGraphicsPipeline(GpuGraphicsPipeline* pipeline) override {
        auto* native = dynamic_cast<VulkanGraphicsPipeline*>(pipeline);
        if (!m_Impl.frameOpen || !native || !native->pipeline)
            return;
        m_CurrentPipeline = native;
        m_CurrentPipelineLayout = native->layout;
        m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        m_PendingDescriptorSet = VK_NULL_HANDLE;
        m_PendingDescriptorLayout = VK_NULL_HANDLE;
        vkCmdBindPipeline(CommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, native->pipeline);
    }
    void SetDepthOnlyShader(GpuShader*) override {}
    void SetComputePipeline(GpuComputePipeline* pipeline) override {
        auto* native = dynamic_cast<VulkanComputePipeline*>(pipeline);
        if (!m_Impl.frameOpen || !native || !native->pipeline)
            return;
        m_CurrentComputePipeline = native;
        m_CurrentPipelineLayout = native->layout;
        m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        m_PendingDescriptorSet = VK_NULL_HANDLE;
        m_PendingDescriptorLayout = VK_NULL_HANDLE;
        vkCmdBindPipeline(CommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, native->pipeline);
    }
    void SetBindGroup(uint32_t, GpuBindGroup* group) override {
        auto* native = dynamic_cast<VulkanBindGroup*>(group);
        if (!native || !native->GetShader())
            return;
        if (!UpdateDescriptors(*native))
            return;
        auto shader = std::dynamic_pointer_cast<VulkanShader>(native->GetShader());
        m_PendingDescriptorSet = native->descriptorSet;
        m_PendingDescriptorLayout = shader ? shader->pipelineLayout : VK_NULL_HANDLE;
        BindPendingDescriptorSet();
    }
    void SetVertexBuffer(GpuBuffer* buffer) override {
        if (!m_Impl.frameOpen)
            return;
        auto* native = dynamic_cast<VulkanBuffer*>(buffer);
        if (!native || !native->buffer)
            return;
        VkDeviceSize offset = 0;
        VkBuffer vkBuffer = native->buffer;
        vkCmdBindVertexBuffers(CommandBuffer(), 0, 1, &vkBuffer, &offset);
    }
    void SetIndexBuffer(GpuBuffer* buffer) override {
        if (!m_Impl.frameOpen)
            return;
        auto* native = dynamic_cast<VulkanBuffer*>(buffer);
        if (!native || !native->buffer)
            return;
        vkCmdBindIndexBuffer(CommandBuffer(), native->buffer, 0, VK_INDEX_TYPE_UINT32);
    }
    void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override {
        if (!m_Impl.frameOpen)
            return;
        const int64_t right = (std::min)(static_cast<int64_t>(m_Impl.swapchainExtent.width),
                                         static_cast<int64_t>(x) + static_cast<int64_t>(w));
        const int64_t bottom = (std::min)(static_cast<int64_t>(m_Impl.swapchainExtent.height),
                                          static_cast<int64_t>(y) + static_cast<int64_t>(h));
        const int32_t left = (std::max)(x, 0);
        const int32_t top = (std::max)(y, 0);
        VkRect2D scissor{{left, top},
                         {right > left ? static_cast<uint32_t>(right - left) : 0u,
                          bottom > top ? static_cast<uint32_t>(bottom - top) : 0u}};
        vkCmdSetScissor(m_Impl.commandBuffers[m_Impl.frameIndex], 0, 1, &scissor);
    }
    void Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1) override {
        if (!m_Impl.frameOpen || !m_CurrentComputePipeline)
            return;
        BindPendingDescriptorSet();
        vkCmdDispatch(CommandBuffer(), x, y, z);
    }
    void DispatchIndirect(GpuBuffer* args, uint64_t offset = 0) override {
        auto* native = dynamic_cast<VulkanBuffer*>(args);
        if (m_Impl.frameOpen && native && native->buffer && m_CurrentComputePipeline) {
            BindPendingDescriptorSet();
            vkCmdDispatchIndirect(CommandBuffer(), native->buffer, offset);
        }
    }
    void WriteTimestamp(GpuTimestampQueryPool* pool, uint32_t index) override {
        auto* native = dynamic_cast<VulkanTimestampPool*>(pool);
        if (!m_Impl.frameOpen || !native || !native->pool || index >= native->count)
            return;
        if (index == 0)
            vkCmdResetQueryPool(CommandBuffer(), native->pool, 0, native->count);
        vkCmdWriteTimestamp(CommandBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, native->pool, index);
    }
    void ResolveTimestamps(GpuTimestampQueryPool*, uint32_t, uint32_t) override {}
    void CopyBuffer(GpuBuffer* dst, uint32_t dstOffset, GpuBuffer* src, uint32_t srcOffset,
                    uint32_t byteSize) override {
        if (!m_Impl.frameOpen || byteSize == 0)
            return;
        auto* nativeDst = dynamic_cast<VulkanBuffer*>(dst);
        auto* nativeSrc = dynamic_cast<VulkanBuffer*>(src);
        if (!nativeDst || !nativeDst->buffer || !nativeSrc || !nativeSrc->buffer)
            return;
        VkBufferCopy copy{};
        copy.srcOffset = srcOffset;
        copy.dstOffset = dstOffset;
        copy.size = byteSize;
        vkCmdCopyBuffer(CommandBuffer(), nativeSrc->buffer, nativeDst->buffer, 1, &copy);
    }
    void ClearStorageBuffer(GpuBufferView* bufferView, uint32_t value) override {
        auto* view = dynamic_cast<VulkanBufferView*>(bufferView);
        if (!m_Impl.frameOpen || !view || !view->nativeBuffer || !view->nativeBuffer->buffer)
            return;
        const VkDeviceSize stride = (std::max)(view->nativeBuffer->desc.stride, 1u);
        const VkDeviceSize offset = static_cast<VkDeviceSize>(view->desc.firstElement) * stride;
        const VkDeviceSize size =
            view->desc.elementCount ? static_cast<VkDeviceSize>(view->desc.elementCount) * stride : VK_WHOLE_SIZE;
        // RenderGraph exposes storage/indirect states, while vkCmdFillBuffer executes in the transfer stage. Make the
        // transfer write explicitly wait for any prior shader, indirect or transfer access before publishing the clear
        // back to compute. This keeps repeated reset/cull/draw streams valid on Vulkan validation layers as well as on
        // drivers that do not implicitly serialize transfer commands.
        VkBufferMemoryBarrier beforeFill{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        beforeFill.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                   VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                                   VK_ACCESS_TRANSFER_WRITE_BIT;
        beforeFill.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        beforeFill.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        beforeFill.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        beforeFill.buffer = view->nativeBuffer->buffer;
        beforeFill.offset = offset;
        beforeFill.size = size;
        vkCmdPipelineBarrier(CommandBuffer(),
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &beforeFill, 0, nullptr);
        vkCmdFillBuffer(CommandBuffer(), view->nativeBuffer->buffer, offset, size, value);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = view->nativeBuffer->buffer;
        barrier.offset = offset;
        barrier.size = size;
        vkCmdPipelineBarrier(CommandBuffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             0, nullptr, 1, &barrier, 0, nullptr);
    }
    void CopyTexture(GpuTexture*, GpuTexture*) override {}
    void CopyTexture(GpuTexture*, const RHITextureRegion&, GpuTexture*, const RHITextureRegion&) override {}
    void DrawIndirect(GpuBuffer* args, uint64_t offset = 0) override {
        auto* native = dynamic_cast<VulkanBuffer*>(args);
        if (m_Impl.frameOpen && native && native->buffer && m_CurrentPipeline) {
            BindPendingDescriptorSet();
            vkCmdDrawIndirect(CommandBuffer(), native->buffer, offset, 1, sizeof(RHIDrawIndirectArgs));
        }
    }
    void DrawIndexedIndirect(GpuBuffer* args, uint64_t offset = 0) override {
        auto* native = dynamic_cast<VulkanBuffer*>(args);
        if (m_Impl.frameOpen && native && native->buffer && m_CurrentPipeline) {
            BindPendingDescriptorSet();
            vkCmdDrawIndexedIndirect(CommandBuffer(), native->buffer, offset, 1, sizeof(RHIDrawIndexedIndirectArgs));
        }
    }
    void DrawIndexedIndirectCount(GpuBuffer* args, uint64_t argumentOffset, GpuBuffer* countBuffer,
                                  uint64_t countOffset, uint32_t maxDrawCount, uint32_t stride) override {
        auto* native = dynamic_cast<VulkanBuffer*>(args);
        auto* nativeCount = dynamic_cast<VulkanBuffer*>(countBuffer);
        if (m_Impl.frameOpen && native && native->buffer && nativeCount && nativeCount->buffer && m_CurrentPipeline &&
            maxDrawCount > 0 && stride >= sizeof(RHIDrawIndexedIndirectArgs)) {
            BindPendingDescriptorSet();
            // Object-indexed records place a D3D12 root constant before the native draw arguments. Vulkan skips that
            // word and exposes firstInstance through Slang's raw SV_VulkanInstanceID semantic.
            const VkDeviceSize nativeArgumentOffset =
                argumentOffset + (stride == sizeof(RHIObjectDrawIndexedIndirectArgs)
                                      ? offsetof(RHIObjectDrawIndexedIndirectArgs, indexCount)
                                      : 0u);
            vkCmdDrawIndexedIndirectCount(CommandBuffer(), native->buffer, nativeArgumentOffset, nativeCount->buffer,
                                          countOffset, maxDrawCount, stride);
        }
    }
    void UAVBarrier(GpuResource* resource) override {
        if (!m_Impl.frameOpen || !resource)
            return;
        auto* texture = dynamic_cast<VulkanTexture*>(resource);
        if (texture && texture->image) {
            UAVMemoryBarrier(CommandBuffer(), texture->image, texture->desc.format);
            texture->layout = VK_IMAGE_LAYOUT_GENERAL;
            return;
        }
        auto* buffer = dynamic_cast<VulkanBuffer*>(resource);
        if (buffer && buffer->buffer) {
            UAVMemoryBarrier(CommandBuffer(), buffer->buffer);
        }
    }

private:
    VkCommandBuffer CommandBuffer() const { return m_Impl.commandBuffers[m_Impl.frameIndex]; }

    static VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
        alignment = (std::max)(alignment, VkDeviceSize{1});
        return ((value + alignment - 1) / alignment) * alignment;
    }

    bool UploadConstants(const void* data, VkDeviceSize size, std::shared_ptr<VulkanBuffer>& buffer,
                         VkDeviceSize& offset) {
        if (!data || size == 0)
            return false;
        auto& chunks = m_Impl.constantUploadChunks[m_Impl.frameIndex];
        const VkDeviceSize alignment = (std::max)(m_Impl.uniformBufferOffsetAlignment, VkDeviceSize{1});
        VulkanConstantUploadChunk* selected = nullptr;
        VkDeviceSize selectedOffset = 0;
        for (auto& chunk : chunks) {
            const VkDeviceSize candidate = AlignUp(chunk.writeOffset, alignment);
            if (chunk.buffer && chunk.mapped && candidate + size <= chunk.buffer->desc.size) {
                selected = &chunk;
                selectedOffset = candidate;
                break;
            }
        }
        if (!selected) {
            const VkDeviceSize requiredSize = AlignUp(size, alignment);
            const VkDeviceSize chunkSize = (std::max)(VkDeviceSize{kConstantUploadChunkBytes}, requiredSize);
            if (chunkSize > UINT32_MAX)
                return false;
            auto upload = CreateVulkanBuffer(
                m_Impl.device, m_Impl.physicalDevice,
                {static_cast<uint32_t>(chunkSize), 1, RHIResourceUsage::ConstantBuffer, "VulkanConstantUploadArena"},
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_Impl.deferredReleases);
            if (!upload) {
                upload = CreateVulkanBuffer(m_Impl.device, m_Impl.physicalDevice,
                                            {static_cast<uint32_t>(chunkSize), 1, RHIResourceUsage::ConstantBuffer,
                                             "VulkanConstantUploadArena"},
                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                            m_Impl.deferredReleases);
            }
            if (!upload)
                return false;
            void* mapped = nullptr;
            if (vkMapMemory(m_Impl.device, upload->memory, 0, upload->allocationSize, 0, &mapped) != VK_SUCCESS)
                return false;
            chunks.push_back({std::move(upload), mapped, 0});
            selected = &chunks.back();
            selectedOffset = 0;
        }

        std::memcpy(static_cast<uint8_t*>(selected->mapped) + selectedOffset, data, static_cast<size_t>(size));
        if ((selected->buffer->memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            const VkDeviceSize atomSize = (std::max)(m_Impl.nonCoherentAtomSize, VkDeviceSize{1});
            const VkDeviceSize flushOffset = (selectedOffset / atomSize) * atomSize;
            const VkDeviceSize flushEnd = AlignUp(selectedOffset + size, atomSize);
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = selected->buffer->memory;
            range.offset = flushOffset;
            range.size = flushEnd >= selected->buffer->allocationSize ? VK_WHOLE_SIZE : flushEnd - flushOffset;
            if (vkFlushMappedMemoryRanges(m_Impl.device, 1, &range) != VK_SUCCESS)
                return false;
        }
        selected->writeOffset = selectedOffset + size;
        buffer = selected->buffer;
        offset = selectedOffset;
        return true;
    }

    void BindPendingDescriptorSet() {
        if (!m_Impl.frameOpen || !m_CurrentPipelineLayout || !m_PendingDescriptorSet ||
            m_PendingDescriptorLayout != m_CurrentPipelineLayout) {
            return;
        }
        vkCmdBindDescriptorSets(CommandBuffer(), m_CurrentBindPoint, m_CurrentPipelineLayout, 0, 1,
                                &m_PendingDescriptorSet, 0, nullptr);
        if (m_Impl.bindlessDescriptorSet) {
            vkCmdBindDescriptorSets(CommandBuffer(), m_CurrentBindPoint, m_CurrentPipelineLayout, 1, 1,
                                    &m_Impl.bindlessDescriptorSet, 0, nullptr);
        }
    }

    bool UpdateDescriptors(VulkanBindGroup& group) {
        auto shader = std::dynamic_pointer_cast<VulkanShader>(group.GetShader());
        if (!shader || !shader->descriptorSetLayout)
            return false;
        const VkDescriptorPool framePool = m_Impl.rhiDescriptorPools[m_Impl.frameIndex];
        if (!framePool)
            return false;
        {
            VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            alloc.descriptorPool = framePool;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &shader->descriptorSetLayout;
            group.descriptorSet = VK_NULL_HANDLE;
            if (vkAllocateDescriptorSets(m_Impl.device, &alloc, &group.descriptorSet) != VK_SUCCESS) {
                Logger::Error("[Vulkan] vkAllocateDescriptorSets failed for bind group");
                return false;
            }
            group.device = m_Impl.device;
            group.pool = framePool;
        }

        group.keepAlive.clear();
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkWriteDescriptorSet> writes;
        const size_t bindingCount = shader->reflection.bindings.size();
        bufferInfos.reserve(bindingCount);
        imageInfos.reserve(bindingCount);
        writes.reserve(bindingCount);

        auto addWrite = [&](uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* buffer,
                            const VkDescriptorImageInfo* image) {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = group.descriptorSet;
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = type;
            write.pBufferInfo = buffer;
            write.pImageInfo = image;
            writes.push_back(write);
        };
        auto warnBindingSkip = [&](const std::string& name, ShaderBindingType expected, const std::string& reason) {
            const auto* binding = shader->reflection.Find(name);
            std::string message = "[Vulkan] Skipped descriptor '" + name + "': " + reason;
            if (binding) {
                message += " (reflected type=";
                message += ShaderBindingTypeName(binding->type);
                message += " binding=" + std::to_string(binding->bindPoint);
                message += ", expected=";
                message += ShaderBindingTypeName(expected);
                message += ")";
            } else {
                message += " (not found in reflection, expected=";
                message += ShaderBindingTypeName(expected);
                message += ")";
            }
            WarnOnce("descriptor:" + name + ":" + reason, message);
        };

        for (const auto& value : group.GetConstants()) {
            const auto* binding = shader->reflection.Find(value.first);
            if (!binding || binding->type != ShaderBindingType::ConstantBuffer || value.second.empty()) {
                warnBindingSkip(value.first, ShaderBindingType::ConstantBuffer,
                                value.second.empty() ? "empty constants" : "binding mismatch");
                continue;
            }
            std::shared_ptr<VulkanBuffer> buffer;
            VkDeviceSize offset = 0;
            if (!UploadConstants(value.second.data(), value.second.size(), buffer, offset))
                continue;
            bufferInfos.push_back({buffer->buffer, offset, value.second.size()});
            addWrite(binding->bindPoint, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &bufferInfos.back(), nullptr);
        }

        for (const auto& value : group.GetTextures()) {
            const auto* binding = shader->reflection.Find(value.first);
            auto* view = dynamic_cast<VulkanTextureView*>(value.second.get());
            if (!binding || binding->type != ShaderBindingType::Texture || !view || !view->imageView) {
                warnBindingSkip(value.first, ShaderBindingType::Texture,
                                (!view || !view->imageView) ? "invalid texture view" : "binding mismatch");
                continue;
            }
            imageInfos.push_back({VK_NULL_HANDLE, view->imageView, view->imageLayout});
            addWrite(binding->bindPoint, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, nullptr, &imageInfos.back());
            group.keepAlive.push_back(value.second);
        }

        for (const auto& value : group.GetSamplers()) {
            const auto* binding = shader->reflection.Find(value.first);
            auto* sampler = dynamic_cast<VulkanSampler*>(value.second.get());
            if (!binding || binding->type != ShaderBindingType::Sampler) {
                warnBindingSkip(value.first, ShaderBindingType::Sampler, "binding mismatch");
                continue;
            }
            const VkSampler vkSampler = sampler && sampler->sampler ? sampler->sampler : m_Impl.defaultSampler;
            imageInfos.push_back({vkSampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
            addWrite(binding->bindPoint, VK_DESCRIPTOR_TYPE_SAMPLER, nullptr, &imageInfos.back());
            group.keepAlive.push_back(value.second);
        }

        for (const auto& value : group.GetStorageBuffers()) {
            const auto* binding = shader->reflection.Find(value.first);
            auto* view = dynamic_cast<VulkanBufferView*>(value.second.get());
            if (!binding || binding->type != ShaderBindingType::StorageBuffer || !view || !view->nativeBuffer ||
                !view->nativeBuffer->buffer) {
                warnBindingSkip(value.first, ShaderBindingType::StorageBuffer,
                                (!view || !view->nativeBuffer || !view->nativeBuffer->buffer) ? "invalid buffer view"
                                                                                              : "binding mismatch");
                continue;
            }
            const uint64_t stride = (std::max)(view->nativeBuffer->desc.stride, 1u);
            const uint64_t offset = static_cast<uint64_t>(view->desc.firstElement) * stride;
            uint64_t range = VK_WHOLE_SIZE;
            if (view->desc.elementCount != 0)
                range = static_cast<uint64_t>(view->desc.elementCount) * stride;
            bufferInfos.push_back({view->nativeBuffer->buffer, offset, range});
            addWrite(binding->bindPoint, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufferInfos.back(), nullptr);
            group.keepAlive.push_back(value.second);
        }

        for (const auto& value : group.GetBuffers()) {
            const auto* binding = shader->reflection.Find(value.first);
            auto* view = dynamic_cast<VulkanBufferView*>(value.second.get());
            if (!binding || binding->type != ShaderBindingType::StructuredBuffer || !view || !view->nativeBuffer ||
                !view->nativeBuffer->buffer) {
                warnBindingSkip(value.first, ShaderBindingType::StructuredBuffer,
                                (!view || !view->nativeBuffer || !view->nativeBuffer->buffer) ? "invalid buffer view"
                                                                                              : "binding mismatch");
                continue;
            }
            const VkDeviceSize offset = static_cast<VkDeviceSize>(view->desc.firstElement) * view->nativeBuffer->stride;
            const VkDeviceSize range = view->desc.elementCount ? static_cast<VkDeviceSize>(view->desc.elementCount) *
                                                                     view->nativeBuffer->stride
                                                               : VK_WHOLE_SIZE;
            bufferInfos.push_back({view->nativeBuffer->buffer, offset, range});
            addWrite(binding->bindPoint, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bufferInfos.back(), nullptr);
            group.keepAlive.push_back(value.second);
        }

        for (const auto& value : group.GetStorageTextures()) {
            const auto* binding = shader->reflection.Find(value.first);
            auto* view = dynamic_cast<VulkanTextureView*>(value.second.get());
            if (!binding || binding->type != ShaderBindingType::StorageTexture || !view || !view->imageView) {
                warnBindingSkip(value.first, ShaderBindingType::StorageTexture,
                                (!view || !view->imageView) ? "invalid texture view" : "binding mismatch");
                continue;
            }
            imageInfos.push_back({VK_NULL_HANDLE, view->imageView, VK_IMAGE_LAYOUT_GENERAL});
            addWrite(binding->bindPoint, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imageInfos.back());
            group.keepAlive.push_back(value.second);
        }

        if (!writes.empty())
            vkUpdateDescriptorSets(m_Impl.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        for (const auto& binding : shader->reflection.bindings) {
            if (binding.type != ShaderBindingType::StorageBuffer &&
                binding.type != ShaderBindingType::StructuredBuffer &&
                binding.type != ShaderBindingType::StorageTexture)
                continue;
            const bool provided = binding.type == ShaderBindingType::StorageBuffer
                                      ? group.GetStorageBuffers().count(binding.name) != 0
                                      : (binding.type == ShaderBindingType::StructuredBuffer
                                             ? group.GetBuffers().count(binding.name) != 0
                                             : group.GetStorageTextures().count(binding.name) != 0);
            if (!provided) {
                const char* resourceName =
                    binding.type == ShaderBindingType::StorageBuffer
                        ? "storage buffer"
                        : (binding.type == ShaderBindingType::StructuredBuffer ? "structured buffer"
                                                                               : "storage texture");
                WarnOnce("descriptor-missing-storage:" + binding.name,
                         std::string("[Vulkan] Missing required ") + resourceName + " descriptor '" + binding.name +
                             "' at binding " + std::to_string(binding.bindPoint));
            }
        }
        auto& frameKeepAlive = m_Impl.frameKeepAlive[m_Impl.frameIndex];
        frameKeepAlive.insert(frameKeepAlive.end(), group.keepAlive.begin(), group.keepAlive.end());
        return true;
    }

    VulkanContext::Impl& m_Impl;
    bool m_RenderingOpen = false;
    VulkanGraphicsPipeline* m_CurrentPipeline = nullptr;
    VulkanComputePipeline* m_CurrentComputePipeline = nullptr;
    VkPipelineLayout m_CurrentPipelineLayout = VK_NULL_HANDLE;
    VkPipelineBindPoint m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkDescriptorSet m_PendingDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout m_PendingDescriptorLayout = VK_NULL_HANDLE;
};

VulkanContext::VulkanContext()
    : m_Impl(std::make_unique<Impl>()), m_SwapChainInterface(std::make_unique<VulkanSwapChain>(*this)) {
    m_GraphicsCommandList = std::make_unique<VulkanImmediateCommandList>(*m_Impl);
    m_GraphicsQueue = std::make_shared<VulkanQueue>();
}

uint32_t VulkanSwapChain::GetWidth() const {
    const ImGuiBackendHandles handles = m_Owner.GetImGuiBackendHandles();
    return handles.width;
}

uint32_t VulkanSwapChain::GetHeight() const {
    const ImGuiBackendHandles handles = m_Owner.GetImGuiBackendHandles();
    return handles.height;
}

VulkanContext::~VulkanContext() {
    Shutdown();
}

bool VulkanContext::Init(IWindow* window) {
    if (!window || !window->GetNativeHandle()) {
        Logger::Error("[Vulkan] Init failed: missing Win32 window handle");
        return false;
    }
    m_Impl->window = window;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "MyEngine";
    app.apiVersion = VK_API_VERSION_1_3;
    const char* instanceExtensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(instanceExtensions));
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    if (vkCreateInstance(&instanceInfo, nullptr, &m_Impl->instance) != VK_SUCCESS) {
        m_LastDeviceError = "vkCreateInstance failed";
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        return false;
    }

    VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.hinstance = GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(window->GetNativeHandle());
    if (vkCreateWin32SurfaceKHR(m_Impl->instance, &surfaceInfo, nullptr, &m_Impl->surface) != VK_SUCCESS) {
        m_LastDeviceError = "vkCreateWin32SurfaceKHR failed";
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        return false;
    }

    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(m_Impl->instance, &gpuCount, nullptr);
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    if (gpuCount)
        vkEnumeratePhysicalDevices(m_Impl->instance, &gpuCount, gpus.data());
    for (VkPhysicalDevice gpu : gpus) {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, extensions.data());
        if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());
        std::optional<uint32_t> graphics;
        std::optional<uint32_t> present;
        for (uint32_t i = 0; i < familyCount; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                graphics = i;
            VkBool32 supportsPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, m_Impl->surface, &supportsPresent);
            if (supportsPresent)
                present = i;
        }
        if (graphics && present) {
            m_Impl->physicalDevice = gpu;
            m_Impl->graphicsFamily = *graphics;
            m_Impl->presentFamily = *present;
            break;
        }
    }
    if (!m_Impl->physicalDevice) {
        m_LastDeviceError = "no Vulkan device supports graphics and present";
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        return false;
    }

    const float queuePriority = 1.0f;
    std::set<uint32_t> uniqueFamilies = {m_Impl->graphicsFamily, m_Impl->presentFamily};
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queues.push_back(queueInfo);
    }
    VkPhysicalDeviceVulkan11Features available11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features available12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceDynamicRenderingFeatures availableDynamicRendering{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    VkPhysicalDeviceFeatures2 availableFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    availableFeatures.pNext = &available11;
    available11.pNext = &available12;
    available12.pNext = &availableDynamicRendering;
    vkGetPhysicalDeviceFeatures2(m_Impl->physicalDevice, &availableFeatures);
    m_Impl->shaderDrawParametersSupported = available11.shaderDrawParameters == VK_TRUE;
    m_Impl->indirectCountSupported = available12.drawIndirectCount == VK_TRUE;
    m_Impl->descriptorIndexingSupported = available12.descriptorIndexing == VK_TRUE &&
                                          available12.runtimeDescriptorArray == VK_TRUE &&
                                          available12.descriptorBindingPartiallyBound == VK_TRUE &&
                                          available12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
                                          available12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    if (availableDynamicRendering.dynamicRendering != VK_TRUE) {
        m_LastDeviceError = "selected Vulkan device does not support dynamic rendering";
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        return false;
    }

    VkPhysicalDeviceVulkan11Features enabled11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    enabled11.shaderDrawParameters = available11.shaderDrawParameters;
    VkPhysicalDeviceVulkan12Features enabled12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    enabled12.drawIndirectCount = available12.drawIndirectCount;
    if (m_Impl->descriptorIndexingSupported) {
        enabled12.descriptorIndexing = VK_TRUE;
        enabled12.runtimeDescriptorArray = VK_TRUE;
        enabled12.descriptorBindingPartiallyBound = VK_TRUE;
        enabled12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        enabled12.descriptorBindingUpdateUnusedWhilePending = available12.descriptorBindingUpdateUnusedWhilePending;
        enabled12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    }
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    dynamicRendering.dynamicRendering = availableDynamicRendering.dynamicRendering;
    enabled11.pNext = &enabled12;
    enabled12.pNext = &dynamicRendering;
    uint32_t selectedExtensionCount = 0;
    vkEnumerateDeviceExtensionProperties(m_Impl->physicalDevice, nullptr, &selectedExtensionCount, nullptr);
    std::vector<VkExtensionProperties> selectedExtensions(selectedExtensionCount);
    if (selectedExtensionCount) {
        vkEnumerateDeviceExtensionProperties(m_Impl->physicalDevice, nullptr, &selectedExtensionCount,
                                             selectedExtensions.data());
    }
    std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (HasExtension(selectedExtensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &enabled11;
    deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
    deviceInfo.pQueueCreateInfos = queues.data();
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    if (vkCreateDevice(m_Impl->physicalDevice, &deviceInfo, nullptr, &m_Impl->device) != VK_SUCCESS) {
        m_LastDeviceError = "vkCreateDevice failed";
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        return false;
    }
    m_Impl->pipelineCachePath = GetVulkanPipelineCachePath(m_Impl->physicalDevice);
    std::vector<uint8_t> pipelineCacheData;
    ReadVulkanPipelineCache(m_Impl->pipelineCachePath, pipelineCacheData);
    VkPipelineCacheCreateInfo pipelineCacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    pipelineCacheInfo.initialDataSize = pipelineCacheData.size();
    pipelineCacheInfo.pInitialData = pipelineCacheData.empty() ? nullptr : pipelineCacheData.data();
    VkResult pipelineCacheResult =
        vkCreatePipelineCache(m_Impl->device, &pipelineCacheInfo, nullptr, &m_Impl->pipelineCache);
    if (pipelineCacheResult != VK_SUCCESS && !pipelineCacheData.empty()) {
        // The Vulkan header validates adapter/driver identity, but a truncated or vendor-invalidated blob can still
        // reach this point. Discard it in memory and continue with an empty cache instead of failing device startup.
        pipelineCacheInfo.initialDataSize = 0;
        pipelineCacheInfo.pInitialData = nullptr;
        pipelineCacheResult =
            vkCreatePipelineCache(m_Impl->device, &pipelineCacheInfo, nullptr, &m_Impl->pipelineCache);
    }
    if (pipelineCacheResult != VK_SUCCESS)
        Logger::Warn("[Vulkan] Pipeline cache is unavailable; PSOs will compile without persistence");
    m_Impl->deferredReleases->Attach(m_Impl->device);
    vkGetDeviceQueue(m_Impl->device, m_Impl->graphicsFamily, 0, &m_Impl->graphicsQueue);
    vkGetDeviceQueue(m_Impl->device, m_Impl->presentFamily, 0, &m_Impl->presentQueue);

    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = m_Impl->graphicsFamily;
    if (vkCreateCommandPool(m_Impl->device, &pool, nullptr, &m_Impl->commandPool) != VK_SUCCESS) {
        Logger::Error("[Vulkan] vkCreateCommandPool failed");
        return false;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = m_Impl->commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = kFramesInFlight;
    if (vkAllocateCommandBuffers(m_Impl->device, &alloc, m_Impl->commandBuffers.data()) != VK_SUCCESS) {
        Logger::Error("[Vulkan] vkAllocateCommandBuffers failed");
        return false;
    }
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        vkCreateSemaphore(m_Impl->device, &semaphore, nullptr, &m_Impl->imageAvailable[i]);
        vkCreateSemaphore(m_Impl->device, &semaphore, nullptr, &m_Impl->renderFinished[i]);
        vkCreateFence(m_Impl->device, &fence, nullptr, &m_Impl->inFlight[i]);
    }

    m_Impl->imguiDescriptorPool =
        CreateVulkanDescriptorPool(m_Impl->device, 1024, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    if (!m_Impl->imguiDescriptorPool) {
        Logger::Error("[Vulkan] Failed to create ImGui descriptor pool");
        return false;
    }
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        m_Impl->rhiDescriptorPools[i] = CreateVulkanDescriptorPool(m_Impl->device, 4096);
        if (!m_Impl->rhiDescriptorPools[i]) {
            Logger::Error("[Vulkan] Failed to create RHI descriptor pool");
            return false;
        }
    }
    if (m_Impl->descriptorIndexingSupported) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        binding.descriptorCount = kBindlessTextureCapacity;
        binding.stageFlags = VK_SHADER_STAGE_ALL;
        VkDescriptorBindingFlags bindingFlags =
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        bindingFlagsInfo.bindingCount = 1;
        bindingFlagsInfo.pBindingFlags = &bindingFlags;
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.pNext = &bindingFlagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(m_Impl->device, &layoutInfo, nullptr, &m_Impl->bindlessDescriptorSetLayout) !=
            VK_SUCCESS) {
            Logger::Warn("[Vulkan] Descriptor indexing is available but the bindless layout could not be created");
            m_Impl->descriptorIndexingSupported = false;
        }
    }
    if (m_Impl->descriptorIndexingSupported) {
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kBindlessTextureCapacity};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(m_Impl->device, &poolInfo, nullptr, &m_Impl->bindlessDescriptorPool) != VK_SUCCESS) {
            Logger::Warn("[Vulkan] Failed to create the bindless descriptor pool");
            m_Impl->descriptorIndexingSupported = false;
        }
    }
    if (m_Impl->descriptorIndexingSupported) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_Impl->bindlessDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_Impl->bindlessDescriptorSetLayout;
        if (vkAllocateDescriptorSets(m_Impl->device, &allocInfo, &m_Impl->bindlessDescriptorSet) != VK_SUCCESS) {
            Logger::Warn("[Vulkan] Failed to allocate the bindless descriptor set");
            m_Impl->descriptorIndexingSupported = false;
        }
    }

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(m_Impl->device, &sampler, nullptr, &m_Impl->defaultSampler);

    if (!RecreateSwapchain(static_cast<uint32_t>(window->GetPixelWidth() > 0 ? window->GetPixelWidth() : 1),
                           static_cast<uint32_t>(window->GetPixelHeight() > 0 ? window->GetPixelHeight() : 1))) {
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_Impl->physicalDevice, &properties);
    m_Impl->uniformBufferOffsetAlignment =
        (std::max)(properties.limits.minUniformBufferOffsetAlignment, VkDeviceSize{1});
    m_Impl->nonCoherentAtomSize = (std::max)(properties.limits.nonCoherentAtomSize, VkDeviceSize{1});
    ++m_DeviceGeneration;
    Logger::Info("[Vulkan] Initialized - GPU: ", properties.deviceName, " generation=", m_DeviceGeneration);
    return true;
}

bool VulkanContext::RecreateSwapchain(uint32_t requestedWidth, uint32_t requestedHeight) {
    if (!m_Impl || !m_Impl->device || !m_Impl->physicalDevice || !m_Impl->surface)
        return false;
    if (m_Impl->frameOpen)
        return false;

    VkSurfaceCapabilitiesKHR caps{};
    const VkResult capsResult =
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Impl->physicalDevice, m_Impl->surface, &caps);
    if (capsResult != VK_SUCCESS) {
        m_DeviceLost = capsResult == VK_ERROR_DEVICE_LOST;
        m_LastDeviceError =
            std::string("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: ") + VkResultName(capsResult);
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        return false;
    }
    const VkExtent2D extent = ChooseSwapchainExtent(caps, m_Impl->window, requestedWidth, requestedHeight);
    if (extent.width == 0 || extent.height == 0) {
        m_Impl->swapchainOutOfDate = true;
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_Impl->physicalDevice, m_Impl->surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_Impl->physicalDevice, m_Impl->surface, &formatCount, formats.data());
    }
    VkSurfaceFormatKHR chosen =
        formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM)
            chosen = format;
    }

    uint32_t desiredImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount != 0 && desiredImageCount > caps.maxImageCount) {
        desiredImageCount = caps.maxImageCount;
    }

    const VkSwapchainKHR oldSwapchain = m_Impl->swapchain;
    VkSwapchainCreateInfoKHR swapchain{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain.surface = m_Impl->surface;
    swapchain.minImageCount = desiredImageCount;
    swapchain.imageFormat = chosen.format;
    swapchain.imageColorSpace = chosen.colorSpace;
    swapchain.imageExtent = extent;
    swapchain.imageArrayLayers = 1;
    swapchain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    uint32_t families[] = {m_Impl->graphicsFamily, m_Impl->presentFamily};
    if (m_Impl->graphicsFamily != m_Impl->presentFamily) {
        swapchain.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchain.queueFamilyIndexCount = 2;
        swapchain.pQueueFamilyIndices = families;
    } else {
        swapchain.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    swapchain.preTransform = caps.currentTransform;
    swapchain.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain.clipped = VK_TRUE;
    swapchain.oldSwapchain = oldSwapchain;

    vkDeviceWaitIdle(m_Impl->device);
    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    const VkResult createResult = vkCreateSwapchainKHR(m_Impl->device, &swapchain, nullptr, &newSwapchain);
    if (createResult != VK_SUCCESS) {
        m_DeviceLost = createResult == VK_ERROR_DEVICE_LOST;
        m_LastDeviceError = std::string("vkCreateSwapchainKHR failed: ") + VkResultName(createResult);
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        return false;
    }

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_Impl->device, newSwapchain, &imageCount, nullptr);
    std::vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(m_Impl->device, newSwapchain, &imageCount, images.data());

    std::vector<std::shared_ptr<VulkanTexture>> textures(imageCount);
    std::vector<std::shared_ptr<VulkanTextureView>> views(imageCount);
    std::vector<VkSemaphore> renderFinished(imageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < imageCount; ++i) {
        auto texture = std::make_shared<VulkanTexture>();
        texture->device = m_Impl->device;
        texture->deferredReleases = m_Impl->deferredReleases;
        texture->image = images[i];
        texture->ownsImage = false;
        // Swapchain images begin in an undefined layout. After the first successful present, EndFrame leaves them in
        // PRESENT_SRC_KHR and subsequent acquisitions use the regular Present -> RenderTarget transition.
        texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
        texture->desc.width = extent.width;
        texture->desc.height = extent.height;
        texture->desc.format = FromVulkanFormat(chosen.format);
        texture->desc.usage = RHIResourceUsage::RenderTarget;

        auto view = std::make_shared<VulkanTextureView>();
        view->device = m_Impl->device;
        view->deferredReleases = m_Impl->deferredReleases;
        view->texture = texture;
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = texture->image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = chosen.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_Impl->device, &viewInfo, nullptr, &view->imageView) != VK_SUCCESS) {
            vkDestroySwapchainKHR(m_Impl->device, newSwapchain, nullptr);
            Logger::Error("[Vulkan] vkCreateImageView failed for swapchain backbuffer");
            return false;
        }
        textures[i] = texture;
        views[i] = view;
        if (vkCreateSemaphore(m_Impl->device, &semaphoreInfo, nullptr, &renderFinished[i]) != VK_SUCCESS) {
            for (VkSemaphore semaphore : renderFinished) {
                if (semaphore)
                    vkDestroySemaphore(m_Impl->device, semaphore, nullptr);
            }
            vkDestroySwapchainKHR(m_Impl->device, newSwapchain, nullptr);
            Logger::Error("[Vulkan] vkCreateSemaphore failed for swapchain image");
            return false;
        }
    }

    for (auto& keepAlive : m_Impl->frameKeepAlive)
        keepAlive.clear();
    for (VkSemaphore semaphore : m_Impl->imageRenderFinished) {
        if (semaphore)
            vkDestroySemaphore(m_Impl->device, semaphore, nullptr);
    }
    m_Impl->imageRenderFinished.clear();
    m_Impl->backBufferViews.clear();
    m_Impl->backBufferTextures.clear();
    if (m_Impl->deferredReleases)
        m_Impl->deferredReleases->FlushAndDetach(false);
    if (oldSwapchain)
        vkDestroySwapchainKHR(m_Impl->device, oldSwapchain, nullptr);

    m_Impl->swapchain = newSwapchain;
    m_Impl->swapchainFormat = chosen.format;
    m_Impl->swapchainExtent = extent;
    m_Impl->swapchainImages = std::move(images);
    m_Impl->backBufferTextures = std::move(textures);
    m_Impl->backBufferViews = std::move(views);
    m_Impl->imageRenderFinished = std::move(renderFinished);
    m_Impl->imageIndex = 0;
    m_Impl->swapchainOutOfDate = false;
    m_Impl->swapchainSuboptimal = false;
    if (m_ResizeCallback)
        m_ResizeCallback();
    return true;
}

void VulkanContext::Shutdown() {
    if (!m_Impl || !m_Impl->instance)
        return;
    if (m_Impl->device)
        vkDeviceWaitIdle(m_Impl->device);
    if (m_Impl->device && m_Impl->pipelineCache && m_Impl->pipelineCacheDirty && !m_Impl->pipelineCachePath.empty()) {
        size_t cacheSize = 0;
        if (vkGetPipelineCacheData(m_Impl->device, m_Impl->pipelineCache, &cacheSize, nullptr) == VK_SUCCESS &&
            cacheSize > 0 && cacheSize <= kMaxVulkanPipelineCacheBytes) {
            std::vector<uint8_t> cacheData(cacheSize);
            if (vkGetPipelineCacheData(m_Impl->device, m_Impl->pipelineCache, &cacheSize, cacheData.data()) ==
                VK_SUCCESS) {
                cacheData.resize(cacheSize);
                WriteVulkanPipelineCache(m_Impl->pipelineCachePath, cacheData);
            }
        }
    }
    for (auto& chunks : m_Impl->constantUploadChunks) {
        for (auto& chunk : chunks) {
            if (m_Impl->device && chunk.buffer && chunk.buffer->memory && chunk.mapped)
                vkUnmapMemory(m_Impl->device, chunk.buffer->memory);
            chunk.mapped = nullptr;
        }
        chunks.clear();
    }
    for (auto& keepAlive : m_Impl->frameKeepAlive)
        keepAlive.clear();
    for (VkSemaphore semaphore : m_Impl->imageRenderFinished) {
        if (m_Impl->device && semaphore)
            vkDestroySemaphore(m_Impl->device, semaphore, nullptr);
    }
    m_Impl->imageRenderFinished.clear();
    m_Impl->backBufferViews.clear();
    m_Impl->backBufferTextures.clear();
    if (m_Impl->deferredReleases)
        m_Impl->deferredReleases->FlushAndDetach(true);
    if (m_Impl->device && m_Impl->defaultSampler)
        vkDestroySampler(m_Impl->device, m_Impl->defaultSampler, nullptr);
    if (m_Impl->device && m_Impl->imguiDescriptorPool)
        vkDestroyDescriptorPool(m_Impl->device, m_Impl->imguiDescriptorPool, nullptr);
    for (VkDescriptorPool pool : m_Impl->rhiDescriptorPools) {
        if (m_Impl->device && pool)
            vkDestroyDescriptorPool(m_Impl->device, pool, nullptr);
    }
    if (m_Impl->device && m_Impl->bindlessDescriptorPool)
        vkDestroyDescriptorPool(m_Impl->device, m_Impl->bindlessDescriptorPool, nullptr);
    if (m_Impl->device && m_Impl->bindlessDescriptorSetLayout)
        vkDestroyDescriptorSetLayout(m_Impl->device, m_Impl->bindlessDescriptorSetLayout, nullptr);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (m_Impl->device && m_Impl->imageAvailable[i])
            vkDestroySemaphore(m_Impl->device, m_Impl->imageAvailable[i], nullptr);
        if (m_Impl->device && m_Impl->renderFinished[i])
            vkDestroySemaphore(m_Impl->device, m_Impl->renderFinished[i], nullptr);
        if (m_Impl->device && m_Impl->inFlight[i])
            vkDestroyFence(m_Impl->device, m_Impl->inFlight[i], nullptr);
    }
    if (m_Impl->device && m_Impl->commandPool)
        vkDestroyCommandPool(m_Impl->device, m_Impl->commandPool, nullptr);
    if (m_Impl->device && m_Impl->swapchain)
        vkDestroySwapchainKHR(m_Impl->device, m_Impl->swapchain, nullptr);
    if (m_Impl->device && m_Impl->pipelineCache)
        vkDestroyPipelineCache(m_Impl->device, m_Impl->pipelineCache, nullptr);
    if (m_Impl->device)
        vkDestroyDevice(m_Impl->device, nullptr);
    if (m_Impl->surface)
        vkDestroySurfaceKHR(m_Impl->instance, m_Impl->surface, nullptr);
    vkDestroyInstance(m_Impl->instance, nullptr);
    *m_Impl = Impl{};
    Logger::Info("[Vulkan] Shutdown");
}

void VulkanContext::BeginFrame(float r, float g, float b, float a) {
    if (!m_Impl || !m_Impl->device || m_Impl->frameOpen)
        return;
    if (m_Impl->swapchainOutOfDate || m_Impl->swapchainSuboptimal) {
        if (!RecreateSwapchain())
            return;
    }
    m_FrameIndex = m_Impl->frameIndex;
    // The fence covers every descriptor and upload resource retained for this frame slot. Reset them only after the GPU
    // has completed that slot; frame-index reuse alone does not make those objects safe to release.
    vkWaitForFences(m_Impl->device, 1, &m_Impl->inFlight[m_Impl->frameIndex], VK_TRUE, UINT64_MAX);
    if (m_Impl->deferredReleases)
        m_Impl->deferredReleases->AdvanceFrame();
    if (m_Impl->bindlessAllocator)
        m_Impl->bindlessAllocator->AdvanceFrame();
    m_Impl->frameKeepAlive[m_Impl->frameIndex].clear();
    for (auto& chunk : m_Impl->constantUploadChunks[m_Impl->frameIndex])
        chunk.writeOffset = 0;
    if (m_Impl->rhiDescriptorPools[m_Impl->frameIndex]) {
        vkResetDescriptorPool(m_Impl->device, m_Impl->rhiDescriptorPools[m_Impl->frameIndex], 0);
    }
    const VkResult acquire =
        vkAcquireNextImageKHR(m_Impl->device, m_Impl->swapchain, UINT64_MAX, m_Impl->imageAvailable[m_Impl->frameIndex],
                              VK_NULL_HANDLE, &m_Impl->imageIndex);
    if (acquire == VK_NOT_READY || acquire == VK_TIMEOUT) {
        return;
    }
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        m_Impl->swapchainOutOfDate = true;
        RecreateSwapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        m_DeviceLost = acquire == VK_ERROR_DEVICE_LOST;
        m_LastDeviceError = std::string("vkAcquireNextImageKHR failed: ") + VkResultName(acquire);
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        return;
    }
    if (acquire == VK_SUBOPTIMAL_KHR) {
        m_Impl->swapchainSuboptimal = true;
    }
    vkResetFences(m_Impl->device, 1, &m_Impl->inFlight[m_Impl->frameIndex]);
    VkCommandBuffer cmd = m_Impl->commandBuffers[m_Impl->frameIndex];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin);
    m_Impl->frameOpen = true;
    auto* backBuffer = m_Impl->backBufferTextures[m_Impl->imageIndex].get();
    VulkanImmediateCommandList list(*m_Impl);
    const RHIResourceState backBufferBefore = backBuffer && backBuffer->layout == VK_IMAGE_LAYOUT_UNDEFINED
                                                  ? RHIResourceState::Undefined
                                                  : RHIResourceState::Present;
    list.Transition(backBuffer, backBufferBefore, RHIResourceState::RenderTarget);
    RenderingAttachment color{};
    color.view = m_Impl->backBufferViews[m_Impl->imageIndex].get();
    color.loadOp = RHILoadOp::Clear;
    color.storeOp = RHIStoreOp::Store;
    color.clearColor = {r, g, b, a};
    RenderingInfo info{};
    info.colors = &color;
    info.colorCount = 1;
    info.width = m_Impl->swapchainExtent.width;
    info.height = m_Impl->swapchainExtent.height;
    list.BeginRendering(info);
    list.EndRendering();
}

void VulkanContext::EndFrame() {
    if (!m_Impl || !m_Impl->device || !m_Impl->frameOpen)
        return;
    VulkanImmediateCommandList list(*m_Impl);
    list.Transition(m_Impl->backBufferTextures[m_Impl->imageIndex].get(), RHIResourceState::RenderTarget,
                    RHIResourceState::Present);
    VkCommandBuffer cmd = m_Impl->commandBuffers[m_Impl->frameIndex];
    const VkResult endResult = vkEndCommandBuffer(cmd);
    if (endResult != VK_SUCCESS) {
        m_DeviceLost = endResult == VK_ERROR_DEVICE_LOST;
        m_LastDeviceError = std::string("vkEndCommandBuffer failed: ") + VkResultName(endResult);
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        m_Impl->frameOpen = false;
        return;
    }
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (m_Impl->imageIndex >= m_Impl->imageRenderFinished.size() || !m_Impl->imageRenderFinished[m_Impl->imageIndex]) {
        m_LastDeviceError = "missing swapchain image render-finished semaphore";
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        m_Impl->frameOpen = false;
        return;
    }
    // Present completion is tied to the acquired swapchain image, not the CPU frame slot. An image-owned semaphore
    // avoids re-signalling a semaphore that the presentation engine may still be waiting on.
    VkSemaphore renderFinished = m_Impl->imageRenderFinished[m_Impl->imageIndex];
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &m_Impl->imageAvailable[m_Impl->frameIndex];
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished;
    const VkResult submitResult =
        vkQueueSubmit(m_Impl->graphicsQueue, 1, &submit, m_Impl->inFlight[m_Impl->frameIndex]);
    if (submitResult != VK_SUCCESS) {
        m_DeviceLost = submitResult == VK_ERROR_DEVICE_LOST;
        m_LastDeviceError = std::string("vkQueueSubmit failed: ") + VkResultName(submitResult);
        Logger::Error("[Vulkan] ", m_LastDeviceError);
        m_Impl->frameOpen = false;
        return;
    }
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &m_Impl->swapchain;
    present.pImageIndices = &m_Impl->imageIndex;
    const VkResult presentResult = vkQueuePresentKHR(m_Impl->presentQueue, &present);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        m_Impl->swapchainOutOfDate = true;
    } else if (presentResult == VK_SUBOPTIMAL_KHR) {
        m_Impl->swapchainSuboptimal = true;
    } else if (presentResult != VK_SUCCESS) {
        m_DeviceLost = presentResult == VK_ERROR_DEVICE_LOST;
        m_LastDeviceError = std::string("vkQueuePresentKHR failed: ") + VkResultName(presentResult);
        Logger::Error("[Vulkan] ", m_LastDeviceError);
    }
    m_Impl->frameOpen = false;
    m_Impl->frameIndex = (m_Impl->frameIndex + 1) % kFramesInFlight;
}

GpuCommandList* VulkanContext::GetGraphicsCommandList() {
    return m_Impl && m_Impl->frameOpen ? m_GraphicsCommandList.get() : nullptr;
}
GpuSwapChain* VulkanContext::GetSwapChain() {
    return m_SwapChainInterface.get();
}
GpuTextureView* VulkanContext::GetCurrentBackBufferView() {
    if (!m_Impl || m_Impl->imageIndex >= m_Impl->backBufferViews.size())
        return nullptr;
    return m_Impl->backBufferViews[m_Impl->imageIndex].get();
}

ImGuiBackendHandles VulkanContext::GetImGuiBackendHandles() {
    ImGuiBackendHandles handles{};
    if (!m_Impl)
        return handles;
    handles.backend = RHIBackend::Vulkan;
    handles.device = m_Impl->device;
    handles.physicalDevice = m_Impl->physicalDevice;
    handles.instance = m_Impl->instance;
    handles.queue = m_Impl->graphicsQueue;
    handles.queueFamily = m_Impl->graphicsFamily;
    handles.descriptorPool = m_Impl->imguiDescriptorPool;
    handles.imageCount = static_cast<uint32_t>(m_Impl->swapchainImages.size());
    handles.minImageCount = std::max<uint32_t>(2, handles.imageCount);
    handles.colorFormat = static_cast<uint32_t>(m_Impl->swapchainFormat);
    handles.width = m_Impl->swapchainExtent.width;
    handles.height = m_Impl->swapchainExtent.height;
    if (m_Impl->frameOpen && m_Impl->imageIndex < m_Impl->backBufferViews.size()) {
        handles.commandBuffer = m_Impl->commandBuffers[m_Impl->frameIndex];
        handles.imageView = m_Impl->backBufferViews[m_Impl->imageIndex]->imageView;
    }
    handles.vulkan.instance = handles.instance;
    handles.vulkan.apiVersion = VK_API_VERSION_1_3;
    handles.vulkan.physicalDevice = handles.physicalDevice;
    handles.vulkan.device = handles.device;
    handles.vulkan.queue = handles.queue;
    handles.vulkan.descriptorPool = handles.descriptorPool;
    handles.vulkan.commandBuffer = handles.commandBuffer;
    handles.vulkan.imageView = handles.imageView;
    handles.vulkan.queueFamily = handles.queueFamily;
    handles.vulkan.imageCount = handles.imageCount;
    handles.vulkan.minImageCount = handles.minImageCount;
    handles.vulkan.colorFormat = handles.colorFormat;
    handles.vulkan.width = handles.width;
    handles.vulkan.height = handles.height;
    return handles;
}

void VulkanContext::SetImGuiTextureInteropReady(bool ready) {
    (void)ready;
}

std::shared_ptr<GpuBuffer> VulkanContext::CreateVertexBuffer(const void* data, uint32_t byteSize,
                                                             uint32_t strideBytes) {
    auto desc = RHIBufferDesc{byteSize, strideBytes, RHIResourceUsage::VertexBuffer, "VertexBuffer"};
    auto buffer = std::dynamic_pointer_cast<VulkanBuffer>(CreateBuffer(desc, data));
    if (buffer)
        buffer->stride = strideBytes;
    return buffer;
}

std::shared_ptr<GpuBuffer> VulkanContext::CreateIndexBuffer(const void* data, uint32_t byteSize) {
    return CreateBuffer({byteSize, sizeof(uint32_t), RHIResourceUsage::IndexBuffer, "IndexBuffer"}, data);
}

std::shared_ptr<GpuBuffer> VulkanContext::CreateBuffer(const RHIBufferDesc& desc, const void* initialData) {
    if (!m_Impl || !m_Impl->device || desc.size == 0)
        return nullptr;
    auto result = CreateVulkanBuffer(m_Impl->device, m_Impl->physicalDevice, desc,
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     m_Impl->deferredReleases, initialData);
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuBuffer>(result));
    return result;
}

std::shared_ptr<GpuBufferView> VulkanContext::CreateBufferView(const std::shared_ptr<GpuBuffer>& buffer,
                                                               const RHIBufferViewDesc& desc) {
    auto native = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
    if (!native)
        return nullptr;
    auto view = std::make_shared<VulkanBufferView>();
    view->buffer = buffer;
    view->desc = desc;
    view->nativeBuffer = native;
    return view;
}

bool VulkanContext::UpdateBuffer(const std::shared_ptr<GpuBuffer>& buffer, uint64_t offset, const void* data,
                                 uint64_t size) {
    auto native = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
    if (!m_Impl || !native || !data || offset + size > native->desc.size)
        return false;
    void* mapped = nullptr;
    if (vkMapMemory(m_Impl->device, native->memory, offset, size, 0, &mapped) != VK_SUCCESS)
        return false;
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(m_Impl->device, native->memory);
    return true;
}

std::shared_ptr<GpuShader> VulkanContext::CreateShader(const std::string&, const std::string&, const std::string&,
                                                       const VertexElement*, uint32_t) {
    Logger::Error("[Vulkan] Runtime source shader creation is unsupported; use Slang SPIR-V bytecode");
    return nullptr;
}

std::shared_ptr<GpuShader> VulkanContext::CreateShaderFromBytecode(const void* vsBytecode, size_t vsSize,
                                                                   const void* psBytecode, size_t psSize,
                                                                   const VertexElement* layout, uint32_t layoutCount) {
    if (!m_Impl || !m_Impl->device || !vsBytecode || !psBytecode || vsSize == 0 || psSize == 0)
        return nullptr;
    auto shader = MakeShader(m_Impl->device, m_Impl->deferredReleases, vsBytecode, vsSize, &VulkanShader::vertexModule);
    if (!shader)
        return nullptr;
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = psSize;
    createInfo.pCode = static_cast<const uint32_t*>(psBytecode);
    if (vkCreateShaderModule(m_Impl->device, &createInfo, nullptr, &shader->pixelModule) != VK_SUCCESS) {
        return nullptr;
    }
    if (layout && layoutCount)
        shader->vertexLayout.assign(layout, layout + layoutCount);
    shader->vertexBytecode.assign(static_cast<const uint8_t*>(vsBytecode),
                                  static_cast<const uint8_t*>(vsBytecode) + vsSize);
    shader->pixelBytecode.assign(static_cast<const uint8_t*>(psBytecode),
                                 static_cast<const uint8_t*>(psBytecode) + psSize);
    ReflectSpirvStage(vsBytecode, vsSize, ShaderStageVertex, shader->reflection);
    ReflectSpirvStage(psBytecode, psSize, ShaderStagePixel, shader->reflection);
    if (!CreateShaderLayouts(m_Impl->device, m_Impl->bindlessDescriptorSetLayout, *shader)) {
        Logger::Error("[Vulkan] Failed to create shader descriptor/pipeline layout");
        return nullptr;
    }
    return shader;
}

std::shared_ptr<GpuShader> VulkanContext::CreateComputeShaderFromBytecode(const void* bytecode, size_t byteSize) {
    if (!m_Impl || !m_Impl->device || !bytecode || byteSize == 0)
        return nullptr;
    auto shader =
        MakeShader(m_Impl->device, m_Impl->deferredReleases, bytecode, byteSize, &VulkanShader::computeModule);
    if (!shader)
        return nullptr;
    shader->computeBytecode.assign(static_cast<const uint8_t*>(bytecode),
                                   static_cast<const uint8_t*>(bytecode) + byteSize);
    ReflectSpirvStage(bytecode, byteSize, ShaderStageCompute, shader->reflection);
    if (!CreateShaderLayouts(m_Impl->device, m_Impl->bindlessDescriptorSetLayout, *shader)) {
        Logger::Error("[Vulkan] Failed to create compute shader descriptor/pipeline layout");
        return nullptr;
    }
    return shader;
}

std::shared_ptr<GpuTexture> VulkanContext::UploadTexture2D(const void* rgba8Data, int width, int height) {
    if (!rgba8Data || width <= 0 || height <= 0)
        return nullptr;
    RHITextureDesc desc{};
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.format = RHIFormat::RGBA8UNorm;
    desc.usage = RHIResourceUsage::ShaderResource | RHIResourceUsage::CopyDestination;
    RHITextureSubresourceData data{};
    data.data = rgba8Data;
    data.rowPitch = desc.width * 4;
    data.slicePitch = data.rowPitch * desc.height;
    return UploadTexture(desc, &data, 1);
}

std::shared_ptr<GpuTexture> VulkanContext::UploadTexture(const RHITextureDesc& desc,
                                                         const RHITextureSubresourceData* data,
                                                         uint32_t subresourceCount) {
    const auto blockLayout = GetTextureBlockLayout(desc.format);
    if (!m_Impl || !m_Impl->device || !data || subresourceCount == 0 || !data[0].data || !blockLayout ||
        subresourceCount != desc.mipLevels * desc.arrayLayers) {
        Logger::Warn("[Vulkan] UploadTexture requires complete RGBA8/BC1/BC3/D32 mip and array-layer data");
        return nullptr;
    }
    RHITextureDesc uploadDesc = desc;
    uploadDesc.usage = uploadDesc.usage | RHIResourceUsage::CopyDestination;
    auto texture = std::dynamic_pointer_cast<VulkanTexture>(CreateTexture(uploadDesc));
    if (!texture || !texture->image)
        return nullptr;

    std::vector<uint64_t> offsets(subresourceCount, 0);
    std::vector<VkBufferImageCopy> copies;
    copies.reserve(subresourceCount);
    std::vector<bool> seenSubresources(static_cast<size_t>(desc.mipLevels) * desc.arrayLayers, false);
    uint64_t uploadSize = 0;
    for (uint32_t i = 0; i < subresourceCount; ++i) {
        const auto& src = data[i];
        if (!src.data || src.mipLevel >= desc.mipLevels || src.arrayLayer >= desc.arrayLayers)
            return nullptr;
        const size_t subresourceIndex = static_cast<size_t>(src.arrayLayer) * desc.mipLevels + src.mipLevel;
        if (seenSubresources[subresourceIndex])
            return nullptr;
        seenSubresources[subresourceIndex] = true;
        const uint32_t mipWidth = (std::max)(1u, desc.width >> src.mipLevel);
        const uint32_t mipHeight = (std::max)(1u, desc.height >> src.mipLevel);
        const uint32_t blockColumns = (mipWidth + blockLayout->width - 1u) / blockLayout->width;
        const uint32_t blockRows = (mipHeight + blockLayout->height - 1u) / blockLayout->height;
        const uint32_t tightRowPitch = blockColumns * blockLayout->bytes;
        const uint32_t tightSlicePitch = tightRowPitch * blockRows;
        const uint32_t sourceRowPitch = src.rowPitch ? src.rowPitch : tightRowPitch;
        const uint64_t requiredSourceBytes = static_cast<uint64_t>(blockRows - 1u) * sourceRowPitch + tightRowPitch;
        if (sourceRowPitch < tightRowPitch || (src.slicePitch && src.slicePitch < requiredSourceBytes))
            return nullptr;
        const uint64_t alignment = blockLayout->bytes;
        uploadSize = (uploadSize + alignment - 1u) & ~(alignment - 1u);
        offsets[i] = uploadSize;
        uploadSize += tightSlicePitch;

        VkBufferImageCopy copy{};
        copy.bufferOffset = offsets[i];
        copy.imageSubresource.aspectMask = AspectMask(desc.format);
        copy.imageSubresource.mipLevel = src.mipLevel;
        copy.imageSubresource.baseArrayLayer = src.arrayLayer;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {mipWidth, mipHeight, 1};
        copies.push_back(copy);
    }
    if (std::find(seenSubresources.begin(), seenSubresources.end(), false) != seenSubresources.end())
        return nullptr;
    auto staging = std::dynamic_pointer_cast<VulkanBuffer>(
        CreateBuffer({static_cast<uint32_t>(uploadSize), 1, RHIResourceUsage::CopySource, "TextureUpload"}, nullptr));
    if (!staging || !staging->buffer)
        return nullptr;

    void* mapped = nullptr;
    if (vkMapMemory(m_Impl->device, staging->memory, 0, uploadSize, 0, &mapped) != VK_SUCCESS)
        return nullptr;
    auto* destination = static_cast<uint8_t*>(mapped);
    for (uint32_t i = 0; i < subresourceCount; ++i) {
        const auto& src = data[i];
        const uint32_t mipWidth = (std::max)(1u, desc.width >> src.mipLevel);
        const uint32_t mipHeight = (std::max)(1u, desc.height >> src.mipLevel);
        const uint32_t blockColumns = (mipWidth + blockLayout->width - 1u) / blockLayout->width;
        const uint32_t blockRows = (mipHeight + blockLayout->height - 1u) / blockLayout->height;
        const uint32_t tightRowPitch = blockColumns * blockLayout->bytes;
        const uint32_t sourcePitch = src.rowPitch ? src.rowPitch : tightRowPitch;
        const uint8_t* source = static_cast<const uint8_t*>(src.data);
        uint8_t* target = destination + offsets[i];
        for (uint32_t y = 0; y < blockRows; ++y) {
            std::memcpy(target + static_cast<uint64_t>(y) * tightRowPitch,
                        source + static_cast<uint64_t>(y) * sourcePitch, tightRowPitch);
        }
    }
    vkUnmapMemory(m_Impl->device, staging->memory);

    auto recordUpload = [&](VkCommandBuffer cmd) {
        ImageBarrier(cmd, texture->image, texture->desc.format, RHIResourceState::Undefined,
                     RHIResourceState::CopyDestination);
        vkCmdCopyBufferToImage(cmd, staging->buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(copies.size()), copies.data());
        ImageBarrier(cmd, texture->image, texture->desc.format, RHIResourceState::CopyDestination,
                     RHIResourceState::ShaderResource);
        texture->layout = ToLayout(RHIResourceState::ShaderResource, texture->desc.format);
    };

    // Material and UI assets can be uploaded lazily while a RenderGraph raster pass has dynamic rendering open.
    // Transfer barriers and vkCmdCopyBufferToImage are illegal inside that scope, so publish uploads through a
    // separate one-time command buffer. Queue ordering makes the texture visible to the frame command buffer that is
    // submitted later; waiting here also makes the staging lifetime unambiguous during the one-time startup upload.
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = m_Impl->commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_Impl->device, &alloc, &cmd) != VK_SUCCESS)
        return nullptr;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    recordUpload(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    const VkResult submitted = vkQueueSubmit(m_Impl->graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    if (submitted == VK_SUCCESS)
        vkQueueWaitIdle(m_Impl->graphicsQueue);
    vkFreeCommandBuffers(m_Impl->device, m_Impl->commandPool, 1, &cmd);
    if (submitted != VK_SUCCESS) {
        Logger::Error("[Vulkan] vkQueueSubmit failed during texture upload: ", VkResultName(submitted));
    }
    if (submitted != VK_SUCCESS)
        return nullptr;
    return texture;
}

std::shared_ptr<GpuTexture> VulkanContext::CreateTexture(const RHITextureDesc& desc) {
    if (!m_Impl || !m_Impl->device || desc.width == 0 || desc.height == 0)
        return nullptr;
    auto result = std::make_shared<VulkanTexture>();
    result->device = m_Impl->device;
    result->deferredReleases = m_Impl->deferredReleases;
    result->desc = desc;
    result->isCube = desc.cube;
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.flags = desc.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = ToVulkanFormat(desc.format);
    create.extent = {desc.width, desc.height, 1};
    create.mipLevels = desc.mipLevels;
    create.arrayLayers = desc.arrayLayers;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = ToVulkanImageUsage(desc.usage);
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (create.format == VK_FORMAT_UNDEFINED || create.usage == 0 ||
        vkCreateImage(m_Impl->device, &create, nullptr, &result->image) != VK_SUCCESS) {
        return nullptr;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(m_Impl->device, result->image, &req);
    const uint32_t memoryType =
        FindMemoryType(m_Impl->physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX)
        return nullptr;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(m_Impl->device, &alloc, nullptr, &result->memory) != VK_SUCCESS)
        return nullptr;
    vkBindImageMemory(m_Impl->device, result->image, result->memory, 0);
    CommitRHIResourceAccounting(std::static_pointer_cast<GpuTexture>(result));
    return result;
}

std::shared_ptr<GpuTextureView> VulkanContext::CreateTextureView(const std::shared_ptr<GpuTexture>& texture,
                                                                 const RHITextureViewDesc& desc) {
    auto native = std::dynamic_pointer_cast<VulkanTexture>(texture);
    if (!m_Impl || !native || !native->image)
        return nullptr;
    RHITextureViewDesc resolved = desc;
    if (resolved.mipCount == 0 || resolved.firstMip + resolved.mipCount > native->desc.mipLevels) {
        if (resolved.firstMip >= native->desc.mipLevels)
            return nullptr;
        resolved.mipCount = native->desc.mipLevels - resolved.firstMip;
    }
    if (resolved.layerCount == 0 || resolved.firstLayer + resolved.layerCount > native->desc.arrayLayers) {
        if (resolved.firstLayer >= native->desc.arrayLayers)
            return nullptr;
        resolved.layerCount = native->desc.arrayLayers - resolved.firstLayer;
    }
    auto view = std::make_shared<VulkanTextureView>();
    view->device = m_Impl->device;
    view->deferredReleases = m_Impl->deferredReleases;
    view->texture = texture;
    view->desc = resolved;
    view->imageLayout = HasUsage(resolved.usage, RHIResourceUsage::UnorderedAccess) ? VK_IMAGE_LAYOUT_GENERAL
                        : HasUsage(resolved.usage, RHIResourceUsage::RenderTarget)
                            ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                            : (HasUsage(resolved.usage, RHIResourceUsage::DepthStencil)
                                   ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                   : ToLayout(RHIResourceState::ShaderResource, native->desc.format));
    view->aspectMask = ViewAspectMask(native->desc.format, resolved.usage);
    VkImageViewCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    create.image = native->image;
    create.viewType = ToVulkanImageViewType(native->desc, resolved);
    create.format = ToVulkanFormat(native->desc.format);
    create.subresourceRange.aspectMask = view->aspectMask;
    create.subresourceRange.baseMipLevel = resolved.firstMip;
    create.subresourceRange.levelCount = resolved.mipCount;
    create.subresourceRange.baseArrayLayer = resolved.firstLayer;
    create.subresourceRange.layerCount = resolved.layerCount;
    if (vkCreateImageView(m_Impl->device, &create, nullptr, &view->imageView) != VK_SUCCESS) {
        return nullptr;
    }
    view->imguiSampler = m_Impl->defaultSampler;
    if (HasUsage(resolved.usage, RHIResourceUsage::ShaderResource) && m_Impl->descriptorIndexingSupported &&
        m_Impl->bindlessDescriptorSet && m_Impl->bindlessAllocator) {
        view->bindlessIndex = m_Impl->bindlessAllocator->Allocate();
        if (view->bindlessIndex == UINT32_MAX)
            return view;
        view->bindlessAllocator = m_Impl->bindlessAllocator;
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = view->imageView;
        imageInfo.imageLayout = view->imageLayout;
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_Impl->bindlessDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = view->bindlessIndex;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_Impl->device, 1, &write, 0, nullptr);
    }
    return view;
}

std::shared_ptr<GpuSampler> VulkanContext::CreateSampler(const RHISamplerDesc& desc) {
    if (!m_Impl || !m_Impl->device)
        return nullptr;
    auto sampler = std::make_shared<VulkanSampler>();
    sampler->device = m_Impl->device;
    sampler->deferredReleases = m_Impl->deferredReleases;
    VkSamplerCreateInfo create{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    create.magFilter = desc.filter == RHIFilter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    create.minFilter = create.magFilter;
    create.mipmapMode =
        desc.filter == RHIFilter::Point ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    auto address = [](RHIAddressMode mode) {
        return mode == RHIAddressMode::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                             : (mode == RHIAddressMode::Border ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                                                                               : VK_SAMPLER_ADDRESS_MODE_REPEAT);
    };
    create.addressModeU = address(desc.addressU);
    create.addressModeV = address(desc.addressV);
    create.addressModeW = address(desc.addressW);
    if (desc.filter == RHIFilter::ComparisonLinear) {
        create.compareEnable = VK_TRUE;
        create.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    }
    create.minLod = 0.0f;
    create.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(m_Impl->device, &create, nullptr, &sampler->sampler) != VK_SUCCESS)
        return nullptr;
    return sampler;
}

std::shared_ptr<GpuGraphicsPipeline> VulkanContext::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (!m_Impl || !m_Impl->device || !desc.shader)
        return nullptr;
    auto shader = std::dynamic_pointer_cast<VulkanShader>(desc.shader);
    if (!shader || !shader->vertexModule || !shader->pixelModule || !shader->pipelineLayout)
        return nullptr;

    auto pipeline = std::make_shared<VulkanGraphicsPipeline>();
    pipeline->device = m_Impl->device;
    pipeline->deferredReleases = m_Impl->deferredReleases;
    pipeline->desc = desc;
    pipeline->layout = shader->pipelineLayout;
    pipeline->topology = ToVulkanTopology(desc.topology);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader->vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader->pixelModule;
    stages[1].pName = "main";

    std::vector<VkVertexInputAttributeDescription> attributes;
    VkVertexInputBindingDescription vertexBinding{};
    uint32_t vertexStride = 0;
    if (!shader->vertexLayout.empty()) {
        attributes.reserve(shader->vertexLayout.size());
        for (uint32_t i = 0; i < shader->vertexLayout.size(); ++i) {
            const auto& element = shader->vertexLayout[i];
            attributes.push_back({i, 0, ToVulkanVertexFormat(element.format), element.offset});
            vertexStride = (std::max)(vertexStride, element.offset + VertexFormatSize(element.format));
        }
        vertexBinding.binding = 0;
        vertexBinding.stride = vertexStride;
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = vertexStride ? 1u : 0u;
    vertexInput.pVertexBindingDescriptions = vertexStride ? &vertexBinding : nullptr;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = pipeline->topology;
    inputAssembly.primitiveRestartEnable =
        desc.topology == RHIPrimitiveTopology::LineStrip || desc.topology == RHIPrimitiveTopology::TriangleStrip;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode =
        desc.rasterizer.fillMode == RHIFillMode::Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    raster.cullMode =
        desc.rasterizer.cullMode == RHICullMode::None
            ? VK_CULL_MODE_NONE
            : (desc.rasterizer.cullMode == RHICullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
    raster.frontFace = ToVulkanFrontFace(desc.rasterizer.frontFace);
    raster.depthBiasEnable = desc.rasterizer.depthBias != 0 || desc.rasterizer.depthBiasClamp != 0.0f ||
                             desc.rasterizer.slopeScaledDepthBias != 0.0f;
    raster.depthBiasConstantFactor = static_cast<float>(desc.rasterizer.depthBias);
    raster.depthBiasClamp = desc.rasterizer.depthBiasClamp;
    raster.depthBiasSlopeFactor = desc.rasterizer.slopeScaledDepthBias;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.alphaToCoverageEnable = desc.blend.alphaToCoverageEnable;

    std::vector<VkPipelineColorBlendAttachmentState> colorAttachments;
    colorAttachments.reserve(std::max<size_t>(desc.colorFormats.size(), 1));
    const size_t attachmentCount = std::max<size_t>(desc.colorFormats.size(), 1);
    for (size_t i = 0; i < desc.colorFormats.size(); ++i) {
        const auto& state = desc.blend.attachments.empty()
                                ? RHIBlendAttachmentState{}
                                : desc.blend.attachments[(std::min)(i, desc.blend.attachments.size() - 1)];
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = state.blendEnable;
        blend.srcColorBlendFactor = ToVulkanBlendFactor(state.srcColorFactor);
        blend.dstColorBlendFactor = ToVulkanBlendFactor(state.dstColorFactor);
        blend.colorBlendOp = ToVulkanBlendOp(state.colorOp);
        blend.srcAlphaBlendFactor = ToVulkanBlendFactor(state.srcAlphaFactor);
        blend.dstAlphaBlendFactor = ToVulkanBlendFactor(state.dstAlphaFactor);
        blend.alphaBlendOp = ToVulkanBlendOp(state.alphaOp);
        blend.colorWriteMask = ToVulkanColorMask(state.colorWriteMask);
        colorAttachments.push_back(blend);
    }
    (void)attachmentCount;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.logicOpEnable = VK_FALSE;
    colorBlend.attachmentCount = static_cast<uint32_t>(colorAttachments.size());
    colorBlend.pAttachments = colorAttachments.data();
    std::copy(std::begin(desc.blend.blendConstants), std::end(desc.blend.blendConstants), colorBlend.blendConstants);

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable;
    depthStencil.depthCompareOp = ToVulkanCompare(desc.depthStencil.depthCompareOp);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = desc.depthStencil.stencilEnable;
    depthStencil.front = ToVulkanStencilFace(desc.depthStencil.frontFace, desc.depthStencil);
    depthStencil.back = ToVulkanStencilFace(desc.depthStencil.backFace, desc.depthStencil);

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamic.pDynamicStates = dynamicStates;

    pipeline->colorFormats.clear();
    pipeline->colorFormats.reserve(desc.colorFormats.size());
    for (RHIFormat format : desc.colorFormats) {
        VkFormat native = ToVulkanFormat(format);
        if (native == VK_FORMAT_UNDEFINED)
            return nullptr;
        pipeline->colorFormats.push_back(native);
    }
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = static_cast<uint32_t>(pipeline->colorFormats.size());
    rendering.pColorAttachmentFormats = pipeline->colorFormats.data();
    rendering.depthAttachmentFormat =
        desc.depthFormat == RHIFormat::Unknown ? VK_FORMAT_UNDEFINED : ToVulkanFormat(desc.depthFormat);
    rendering.stencilAttachmentFormat =
        desc.depthFormat == RHIFormat::D24S8 ? ToVulkanFormat(desc.depthFormat) : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    create.pNext = &rendering;
    create.stageCount = static_cast<uint32_t>(std::size(stages));
    create.pStages = stages;
    create.pVertexInputState = &vertexInput;
    create.pInputAssemblyState = &inputAssembly;
    create.pViewportState = &viewport;
    create.pRasterizationState = &raster;
    create.pMultisampleState = &multisample;
    create.pDepthStencilState = &depthStencil;
    create.pColorBlendState = &colorBlend;
    create.pDynamicState = &dynamic;
    create.layout = shader->pipelineLayout;
    create.renderPass = VK_NULL_HANDLE;
    create.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Impl->device, m_Impl->pipelineCache, 1, &create, nullptr, &pipeline->pipeline) !=
        VK_SUCCESS) {
        Logger::Error("[Vulkan] vkCreateGraphicsPipelines failed");
        return nullptr;
    }
    m_Impl->pipelineCacheDirty = true;
    return pipeline;
}

std::shared_ptr<GpuComputePipeline> VulkanContext::CreateComputePipeline(const ComputePipelineDesc& desc) {
    if (!m_Impl || !m_Impl->device || !desc.shader)
        return nullptr;
    auto shader = std::dynamic_pointer_cast<VulkanShader>(desc.shader);
    if (!shader || !shader->computeModule || !shader->pipelineLayout)
        return nullptr;
    auto pipeline = std::make_shared<VulkanComputePipeline>();
    pipeline->device = m_Impl->device;
    pipeline->deferredReleases = m_Impl->deferredReleases;
    pipeline->desc = desc;
    pipeline->layout = shader->pipelineLayout;
    VkComputePipelineCreateInfo create{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    create.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    create.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    create.stage.module = shader->computeModule;
    create.stage.pName = "main";
    create.layout = shader->pipelineLayout;
    if (vkCreateComputePipelines(m_Impl->device, m_Impl->pipelineCache, 1, &create, nullptr, &pipeline->pipeline) !=
        VK_SUCCESS) {
        Logger::Error("[Vulkan] vkCreateComputePipelines failed");
        return nullptr;
    }
    m_Impl->pipelineCacheDirty = true;
    return pipeline;
}

std::shared_ptr<GpuBindGroup> VulkanContext::CreateBindGroup(const std::shared_ptr<GpuShader>& shader) {
    if (!shader)
        return nullptr;
    auto group = std::make_shared<VulkanBindGroup>(shader);
    group->device = m_Impl ? m_Impl->device : VK_NULL_HANDLE;
    group->pool = m_Impl ? m_Impl->imguiDescriptorPool : VK_NULL_HANDLE;
    return group;
}

RHIDeviceCapabilities VulkanContext::GetCapabilities() const {
    RHIDeviceCapabilities caps{};
    caps.maxTextureDimension2D = 16384;
    caps.maxTextureArrayLayers = 2048;
    caps.maxColorAttachments = 8;
    caps.maxSamples = 1;
    caps.computeShaders = true;
    caps.storageTextures = true;
    caps.indirectDraw = true;
    caps.indirectDrawCount = m_Impl && m_Impl->indirectCountSupported;
    caps.indirectDispatch = true;
    caps.shaderDrawParameters = m_Impl && m_Impl->shaderDrawParametersSupported;
    caps.bindlessResources = m_Impl && m_Impl->descriptorIndexingSupported && m_Impl->bindlessDescriptorSet;
    caps.maxBindlessResources = caps.bindlessResources ? kBindlessTextureCapacity : 0;
    caps.modernDeferredFormats =
        IsFormatSupported(RHIFormat::RGBA16Float, RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess |
                                                      RHIResourceUsage::RenderTarget) &&
        IsFormatSupported(RHIFormat::RG32Float, RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess) &&
        IsFormatSupported(RHIFormat::RG16Float, RHIResourceUsage::ShaderResource | RHIResourceUsage::RenderTarget) &&
        IsFormatSupported(RHIFormat::R8UNorm, RHIResourceUsage::ShaderResource | RHIResourceUsage::UnorderedAccess);
    if (m_Impl && m_Impl->physicalDevice) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_Impl->physicalDevice, &properties);
        caps.timestampQueries =
            properties.limits.timestampComputeAndGraphics == VK_TRUE && properties.limits.timestampPeriod > 0.0f;
    }
    return caps;
}

std::shared_ptr<GpuTimestampQueryPool> VulkanContext::CreateTimestampQueryPool(uint32_t count) {
    if (!m_Impl || !m_Impl->device || count == 0 || !GetCapabilities().timestampQueries)
        return nullptr;
    auto result = std::make_shared<VulkanTimestampPool>();
    result->device = m_Impl->device;
    result->deferredReleases = m_Impl->deferredReleases;
    result->count = count;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_Impl->physicalDevice, &properties);
    result->timestampPeriod = properties.limits.timestampPeriod;
    VkQueryPoolCreateInfo create{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    create.queryType = VK_QUERY_TYPE_TIMESTAMP;
    create.queryCount = count;
    if (vkCreateQueryPool(m_Impl->device, &create, nullptr, &result->pool) != VK_SUCCESS)
        return nullptr;
    return result;
}

bool VulkanContext::IsFormatSupported(RHIFormat format, RHIResourceUsage usage) const {
    if (!m_Impl || !m_Impl->physicalDevice)
        return false;
    const VkFormat native = ToVulkanFormat(format);
    if (native == VK_FORMAT_UNDEFINED)
        return false;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(m_Impl->physicalDevice, native, &properties);
    const VkFormatFeatureFlags features = properties.optimalTilingFeatures;
    if (HasUsage(usage, RHIResourceUsage::ShaderResource) && !(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        return false;
    if (HasUsage(usage, RHIResourceUsage::RenderTarget) && !(features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
        return false;
    if (HasUsage(usage, RHIResourceUsage::DepthStencil) && !(features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        return false;
    if (HasUsage(usage, RHIResourceUsage::UnorderedAccess) && !(features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        return false;
    if (HasUsage(usage, RHIResourceUsage::CopySource) && !(features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        return false;
    if (HasUsage(usage, RHIResourceUsage::CopyDestination) && !(features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
        return false;
    return true;
}

std::shared_ptr<GpuReadbackTicket> VulkanContext::ReadbackBufferAsync(const std::shared_ptr<GpuBuffer>& buffer) {
    auto native = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
    if (!m_Impl || !m_Impl->device || !native || !native->buffer || native->desc.size == 0)
        return nullptr;
    const uint64_t readbackSize64 = native->desc.size;
    if (readbackSize64 > (std::numeric_limits<uint32_t>::max)()) {
        Logger::Error("[Vulkan] ReadbackBufferAsync failed: buffer too large");
        return nullptr;
    }
    RHIBufferDesc stagingDesc{};
    stagingDesc.size = static_cast<uint32_t>(readbackSize64);
    stagingDesc.stride = 1;
    stagingDesc.usage = RHIResourceUsage::CopyDestination;
    stagingDesc.debugName = "VulkanReadback";
    auto staging = CreateVulkanBuffer(
        m_Impl->device, m_Impl->physicalDevice, stagingDesc, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_Impl->deferredReleases);
    if (!staging || !staging->buffer)
        return nullptr;

    auto ticket = std::make_shared<VulkanReadbackTicket>();
    ticket->device = m_Impl->device;
    ticket->deferredReleases = m_Impl->deferredReleases;
    ticket->staging = staging;
    ticket->size = stagingDesc.size;

    auto recordCopy = [&](VkCommandBuffer cmd, bool sourceAlreadyCopySource) {
        if (!sourceAlreadyCopySource) {
            BufferBarrier(cmd, native->buffer, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource);
        }
        VkBufferCopy copy{};
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = stagingDesc.size;
        vkCmdCopyBuffer(cmd, native->buffer, staging->buffer, 1, &copy);
    };

    if (m_Impl->frameOpen) {
        recordCopy(m_Impl->commandBuffers[m_Impl->frameIndex], true);
        ticket->fence = m_Impl->inFlight[m_Impl->frameIndex];
        ticket->ownsFence = false;
        m_Impl->frameKeepAlive[m_Impl->frameIndex].push_back(staging);
        return ticket;
    }

    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = m_Impl->commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_Impl->device, &alloc, &cmd) != VK_SUCCESS)
        return nullptr;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    recordCopy(cmd, false);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_Impl->device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_Impl->device, m_Impl->commandPool, 1, &cmd);
        return nullptr;
    }
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    const VkResult submitted = vkQueueSubmit(m_Impl->graphicsQueue, 1, &submit, fence);
    if (submitted != VK_SUCCESS) {
        Logger::Error("[Vulkan] vkQueueSubmit failed during buffer readback: ", VkResultName(submitted));
        vkDestroyFence(m_Impl->device, fence, nullptr);
        vkFreeCommandBuffers(m_Impl->device, m_Impl->commandPool, 1, &cmd);
        return nullptr;
    }
    vkWaitForFences(m_Impl->device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkFreeCommandBuffers(m_Impl->device, m_Impl->commandPool, 1, &cmd);
    ticket->fence = fence;
    ticket->ownsFence = true;
    return ticket;
}

std::shared_ptr<GpuTextureReadbackTicket> VulkanContext::ReadbackTextureAsync(const std::shared_ptr<GpuTexture>&,
                                                                              const RHITextureRegion&) {
    return nullptr;
}

std::unique_ptr<IRenderContext> CreateVulkanContext() {
    return std::make_unique<VulkanContext>();
}

#endif
