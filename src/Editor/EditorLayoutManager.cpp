#include "Editor/EditorLayoutManager.h"

#include "Core/Logger.h"
#include "Editor/EditorPanel.h"
#include "Editor/EditorProject.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <imgui_internal.h>
#endif

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unordered_set>

namespace {
constexpr const char* kDockSpaceName = "EditorDockSpace";
constexpr const char* kRequiredPanels[] = {
    "toolbar", "sceneHierarchy", "viewport", "inspector", "assetBrowser", "log"
};

void SetError(std::string* error, std::string message)
{
    if (error) *error = std::move(message);
}

nlohmann::json ToJson(const EditorLayoutConfig& config)
{
    nlohmann::json panels = nlohmann::json::array();
    for (const auto& panel : config.panels) {
        panels.push_back({
            {"id", panel.panelID},
            {"title", panel.title},
            {"area", panel.area}
        });
    }
    return {
        {"version", config.version},
        {"toolbarHeightRatio", config.toolbarHeightRatio},
        {"leftWidthRatio", config.leftWidthRatio},
        {"rightWidthRatio", config.rightWidthRatio},
        {"bottomHeightRatio", config.bottomHeightRatio},
        {"panels", panels}
    };
}

bool RatioValid(float value)
{
    return value > 0.01f && value < 0.95f;
}
}

EditorLayoutConfig EditorLayoutConfig::CreateDefault()
{
    EditorLayoutConfig config;
    config.panels = {
        {"toolbar", "Toolbar", "top"},
        {"sceneHierarchy", "Scene Outliner", "left"},
        {"viewport", "Scene View", "center"},
        {"inspector", "Inspector", "right"},
        {"assetBrowser", "Asset Browser", "bottomLeft"},
        {"log", "Log Output", "bottomCenter"}
    };
    return config;
}

bool EditorLayoutConfig::Validate(std::string* error) const
{
    if (version != 1) {
        SetError(error, "unsupported editor layout version");
        return false;
    }
    if (!RatioValid(toolbarHeightRatio) || !RatioValid(leftWidthRatio) ||
        !RatioValid(rightWidthRatio) || !RatioValid(bottomHeightRatio)) {
        SetError(error, "editor layout split ratio is out of range");
        return false;
    }

    std::unordered_set<std::string> seen;
    for (const auto& panel : panels) {
        if (panel.panelID.empty() || panel.area.empty()) {
            SetError(error, "editor layout panel entry is missing id or area");
            return false;
        }
        if (!seen.insert(panel.panelID).second) {
            SetError(error, "editor layout contains duplicate panel id: " + panel.panelID);
            return false;
        }
    }
    for (const char* required : kRequiredPanels) {
        if (seen.find(required) == seen.end()) {
            SetError(error, "editor layout is missing panel id: " + std::string(required));
            return false;
        }
    }
    return true;
}

bool EditorLayoutConfig::LoadFromFile(const std::filesystem::path& path,
                                      EditorLayoutConfig& config,
                                      std::string* error)
{
    if (error) error->clear();
    std::ifstream input(path);
    if (!input) {
        SetError(error, "failed to open editor layout config: " + path.string());
        return false;
    }

    try {
        nlohmann::json json;
        input >> json;
        EditorLayoutConfig loaded;
        loaded.version = json.value("version", 1);
        loaded.toolbarHeightRatio = json.value("toolbarHeightRatio", loaded.toolbarHeightRatio);
        loaded.leftWidthRatio = json.value("leftWidthRatio", loaded.leftWidthRatio);
        loaded.rightWidthRatio = json.value("rightWidthRatio", loaded.rightWidthRatio);
        loaded.bottomHeightRatio = json.value("bottomHeightRatio", loaded.bottomHeightRatio);

        const auto panels = json.value("panels", nlohmann::json::array());
        if (!panels.is_array()) {
            SetError(error, "editor layout panels must be an array");
            return false;
        }
        loaded.panels.clear();
        for (const auto& item : panels) {
            if (!item.is_object()) continue;
            loaded.panels.push_back({
                item.value("id", std::string{}),
                item.value("title", std::string{}),
                item.value("area", std::string{})
            });
        }
        if (!loaded.Validate(error)) return false;
        config = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        SetError(error, "failed to parse editor layout config: " + std::string(exception.what()));
        return false;
    }
}

