#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class LightingProbeAsset;
class Scene;
class IRHIDevice;
class IRHIFrameContext;
class IRHIReadbackService;

struct ProbeBakeProgress {
    uint32_t completedSteps = 0;
    uint32_t totalSteps = 0;
    std::string stage;
};

struct ProbeBakeResult {
    bool succeeded = false;
    bool cancelled = false;
    uint32_t reflectionProbeCount = 0;
    uint32_t shVolumeCount = 0;
    uint32_t shSampleCount = 0;
    uint32_t clippedReflectionProbes = 0;
    std::string error;
};

class ProbeBakeRenderer {
public:
    using ProgressCallback = std::function<void(const ProbeBakeProgress&)>;

    ProbeBakeRenderer(IRHIDevice* device = nullptr, IRHIFrameContext* frameContext = nullptr,
                      IRHIReadbackService* readbackService = nullptr)
        : m_Device(device), m_FrameContext(frameContext), m_ReadbackService(readbackService) {}

    ProbeBakeResult Bake(const Scene& scene, LightingProbeAsset& output, const ProgressCallback& progress = {},
                         const std::atomic<bool>* cancellation = nullptr) const;
    static uint64_t ComputeDependencyHash(const Scene& scene);

private:
    IRHIDevice* m_Device = nullptr;
    IRHIFrameContext* m_FrameContext = nullptr;
    IRHIReadbackService* m_ReadbackService = nullptr;
};
