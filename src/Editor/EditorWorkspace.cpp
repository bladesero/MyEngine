#include "Editor/EditorWorkspace.h"

#include "Editor/UI/EditorTheme.h"
#include "Editor/UI/EditorUIScaleManager.h"
#include "Input/InputActionMap.h"
#include "Project/ProjectConfig.h"
#include "Project/PublishTargets.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

fs::path FindTemplateRootFromCurrentPath() {
    fs::path cursor = fs::current_path();
    while (!cursor.empty()) {
        const fs::path candidate = cursor / "ProjectTemplates" / "Default";
        std::error_code ec;
        if (fs::is_directory(candidate, ec) && !ec) {
            return fs::absolute(candidate).lexically_normal();
        }
        const fs::path parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
    return {};
}

bool CopyTemplateTree(const fs::path& source, const fs::path& destination,
                      std::string* error) {
    std::error_code ec;
    if (!fs::is_directory(source, ec) || ec) {
        SetError(error, "project template root is missing: " + source.string());
        return false;
    }
    for (fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const fs::path relative = fs::relative(it->path(), source, ec);
        if (ec) {
            SetError(error, "failed to enumerate project template: " + source.string());
            return false;
        }
        const fs::path target = destination / relative;
        if (it->is_directory(ec)) {
            fs::create_directories(target, ec);
            if (ec) {
                SetError(error, "failed to create template directory: " + target.string());
                return false;
            }
            continue;
        }
        if (ec || !it->is_regular_file(ec)) continue;
        if (target.filename() == ProjectConfig::kFileName) continue;
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            SetError(error, "failed to create template directory: " + target.parent_path().string());
            return false;
        }
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SetError(error, "failed to copy template file: " + it->path().string());
            return false;
        }
    }
    if (ec) {
        SetError(error, "failed to enumerate project template: " + source.string());
        return false;
    }
    return true;
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

EditorWorkspace::EditorWorkspace(fs::path settingsPath, fs::path templateRoot)
    : m_SettingsPath(settingsPath.empty() ? DefaultSettingsPath() : std::move(settingsPath))
    , m_TemplateRoot(templateRoot.empty()
          ? FindTemplateRootFromCurrentPath()
          : fs::absolute(std::move(templateRoot)).lexically_normal()) {}

bool EditorWorkspace::Load(std::string* error) {
    if (error) error->clear();
    m_RecentProjects.clear();
    m_Shortcuts.ResetDefaults();
    m_UserUiScale = 1.0f;
    m_EditorThemeId = "dark";
    m_PanelState.clear();
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
        const auto shortcuts = json.find("shortcuts");
        if (shortcuts != json.end()) {
            std::string warning;
            m_Shortcuts.LoadOverrides(*shortcuts, &warning);
            if (!warning.empty()) SetError(error, warning);
        }
        m_UserUiScale = Editor::UI::EditorUIScaleManager::ClampUserScale(
            json.value("userUiScale", m_UserUiScale));
        m_EditorThemeId = Editor::UI::EditorThemeManager::NormalizeThemeID(
            json.value("editorThemeId", m_EditorThemeId));
        const auto panelState = json.find("panelState");
        if (panelState != json.end() && panelState->is_object()) {
            for (const auto& [panelID, state] : panelState->items()) {
                if (!state.is_object()) continue;
                auto& values = m_PanelState[panelID];
                for (const auto& [key, value] : state.items()) {
                    if (value.is_string()) values[key] = value.get<std::string>();
                }
                if (values.empty()) m_PanelState.erase(panelID);
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
        json["shortcuts"] = m_Shortcuts.SaveOverrides();
        json["userUiScale"] = m_UserUiScale;
        json["editorThemeId"] = m_EditorThemeId;
        json["panelState"] = nlohmann::json::object();
        for (const auto& [panelID, state] : m_PanelState) {
            if (state.empty()) continue;
            json["panelState"][panelID] = nlohmann::json::object();
            for (const auto& [key, value] : state) {
                json["panelState"][panelID][key] = value;
            }
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

void EditorWorkspace::SetTemplateRoot(fs::path templateRoot) {
    if (templateRoot.empty()) {
        m_TemplateRoot.clear();
        return;
    }
    std::error_code ec;
    m_TemplateRoot = fs::absolute(std::move(templateRoot), ec).lexically_normal();
    if (ec) m_TemplateRoot.clear();
}

void EditorWorkspace::SetUserUiScale(float value) {
    m_UserUiScale = Editor::UI::EditorUIScaleManager::ClampUserScale(value);
}

void EditorWorkspace::SetEditorThemeId(std::string value) {
    m_EditorThemeId = Editor::UI::EditorThemeManager::NormalizeThemeID(value);
}

void EditorWorkspace::SetPanelStateValue(const std::string& panelID,
                                         const std::string& key,
                                         std::string value) {
    if (panelID.empty() || key.empty()) return;
    m_PanelState[panelID][key] = std::move(value);
}

std::optional<std::string> EditorWorkspace::GetPanelStateValue(
    const std::string& panelID, const std::string& key) const {
    const auto panel = m_PanelState.find(panelID);
    if (panel == m_PanelState.end()) return std::nullopt;
    const auto value = panel->second.find(key);
    if (value == panel->second.end()) return std::nullopt;
    return value->second;
}

void EditorWorkspace::ClearPanelState(const std::string& panelID) {
    if (!panelID.empty()) m_PanelState.erase(panelID);
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
    fs::create_directories(root, ec);
    if (ec) {
        SetError(error, "failed to create project root: " + ec.message());
        return false;
    }
    if (!m_TemplateRoot.empty()) {
        if (!CopyTemplateTree(m_TemplateRoot, root, error)) return false;
    } else {
        const fs::path scenes = root / "Content" / "Scenes";
        fs::create_directories(scenes, ec);
        if (ec) {
            SetError(error, "failed to create project directories: " + ec.message());
            return false;
        }
        nlohmann::json scene = {
            {"name", "Main"}, {"nextID", 1}, {"actors", nlohmann::json::array()}
        };
        std::ofstream sceneOutput(scenes / "Main.scene.json");
        if (!sceneOutput) {
            SetError(error, "failed to create default scene");
            return false;
        }
        sceneOutput << scene.dump(2) << '\n';
        if (!InputActionMap::WriteDefaultFile(root / InputActionMap::kDefaultProjectPath, error)) {
            return false;
        }
    }

    ProjectConfig config;
    if (!config.Open(root, true, error)) return false;
    config.SetName(projectName);
    config.GetPublishSettings().target = PublishTargets::kDefaultTargetId;
    if (!config.SetInputConfigPath(InputActionMap::kDefaultProjectPath, error)) return false;
    const fs::path startupScene = root / "Content" / "Scenes" / "Main.scene.json";
    if (!config.SetStartupScene(startupScene, error) || !config.Save(error)) return false;
    AddRecentProject(root);
    return Save(error);
}
