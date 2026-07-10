#include "Editor/EditorShortcutMap.h"

#include "Editor/EditorAction.h"
#include "Editor/EditorContext.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

void SetError(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
}

std::string Trim(std::string value)
{
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }).base(), value.end());
    return value;
}

std::string Lower(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::vector<std::string> SplitChord(std::string_view text)
{
    std::vector<std::string> parts;
    std::stringstream stream{std::string(text)};
    std::string part;
    while (std::getline(stream, part, '+')) {
        parts.push_back(Trim(part));
    }
    return parts;
}

const std::unordered_map<std::string, int>& KeyNameToCode()
{
    static const std::unordered_map<std::string, int> map = [] {
        std::unordered_map<std::string, int> values;
#if defined(MYENGINE_ENABLE_IMGUI)
        for (char ch = 'a'; ch <= 'z'; ++ch) {
            values[std::string(1, ch)] = ImGuiKey_A + (ch - 'a');
        }
        for (char ch = '0'; ch <= '9'; ++ch) {
            values[std::string(1, ch)] = ImGuiKey_0 + (ch - '0');
        }
        for (int i = 1; i <= 12; ++i) {
            values["f" + std::to_string(i)] = ImGuiKey_F1 + (i - 1);
        }
        values["space"] = ImGuiKey_Space;
        values["enter"] = ImGuiKey_Enter;
        values["return"] = ImGuiKey_Enter;
        values["escape"] = ImGuiKey_Escape;
        values["esc"] = ImGuiKey_Escape;
        values["delete"] = ImGuiKey_Delete;
        values["del"] = ImGuiKey_Delete;
        values["backspace"] = ImGuiKey_Backspace;
        values["tab"] = ImGuiKey_Tab;
        values["insert"] = ImGuiKey_Insert;
        values["home"] = ImGuiKey_Home;
        values["end"] = ImGuiKey_End;
        values["pageup"] = ImGuiKey_PageUp;
        values["pagedown"] = ImGuiKey_PageDown;
        values[","] = ImGuiKey_Comma;
        values["comma"] = ImGuiKey_Comma;
        values["."] = ImGuiKey_Period;
        values["period"] = ImGuiKey_Period;
        values["up"] = ImGuiKey_UpArrow;
        values["down"] = ImGuiKey_DownArrow;
        values["left"] = ImGuiKey_LeftArrow;
        values["right"] = ImGuiKey_RightArrow;
#endif
        return values;
    }();
    return map;
}

std::string KeyCodeToName(int key)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
        return std::string(1, static_cast<char>('A' + (key - ImGuiKey_A)));
    }
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
        return std::string(1, static_cast<char>('0' + (key - ImGuiKey_0)));
    }
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) {
        return "F" + std::to_string((key - ImGuiKey_F1) + 1);
    }
    switch (key) {
    case ImGuiKey_Space: return "Space";
    case ImGuiKey_Enter: return "Enter";
    case ImGuiKey_Escape: return "Escape";
    case ImGuiKey_Delete: return "Delete";
    case ImGuiKey_Backspace: return "Backspace";
    case ImGuiKey_Tab: return "Tab";
    case ImGuiKey_Insert: return "Insert";
    case ImGuiKey_Home: return "Home";
    case ImGuiKey_End: return "End";
    case ImGuiKey_PageUp: return "PageUp";
    case ImGuiKey_PageDown: return "PageDown";
    case ImGuiKey_Comma: return ",";
    case ImGuiKey_Period: return ".";
    case ImGuiKey_UpArrow: return "Up";
    case ImGuiKey_DownArrow: return "Down";
    case ImGuiKey_LeftArrow: return "Left";
    case ImGuiKey_RightArrow: return "Right";
    default: break;
    }
#endif
    return {};
}

bool ChordPressed(const EditorShortcutChord& chord)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!chord.IsValid()) return false;
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl != chord.ctrl || io.KeyShift != chord.shift ||
        io.KeyAlt != chord.alt || io.KeySuper != chord.super) {
        return false;
    }
    return ImGui::IsKeyPressed(static_cast<ImGuiKey>(chord.key), false);
#else
    (void)chord;
    return false;
#endif
}

void AddDefault(EditorShortcutMap& map, const char* action, const char* chord)
{
    EditorShortcutChord parsed;
    std::string ignored;
    if (EditorShortcutMap::ParseChord(chord, parsed, &ignored)) {
        map.SetShortcut(action, parsed);
    }
}

} // namespace

EditorShortcutMap EditorShortcutMap::CreateDefault()
{
    EditorShortcutMap map;
    AddDefault(map, "scene.new", "Ctrl+N");
    AddDefault(map, "scene.open", "Ctrl+O");
    AddDefault(map, "scene.save", "Ctrl+S");
    AddDefault(map, "project.settings", "Ctrl+,");
    AddDefault(map, "project.publish", "Ctrl+Shift+P");
    AddDefault(map, "edit.undo", "Ctrl+Z");
    AddDefault(map, "edit.redo", "Ctrl+Shift+Z");
    AddDefault(map, "edit.delete", "Delete");
    AddDefault(map, "edit.duplicate", "Ctrl+D");
    AddDefault(map, "edit.rename", "F2");
    AddDefault(map, "edit.copy", "Ctrl+C");
    AddDefault(map, "edit.paste", "Ctrl+V");
    AddDefault(map, "edit.selectAll", "Ctrl+A");
    AddDefault(map, "view.frameSelected", "F");
    AddDefault(map, "play.start", "F5");
    AddDefault(map, "play.stop", "Shift+F5");
    AddDefault(map, "play.pause", "F6");
    AddDefault(map, "play.step", "F10");
    AddDefault(map, "shader.recompile", "Ctrl+R");
    return map;
}