bool EditorLayoutConfig::SaveToFile(const std::filesystem::path& path,
                                    const EditorLayoutConfig& config,
                                    std::string* error)
{
    if (error) error->clear();
    std::string validationError;
    if (!config.Validate(&validationError)) {
        SetError(error, validationError);
        return false;
    }

    try {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            SetError(error, "failed to create editor layout directory: " + ec.message());
            return false;
        }
        std::ofstream output(path);
        if (!output) {
            SetError(error, "failed to write editor layout config: " + path.string());
            return false;
        }
        output << ToJson(config).dump(2) << '\n';
        return true;
    } catch (const std::exception& exception) {
        SetError(error, "failed to write editor layout config: " + std::string(exception.what()));
        return false;
    }
}

void EditorLayoutManager::OpenProject(
    const std::filesystem::path& projectRoot,
    EditorProjectState& state,
    const std::vector<std::unique_ptr<EditorPanel>>& panels)
{
    m_ProjectRoot = projectRoot;
    m_ConfigPath = m_ProjectRoot / "Config" / "EditorLayout.default.json";
    m_Config = EditorLayoutConfig::CreateDefault();
    m_LastWarning.clear();
    m_ProjectOpen = true;
    m_UserIniLoaded = false;
    m_ApplyDefaultNextFrame = state.imguiLayoutIni.empty();

    if (std::filesystem::is_regular_file(m_ConfigPath)) {
        std::string error;
        if (!EditorLayoutConfig::LoadFromFile(m_ConfigPath, m_Config, &error)) {
            m_LastWarning = error + "; using built-in editor layout";
            Logger::Warn("[Editor] ", m_LastWarning);
            m_Config = EditorLayoutConfig::CreateDefault();
        }
    } else {
        std::string error;
        if (!EditorLayoutConfig::SaveToFile(m_ConfigPath, m_Config, &error)) {
            m_LastWarning = error;
            Logger::Warn("[Editor] ", error);
        }
    }

    for (const auto& panel : panels) {
        if (!panel) continue;
        const auto found = state.panelVisibility.find(panel->GetID());
        if (found != state.panelVisibility.end()) panel->SetVisible(found->second);
    }
    LoadUserIni(state);
}

void EditorLayoutManager::CloseProject()
{
    m_ProjectRoot.clear();
    m_ConfigPath.clear();
    m_ProjectOpen = false;
    m_UserIniLoaded = false;
    m_ApplyDefaultNextFrame = false;
}

void EditorLayoutManager::LoadUserIni(const EditorProjectState& state)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_UserIniLoaded || state.imguiLayoutIni.empty()) return;
    ImGui::LoadIniSettingsFromMemory(state.imguiLayoutIni.data(),
                                     state.imguiLayoutIni.size());
    m_UserIniLoaded = true;
#else
    (void)state;
#endif
}

