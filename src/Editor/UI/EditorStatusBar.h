#pragma once

#include <string>

class EditorContext;
class EditorProject;

namespace Editor::UI {

class EditorStatusBar {
public:
    float Draw(EditorContext& context, const EditorProject* project, float effectiveScale);

    static std::string FormatSelectedText(const EditorContext& context);
    static std::string FormatEditorModeText(const EditorContext& context);
    static std::string FormatSceneText(const EditorContext& context);
    static std::string FormatProjectText(const EditorProject* project);
};

} // namespace Editor::UI
