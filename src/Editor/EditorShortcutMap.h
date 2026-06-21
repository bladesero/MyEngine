#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

class EditorActionRegistry;
class EditorContext;

struct EditorShortcutChord {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool super = false;
    int key = 0;

    bool IsValid() const { return key != 0; }
    bool operator==(const EditorShortcutChord& other) const {
        return ctrl == other.ctrl && shift == other.shift &&
               alt == other.alt && super == other.super && key == other.key;
    }
};

class EditorShortcutMap {
public:
    static EditorShortcutMap CreateDefault();

    void Clear();
    void ResetDefaults();
    void SetShortcut(std::string actionId, const EditorShortcutChord& chord);
    void ClearShortcut(std::string_view actionId);
    const EditorShortcutChord* FindShortcut(std::string_view actionId) const;

    bool LoadOverrides(const nlohmann::json& json, std::string* warning = nullptr);
    nlohmann::json SaveOverrides() const;

    std::string FindConflict(std::string_view actionId,
                             const EditorShortcutChord& chord) const;
    bool DispatchChord(const EditorShortcutChord& chord,
                       EditorActionRegistry& actions,
                       EditorContext& context) const;
    bool Dispatch(EditorActionRegistry& actions, EditorContext& context) const;

    const std::unordered_map<std::string, EditorShortcutChord>& GetShortcuts() const {
        return m_Shortcuts;
    }

    static bool ParseChord(std::string_view text, EditorShortcutChord& chord,
                           std::string* error = nullptr);
    static std::string FormatChord(const EditorShortcutChord& chord);

private:
    std::unordered_map<std::string, EditorShortcutChord> m_Shortcuts;
};
