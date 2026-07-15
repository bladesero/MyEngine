#include "Editor/EditorPanel.h"

#include "Core/Logger.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorUI/EditorAngelScriptDomain.h"
#include "Editor/UI/EditorNotifications.h"
#include "Editor/UI/EditorWidgets.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

EditorPanel::EditorPanel(std::string id, std::string title) : m_ID(std::move(id)), m_Title(std::move(title)) {
}

std::string EditorPanel::GetStableWindowName() const {
    return m_Title + "###" + m_ID;
}

void EditorPanel::OnImGui() {
#if defined(MYENGINE_ENABLE_IMGUI)
    m_Focused = false;
    if (!m_Visible)
        return;
    bool open = true;
    BeforeBegin();
    const std::string windowName = GetStableWindowName();
    if (m_FocusRequested) {
        ImGui::SetNextWindowFocus();
        m_FocusRequested = false;
    }
    const bool canDraw = ImGui::Begin(windowName.c_str(), &open, GetWindowFlags());
    m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (canDraw && open)
        DrawContent();
    ImGui::End();
    AfterEnd();
    m_Visible = open;
#endif
}

void EditorPanel::RequestFocus() {
    m_Visible = true;
    m_FocusRequested = true;
}

bool EditorPanel::TryDrawScriptedBody(const char* panelID) {
    EditorContext* context = GetContext();
    EditorAngelScriptDomain* domain = context ? context->GetEditorScriptDomain() : nullptr;
    if (!domain || !domain->IsLoaded())
        return false;

    std::string error;
    const std::string id = panelID && panelID[0] ? panelID : m_ID;
    if (domain->ExecutePanelBody(id, *context, &error))
        return true;
    if (!error.empty()) {
        if (domain->IsScriptOnlyDebug()) {
            Editor::UI::EditorWidgets::InlineMessage(Editor::UI::EditorNotificationType::Error, error.c_str());
        }
        Logger::Warn("[EditorScript] Panel body failed for ", id, ": ", error);
    }
    return false;
}

void EditorPanel::RegisterContextMenuHandler(ContextMenuHandler handler) {
    if (handler)
        m_ContextMenuHandlers.push_back(std::move(handler));
}

void EditorPanel::ShowContextMenu(const ContextMenuContext& ctx, EditorContextMenu& menu) {
    for (auto& handler : m_ContextMenuHandlers)
        handler(ctx, menu);
}
