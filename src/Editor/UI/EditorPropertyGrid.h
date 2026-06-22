#pragma once

namespace Editor::UI::EditorPropertyGrid {

void Begin(const char* id, float labelWidth = 0.0f);
void End();
bool BeginRow(const char* label);
void EndRow();

} // namespace Editor::UI::EditorPropertyGrid
