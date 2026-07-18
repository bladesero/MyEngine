#include "RuntimePerformanceBudget.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <sstream>
#include <utility>

#if defined(MYENGINE_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

double Percentile(std::vector<double> values, double percentile) {
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const double rank = percentile * static_cast<double>(values.size() - 1);
    const size_t lower = static_cast<size_t>(std::floor(rank));
    const size_t upper = static_cast<size_t>(std::ceil(rank));
    if (lower == upper)
        return values[lower];
    const double fraction = rank - static_cast<double>(lower);
    return values[lower] + (values[upper] - values[lower]) * fraction;
}

void AddExceeded(std::vector<std::string>& violations, const char* metric, double observed, double limit) {
    std::ostringstream message;
    message << metric << "=" << observed << " exceeds budget=" << limit;
    violations.push_back(message.str());
}

} // namespace

RuntimePerformanceGate::RuntimePerformanceGate(RuntimePerformanceBudget budget) : m_Budget(std::move(budget)) {
}

void RuntimePerformanceGate::AddSample(RuntimePerformanceSample sample) {
    m_Samples.push_back(sample);
}

void RuntimePerformanceGate::Reset() {
    m_Samples.clear();
}

RuntimePerformanceReport RuntimePerformanceGate::Evaluate() const {
    RuntimePerformanceReport report;
    if (m_Samples.size() <= m_Budget.warmupSamples) {
        report.violations.push_back("no samples remain after warmup");
        return report;
    }

    const auto begin = m_Samples.begin() + static_cast<std::ptrdiff_t>(m_Budget.warmupSamples);
    const size_t count = static_cast<size_t>(m_Samples.end() - begin);
    report.samples.assign(begin, m_Samples.end());
    report.summary.sampleCount = count;
    if (count < m_Budget.minimumSamples) {
        report.violations.push_back("sample count is below the configured minimum");
    }

    std::vector<double> frameTimes;
    std::vector<double> gpuTimes;
    std::vector<double> renderSubmissionTimes;
    std::vector<double> shadowCpuTimes;
    std::vector<double> mainCpuTimes;
    std::vector<double> renderGraphBuildTimes;
    std::vector<double> renderGraphPrepareTimes;
    std::vector<double> gpuScenePrepareTimes;
    frameTimes.reserve(count);
    gpuTimes.reserve(count);
    renderSubmissionTimes.reserve(count);
    shadowCpuTimes.reserve(count);
    mainCpuTimes.reserve(count);
    renderGraphBuildTimes.reserve(count);
    renderGraphPrepareTimes.reserve(count);
    gpuScenePrepareTimes.reserve(count);
    const uint64_t baselineWorkingSet = begin->workingSetBytes;
    uint64_t maxWorkingSet = 0;
    for (auto it = begin; it != m_Samples.end(); ++it) {
        if (!std::isfinite(it->frameMs) || it->frameMs < 0.0 || !std::isfinite(it->gpuMs) || it->gpuMs < 0.0) {
            report.violations.push_back("sample contains a non-finite or negative duration");
            continue;
        }
        frameTimes.push_back(it->frameMs);
        if (it->gpuTimingAvailable)
            gpuTimes.push_back(it->gpuMs);
        renderSubmissionTimes.push_back(it->renderSubmissionMs);
        shadowCpuTimes.push_back(it->shadowCpuMs);
        mainCpuTimes.push_back(it->mainCpuMs);
        renderGraphBuildTimes.push_back(it->renderGraphBuildMs);
        renderGraphPrepareTimes.push_back(it->renderGraphPrepareMs);
        gpuScenePrepareTimes.push_back(it->gpuScenePrepareMs);
        maxWorkingSet = std::max(maxWorkingSet, it->workingSetBytes);
        report.summary.droppedFixedTicks += it->droppedFixedTicks;
    }
    if (frameTimes.empty()) {
        report.violations.push_back("no valid timing samples were recorded");
        return report;
    }

    report.summary.p50FrameMs = Percentile(frameTimes, 0.50);
    report.summary.p95FrameMs = Percentile(frameTimes, 0.95);
    report.summary.p99FrameMs = Percentile(frameTimes, 0.99);
    report.summary.maxFrameMs = *std::max_element(frameTimes.begin(), frameTimes.end());
    report.summary.gpuSampleCount = gpuTimes.size();
    report.summary.p95GpuMs = Percentile(gpuTimes, 0.95);
    report.summary.p95RenderSubmissionMs = Percentile(renderSubmissionTimes, 0.95);
    report.summary.p95ShadowCpuMs = Percentile(shadowCpuTimes, 0.95);
    report.summary.p95MainCpuMs = Percentile(mainCpuTimes, 0.95);
    report.summary.p95RenderGraphBuildMs = Percentile(renderGraphBuildTimes, 0.95);
    report.summary.p95RenderGraphPrepareMs = Percentile(renderGraphPrepareTimes, 0.95);
    report.summary.p95GpuScenePrepareMs = Percentile(gpuScenePrepareTimes, 0.95);
    report.summary.workingSetGrowthBytes = maxWorkingSet >= baselineWorkingSet ? maxWorkingSet - baselineWorkingSet : 0;

    if (report.summary.p95FrameMs > m_Budget.maxP95FrameMs)
        AddExceeded(report.violations, "p95FrameMs", report.summary.p95FrameMs, m_Budget.maxP95FrameMs);
    if (report.summary.p99FrameMs > m_Budget.maxP99FrameMs)
        AddExceeded(report.violations, "p99FrameMs", report.summary.p99FrameMs, m_Budget.maxP99FrameMs);
    if (report.summary.maxFrameMs > m_Budget.maxFrameMs)
        AddExceeded(report.violations, "maxFrameMs", report.summary.maxFrameMs, m_Budget.maxFrameMs);
    if (report.summary.gpuSampleCount > 0 && report.summary.p95GpuMs > m_Budget.maxP95GpuMs)
        AddExceeded(report.violations, "p95GpuMs", report.summary.p95GpuMs, m_Budget.maxP95GpuMs);
    if (report.summary.workingSetGrowthBytes > m_Budget.maxWorkingSetGrowthBytes)
        AddExceeded(report.violations, "workingSetGrowthBytes",
                    static_cast<double>(report.summary.workingSetGrowthBytes),
                    static_cast<double>(m_Budget.maxWorkingSetGrowthBytes));
    if (report.summary.droppedFixedTicks > m_Budget.maxDroppedFixedTicks)
        AddExceeded(report.violations, "droppedFixedTicks", static_cast<double>(report.summary.droppedFixedTicks),
                    static_cast<double>(m_Budget.maxDroppedFixedTicks));

    report.passed = report.violations.empty();
    return report;
}

