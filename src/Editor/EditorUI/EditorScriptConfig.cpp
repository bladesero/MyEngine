#include "Editor/EditorUI/EditorScriptConfig.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace {
EditorScriptCorePanelMode ParseMode(const std::string& value) {
    if (value == "cppOnly")
        return EditorScriptCorePanelMode::CppOnly;
    if (value == "scriptOnlyDebug")
        return EditorScriptCorePanelMode::ScriptOnlyDebug;
    return EditorScriptCorePanelMode::Fallback;
}
} // namespace

bool EditorScriptConfig::IsCorePanelEnabled(const std::string& panelID) const {
    return enabledCorePanels.find(panelID) != enabledCorePanels.end();
}

EditorScriptConfig EditorScriptConfig::LoadFromFile(const std::filesystem::path& path, std::string* error) {
    EditorScriptConfig config;
    if (error)
        error->clear();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        return config;

    try {
        std::ifstream input(path, std::ios::binary);
        nlohmann::json json;
        input >> json;
        config.enabled = json.value("enabled", config.enabled);
        config.allowProjectAppend = json.value("allowProjectAppend", config.allowProjectAppend);
        config.allowProjectOverrideCore = json.value("allowProjectOverrideCore", config.allowProjectOverrideCore);
        config.enableToolPanels = json.value("enableToolPanels", config.enableToolPanels);
        config.enableMenuExtensions = json.value("enableMenuExtensions", config.enableMenuExtensions);
        config.enableInspectorExtensions = json.value("enableInspectorExtensions", config.enableInspectorExtensions);
        config.enableContextMenuExtensions =
            json.value("enableContextMenuExtensions", config.enableContextMenuExtensions);
        config.corePanelMode = ParseMode(json.value("corePanelMode", std::string{"fallback"}));

        if (json.contains("enabledCorePanels")) {
            config.enabledCorePanels.clear();
            for (const auto& value : json["enabledCorePanels"]) {
                if (value.is_string())
                    config.enabledCorePanels.insert(value.get<std::string>());
            }
        }
    } catch (const std::exception& ex) {
        if (error)
            *error = ex.what();
        return EditorScriptConfig{};
    }

    return config;
}
