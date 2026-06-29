#include "Editor/EditorProject.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}
}

bool EditorProjectState::IsPanelVisible(const std::string& panelID) const {
    const auto found = panelVisibility.find(panelID);
    if (found != panelVisibility.end()) return found->second;
    if (panelID == "toolbar") return showToolbar;
    if (panelID == "sceneHierarchy") return showSceneHierarchy;
    if (panelID == "viewport") return showViewport;
    if (panelID == "inspector") return showInspector;
    if (panelID == "assetBrowser") return showAssetBrowser;
    if (panelID == "log") return showLog;
    if (panelID == "profiler") return true;
    return true;
}

void EditorProjectState::SetPanelVisible(const std::string& panelID, bool visible) {
    panelVisibility[panelID] = visible;
    SyncLegacyPanelFields();
}

void EditorProjectState::SyncLegacyPanelFields() {
    showToolbar = IsPanelVisible("toolbar");
    showSceneHierarchy = IsPanelVisible("sceneHierarchy");
    showViewport = IsPanelVisible("viewport");
    showInspector = IsPanelVisible("inspector");
    showAssetBrowser = IsPanelVisible("assetBrowser");
    showLog = IsPanelVisible("log");
}

bool EditorProject::Open(std::filesystem::path root, bool allowMissingManifest) {
    m_Root = std::filesystem::absolute(std::move(root)).lexically_normal();
    m_ContentRoot = m_Root / "Content";
    m_StatePath = m_Root / ".myengine_editor_state.json";
    m_LastError.clear();
    m_LastWarning.clear();
    const bool configLoaded = m_Config.Open(m_Root, allowMissingManifest, &m_LastError);
    if (!LoadState(&m_LastWarning) && m_LastError.empty()) {
        m_LastError = m_LastWarning;
    }
    return configLoaded;
}
bool EditorProject::SaveState() const {
    nlohmann::json json;
    json["lastScenePath"] = m_State.lastScenePath;
    json["selectedAssetPath"] = m_State.selectedAssetPath;
    json["lastOpenDirectory"] = m_State.lastOpenDirectory;
    json["imguiLayoutIni"] = m_State.imguiLayoutIni;
    json["activeLayoutName"] = m_State.activeLayoutName;
    json["panelVisibility"] = nlohmann::json::object();
    for (const auto& [panelID, visible] : m_State.panelVisibility) {
        json["panelVisibility"][panelID] = visible;
    }
    json["panels"] = {{"toolbar",m_State.showToolbar},{"sceneHierarchy",m_State.showSceneHierarchy},
        {"viewport",m_State.showViewport},{"inspector",m_State.showInspector},
        {"assetBrowser",m_State.showAssetBrowser},{"log",m_State.showLog}};
    std::ofstream output(m_StatePath); if (!output) return false; output << json.dump(2); return true;
}
bool EditorProject::LoadState(std::string* error) {
    if (error) error->clear();
    m_State = {};
    if (!std::filesystem::exists(m_StatePath)) return true;
    std::ifstream input(m_StatePath);
    if (!input) {
        SetError(error, "failed to open editor state: " + m_StatePath.string());
        return false;
    }
    nlohmann::json json;
    try {
        input >> json;
    } catch (const std::exception& exception) {
        SetError(error, "failed to parse editor state: " + std::string(exception.what()));
        return false;
    }
    m_State.lastScenePath = json.value("lastScenePath", std::string{});
    m_State.selectedAssetPath = json.value("selectedAssetPath", std::string{});
    m_State.lastOpenDirectory = json.value("lastOpenDirectory", std::string{});
    m_State.imguiLayoutIni = json.value("imguiLayoutIni", std::string{});
    m_State.activeLayoutName = json.value("activeLayoutName", std::string{"default"});

    const auto panels = json.value("panels", nlohmann::json::object());
    m_State.showToolbar = panels.value("toolbar", true);
    m_State.showSceneHierarchy = panels.value("sceneHierarchy", true);
    m_State.showViewport = panels.value("viewport", true);
    m_State.showInspector = panels.value("inspector", true);
    m_State.showAssetBrowser = panels.value("assetBrowser", true);
    m_State.showLog = panels.value("log", true);
    m_State.panelVisibility = {
        {"toolbar", m_State.showToolbar},
        {"sceneHierarchy", m_State.showSceneHierarchy},
        {"viewport", m_State.showViewport},
        {"gameViewport", true},
        {"inspector", m_State.showInspector},
        {"assetBrowser", m_State.showAssetBrowser},
        {"log", m_State.showLog},
        {"profiler", true}
    };
    const auto visibility = json.find("panelVisibility");
    if (visibility != json.end() && visibility->is_object()) {
        for (auto it = visibility->begin(); it != visibility->end(); ++it) {
            if (it.value().is_boolean()) {
                m_State.panelVisibility[it.key()] = it.value().get<bool>();
            }
        }
        m_State.SyncLegacyPanelFields();
    }
    return true;
}
