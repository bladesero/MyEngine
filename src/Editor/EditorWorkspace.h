#pragma once

#include "Editor/EditorShortcutMap.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class EditorWorkspace {
public:
    explicit EditorWorkspace(std::filesystem::path settingsPath = {}, std::filesystem::path templateRoot = {});

    bool Load(std::string* error = nullptr);
    bool Save(std::string* error = nullptr) const;
    void AddRecentProject(const std::filesystem::path& projectRoot);
    void SetTemplateRoot(std::filesystem::path templateRoot);
    bool CreateProject(const std::filesystem::path& projectRoot, const std::string& projectName,
                       std::string* error = nullptr);

    const std::vector<std::filesystem::path>& GetRecentProjects() const { return m_RecentProjects; }
    const std::filesystem::path& GetSettingsPath() const { return m_SettingsPath; }
    const std::filesystem::path& GetTemplateRoot() const { return m_TemplateRoot; }
    EditorShortcutMap& GetShortcuts() { return m_Shortcuts; }
    const EditorShortcutMap& GetShortcuts() const { return m_Shortcuts; }
    float GetUserUiScale() const { return m_UserUiScale; }
    void SetUserUiScale(float value);
    const std::string& GetEditorThemeId() const { return m_EditorThemeId; }
    void SetEditorThemeId(std::string value);
    void SetPanelStateValue(const std::string& panelID, const std::string& key, std::string value);
    std::optional<std::string> GetPanelStateValue(const std::string& panelID, const std::string& key) const;
    void ClearPanelState(const std::string& panelID);

private:
    static std::filesystem::path DefaultSettingsPath();

    std::filesystem::path m_SettingsPath;
    std::filesystem::path m_TemplateRoot;
    std::vector<std::filesystem::path> m_RecentProjects;
    EditorShortcutMap m_Shortcuts = EditorShortcutMap::CreateDefault();
    float m_UserUiScale = 1.0f;
    std::string m_EditorThemeId = "dark";
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_PanelState;
};
