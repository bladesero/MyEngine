#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RuntimePerformanceSample {
    double frameMs = 0.0;
    double updateMs = 0.0;
    double renderMs = 0.0;
    double gpuMs = 0.0;
    uint64_t workingSetBytes = 0;
    uint32_t droppedFixedTicks = 0;
    bool gpuTimingAvailable = false;
    double renderSubmissionMs = 0.0;
    double shadowCpuMs = 0.0;
    double mainCpuMs = 0.0;
    double ssaoCpuMs = 0.0;
    double compositeCpuMs = 0.0;
    double renderGraphBuildMs = 0.0;
    double renderGraphPrepareMs = 0.0;
    double pipelinePrepareMs = 0.0;
    double renderGraphAddPassMs = 0.0;
    double renderGraphCompileMs = 0.0;
    double renderGraphEnsureResourcesMs = 0.0;
    double gpuScenePrepareMs = 0.0;
    double frameWaitMs = 0.0;
    double presentMs = 0.0;
};

struct RuntimePerformanceBudget {
    size_t warmupSamples = 0;
    size_t minimumSamples = 120;
    double maxP95FrameMs = 20.0;
    double maxP99FrameMs = 33.34;
    double maxFrameMs = 100.0;
    double maxP95GpuMs = 20.0;
    uint64_t maxWorkingSetGrowthBytes = 256ull * 1024ull * 1024ull;
    uint64_t maxDroppedFixedTicks = 0;
};

struct RuntimePerformanceSummary {
    size_t sampleCount = 0;
    double p50FrameMs = 0.0;
    double p95FrameMs = 0.0;
    double p99FrameMs = 0.0;
    double maxFrameMs = 0.0;
    double p95GpuMs = 0.0;
    double p95RenderSubmissionMs = 0.0;
    double p95ShadowCpuMs = 0.0;
    double p95MainCpuMs = 0.0;
    double p95RenderGraphBuildMs = 0.0;
    double p95RenderGraphPrepareMs = 0.0;
    double p95PipelinePrepareMs = 0.0;
    double p95RenderGraphAddPassMs = 0.0;
    double p95RenderGraphCompileMs = 0.0;
    double p95RenderGraphEnsureResourcesMs = 0.0;
    double p95GpuScenePrepareMs = 0.0;
    size_t gpuSampleCount = 0;
    uint64_t workingSetGrowthBytes = 0;
    uint64_t droppedFixedTicks = 0;
};

struct RuntimePerformanceReport {
    bool passed = false;
    RuntimePerformanceSummary summary;
    std::vector<std::string> violations;
    std::vector<RuntimePerformanceSample> samples;

    std::string ToJson() const;
};

uint64_t GetCurrentProcessWorkingSetBytes();

class RuntimePerformanceGate {
public:
    explicit RuntimePerformanceGate(RuntimePerformanceBudget budget = {});

    void AddSample(RuntimePerformanceSample sample);
    void Reset();
    RuntimePerformanceReport Evaluate() const;

    const RuntimePerformanceBudget& GetBudget() const { return m_Budget; }
    size_t SampleCount() const { return m_Samples.size(); }

private:
    RuntimePerformanceBudget m_Budget;
    std::vector<RuntimePerformanceSample> m_Samples;
};
