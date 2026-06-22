#pragma once

namespace Editor::UI {

enum class EditorNotificationType {
    Info,
    Warning,
    Error,
    Success,
};

class EditorNotifications {
public:
    static void Inline(EditorNotificationType type, const char* text);
};

} // namespace Editor::UI
