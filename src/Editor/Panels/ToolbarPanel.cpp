#include "Editor/EditorPanels.h"

#include "Core/Engine.h"
#include "Editor/EditorAction.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorLayout.h"
#include "Editor/EditorProject.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace {
void DrawActionButton(EditorContext& context, const char* actionID)
{
    EditorActionRegistry* actions = context.GetActionRegistry();
    EditorAction* action = actions ? actions->Find(actionID) : nullptr;
    if (!action) return;

    ImGui::BeginDisabled(!action->CanExecute(context));
    if (ImGui::Button(action->GetLabel())) actions->Execute(actionID, context);
    ImGui::EndDisabled();
    ImGui::SameLine();
}
}

ToolbarPanel::ToolbarPanel()
    : EditorPanel("toolbar", "Toolbar")
{}

void ToolbarPanel::OnImGui()
{
    if (IsVisible()) DrawContent();
}

void ToolbarPanel::DrawContent()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    EditorContext* context = GetContext();
    if (!context) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const EditorPanelRect rect = EditorLayout::Compute(
        viewport->WorkPos.x, viewport->WorkPos.y,
        viewport->WorkSize.x, viewport->WorkSize.y).toolbar;
    ImGui::SetNextWindowPos({rect.x, rect.y});
    ImGui::SetNextWindowSize({rect.width, rect.height});

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##Toolbar", nullptr, flags)) {
        DrawActionButton(*context, "scene.new");
        DrawActionButton(*context, "scene.open");
        DrawActionButton(*context, "scene.save");
        DrawActionButton(*context, "project.settings");
        DrawActionButton(*context, "project.setStartup");
        DrawActionButton(*context, "project.publish");
        DrawActionButton(*context, "editor.runLua");
        DrawActionButton(*context, "edit.undo");
        DrawActionButton(*context, "edit.redo");

        DrawActionButton(*context, "play.start");
        DrawActionButton(*context, "play.stop");
        DrawActionButton(*context, "play.pause");
        DrawActionButton(*context, "play.resume");
        DrawActionButton(*context, "play.step");
        DrawActionButton(*context, "shader.recompile");
        if (context->GetEngine()) {
            const auto& stats = context->GetEngine()->GetFrameStats();
            ImGui::SameLine();
            ImGui::Text("FPS %.1f | %.2f ms", stats.fps, stats.smoothedFrameMs);
        }
        if (auto* project = context->GetProject()) {
            const std::string& startup = project->GetConfig().GetStartupScene();
            ImGui::SameLine();
            ImGui::TextDisabled("Startup: %s", startup.empty() ? "<not set>" : startup.c_str());
        }
    }
    ImGui::End();
#endif
}
