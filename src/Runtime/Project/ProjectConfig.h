#pragma once

#include "Project/PublishTargets.h"
#include "Project/FormatVersions.h"

#include <filesystem>
#include <string>
#include <string_view>

struct ProjectPublishSettings {
    std::string outputDirectory = "Builds";
    std::string target = PublishTargets::kDefaultTargetId;
};

struct ProjectInputSettings {
    std::string config = "Content/Config/Input.input.json";
};

struct ProjectGraphicsSettings {
    std::string backend = "d3d11";
    std::string renderPath = "forward";
};

class ProjectConfig {
public:
    static constexpr int kCurrentVersion = FormatVersions::Project;
    static constexpr const char* kFileName = "MyEngine.project.json";

    bool Open(std::filesystem::path projectRoot, bool allowMissing = false, std::string* error = nullptr);
    bool Save(std::string* error = nullptr);

    bool SetStartupScene(const std::filesystem::path& scenePath, std::string* error = nullptr);
    bool ResolveStartupScene(std::filesystem::path& resolved, std::string* error = nullptr) const;
    bool ResolveScenePath(const std::string& projectRelativePath, std::filesystem::path& resolved,
                          bool requireExists = true, std::string* error = nullptr) const;
    bool SetInputConfigPath(const std::filesystem::path& configPath, std::string* error = nullptr);
    bool ResolveInputConfigPath(std::filesystem::path& resolved, bool requireExists = false,
                                std::string* error = nullptr) const;

    int GetVersion() const { return m_Version; }
    const std::string& GetName() const { return m_Name; }
    const std::string& GetProjectId() const { return m_ProjectId; }
    const std::string& GetStartupScene() const { return m_StartupScene; }
    const std::filesystem::path& GetRoot() const { return m_Root; }
    const std::filesystem::path& GetManifestPath() const { return m_ManifestPath; }
    ProjectPublishSettings& GetPublishSettings() { return m_PublishSettings; }
    const ProjectPublishSettings& GetPublishSettings() const { return m_PublishSettings; }
    ProjectInputSettings& GetInputSettings() { return m_InputSettings; }
    const ProjectInputSettings& GetInputSettings() const { return m_InputSettings; }
    ProjectGraphicsSettings& GetGraphicsSettings() { return m_GraphicsSettings; }
    const ProjectGraphicsSettings& GetGraphicsSettings() const { return m_GraphicsSettings; }
    bool HasManifest() const { return m_HasManifest; }

    void SetName(std::string name) { m_Name = std::move(name); }
    static bool IsSupportedGraphicsBackend(std::string_view backend);
    static bool IsSupportedRenderPath(std::string_view renderPath);

private:
    static void SetError(std::string* error, std::string message);
    static bool IsWithin(const std::filesystem::path& path, const std::filesystem::path& parent);

    std::filesystem::path m_Root;
    std::filesystem::path m_ManifestPath;
    int m_Version = kCurrentVersion;
    std::string m_Name = "MyEngine";
    std::string m_ProjectId;
    std::string m_StartupScene;
    ProjectPublishSettings m_PublishSettings;
    ProjectInputSettings m_InputSettings;
    ProjectGraphicsSettings m_GraphicsSettings;
    bool m_HasManifest = false;
};
