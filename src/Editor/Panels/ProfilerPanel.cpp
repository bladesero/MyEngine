#include "Editor/EditorPanels.h"

#include "Core/Engine.h"
#include "Core/FrameStats.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorProfiler.h"
#include "Scene/Scene.h"
#include "Scene/WorldFrameScheduler.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <string>

ProfilerPanel::ProfilerPanel() : EditorPanel("profiler", "Profiler") {
}

void ProfilerPanel::DrawContent() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("profiler"))
        return;

    EditorContext* context = GetContext();
    EditorProfiler* profiler = context ? context->GetProfiler() : nullptr;
    const FrameStats emptyStats{};
    const FrameStats& stats = context && context->GetEngine() ? context->GetEngine()->GetFrameStats() : emptyStats;
    const RendererFrameStats& renderer = stats.renderer;

    ImGui::SeparatorText("Frame Timing");
    ImGui::Text("Frame %llu  %.1f FPS  Raw %.2f ms  Smoothed %.2f ms",
                static_cast<unsigned long long>(stats.frameNumber), stats.fps, stats.frameMs, stats.smoothedFrameMs);
    ImGui::Text("CPU: Update %.2f ms  Render %.2f ms", stats.updateMs, stats.renderMs);
    if (Scene* scene = context ? context->GetSimulationScene() : nullptr) {
        const WorldSchedulerStats& world = scene->GetFrameScheduler().GetStats();
        ImGui::Text("World fixed ticks %u  Dropped %u", world.fixedTicks, world.droppedFixedTicks);
        if (ImGui::TreeNode("World phases")) {
            for (size_t i = 0; i < kWorldPhaseCount; ++i) {
                ImGui::Text("%s: %.3f ms", WorldPhaseName(static_cast<WorldPhase>(i)), world.phaseMilliseconds[i]);
            }
            ImGui::TreePop();
        }
    }
    ImGui::Text("CPU submission %.2f ms  Graph build %.2f  Execute %.2f", renderer.renderSubmissionCpuMs,
                renderer.renderGraphBuildCpuMs, renderer.renderGraphExecuteCpuMs);
    ImGui::Text("Graph CPU: Pipeline %.2f  AddPass %.2f  Compile %.2f  Ensure %.2f", renderer.pipelinePrepareCpuMs,
                renderer.renderGraphAddPassCpuMs, renderer.renderGraphCompileCpuMs,
                renderer.renderGraphEnsureResourcesCpuMs);
    ImGui::Text("Topology cache %s  hits %llu  misses %llu", renderer.renderGraphTopologyCacheHit ? "hit" : "miss",
                static_cast<unsigned long long>(renderer.renderGraphTopologyCacheHits),
                static_cast<unsigned long long>(renderer.renderGraphTopologyCacheMisses));
    ImGui::Text("Render pass CPU: Shadow %.2f  Main %.2f  SSAO %.2f  Composite %.2f", renderer.shadowCpuMs,
                renderer.mainCpuMs, renderer.ssaoCpuMs, renderer.compositeCpuMs);
    if (renderer.gpuTimingAvailable) {
        ImGui::Text("Render pass GPU: Shadow %.2f  Main %.2f  SSAO %.2f  Composite %.2f", renderer.shadowGpuMs,
                    renderer.mainGpuMs, renderer.ssaoGpuMs, renderer.compositeGpuMs);
        if (!renderer.renderGraphPassGpuTimings.empty() && ImGui::TreeNode("RenderGraph GPU passes")) {
            for (const RenderGraphPassGpuTiming& pass : renderer.renderGraphPassGpuTimings)
                ImGui::Text("%s  %.3f ms", pass.name.c_str(), pass.gpuMs);
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("Render pass GPU timing unavailable");
    }

    ImGui::SeparatorText("Renderer");
    ImGui::Text("Draws %u  Shadow %u  Main %u  Fullscreen %u  Submeshes %u", renderer.drawCalls,
                renderer.shadowDrawCalls, renderer.mainDrawCalls, renderer.fullscreenDrawCalls, renderer.subMeshCount);
    ImGui::Text("Bind groups created %u", renderer.bindGroupCreates);
    ImGui::Text("Texture uploads %u  %.2f MB  %.2f ms", renderer.textureUploads,
                static_cast<double>(renderer.textureUploadBytes) / (1024.0 * 1024.0), renderer.textureUploadMs);
    if (renderer.gpuSceneCandidates || renderer.clusterCount) {
        ImGui::SeparatorText("Modern Deferred");
        ImGui::Text("GPU Scene upload %.2f MB  candidates %u  visible %u  HiZ occluded %u",
                    static_cast<double>(renderer.gpuSceneUploadBytes) / (1024.0 * 1024.0), renderer.gpuSceneCandidates,
                    renderer.gpuFrustumVisible, renderer.gpuHiZOccluded);
        ImGui::Text("GPU Scene prepare %.3f ms  material resolves %u  cache hits %u", renderer.gpuScenePrepareCpuMs,
                    renderer.gpuSceneMaterialResolves, renderer.gpuSceneMaterialCacheHits);
        ImGui::Text("GPU Scene textured materials %u", renderer.gpuSceneTexturedMaterials);
        ImGui::Text("Indirect draws %u  local lights %u  clusters %u  overflow %u", renderer.indirectDrawCount,
                    renderer.localLightCount, renderer.clusterCount, renderer.clusterOverflow);
        ImGui::Text("Bindless descriptors %u / %u", renderer.bindlessResourcesUsed, renderer.bindlessResourcesCapacity);
        if (renderer.rayTracingRequestedMask != 0) {
            ImGui::Text("RT requested/effective 0x%02X / 0x%02X  BLAS %u  TLAS instances %u",
                        renderer.rayTracingRequestedMask, renderer.rayTracingEffectiveMask,
                        renderer.rayTracingBlasCount, renderer.rayTracingTlasInstanceCount);
            ImGui::Text("RT AS %.2f MB  build/update record %.3f ms  TLAS %s",
                        static_cast<double>(renderer.rayTracingAccelerationStructureBytes) / (1024.0 * 1024.0),
                        renderer.rayTracingBuildCpuMs, renderer.rayTracingTlasUpdated ? "update" : "build");
            if (!renderer.rayTracingFallbackReason.empty())
                ImGui::TextDisabled("RT status: %s", renderer.rayTracingFallbackReason.c_str());
        }
        if (!renderer.historyResetReason.empty())
            ImGui::Text("History reset: %s", renderer.historyResetReason.c_str());
    }
    ImGui::SeparatorText("Editor Events");

    if (!profiler) {
        ImGui::TextUnformatted("Profiler unavailable");
        return;
    }

    bool enabled = profiler->IsEnabled();
    if (ImGui::Checkbox("Record", &enabled))
        profiler->SetEnabled(enabled);
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        profiler->Clear();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("Category", m_CategoryFilter, sizeof(m_CategoryFilter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Min ms", &m_MinDurationMs, 0.05f, 0.0f, 1000.0f, "%.2f");

    const std::string categoryFilter = m_CategoryFilter;
    auto events = profiler->Snapshot();
    std::reverse(events.begin(), events.end());
    if (ImGui::BeginTable("ProfilerEvents", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY,
                          {0.0f, 0.0f})) {
        ImGui::TableSetupColumn("Category");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Duration");
        ImGui::TableSetupColumn("Frame");
        ImGui::TableSetupColumn("Details");
        ImGui::TableHeadersRow();
        for (const EditorProfilerEvent& event : events) {
            if (!categoryFilter.empty() && event.category.find(categoryFilter) == std::string::npos) {
                continue;
            }
            if (event.durationMs < static_cast<double>(m_MinDurationMs))
                continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(event.category.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(event.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", event.durationMs);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu", static_cast<unsigned long long>(event.frameIndex));
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(event.details.c_str());
        }
        ImGui::EndTable();
    }
#endif
}
