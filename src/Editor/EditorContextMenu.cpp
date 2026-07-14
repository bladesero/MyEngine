#include "Editor/EditorContextMenu.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

EditorContextMenu::EditorContextMenu(const char* id) {
#if defined(MYENGINE_ENABLE_IMGUI)
    m_Open = ImGui::BeginPopup(id);
#endif
}

EditorContextMenu::~EditorContextMenu() {
    if (m_Open) {
#if defined(MYENGINE_ENABLE_IMGUI)
        ImGui::EndPopup();
#endif
    }
}

// --- Menu items -----------------------------------------------------------

bool EditorContextMenu::AddAction(const char* label, std::function<void()> callback, bool enabled) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (ImGui::MenuItem(label, nullptr, false, enabled)) {
        Close();
        if (callback)
            callback();
        return true;
    }
    return false;
#else
    (void)label;
    (void)enabled;
    if (callback)
        callback();
    return true;
#endif
}

void EditorContextMenu::AddSeparator() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::Separator();
#endif
}

void EditorContextMenu::Close() {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
#endif
    m_Open = false;
}

// --- Static helpers -------------------------------------------------------

void EditorContextMenu::OpenPopup(const char* id) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::OpenPopup(id);
#else
    (void)id;
#endif
}

bool EditorContextMenu::DetectItem(const char* id) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(id);
        return true;
    }
    return false;
#else
    (void)id;
    return false;
#endif
}

bool EditorContextMenu::DetectWindow(const char* id) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(id);
        return true;
    }
    return false;
#else
    (void)id;
    return false;
#endif
}
