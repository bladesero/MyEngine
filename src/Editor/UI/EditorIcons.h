#pragma once

#include "Editor/UI/EditorFontManager.h"

#include <cstring>

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
inline constexpr const char* Search = "search";
inline constexpr const char* Actor = "actor";
inline constexpr const char* Asset = "asset";
inline constexpr const char* Folder = "folder";
inline constexpr const char* File = "file";
inline constexpr const char* Mesh = "mesh";
inline constexpr const char* Material = "material";
inline constexpr const char* Texture = "texture";
inline constexpr const char* Shader = "shader";
inline constexpr const char* Script = "script";
inline constexpr const char* Scene = "scene";
inline constexpr const char* Prefab = "prefab";
inline constexpr const char* Camera = "camera";
inline constexpr const char* Audio = "audio";
inline constexpr const char* Input = "input";
inline constexpr const char* Physics = "physics";
inline constexpr const char* Light = "light";
inline constexpr const char* Renderer = "renderer";
inline constexpr const char* Info = "info";
inline constexpr const char* Warning = "warning";
inline constexpr const char* Error = "error";
inline constexpr const char* Success = "success";
inline constexpr const char* Refresh = "refresh";
inline constexpr const char* EngineEditor = "engine-editor";
inline constexpr const char* EnginePlayer = "engine-player";
inline constexpr const char* EngineCooker = "engine-cooker";
inline constexpr const char* PlayStart = "play-start";
inline constexpr const char* PlayStop = "play-stop";
inline constexpr const char* PlayPause = "play-pause";
inline constexpr const char* PlayStep = "play-step";
inline constexpr const char* SceneNew = "scene-new";
inline constexpr const char* SceneOpen = "scene-open";
inline constexpr const char* SceneSave = "scene-save";
inline constexpr const char* ProjectSettings = "project-settings";
inline constexpr const char* ProjectStartup = "project-startup";
inline constexpr const char* ProjectPublish = "project-publish";
inline constexpr const char* EditUndo = "edit-undo";
inline constexpr const char* EditRedo = "edit-redo";
inline constexpr const char* ShaderRecompile = "shader-recompile";

inline constexpr const char* PlayFA = "\xef\x81\x8b";
inline constexpr const char* StopFA = "\xef\x81\x8d";
inline constexpr const char* PauseFA = "\xef\x81\x8c";
inline constexpr const char* StepFA = "\xef\x81\x91";

inline const char* PickIcon(const char* icon, const char* fallback) {
    return EditorFontManager::HasActiveFont(EditorFontRole::Icon) ? icon : fallback;
}

inline const char* PlayIcon() {
    return PickIcon(PlayFA, ">");
}
inline const char* StopIcon() {
    return PickIcon(StopFA, "[]");
}
inline const char* PauseIcon() {
    return PickIcon(PauseFA, "||");
}
inline const char* StepIcon() {
    return PickIcon(StepFA, "|>");
}

inline bool Matches(const char* lhs, const char* rhs) {
    return lhs && rhs && std::strcmp(lhs, rhs) == 0;
}

inline const char* FallbackFor(const char* icon) {
    if (Matches(icon, PlayStart))
        return PlayIcon();
    if (Matches(icon, PlayStop))
        return StopIcon();
    if (Matches(icon, PlayPause))
        return PauseIcon();
    if (Matches(icon, PlayStep))
        return StepIcon();
    if (Matches(icon, Remove))
        return "X";
    if (Matches(icon, Search))
        return "?";
    if (Matches(icon, Actor))
        return "A";
    if (Matches(icon, Folder))
        return "[]";
    if (Matches(icon, File))
        return "F";
    if (Matches(icon, EngineEditor))
        return "E";
    if (Matches(icon, EnginePlayer))
        return "P";
    if (Matches(icon, EngineCooker))
        return "C";
    return icon ? icon : "";
}

} // namespace Editor::UI::EditorIcons
