#include "Editor/UI/EditorNotifications.h"

#include "Editor/UI/EditorTheme.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace Editor::UI {
namespace {
#if defined(MYENGINE_ENABLE_IMGUI)
ImVec4 MessageColor(EditorNotificationType type) {
    const auto tokens = EditorThemeManager::CreateDefaultTheme().tokens;
    switch (type) {
    case EditorNotificationType::Warning:
        return tokens.warning;
    case EditorNotificationType::Error:
        return tokens.dangerHovered;
    case EditorNotificationType::Success:
        return tokens.success;
    default:
        return tokens.mutedText;
    }
}
#endif
} // namespace

void EditorNotifications::Inline(EditorNotificationType type, const char* text) {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::TextColored(MessageColor(type), "%s", text ? text : "");
#else
    (void)type;
    (void)text;
#endif
}

} // namespace Editor::UI
