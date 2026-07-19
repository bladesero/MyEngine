#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class LightingProbeAsset;
class Scene;

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

    ProbeBakeResult Bake(const Scene& scene, LightingProbeAsset& output, const ProgressCallback& progress = {},
                          const std::atomic<bool>* cancellation = nullptr) const;
    static uint64_t ComputeDependencyHash(const Scene& scene);
};
