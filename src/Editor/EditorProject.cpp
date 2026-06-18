#include "Editor/EditorProject.h"

#include <fstream>
#include <nlohmann/json.hpp>

bool EditorProject::Open(std::filesystem::path root, bool allowMissingManifest) {
    m_Root = std::filesystem::absolute(std::move(root)).lexically_normal();
    m_ContentRoot = m_Root / "Content";
    m_StatePath = m_Root / ".myengine_editor_state.json";
    m_LastError.clear();
    const bool configLoaded = m_Config.Open(m_Root, allowMissingManifest, &m_LastError);
    return LoadState() && configLoaded;
}
bool EditorProject::SaveState() const {
    nlohmann::json json;
    json["lastScenePath"] = m_State.lastScenePath;
    json["selectedAssetPath"] = m_State.selectedAssetPath;
    json["lastOpenDirectory"] = m_State.lastOpenDirectory;
    json["panels"] = {{"toolbar",m_State.showToolbar},{"sceneHierarchy",m_State.showSceneHierarchy},
        {"viewport",m_State.showViewport},{"inspector",m_State.showInspector},
        {"assetBrowser",m_State.showAssetBrowser},{"log",m_State.showLog}};
    std::ofstream output(m_StatePath); if (!output) return false; output << json.dump(2); return true;
}
bool EditorProject::LoadState() {
    m_State = {};
    if (!std::filesystem::exists(m_StatePath)) return true;
    std::ifstream input(m_StatePath); if (!input) return false;
    nlohmann::json json; try { input >> json; } catch (...) { return false; }
    m_State.lastScenePath = json.value("lastScenePath", std::string{});
    m_State.selectedAssetPath = json.value("selectedAssetPath", std::string{});
    m_State.lastOpenDirectory = json.value("lastOpenDirectory", std::string{});
    const auto panels = json.value("panels", nlohmann::json::object());
    m_State.showToolbar = panels.value("toolbar", true);
    m_State.showSceneHierarchy = panels.value("sceneHierarchy", true);
    m_State.showViewport = panels.value("viewport", true);
    m_State.showInspector = panels.value("inspector", true);
    m_State.showAssetBrowser = panels.value("assetBrowser", true);
    m_State.showLog = panels.value("log", true);
    return true;
}
