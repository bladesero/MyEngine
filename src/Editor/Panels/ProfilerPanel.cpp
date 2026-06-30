#include "Editor/EditorPanels.h"

#include "Core/Engine.h"
#include "Core/FrameStats.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorProfiler.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <string>

ProfilerPanel::ProfilerPanel()
    : EditorPanel("profiler", "Profiler")
{
}

void ProfilerPanel::DrawContent()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("profiler")) return;

    EditorContext* context = GetContext();
    EditorProfiler* profiler = context ? context->GetProfiler() : nullptr;
    const FrameStats emptyStats{};
    const FrameStats& stats = context && context->GetEngine()
        ? context->GetEngine()->GetFrameStats()
        : emptyStats;
    const RendererFrameStats& renderer = stats.renderer;

    ImGui::Text("Frame %.2f ms (%.1f FPS)  Update %.2f ms  Render %.2f ms",
                stats.smoothedFrameMs, stats.fps, stats.updateMs, stats.renderMs);
    ImGui::Text("Pass CPU: Shadow %.2f  Main %.2f  SSAO %.2f  Composite %.2f",
                renderer.shadowCpuMs, renderer.mainCpuMs,
                renderer.ssaoCpuMs, renderer.compositeCpuMs);
    ImGui::Text("Draws %u  Shadow %u  Main %u  Fullscreen %u  BindGroups %u",
                renderer.drawCalls, renderer.shadowDrawCalls,
                renderer.mainDrawCalls, renderer.fullscreenDrawCalls,
                renderer.bindGroupCreates);
    ImGui::Text("Texture uploads %u  %.2f MB  %.2f ms",
                renderer.textureUploads,
                static_cast<double>(renderer.textureUploadBytes) / (1024.0 * 1024.0),
                renderer.textureUploadMs);
    ImGui::Separator();

    if (!profiler) {
        ImGui::TextUnformatted("Profiler unavailable");
        return;
    }

    bool enabled = profiler->IsEnabled();
    if (ImGui::Checkbox("Record", &enabled)) profiler->SetEnabled(enabled);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) profiler->Clear();
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
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          {0.0f, 0.0f})) {
        ImGui::TableSetupColumn("Category");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Duration");
        ImGui::TableSetupColumn("Frame");
        ImGui::TableSetupColumn("Details");
        ImGui::TableHeadersRow();
        for (const EditorProfilerEvent& event : events) {
            if (!categoryFilter.empty() &&
                event.category.find(categoryFilter) == std::string::npos) {
                continue;
            }
            if (event.durationMs < static_cast<double>(m_MinDurationMs)) continue;
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
