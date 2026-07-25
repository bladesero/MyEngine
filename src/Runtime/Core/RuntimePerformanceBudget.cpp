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
    std::vector<double> renderTimes;
    std::vector<double> gpuTimes;
    std::vector<double> renderSubmissionTimes;
    std::vector<double> shadowCpuTimes;
    std::vector<double> mainCpuTimes;
    std::vector<double> renderGraphBuildTimes;
    std::vector<double> renderGraphExecuteTimes;
    std::vector<double> renderGraphRecordTimes;
    std::vector<double> renderGraphPrepareTimes;
    std::vector<double> renderGraphFinalizeTimes;
    std::vector<double> sceneCollectTimes;
    std::vector<double> pipelinePrepareTimes;
    std::vector<double> renderGraphAddPassTimes;
    std::vector<double> renderGraphCompileTimes;
    std::vector<double> renderGraphEnsureResourcesTimes;
    std::vector<double> gpuScenePrepareTimes;
    std::vector<double> uploadQueueTimes;
    std::vector<double> frameWaitTimes;
    std::vector<double> presentTimes;
    std::vector<double> editorUiTimes;
    std::vector<double> editorUiBuildTimes;
    std::vector<double> editorUiSubmitTimes;
    std::vector<double> platformWindowsTimes;
    frameTimes.reserve(count);
    renderTimes.reserve(count);
    gpuTimes.reserve(count);
    renderSubmissionTimes.reserve(count);
    shadowCpuTimes.reserve(count);
    mainCpuTimes.reserve(count);
    renderGraphBuildTimes.reserve(count);
    renderGraphExecuteTimes.reserve(count);
    renderGraphRecordTimes.reserve(count);
    renderGraphPrepareTimes.reserve(count);
    renderGraphFinalizeTimes.reserve(count);
    sceneCollectTimes.reserve(count);
    pipelinePrepareTimes.reserve(count);
    renderGraphAddPassTimes.reserve(count);
    renderGraphCompileTimes.reserve(count);
    renderGraphEnsureResourcesTimes.reserve(count);
    gpuScenePrepareTimes.reserve(count);
    uploadQueueTimes.reserve(count);
    frameWaitTimes.reserve(count);
    presentTimes.reserve(count);
    editorUiTimes.reserve(count);
    editorUiBuildTimes.reserve(count);
    editorUiSubmitTimes.reserve(count);
    platformWindowsTimes.reserve(count);
    const uint64_t baselineWorkingSet = begin->workingSetBytes;
    uint64_t maxWorkingSet = 0;
    for (auto it = begin; it != m_Samples.end(); ++it) {
        if (!std::isfinite(it->frameMs) || it->frameMs < 0.0 || !std::isfinite(it->gpuMs) || it->gpuMs < 0.0) {
            report.violations.push_back("sample contains a non-finite or negative duration");
            continue;
        }
        frameTimes.push_back(it->frameMs);
        renderTimes.push_back(it->renderMs);
        if (it->gpuTimingAvailable)
            gpuTimes.push_back(it->gpuMs);
        renderSubmissionTimes.push_back(it->renderSubmissionMs);
        shadowCpuTimes.push_back(it->shadowCpuMs);
        mainCpuTimes.push_back(it->mainCpuMs);
        renderGraphBuildTimes.push_back(it->renderGraphBuildMs);
        renderGraphExecuteTimes.push_back(it->renderGraphExecuteMs);
        renderGraphRecordTimes.push_back(it->renderGraphRecordMs);
        renderGraphPrepareTimes.push_back(it->renderGraphPrepareMs);
        renderGraphFinalizeTimes.push_back(it->renderGraphFinalizeMs);
        sceneCollectTimes.push_back(it->sceneCollectMs);
        pipelinePrepareTimes.push_back(it->pipelinePrepareMs);
        renderGraphAddPassTimes.push_back(it->renderGraphAddPassMs);
        renderGraphCompileTimes.push_back(it->renderGraphCompileMs);
        renderGraphEnsureResourcesTimes.push_back(it->renderGraphEnsureResourcesMs);
        gpuScenePrepareTimes.push_back(it->gpuScenePrepareMs);
        uploadQueueTimes.push_back(it->uploadQueueMs);
        frameWaitTimes.push_back(it->frameWaitMs);
        presentTimes.push_back(it->presentMs);
        editorUiTimes.push_back(it->editorUiMs);
        editorUiBuildTimes.push_back(it->editorUiBuildMs);
        editorUiSubmitTimes.push_back(it->editorUiSubmitMs);
        platformWindowsTimes.push_back(it->platformWindowsMs);
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
    report.summary.p50RenderMs = Percentile(renderTimes, 0.50);
    report.summary.p95RenderMs = Percentile(renderTimes, 0.95);
    report.summary.gpuSampleCount = gpuTimes.size();
    report.summary.p95GpuMs = Percentile(gpuTimes, 0.95);
    report.summary.p95RenderSubmissionMs = Percentile(renderSubmissionTimes, 0.95);
    report.summary.p95ShadowCpuMs = Percentile(shadowCpuTimes, 0.95);
    report.summary.p95MainCpuMs = Percentile(mainCpuTimes, 0.95);
    report.summary.p50RenderGraphBuildMs = Percentile(renderGraphBuildTimes, 0.50);
    report.summary.p95RenderGraphBuildMs = Percentile(renderGraphBuildTimes, 0.95);
    report.summary.p50RenderGraphExecuteMs = Percentile(renderGraphExecuteTimes, 0.50);
    report.summary.p95RenderGraphExecuteMs = Percentile(renderGraphExecuteTimes, 0.95);
    report.summary.p95RenderGraphRecordMs = Percentile(renderGraphRecordTimes, 0.95);
    report.summary.p95RenderGraphPrepareMs = Percentile(renderGraphPrepareTimes, 0.95);
    report.summary.p95RenderGraphFinalizeMs = Percentile(renderGraphFinalizeTimes, 0.95);
    report.summary.p95SceneCollectMs = Percentile(sceneCollectTimes, 0.95);
    report.summary.p95PipelinePrepareMs = Percentile(pipelinePrepareTimes, 0.95);
    report.summary.p95RenderGraphAddPassMs = Percentile(renderGraphAddPassTimes, 0.95);
    report.summary.p95RenderGraphCompileMs = Percentile(renderGraphCompileTimes, 0.95);
    report.summary.p95RenderGraphEnsureResourcesMs = Percentile(renderGraphEnsureResourcesTimes, 0.95);
    report.summary.p95GpuScenePrepareMs = Percentile(gpuScenePrepareTimes, 0.95);
    report.summary.p95UploadQueueMs = Percentile(uploadQueueTimes, 0.95);
    report.summary.p95FrameWaitMs = Percentile(frameWaitTimes, 0.95);
    report.summary.p95PresentMs = Percentile(presentTimes, 0.95);
    report.summary.p50EditorUiMs = Percentile(editorUiTimes, 0.50);
    report.summary.p95EditorUiMs = Percentile(editorUiTimes, 0.95);
    report.summary.p95EditorUiBuildMs = Percentile(editorUiBuildTimes, 0.95);
    report.summary.p95EditorUiSubmitMs = Percentile(editorUiSubmitTimes, 0.95);
    report.summary.p95PlatformWindowsMs = Percentile(platformWindowsTimes, 0.95);
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
                              {"p50RenderMs", summary.p50RenderMs},
                              {"p95RenderMs", summary.p95RenderMs},
                              {"p95GpuMs", summary.p95GpuMs},
                              {"p95RenderSubmissionMs", summary.p95RenderSubmissionMs},
                              {"p95ShadowCpuMs", summary.p95ShadowCpuMs},
                              {"p95MainCpuMs", summary.p95MainCpuMs},
                              {"p50RenderGraphBuildMs", summary.p50RenderGraphBuildMs},
                              {"p95RenderGraphBuildMs", summary.p95RenderGraphBuildMs},
                              {"p50RenderGraphExecuteMs", summary.p50RenderGraphExecuteMs},
                              {"p95RenderGraphExecuteMs", summary.p95RenderGraphExecuteMs},
                              {"p95RenderGraphRecordMs", summary.p95RenderGraphRecordMs},
                              {"p95RenderGraphPrepareMs", summary.p95RenderGraphPrepareMs},
                              {"p95RenderGraphFinalizeMs", summary.p95RenderGraphFinalizeMs},
                              {"p95SceneCollectMs", summary.p95SceneCollectMs},
                              {"p95PipelinePrepareMs", summary.p95PipelinePrepareMs},
                              {"p95RenderGraphAddPassMs", summary.p95RenderGraphAddPassMs},
                              {"p95RenderGraphCompileMs", summary.p95RenderGraphCompileMs},
                              {"p95RenderGraphEnsureResourcesMs", summary.p95RenderGraphEnsureResourcesMs},
                              {"p95GpuScenePrepareMs", summary.p95GpuScenePrepareMs},
                              {"p95UploadQueueMs", summary.p95UploadQueueMs},
                              {"p95FrameWaitMs", summary.p95FrameWaitMs},
                              {"p95PresentMs", summary.p95PresentMs},
                              {"p50EditorUiMs", summary.p50EditorUiMs},
                              {"p95EditorUiMs", summary.p95EditorUiMs},
                              {"p95EditorUiBuildMs", summary.p95EditorUiBuildMs},
                              {"p95EditorUiSubmitMs", summary.p95EditorUiSubmitMs},
                              {"p95PlatformWindowsMs", summary.p95PlatformWindowsMs},
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
                                    {"renderGraphExecuteMs", sample.renderGraphExecuteMs},
                                    {"renderGraphRecordMs", sample.renderGraphRecordMs},
                                    {"renderGraphPrepareMs", sample.renderGraphPrepareMs},
                                    {"renderGraphFinalizeMs", sample.renderGraphFinalizeMs},
                                    {"sceneCollectMs", sample.sceneCollectMs},
                                    {"pipelinePrepareMs", sample.pipelinePrepareMs},
                                    {"renderGraphAddPassMs", sample.renderGraphAddPassMs},
                                    {"renderGraphCompileMs", sample.renderGraphCompileMs},
                                    {"renderGraphEnsureResourcesMs", sample.renderGraphEnsureResourcesMs},
                                    {"gpuScenePrepareMs", sample.gpuScenePrepareMs},
                                    {"uploadQueueMs", sample.uploadQueueMs},
                                    {"frameWaitMs", sample.frameWaitMs},
                                    {"presentMs", sample.presentMs},
                                    {"editorUiMs", sample.editorUiMs},
                                    {"editorUiBuildMs", sample.editorUiBuildMs},
                                    {"editorUiSubmitMs", sample.editorUiSubmitMs},
                                    {"platformWindowsMs", sample.platformWindowsMs},
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
