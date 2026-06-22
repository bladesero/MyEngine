#pragma once

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace Editor::UI {

enum class EditorWidgetVariant {
    Neutral,
    Accent,
    Danger,
    Warning,
};

struct EditorStyleTokens {
#if defined(MYENGINE_ENABLE_IMGUI)
    ImVec4 windowBg{0.10f, 0.105f, 0.11f, 1.0f};
    ImVec4 panelBg{0.13f, 0.135f, 0.145f, 1.0f};
    ImVec4 header{0.20f, 0.22f, 0.24f, 1.0f};
    ImVec4 headerHovered{0.25f, 0.28f, 0.30f, 1.0f};
    ImVec4 accent{0.23f, 0.48f, 0.78f, 1.0f};
    ImVec4 accentHovered{0.30f, 0.58f, 0.92f, 1.0f};
    ImVec4 warning{1.0f, 0.67f, 0.25f, 1.0f};
    ImVec4 warningHovered{1.0f, 0.76f, 0.36f, 1.0f};
    ImVec4 danger{0.92f, 0.25f, 0.23f, 1.0f};
    ImVec4 dangerHovered{1.0f, 0.35f, 0.30f, 1.0f};
    ImVec4 success{0.45f, 0.90f, 0.55f, 1.0f};
    ImVec4 mutedText{0.58f, 0.61f, 0.64f, 1.0f};
    ImVec4 dropHighlight{0.32f, 0.62f, 1.0f, 0.32f};
#endif
    float windowRounding = 0.0f;
    float childRounding = 4.0f;
    float frameRounding = 3.0f;
    float popupRounding = 4.0f;
    float grabRounding = 3.0f;
    float tabRounding = 3.0f;
    float windowBorderSize = 1.0f;
    float frameBorderSize = 0.0f;
    float itemSpacingX = 8.0f;
    float itemSpacingY = 5.0f;
    float framePaddingX = 8.0f;
    float framePaddingY = 4.0f;
    float windowPaddingX = 8.0f;
    float windowPaddingY = 8.0f;
    float indentSpacing = 18.0f;
    float menuBarHeight = 24.0f;
    float toolbarHeight = 36.0f;
    float statusBarHeight = 24.0f;
    float propertyLabelWidth = 140.0f;
};

float ScaleToken(float value, float effectiveScale);

} // namespace Editor::UI
