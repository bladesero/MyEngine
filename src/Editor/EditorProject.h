#pragma once

#include "Project/ProjectConfig.h"

#include <filesystem>
#include <string>

struct EditorProjectState {
    std::string lastScenePath;
    std::string selectedAssetPath;
    std::string lastOpenDirectory;
    bool showToolbar = true;
    bool showSceneHierarchy = true;
    bool showViewport = true;
    bool showInspector = true;
    bool showAssetBrowser = true;
    bool showLog = true;
};

class EditorProject {
public:
    bool Open(std::filesystem::path root, bool allowMissingManifest = true);
    bool SaveState() const;
    bool LoadState();
    const std::filesystem::path& GetRoot() const { return m_Root; }
    const std::filesystem::path& GetContentRoot() const { return m_ContentRoot; }
    EditorProjectState& GetState() { return m_State; }
    const EditorProjectState& GetState() const { return m_State; }
    const std::string& GetLastScenePath() const { return m_State.lastScenePath; }
    void SetLastScenePath(std::string path) { m_State.lastScenePath = std::move(path); }
    ProjectConfig& GetConfig() { return m_Config; }
    const ProjectConfig& GetConfig() const { return m_Config; }
    const std::string& GetLastError() const { return m_LastError; }
private:
    std::filesystem::path m_Root, m_ContentRoot, m_StatePath;
    EditorProjectState m_State;
    ProjectConfig m_Config;
    std::string m_LastError;
};
