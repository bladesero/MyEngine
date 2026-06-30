#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>

enum class EditorScriptCorePanelMode {
    Fallback,
    CppOnly,
    ScriptOnlyDebug,
};

struct EditorScriptConfig {
    bool enabled = true;
    bool allowProjectAppend = true;
    bool allowProjectOverrideCore = false;
    bool enableToolPanels = true;
    bool enableMenuExtensions = true;
    bool enableInspectorExtensions = true;
    bool enableContextMenuExtensions = true;
    EditorScriptCorePanelMode corePanelMode = EditorScriptCorePanelMode::Fallback;
    std::unordered_set<std::string> enabledCorePanels {"toolbar"};

    bool IsCorePanelEnabled(const std::string& panelID) const;
    static EditorScriptConfig LoadFromFile(const std::filesystem::path& path,
                                           std::string* error = nullptr);
};
