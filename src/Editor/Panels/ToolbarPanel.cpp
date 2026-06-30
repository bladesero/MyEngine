#include "Editor/EditorPanels.h"

#include "Core/Logger.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorWidgets.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace EditorIcons = Editor::UI::EditorIcons;

ToolbarPanel::ToolbarPanel()
    : EditorPanel("toolbar", "Toolbar")
{}

int ToolbarPanel::GetWindowFlags() const
{
#if defined(MYENGINE_ENABLE_IMGUI)
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
#else
    return 0;
#endif
}

void ToolbarPanel::DrawContent()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (TryDrawScriptedBody("toolbar")) return;

    EditorContext* context = GetContext();
    if (!context) return;

    Editor::UI::EditorWidgets::ToolbarActionButton(
        *context, "play.start", EditorIcons::PlayStart, Editor::UI::EditorWidgetVariant::Accent);
    Editor::UI::EditorWidgets::ToolbarActionButton(
        *context, "play.stop", EditorIcons::PlayStop, Editor::UI::EditorWidgetVariant::Danger);
    Editor::UI::EditorWidgets::ToolbarActionButton(
        *context, "play.pause", EditorIcons::PlayPause, Editor::UI::EditorWidgetVariant::Warning);
    Editor::UI::EditorWidgets::ToolbarActionButton(
        *context, "play.step", EditorIcons::PlayStep, Editor::UI::EditorWidgetVariant::Neutral, false);

    EditorAngelScriptDomain* domain = context->GetEditorScriptDomain();
    if (domain && domain->IsLoaded() && domain->GetConfig().enableToolPanels) {
        for (const auto& item : domain->GetRegistry().GetToolbarItems()) {
            ImGui::SameLine();
            std::string error;
            const std::string stateKey = "toolbarItem:" + item.id;
            if (!domain->ExecuteExtension(item.callback, stateKey, *context, &error) &&
                !error.empty()) {
                Logger::Warn("[EditorScript] Toolbar item failed for ", item.id, ": ", error);
            }
        }
    }
#endif
}
