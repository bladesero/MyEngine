#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class EditorScriptPanelArea {
    Top = 0,
    Left = 1,
    Center = 2,
    Right = 3,
    BottomLeft = 4,
    BottomCenter = 5,
};

struct EditorScriptPanelSpec {
    std::string id;
    std::string title;
    EditorScriptPanelArea area = EditorScriptPanelArea::Center;
    std::string callback;
};

struct EditorScriptMenuSpec {
    std::string path;
    std::string target;
};

struct EditorScriptInspectorSpec {
    std::string targetType;
    int order = 0;
    std::string callback;
};

struct EditorScriptPanelBodySpec {
    std::string id;
    std::string callback;
};

struct EditorScriptToolbarItemSpec {
    std::string id;
    int order = 0;
    std::string callback;
};

struct EditorScriptContextMenuSpec {
    std::string targetType;
    std::string callback;
};

enum class EditorScriptRegistrationLayer {
    Engine,
    Project,
};

class EditorScriptRegistry {
public:
    void Clear();
    void SetRegistrationLayer(EditorScriptRegistrationLayer layer) { m_RegistrationLayer = layer; }
    void SetAllowProjectAppend(bool value) { m_AllowProjectAppend = value; }
    void SetAllowProjectOverrideCore(bool value) { m_AllowProjectOverrideCore = value; }
    void Panel(const std::string& id, const std::string& title, int area,
               const std::string& callback);
    void ToolPanel(const std::string& id, const std::string& title, int area,
                   const std::string& callback);
    void PanelBody(const std::string& id, const std::string& callback);
    void Menu(const std::string& path, const std::string& actionID);
    void MenuItem(const std::string& path, const std::string& target);
    void ToolbarItem(const std::string& id, int order, const std::string& callback);
    void Inspector(const std::string& targetType, int order, const std::string& callback);
    void InspectorSection(const std::string& targetType, int order, const std::string& callback);
    void AssetContextMenu(const std::string& assetTypeOrAny, const std::string& callback);
    void ActorContextMenu(const std::string& callback);

    const std::vector<EditorScriptPanelSpec>& GetPanels() const { return m_Panels; }
    const std::vector<EditorScriptPanelBodySpec>& GetPanelBodies() const { return m_PanelBodies; }
    const std::vector<EditorScriptMenuSpec>& GetMenus() const { return m_Menus; }
    const std::vector<EditorScriptInspectorSpec>& GetInspectors() const { return m_Inspectors; }
    const std::vector<EditorScriptToolbarItemSpec>& GetToolbarItems() const { return m_ToolbarItems; }
    const std::vector<EditorScriptContextMenuSpec>& GetAssetContextMenus() const { return m_AssetContextMenus; }
    const std::vector<EditorScriptContextMenuSpec>& GetActorContextMenus() const { return m_ActorContextMenus; }
    const std::vector<std::string>& GetDiagnostics() const { return m_Diagnostics; }
    const std::string* FindPanelBodyCallback(const std::string& id) const;
    bool IsCorePanelID(const std::string& id) const;

private:
    void AddDiagnostic(std::string message);
    bool RejectProjectAppend(const char* kind, const std::string& id);
    std::unordered_map<std::string, size_t> m_PanelByID;
    std::unordered_map<std::string, size_t> m_PanelBodyByID;
    std::vector<EditorScriptPanelSpec> m_Panels;
    std::vector<EditorScriptPanelBodySpec> m_PanelBodies;
    std::vector<EditorScriptMenuSpec> m_Menus;
    std::vector<EditorScriptInspectorSpec> m_Inspectors;
    std::vector<EditorScriptToolbarItemSpec> m_ToolbarItems;
    std::vector<EditorScriptContextMenuSpec> m_AssetContextMenus;
    std::vector<EditorScriptContextMenuSpec> m_ActorContextMenus;
    std::vector<std::string> m_Diagnostics;
    EditorScriptRegistrationLayer m_RegistrationLayer = EditorScriptRegistrationLayer::Engine;
    bool m_AllowProjectAppend = true;
    bool m_AllowProjectOverrideCore = false;
};
