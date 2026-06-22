#pragma once

#include "Renderer/IRenderContext.h"
#include "Renderer/RHI/RHITypes.h"

#include <string>

class EditorContext;
class EditorProject;
class Engine;

namespace Editor::UI {

class EditorStatusBar {
public:
    float Draw(const EditorContext& context,
               const EditorProject* project,
               IRenderContext* renderContext,
               Engine* engine,
               float effectiveScale);

    static std::string FormatSelectedText(const EditorContext& context);
    static std::string FormatBackendText(IRenderContext* renderContext);
    static std::string FormatBackendText(RHIBackend backend);
    static std::string FormatProjectText(const EditorProject* project);
};

} // namespace Editor::UI
