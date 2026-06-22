#include "Editor/EditorPanel.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

EditorPanel::EditorPanel(std::string id, std::string title)
    : m_ID(std::move(id)), m_Title(std::move(title)) {}

std::string EditorPanel::GetStableWindowName() const
{
    return m_Title + "###" + m_ID;
}

void EditorPanel::OnImGui()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_Visible) return;
    bool open = true;
    BeforeBegin();
    const std::string windowName = GetStableWindowName();
    if (ImGui::Begin(windowName.c_str(), &open, GetWindowFlags())) DrawContent();
    ImGui::End();
    AfterEnd();
    m_Visible = open;
#endif
}

void EditorPanel::RegisterContextMenuHandler(ContextMenuHandler handler) {
    if (handler) m_ContextMenuHandlers.push_back(std::move(handler));
}

void EditorPanel::ShowContextMenu(const ContextMenuContext& ctx,
                                  EditorContextMenu& menu) {
    for (auto& handler : m_ContextMenuHandlers)
        handler(ctx, menu);
}
