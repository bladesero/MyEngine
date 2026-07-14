#include "Editor/EditorUI/EditorScriptRegistry.h"

#include <algorithm>
#include <utility>

void EditorScriptRegistry::Clear() {
    m_PanelByID.clear();
    m_PanelBodyByID.clear();
    m_Panels.clear();
    m_PanelBodies.clear();
    m_Menus.clear();
    m_Inspectors.clear();
    m_ToolbarItems.clear();
    m_AssetContextMenus.clear();
    m_ActorContextMenus.clear();
    m_Diagnostics.clear();
}

void EditorScriptRegistry::AddDiagnostic(std::string message) {
    if (!message.empty())
        m_Diagnostics.push_back(std::move(message));
}

bool EditorScriptRegistry::RejectProjectAppend(const char* kind, const std::string& id) {
    if (m_RegistrationLayer != EditorScriptRegistrationLayer::Project || m_AllowProjectAppend) {
        return false;
    }
    AddDiagnostic(std::string("project editor script append disabled for ") + kind + ": " + id);
    return true;
}

void EditorScriptRegistry::Panel(const std::string& id, const std::string& title, int area,
                                 const std::string& callback) {
    ToolPanel(id, title, area, callback);
}

void EditorScriptRegistry::ToolPanel(const std::string& id, const std::string& title, int area,
                                     const std::string& callback) {
    if (id.empty() || callback.empty())
        return;
    if (RejectProjectAppend("ToolPanel", id))
        return;
    if (IsCorePanelID(id)) {
        AddDiagnostic("scripted ToolPanel rejected core panel id: " + id);
        return;
    }

    EditorScriptPanelSpec spec;
    spec.id = id;
    spec.title = title.empty() ? id : title;
    spec.callback = callback;
    if (area < static_cast<int>(EditorScriptPanelArea::Top) ||
        area > static_cast<int>(EditorScriptPanelArea::BottomCenter)) {
        spec.area = EditorScriptPanelArea::Center;
    } else {
        spec.area = static_cast<EditorScriptPanelArea>(area);
    }

    auto found = m_PanelByID.find(id);
    if (found != m_PanelByID.end()) {
        m_Panels[found->second] = std::move(spec);
        return;
    }
    m_PanelByID[id] = m_Panels.size();
    m_Panels.push_back(std::move(spec));
}

bool EditorScriptRegistry::IsCorePanelID(const std::string& id) const {
    static const std::unordered_set<std::string> kCorePanels{
        "toolbar", "viewport", "gameViewport", "sceneHierarchy", "inspector", "assetBrowser", "log", "profiler"};
    return kCorePanels.find(id) != kCorePanels.end();
}

void EditorScriptRegistry::PanelBody(const std::string& id, const std::string& callback) {
    if (id.empty() || callback.empty())
        return;
    if (m_RegistrationLayer == EditorScriptRegistrationLayer::Project && IsCorePanelID(id) &&
        !m_AllowProjectOverrideCore) {
        AddDiagnostic("project PanelBody rejected core panel id: " + id);
        return;
    }

    EditorScriptPanelBodySpec spec;
    spec.id = id;
    spec.callback = callback;
    auto found = m_PanelBodyByID.find(id);
    if (found != m_PanelBodyByID.end()) {
        m_PanelBodies[found->second] = std::move(spec);
        return;
    }
    m_PanelBodyByID[id] = m_PanelBodies.size();
    m_PanelBodies.push_back(std::move(spec));
}

const std::string* EditorScriptRegistry::FindPanelBodyCallback(const std::string& id) const {
    auto found = m_PanelBodyByID.find(id);
    if (found == m_PanelBodyByID.end())
        return nullptr;
    return &m_PanelBodies[found->second].callback;
}

void EditorScriptRegistry::Menu(const std::string& path, const std::string& actionID) {
    MenuItem(path, actionID);
}

void EditorScriptRegistry::MenuItem(const std::string& path, const std::string& target) {
    if (path.empty() || target.empty())
        return;
    if (RejectProjectAppend("MenuItem", path))
        return;
    m_Menus.push_back({path, target});
}

void EditorScriptRegistry::ToolbarItem(const std::string& id, int order, const std::string& callback) {
    if (id.empty() || callback.empty())
        return;
    if (RejectProjectAppend("ToolbarItem", id))
        return;
    m_ToolbarItems.push_back({id, order, callback});
    std::sort(m_ToolbarItems.begin(), m_ToolbarItems.end(),
              [](const EditorScriptToolbarItemSpec& a, const EditorScriptToolbarItemSpec& b) {
                  if (a.order != b.order)
                      return a.order < b.order;
                  return a.id < b.id;
              });
}

void EditorScriptRegistry::Inspector(const std::string& targetType, int order, const std::string& callback) {
    InspectorSection(targetType, order, callback);
}

void EditorScriptRegistry::InspectorSection(const std::string& targetType, int order, const std::string& callback) {
    if (targetType.empty() || callback.empty())
        return;
    if (RejectProjectAppend("InspectorSection", targetType))
        return;
    m_Inspectors.push_back({targetType, order, callback});
    std::sort(m_Inspectors.begin(), m_Inspectors.end(),
              [](const EditorScriptInspectorSpec& a, const EditorScriptInspectorSpec& b) {
                  if (a.order != b.order)
                      return a.order < b.order;
                  return a.targetType < b.targetType;
              });
}

void EditorScriptRegistry::AssetContextMenu(const std::string& assetTypeOrAny, const std::string& callback) {
    if (assetTypeOrAny.empty() || callback.empty())
        return;
    if (RejectProjectAppend("AssetContextMenu", assetTypeOrAny))
        return;
    m_AssetContextMenus.push_back({assetTypeOrAny, callback});
}

void EditorScriptRegistry::ActorContextMenu(const std::string& callback) {
    if (callback.empty())
        return;
    if (RejectProjectAppend("ActorContextMenu", callback))
        return;
    m_ActorContextMenus.push_back({"Actor", callback});
}