std::string RuntimePerformanceReport::ToJson() const {
    nlohmann::json value = {{"passed", passed},
                            {"summary",
                             {{"sampleCount", summary.sampleCount},
                              {"p50FrameMs", summary.p50FrameMs},
                              {"p95FrameMs", summary.p95FrameMs},
                              {"p99FrameMs", summary.p99FrameMs},
                              {"maxFrameMs", summary.maxFrameMs},
                              {"p95GpuMs", summary.p95GpuMs},
                              {"p95RenderSubmissionMs", summary.p95RenderSubmissionMs},
                              {"p95ShadowCpuMs", summary.p95ShadowCpuMs},
                              {"p95MainCpuMs", summary.p95MainCpuMs},
                              {"p95RenderGraphBuildMs", summary.p95RenderGraphBuildMs},
                              {"p95RenderGraphPrepareMs", summary.p95RenderGraphPrepareMs},
                              {"p95GpuScenePrepareMs", summary.p95GpuScenePrepareMs},
                              {"gpuSampleCount", summary.gpuSampleCount},
                              {"workingSetGrowthBytes", summary.workingSetGrowthBytes},
                              {"droppedFixedTicks", summary.droppedFixedTicks}}},
                            {"violations", violations}};
    value["samples"] = nlohmann::json::array();
    for (const RuntimePerformanceSample& sample : samples) {
        value["samples"].push_back({{"frameMs", sample.frameMs},
                                    {"updateMs", sample.updateMs},
                                    {"renderMs", sample.renderMs},
                                    {"renderSubmissionMs", sample.renderSubmissionMs},
                                    {"shadowCpuMs", sample.shadowCpuMs},
                                    {"mainCpuMs", sample.mainCpuMs},
                                    {"ssaoCpuMs", sample.ssaoCpuMs},
                                    {"compositeCpuMs", sample.compositeCpuMs},
                                    {"renderGraphBuildMs", sample.renderGraphBuildMs},
                                    {"renderGraphPrepareMs", sample.renderGraphPrepareMs},
                                    {"gpuScenePrepareMs", sample.gpuScenePrepareMs},
                                    {"frameWaitMs", sample.frameWaitMs},
                                    {"presentMs", sample.presentMs},
                                    {"gpuMs", sample.gpuMs},
                                    {"workingSetBytes", sample.workingSetBytes},
                                    {"droppedFixedTicks", sample.droppedFixedTicks},
                                    {"gpuTimingAvailable", sample.gpuTimingAvailable}});
    }
    return value.dump(2);
}

uint64_t GetCurrentProcessWorkingSetBytes() {
#if defined(MYENGINE_PLATFORM_WINDOWS)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<uint64_t>(counters.WorkingSetSize);
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<uint64_t>(info.resident_size);
#elif defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return static_cast<uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
    return 0;
}
