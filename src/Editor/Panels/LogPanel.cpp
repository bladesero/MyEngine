#include "Editor/EditorPanels.h"

#include "Editor/EditorContext.h"
#include "Editor/EditorLogService.h"
#include "Editor/UI/EditorFontManager.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

LogPanel::LogPanel() : EditorPanel("log", "Log Output") {
}
void LogPanel::DrawContent() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("log"))
        return;

    auto* context = GetContext();
    auto* logs = context ? context->GetService<EditorLogService>() : nullptr;
    if (!logs)
        return;
    if (ImGui::Button("Clear"))
        logs->Clear();
    ImGui::SameLine();
    bool autoScroll = logs->IsAutoScroll();
    if (ImGui::Checkbox("Auto-scroll", &autoScroll))
        logs->SetAutoScroll(autoScroll);
    ImGui::Separator();
    ImGui::BeginChild("##LogScroll", {0, 0}, false, ImGuiWindowFlags_HorizontalScrollbar);
    ImFont* logFont = Editor::UI::EditorFontManager::GetActiveFont(Editor::UI::EditorFontRole::Log);
    if (logFont)
        ImGui::PushFont(logFont);
    for (const std::string& line : logs->Snapshot()) {
        const bool error =
            line.find("ShaderCompileError") != std::string::npos || line.find("[Error]") != std::string::npos;
        if (error)
            ImGui::PushStyleColor(ImGuiCol_Text, {1, 0.42f, 0.38f, 1});
        ImGui::TextUnformatted(line.c_str());
        if (error)
            ImGui::PopStyleColor();
    }
    if (logs->IsAutoScroll() && logs->ConsumeScrollRequest())
        ImGui::SetScrollHereY(1);
    if (logFont)
        ImGui::PopFont();
    ImGui::EndChild();
#endif
}
