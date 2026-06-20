#pragma once

#include <filesystem>
#include <string>
#include <vector>

class EditorWorkspace {
public:
    explicit EditorWorkspace(std::filesystem::path settingsPath = {},
                             std::filesystem::path templateRoot = {});

    bool Load(std::string* error = nullptr);
    bool Save(std::string* error = nullptr) const;
    void AddRecentProject(const std::filesystem::path& projectRoot);
    void SetTemplateRoot(std::filesystem::path templateRoot);
    bool CreateProject(const std::filesystem::path& projectRoot,
                       const std::string& projectName,
                       std::string* error = nullptr);

    const std::vector<std::filesystem::path>& GetRecentProjects() const {
        return m_RecentProjects;
    }
    const std::filesystem::path& GetSettingsPath() const { return m_SettingsPath; }
    const std::filesystem::path& GetTemplateRoot() const { return m_TemplateRoot; }

private:
    static std::filesystem::path DefaultSettingsPath();

    std::filesystem::path m_SettingsPath;
    std::filesystem::path m_TemplateRoot;
    std::vector<std::filesystem::path> m_RecentProjects;
};
