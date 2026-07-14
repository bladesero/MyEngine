#include "Editor/UI/EditorTheme.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace Editor::UI {

float ScaleToken(float value, float effectiveScale) {
    const float scale = effectiveScale > 0.0f ? effectiveScale : 1.0f;
    return value * scale;
}

void EditorThemeManager::Initialize(std::string themeID) {
    SetThemeID(std::move(themeID));
}

void EditorThemeManager::SetThemeID(std::string themeID) {
    m_Theme = CreateDefaultTheme();
    m_Theme.id = NormalizeThemeID(themeID);
}

void EditorThemeManager::Apply(float effectiveScale) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!ImGui::GetCurrentContext())
        return;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    const auto& t = m_Theme.tokens;

    style.WindowRounding = ScaleValue(t.windowRounding, effectiveScale);
    style.ChildRounding = ScaleValue(t.childRounding, effectiveScale);
    style.FrameRounding = ScaleValue(t.frameRounding, effectiveScale);
    style.PopupRounding = ScaleValue(t.popupRounding, effectiveScale);
    style.GrabRounding = ScaleValue(t.grabRounding, effectiveScale);
    style.TabRounding = ScaleValue(t.tabRounding, effectiveScale);
    style.WindowBorderSize = t.windowBorderSize;
    style.FrameBorderSize = t.frameBorderSize;
    style.ItemSpacing = {ScaleValue(t.itemSpacingX, effectiveScale), ScaleValue(t.itemSpacingY, effectiveScale)};
    style.FramePadding = {ScaleValue(t.framePaddingX, effectiveScale), ScaleValue(t.framePaddingY, effectiveScale)};
    style.WindowPadding = {ScaleValue(t.windowPaddingX, effectiveScale), ScaleValue(t.windowPaddingY, effectiveScale)};
    style.IndentSpacing = ScaleValue(t.indentSpacing, effectiveScale);

    style.Colors[ImGuiCol_WindowBg] = t.windowBg;
    style.Colors[ImGuiCol_ChildBg] = t.panelBg;
    style.Colors[ImGuiCol_Header] = t.header;
    style.Colors[ImGuiCol_HeaderHovered] = t.headerHovered;
    style.Colors[ImGuiCol_HeaderActive] = t.accent;
    style.Colors[ImGuiCol_Button] = t.header;
    style.Colors[ImGuiCol_ButtonHovered] = t.headerHovered;
    style.Colors[ImGuiCol_ButtonActive] = t.accent;
    style.Colors[ImGuiCol_CheckMark] = t.accentHovered;
    style.Colors[ImGuiCol_SliderGrab] = t.accent;
    style.Colors[ImGuiCol_SliderGrabActive] = t.accentHovered;
    style.Colors[ImGuiCol_Tab] = t.panelBg;
    style.Colors[ImGuiCol_TabHovered] = t.headerHovered;
    style.Colors[ImGuiCol_TabActive] = t.header;
    style.Colors[ImGuiCol_TextDisabled] = t.mutedText;

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
#else
    (void)effectiveScale;
#endif
}

EditorTheme EditorThemeManager::CreateDefaultTheme() {
    return {};
}

std::string EditorThemeManager::NormalizeThemeID(const std::string& themeID) {
    return themeID == "dark" || themeID.empty() ? "dark" : "dark";
}

float EditorThemeManager::ScaleValue(float value, float effectiveScale) {
    return ScaleToken(value, effectiveScale);
}

} // namespace Editor::UI
