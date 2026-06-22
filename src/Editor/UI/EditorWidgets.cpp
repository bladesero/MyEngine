#include "Editor/UI/EditorWidgets.h"

#include "Editor/EditorAction.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImGuiBackend.h"
#include "Editor/UI/EditorIcons.h"
#include "Editor/UI/EditorNotifications.h"
#include "Editor/UI/EditorPropertyGrid.h"
#include "Editor/UI/EditorTheme.h"
#include "Renderer/IRenderContext.h"
#include "Miscs/IconsManager.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <string>

namespace Editor::UI::EditorWidgets {
namespace {
#if defined(MYENGINE_ENABLE_IMGUI)
void PushButtonVariant(EditorWidgetVariant variant)
{
    const auto tokens = EditorThemeManager::CreateDefaultTheme().tokens;
    ImVec4 color = tokens.header;
    ImVec4 hovered = tokens.headerHovered;
    ImVec4 active = tokens.accent;
    switch (variant) {
        case EditorWidgetVariant::Accent:
            color = tokens.accent;
            hovered = tokens.accentHovered;
            active = tokens.accentHovered;
            break;
        case EditorWidgetVariant::Danger:
            color = tokens.danger;
            hovered = tokens.dangerHovered;
            active = tokens.dangerHovered;
            break;
        case EditorWidgetVariant::Warning:
            color = tokens.warning;
            hovered = tokens.warningHovered;
            active = tokens.warningHovered;
            break;
        default:
            break;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
}
#endif
}

bool ToolbarActionButton(EditorContext& context, const char* actionID,
                         const char* icon, EditorWidgetVariant variant,
                         bool sameLineAfter)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    EditorActionRegistry* actions = context.GetActionRegistry();
    EditorAction* action = actions ? actions->Find(actionID) : nullptr;
    if (!action) return false;

    const bool enabled = action->CanExecute(context);
    const std::string label = icon && icon[0] != '\0'
        ? std::string(EditorIcons::FallbackFor(icon)) + " " + action->GetLabel()
        : std::string(action->GetLabel());
    if (icon && icon[0] != '\0') {
        SvgIcon(context, icon, 16.0f);
        ImGui::SameLine();
    }
    PushButtonVariant(variant);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button(label.c_str());
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", action->GetLabel());
    }
    if (clicked && enabled) actions->Execute(actionID, context);
    if (sameLineAfter) ImGui::SameLine();
    return clicked && enabled;
#else
    (void)context;
    (void)actionID;
    (void)icon;
    (void)variant;
    (void)sameLineAfter;
    return false;
#endif
}

bool SvgIcon(EditorContext& context, const char* icon, float size, IconColor color)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!icon || icon[0] == '\0' || size <= 0.0f) return false;
    IRenderContext* renderContext = context.GetRenderContext();
    EditorImGuiBackend* backend = context.GetImGuiBackend();
    if (!renderContext || !backend) return false;
    GpuTextureView* view = IconsManager::Get().GetOrUpload(
        *renderContext, icon, static_cast<int>(size + 0.5f), color);
    void* texture = view ? backend->GetTextureId(view) : nullptr;
    if (!texture) return false;
    ImGui::Image(reinterpret_cast<ImTextureID>(texture), {size, size});
    return true;
#else
    (void)context;
    (void)icon;
    (void)size;
    (void)color;
    return false;
#endif
}

bool IconButton(EditorContext& context, const char* id, const char* icon,
                const char* tooltip, bool enabled)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const char* fallback = EditorIcons::FallbackFor(icon);
    ImGui::PushID(id ? id : "");
    ImGui::BeginDisabled(!enabled);
    bool clicked = false;
    const float size = ImGui::GetFrameHeight();
    const bool hasSvg = context.GetRenderContext() && context.GetImGuiBackend() &&
        IconsManager::Get().ResolveIconPath(icon ? icon : "").empty() == false;
    if (hasSvg) {
        clicked = ImGui::Button("##svgIconButton", {size, size});
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 cursorAfterButton = ImGui::GetCursorScreenPos();
        const float iconSize = ImGui::GetFontSize();
        ImGui::SetCursorScreenPos({
            min.x + (max.x - min.x - iconSize) * 0.5f,
            min.y + (max.y - min.y - iconSize) * 0.5f
        });
        SvgIcon(context, icon, iconSize);
        ImGui::SetCursorScreenPos(cursorAfterButton);
    } else {
        clicked = ImGui::SmallButton(fallback && fallback[0] ? fallback : "?");
    }
    ImGui::EndDisabled();
    if (tooltip && tooltip[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    return clicked && enabled;
#else
    (void)context;
    (void)id;
    (void)icon;
    (void)tooltip;
    (void)enabled;
    return false;
#endif
}

bool IconButton(const char* id, const char* icon, const char* tooltip, bool enabled)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGui::BeginDisabled(!enabled);
    const std::string label = std::string(EditorIcons::FallbackFor(icon)) + "##" + id;
    const bool clicked = ImGui::SmallButton(label.c_str());
    ImGui::EndDisabled();
    if (tooltip && tooltip[0] && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return clicked && enabled;
#else
    (void)id;
    (void)icon;
    (void)tooltip;
    (void)enabled;
    return false;
#endif
}

bool SectionHeader(const char* label, bool defaultOpen)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_AllowOverlap;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    return ImGui::CollapsingHeader(label, flags);
#else
    (void)label;
    (void)defaultOpen;
    return true;
#endif
}

void BeginPropertyGrid(const char* id)
{
    EditorPropertyGrid::Begin(id);
}

void EndPropertyGrid()
{
    EditorPropertyGrid::End();
}

bool BeginPropertyRow(const char* label)
{
    return EditorPropertyGrid::BeginRow(label);
}

void EndPropertyRow()
{
    EditorPropertyGrid::EndRow();
}

void InlineMessage(EditorNotificationType type, const char* text)
{
    EditorNotifications::Inline(type, text);
}

} // namespace Editor::UI::EditorWidgets
