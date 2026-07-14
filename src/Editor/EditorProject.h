#pragma once

#include "Project/ProjectConfig.h"

#include <filesystem>
#include <string>
#include <unordered_map>

struct EditorProjectState {
    std::string lastScenePath;
    std::string selectedAssetPath;
    std::string lastOpenDirectory;
    std::string imguiLayoutIni;
    std::string activeLayoutName = "default";
    std::unordered_map<std::string, bool> panelVisibility;
    bool showToolbar = true;
    bool showSceneHierarchy = true;
    bool showViewport = true;
    bool showInspector = true;
    bool showAssetBrowser = true;
    bool showLog = true;

    bool IsPanelVisible(const std::string& panelID) const;
    void SetPanelVisible(const std::string& panelID, bool visible);
    void SyncLegacyPanelFields();
};

class EditorProject {
public:
    bool Open(std::filesystem::path root, bool allowMissingManifest = true);
    bool SaveState() const;
    bool LoadState(std::string* error = nullptr);
    const std::filesystem::path& GetRoot() const { return m_Root; }
    const std::filesystem::path& GetContentRoot() const { return m_ContentRoot; }
    EditorProjectState& GetState() { return m_State; }
    const EditorProjectState& GetState() const { return m_State; }
    const std::string& GetLastScenePath() const { return m_State.lastScenePath; }
    void SetLastScenePath(std::string path) { m_State.lastScenePath = std::move(path); }
    ProjectConfig& GetConfig() { return m_Config; }
    const ProjectConfig& GetConfig() const { return m_Config; }
    const std::string& GetLastError() const { return m_LastError; }
    const std::string& GetLastWarning() const { return m_LastWarning; }

private:
    std::filesystem::path m_Root, m_ContentRoot, m_StatePath;
    EditorProjectState m_State;
    ProjectConfig m_Config;
    std::string m_LastError;
    std::string m_LastWarning;
};
