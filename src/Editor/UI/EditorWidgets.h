#pragma once

#include "Editor/UI/EditorNotifications.h"
#include "Editor/UI/EditorStyleTokens.h"

class EditorContext;

namespace Editor::UI::EditorWidgets {

using MessageType = EditorNotificationType;

bool ToolbarActionButton(EditorContext& context, const char* actionID,
                         const char* icon = nullptr,
                         EditorWidgetVariant variant = EditorWidgetVariant::Neutral,
                         bool sameLineAfter = true);
bool IconButton(const char* id, const char* icon, const char* tooltip,
                bool enabled = true);
bool SectionHeader(const char* label, bool defaultOpen = true);
void BeginPropertyGrid(const char* id);
void EndPropertyGrid();
bool BeginPropertyRow(const char* label);
void EndPropertyRow();
void InlineMessage(EditorNotificationType type, const char* text);

} // namespace Editor::UI::EditorWidgets