void EditorLayoutManager::BeginDockSpace(
    const std::vector<std::unique_ptr<EditorPanel>>& panels,
    float reservedTop,
    float reservedBottom)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ProjectOpen) return;
    m_ReservedTop = std::max(0.0f, reservedTop);
    m_ReservedBottom = std::max(0.0f, reservedBottom);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 dockPos{viewport->WorkPos.x, viewport->WorkPos.y + m_ReservedTop};
    const ImVec2 dockSize{
        viewport->WorkSize.x,
        std::max(1.0f, viewport->WorkSize.y - m_ReservedTop - m_ReservedBottom)
    };
    ImGui::SetNextWindowPos(dockPos);
    ImGui::SetNextWindowSize(dockSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin("Editor DockSpace Host", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceID = ImGui::GetID(kDockSpaceName);
    ImGui::DockSpace(dockspaceID, {0.0f, 0.0f},
                     ImGuiDockNodeFlags_PassthruCentralNode);
    if (m_ApplyDefaultNextFrame) {
        ApplyDefaultLayout(panels);
        m_ApplyDefaultNextFrame = false;
    }
    ImGui::End();
#else
    (void)panels;
#endif
}

std::string EditorLayoutManager::FindWindowName(
    const std::vector<std::unique_ptr<EditorPanel>>& panels,
    const std::string& panelID) const
{
    for (const auto& panel : panels) {
        if (panel && panel->GetID() == panelID) return panel->GetStableWindowName();
    }
    return {};
}

void EditorLayoutManager::ApplyDefaultLayout(
    const std::vector<std::unique_ptr<EditorPanel>>& panels)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceID = ImGui::GetID(kDockSpaceName);
    ImGui::DockBuilderRemoveNode(dockspaceID);
    ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    const ImVec2 dockPos{viewport->WorkPos.x, viewport->WorkPos.y + m_ReservedTop};
    const ImVec2 dockSize{
        viewport->WorkSize.x,
        std::max(1.0f, viewport->WorkSize.y - m_ReservedTop - m_ReservedBottom)
    };
    ImGui::DockBuilderSetNodePos(dockspaceID, dockPos);
    ImGui::DockBuilderSetNodeSize(dockspaceID, dockSize);

    ImGuiID mainID = dockspaceID;
    const float topRatio = std::clamp(m_Config.toolbarHeightRatio, 0.01f, 0.4f);
    const float leftRatio = std::clamp(m_Config.leftWidthRatio, 0.05f, 0.45f);
    const float rightRatio = std::clamp(m_Config.rightWidthRatio, 0.05f, 0.45f);
    const float bottomRatio = std::clamp(m_Config.bottomHeightRatio, 0.05f, 0.45f);

    ImGuiID topID = ImGui::DockBuilderSplitNode(mainID, ImGuiDir_Up, topRatio, nullptr, &mainID);
    ImGuiID leftColumnID = ImGui::DockBuilderSplitNode(mainID, ImGuiDir_Left, leftRatio, nullptr, &mainID);
    ImGuiID rightID = ImGui::DockBuilderSplitNode(mainID, ImGuiDir_Right, rightRatio, nullptr, &mainID);
    ImGuiID leftBottomID = ImGui::DockBuilderSplitNode(leftColumnID, ImGuiDir_Down, bottomRatio,
                                                       nullptr, &leftColumnID);
    ImGuiID bottomCenterID = ImGui::DockBuilderSplitNode(mainID, ImGuiDir_Down, bottomRatio,
                                                         nullptr, &mainID);

    const auto dockToArea = [&](const EditorPanelLayoutNode& node) {
        const std::string windowName = FindWindowName(panels, node.panelID);
        if (windowName.empty()) return;
        ImGuiID target = mainID;
        if (node.area == "top") target = topID;
        else if (node.area == "left") target = leftColumnID;
        else if (node.area == "right") target = rightID;
        else if (node.area == "bottomLeft") target = leftBottomID;
        else if (node.area == "bottomCenter") target = bottomCenterID;
        ImGui::DockBuilderDockWindow(windowName.c_str(), target);
    };

    for (const auto& panel : m_Config.panels) dockToArea(panel);
    ImGui::DockBuilderFinish(dockspaceID);
#else
    (void)panels;
#endif
}

void EditorLayoutManager::SaveCurrentLayout(EditorProjectState& state) const
{
#if defined(MYENGINE_ENABLE_IMGUI)
    size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    state.imguiLayoutIni.assign(data ? data : "", size);
#else
    (void)state;
#endif
}

void EditorLayoutManager::ResetToDefault(EditorProjectState& state)
{
    state.imguiLayoutIni.clear();
    m_ApplyDefaultNextFrame = true;
}

void EditorLayoutManager::LoadDefaultLayout(EditorProjectState& state)
{
    ResetToDefault(state);
}
