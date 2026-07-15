#pragma once

#include <cstdint>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace Editor::UI::EditorViewportPolicy {

inline void BindNextModalToMainViewport() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (ImGuiViewport* viewport = ImGui::GetMainViewport())
        ImGui::SetNextWindowViewport(viewport->ID);
#endif
}

inline uint32_t GetCurrentViewportID() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (ImGuiViewport* viewport = ImGui::GetWindowViewport())
        return viewport->ID;
    if (ImGuiViewport* viewport = ImGui::GetMainViewport())
        return viewport->ID;
#endif
    return 0;
}

inline void BindNextPopupToViewport(uint32_t viewportID) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (viewportID != 0)
        ImGui::SetNextWindowViewport(viewportID);
#else
    (void)viewportID;
#endif
}

inline void BindNextPopupToCurrentViewport() {
    BindNextPopupToViewport(GetCurrentViewportID());
}

} // namespace Editor::UI::EditorViewportPolicy
