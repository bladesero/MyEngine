#include "Editor/UI/EditorPropertyGrid.h"

#include "Editor/UI/EditorTheme.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

namespace Editor::UI::EditorPropertyGrid {

void Begin(const char* id, float labelWidth)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::PushID(id);
    ImGui::Columns(2, "##PropertyGrid", false);
    const float width = labelWidth > 0.0f
        ? labelWidth
        : ScaleToken(EditorStyleTokens{}.propertyLabelWidth, 1.0f);
    ImGui::SetColumnWidth(0, width);
#else
    (void)id;
    (void)labelWidth;
#endif
}

void End()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::Columns(1);
    ImGui::PopID();
#endif
}

bool BeginRow(const char* label)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::NextColumn();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushID(label);
    return true;
#else
    (void)label;
    return true;
#endif
}

void EndRow()
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::PopID();
    ImGui::NextColumn();
#endif
}

} // namespace Editor::UI::EditorPropertyGrid