void EditorShortcutMap::Clear()
{
    m_Shortcuts.clear();
}

void EditorShortcutMap::ResetDefaults()
{
    *this = CreateDefault();
}

void EditorShortcutMap::SetShortcut(std::string actionId, const EditorShortcutChord& chord)
{
    if (actionId.empty()) return;
    m_Shortcuts[std::move(actionId)] = chord;
}

void EditorShortcutMap::ClearShortcut(std::string_view actionId)
{
    m_Shortcuts[std::string(actionId)] = {};
}

const EditorShortcutChord* EditorShortcutMap::FindShortcut(std::string_view actionId) const
{
    const auto it = m_Shortcuts.find(std::string(actionId));
    return it == m_Shortcuts.end() ? nullptr : &it->second;
}

bool EditorShortcutMap::LoadOverrides(const nlohmann::json& json, std::string* warning)
{
    if (warning) warning->clear();
    ResetDefaults();
    if (json.is_null()) return true;
    if (!json.is_object()) {
        SetError(warning, "workspace shortcuts must be an object");
        return false;
    }

    bool ok = true;
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (!it.value().is_string()) {
            ok = false;
            continue;
        }
        if (it.value().get<std::string>().empty()) {
            ClearShortcut(it.key());
            continue;
        }
        EditorShortcutChord chord;
        std::string error;
        if (!ParseChord(it.value().get<std::string>(), chord, &error)) {
            ok = false;
            continue;
        }
        SetShortcut(it.key(), chord);
    }
    if (!ok) SetError(warning, "some workspace shortcuts were ignored");
    return ok;
}

nlohmann::json EditorShortcutMap::SaveOverrides() const
{
    nlohmann::json json = nlohmann::json::object();
    for (const auto& [action, chord] : m_Shortcuts) {
        json[action] = chord.IsValid() ? FormatChord(chord) : std::string{};
    }
    return json;
}

std::string EditorShortcutMap::FindConflict(std::string_view actionId,
                                            const EditorShortcutChord& chord) const
{
    if (!chord.IsValid()) return {};
    for (const auto& [candidateId, candidateChord] : m_Shortcuts) {
        if (candidateId != actionId && candidateChord == chord) return candidateId;
    }
    return {};
}

bool EditorShortcutMap::Dispatch(EditorActionRegistry& actions, EditorContext& context) const
{
    for (EditorAction* action : actions.GetOrderedActions()) {
        if (!action) continue;
        const auto* chord = FindShortcut(action->GetID());
        if (!chord || !chord->IsValid()) continue;
        if (ChordPressed(*chord)) return DispatchChord(*chord, actions, context);
    }
    return false;
}

bool EditorShortcutMap::DispatchChord(const EditorShortcutChord& chord,
                                      EditorActionRegistry& actions,
                                      EditorContext& context) const
{
    if (!chord.IsValid()) return false;
    for (EditorAction* action : actions.GetOrderedActions()) {
        if (!action) continue;
        const auto* candidate = FindShortcut(action->GetID());
        if (!candidate || !candidate->IsValid() || !(*candidate == chord)) continue;
        if (!FindConflict(action->GetID(), *candidate).empty()) continue;
        return actions.Execute(action->GetID(), context);
    }
    return false;
}

bool EditorShortcutMap::ParseChord(std::string_view text, EditorShortcutChord& chord,
                                   std::string* error)
{
    chord = {};
    const auto parts = SplitChord(text);
    if (parts.empty()) {
        SetError(error, "shortcut is empty");
        return false;
    }
    for (const std::string& raw : parts) {
        if (raw.empty()) {
            SetError(error, "shortcut contains an empty part");
            return false;
        }
        const std::string part = Lower(raw);
        if (part == "ctrl" || part == "control") chord.ctrl = true;
        else if (part == "shift") chord.shift = true;
        else if (part == "alt" || part == "option") chord.alt = true;
        else if (part == "super" || part == "cmd" || part == "command" || part == "win") chord.super = true;
        else {
            if (chord.key != 0) {
                SetError(error, "shortcut has more than one key");
                return false;
            }
            const auto& keys = KeyNameToCode();
            const auto it = keys.find(part);
            if (it == keys.end()) {
                SetError(error, "unknown shortcut key: " + raw);
                return false;
            }
            chord.key = it->second;
        }
    }
    if (!chord.IsValid()) {
        SetError(error, "shortcut must include a key");
        return false;
    }
    return true;
}

std::string EditorShortcutMap::FormatChord(const EditorShortcutChord& chord)
{
    if (!chord.IsValid()) return {};
    std::string result;
    if (chord.ctrl) result += "Ctrl+";
    if (chord.shift) result += "Shift+";
    if (chord.alt) result += "Alt+";
    if (chord.super) result += "Super+";
    result += KeyCodeToName(chord.key);
    return result;
}
