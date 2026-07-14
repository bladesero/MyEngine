#include "Editor/EditorPanels.h"

#include "Core/Logger.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"
#include "Editor/UI/EditorNotifications.h"
#include "Editor/UI/EditorWidgets.h"

namespace {
std::string AreaToLayout(EditorScriptPanelArea area) {
    switch (area) {
    case EditorScriptPanelArea::Top:
        return "top";
    case EditorScriptPanelArea::Left:
        return "left";
    case EditorScriptPanelArea::Right:
        return "right";
    case EditorScriptPanelArea::BottomLeft:
        return "bottomLeft";
    case EditorScriptPanelArea::BottomCenter:
        return "bottomCenter";
    case EditorScriptPanelArea::Center:
    default:
        return "center";
    }
}
} // namespace

ScriptedToolPanel::ScriptedToolPanel(EditorScriptPanelSpec spec)
    : EditorPanel(std::move(spec.id), std::move(spec.title)), m_Spec(std::move(spec)) {
}

std::string ScriptedToolPanel::GetDefaultDockArea() const {
    return AreaToLayout(m_Spec.area);
}

void ScriptedToolPanel::DrawContent() {
    EditorContext* context = GetContext();
    EditorAngelScriptDomain* domain = context ? context->GetEditorScriptDomain() : nullptr;
    if (!context || !domain || !domain->IsLoaded())
        return;

    std::string error;
    const std::string stateKey = "tool:" + GetID();
    if (!domain->ExecuteExtension(m_Spec.callback, stateKey, *context, &error) && !error.empty()) {
        Logger::Warn("[EditorScript] Tool panel failed for ", GetID(), ": ", error);
        Editor::UI::EditorWidgets::InlineMessage(Editor::UI::EditorNotificationType::Error, error.c_str());
    }
}
