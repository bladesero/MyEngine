#pragma once

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace Editor::UI::EditorViewportPolicy {

inline void BindNextModalToMainViewport()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (ImGuiViewport* viewport = ImGui::GetMainViewport())
        ImGui::SetNextWindowViewport(viewport->ID);
#endif
}

inline void BindNextPopupToCurrentViewport()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (ImGuiViewport* viewport = ImGui::GetWindowViewport())
        ImGui::SetNextWindowViewport(viewport->ID);
#endif
}

} // namespace Editor::UI::EditorViewportPolicy
