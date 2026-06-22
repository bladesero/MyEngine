#pragma once

#include "Editor/UI/EditorFontManager.h"

namespace Editor::UI::EditorIcons {

inline constexpr const char* New = "+";
inline constexpr const char* Open = "O";
inline constexpr const char* Save = "S";
inline constexpr const char* Settings = "*";
inline constexpr const char* Publish = "P";
inline constexpr const char* Undo = "<";
inline constexpr const char* Redo = ">";
inline constexpr const char* Recompile = "C";
inline constexpr const char* Remove = "X";

inline constexpr const char* PlayFA = "\xef\x81\x8b";
inline constexpr const char* StopFA = "\xef\x81\x8d";
inline constexpr const char* PauseFA = "\xef\x81\x8c";
inline constexpr const char* StepFA = "\xef\x81\x91";

inline const char* PickIcon(const char* icon, const char* fallback)
{
    return EditorFontManager::HasActiveFont(EditorFontRole::Icon) ? icon : fallback;
}

inline const char* PlayIcon() { return PickIcon(PlayFA, ">"); }
inline const char* StopIcon() { return PickIcon(StopFA, "[]"); }
inline const char* PauseIcon() { return PickIcon(PauseFA, "||"); }
inline const char* StepIcon() { return PickIcon(StepFA, "|>"); }

} // namespace Editor::UI::EditorIcons
