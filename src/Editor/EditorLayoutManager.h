#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class EditorPanel;
struct EditorProjectState;

struct EditorPanelLayoutNode {
    std::string panelID;
    std::string title;
    std::string area;
};

struct EditorLayoutConfig {
    int version = 1;
    float toolbarHeightRatio = 0.04f;
    float leftWidthRatio = 0.14583333f;
    float rightWidthRatio = 0.16666667f;
    float bottomHeightRatio = 0.21153846f;
    std::vector<EditorPanelLayoutNode> panels;

    static EditorLayoutConfig CreateDefault();
    bool Validate(std::string* error = nullptr) const;
    static bool LoadFromFile(const std::filesystem::path& path,
                             EditorLayoutConfig& config,
                             std::string* error = nullptr);
    static bool SaveToFile(const std::filesystem::path& path,
                           const EditorLayoutConfig& config,
                           std::string* error = nullptr);
};

class EditorLayoutManager {
public:
    void OpenProject(const std::filesystem::path& projectRoot,
                     EditorProjectState& state,
                     const std::vector<std::unique_ptr<EditorPanel>>& panels);
    void CloseProject();
    void BeginDockSpace(const std::vector<std::unique_ptr<EditorPanel>>& panels,
                        float reservedTop = 0.0f,
                        float reservedBottom = 0.0f);
    void SaveCurrentLayout(EditorProjectState& state) const;
    void ResetToDefault(EditorProjectState& state);
    void LoadDefaultLayout(EditorProjectState& state);

    const std::filesystem::path& GetConfigPath() const { return m_ConfigPath; }
    const std::string& GetLastWarning() const { return m_LastWarning; }

private:
    void ApplyDefaultLayout(const std::vector<std::unique_ptr<EditorPanel>>& panels);
    void LoadUserIni(const EditorProjectState& state);
    std::string FindWindowName(const std::vector<std::unique_ptr<EditorPanel>>& panels,
                               const std::string& panelID) const;

    std::filesystem::path m_ProjectRoot;
    std::filesystem::path m_ConfigPath;
    EditorLayoutConfig m_Config = EditorLayoutConfig::CreateDefault();
    std::string m_LastWarning;
    bool m_ProjectOpen = false;
    bool m_UserIniLoaded = false;
    bool m_ApplyDefaultNextFrame = false;
    float m_ReservedTop = 0.0f;
    float m_ReservedBottom = 0.0f;
};
