#include "Editor/EditorWorkspace.h"

#include "Project/ProjectConfig.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}
}

fs::path EditorWorkspace::DefaultSettingsPath() {
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA")) {
        return fs::path(appData) / "MyEngine" / "workspace.json";
    }
#else
    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / ".config" / "MyEngine" / "workspace.json";
    }
#endif
    return fs::temp_directory_path() / "MyEngine" / "workspace.json";
}

EditorWorkspace::EditorWorkspace(fs::path settingsPath)
    : m_SettingsPath(settingsPath.empty() ? DefaultSettingsPath() : std::move(settingsPath)) {}

bool EditorWorkspace::Load(std::string* error) {
    if (error) error->clear();
    m_RecentProjects.clear();
    std::error_code ec;
    if (!fs::exists(m_SettingsPath, ec)) return true;
    try {
        std::ifstream input(m_SettingsPath);
        nlohmann::json json;
        input >> json;
        const auto recent = json.find("recentProjects");
        if (recent != json.end() && recent->is_array()) {
            for (const auto& item : *recent) {
                if (!item.is_string()) continue;
                fs::path path = fs::absolute(item.get<std::string>(), ec).lexically_normal();
                if (!ec && fs::is_directory(path, ec) && !ec) m_RecentProjects.push_back(std::move(path));
                ec.clear();
            }
        }
    }
    catch (const std::exception& exception) {
        SetError(error, "failed to load workspace: " + std::string(exception.what()));
        return false;
    }
    return true;
}

bool EditorWorkspace::Save(std::string* error) const {
    if (error) error->clear();
    try {
        std::error_code ec;
        fs::create_directories(m_SettingsPath.parent_path(), ec);
        if (ec) {
            SetError(error, "failed to create workspace directory: " + ec.message());
            return false;
        }
        nlohmann::json json;
        json["version"] = 1;
        json["recentProjects"] = nlohmann::json::array();
        for (const auto& project : m_RecentProjects) {
            json["recentProjects"].push_back(project.string());
        }
        std::ofstream output(m_SettingsPath);
        if (!output) {
            SetError(error, "failed to write workspace: " + m_SettingsPath.string());
            return false;
        }
        output << json.dump(2) << '\n';
    }
    catch (const std::exception& exception) {
        SetError(error, "failed to save workspace: " + std::string(exception.what()));
        return false;
    }
    return true;
}

void EditorWorkspace::AddRecentProject(const fs::path& projectRoot) {
    std::error_code ec;
    const fs::path normalized = fs::absolute(projectRoot, ec).lexically_normal();
    if (ec) return;
    m_RecentProjects.erase(std::remove(m_RecentProjects.begin(), m_RecentProjects.end(), normalized),
                           m_RecentProjects.end());
    m_RecentProjects.insert(m_RecentProjects.begin(), normalized);
    if (m_RecentProjects.size() > 10) m_RecentProjects.resize(10);
}

bool EditorWorkspace::CreateProject(const fs::path& projectRoot,
                                    const std::string& projectName,
                                    std::string* error) {
    if (error) error->clear();
    if (projectName.empty()) {
        SetError(error, "project name must not be empty");
        return false;
    }
    std::error_code ec;
    const fs::path root = fs::absolute(projectRoot, ec).lexically_normal();
    if (ec || root.empty()) {
        SetError(error, "failed to resolve project directory");
        return false;
    }
    if (fs::exists(root / ProjectConfig::kFileName, ec)) {
        SetError(error, "a project manifest already exists in this directory");
        return false;
    }
    const fs::path scenes = root / "Content" / "Scenes";
    fs::create_directories(scenes, ec);
    if (ec) {
        SetError(error, "failed to create project directories: " + ec.message());
        return false;
    }
    const fs::path mainScene = scenes / "Main.scene.json";
    if (fs::exists(mainScene, ec)) {
        SetError(error, "default scene already exists: " + mainScene.string());
        return false;
    }
    nlohmann::json scene = {
        {"name", "Main"}, {"nextID", 1}, {"actors", nlohmann::json::array()}
    };
    std::ofstream sceneOutput(mainScene);
    if (!sceneOutput) {
        SetError(error, "failed to create default scene");
        return false;
    }
    sceneOutput << scene.dump(2) << '\n';
    sceneOutput.close();

    ProjectConfig config;
    if (!config.Open(root, true, error)) return false;
    config.SetName(projectName);
    if (!config.SetStartupScene(mainScene, error) || !config.Save(error)) return false;
    AddRecentProject(root);
    return Save(error);
}
