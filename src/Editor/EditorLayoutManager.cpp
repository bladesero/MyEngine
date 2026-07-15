#include "Editor/EditorLayoutManager.h"

#include "Core/Logger.h"
#include "Editor/EditorPanel.h"
#include "Editor/EditorProject.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <imgui_internal.h>
#endif

#include <algorithm>
#include <charconv>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace {
constexpr const char* kDockSpaceName = "EditorDockSpace";
constexpr const char* kRequiredPanels[] = {"toolbar",   "sceneHierarchy", "viewport", "gameViewport",
                                           "inspector", "assetBrowser",   "log",      "profiler"};

struct PersistedDockSpaceInfo {
    uint32_t id = 0;
    bool found = false;
    bool hasChildNodes = false;
    bool rootHasWindows = false;
    bool hasDockedWindowReferences = false;

    bool IsUsable(uint32_t stableID) const {
        // An empty legacy root is ambiguous: after a scoped-ID change ImGui
        // saves the detached panels as fixed Pos/Size windows and removes their
        // DockId fields. Only the globally stable root may intentionally remain
        // empty (for example when the user floats every panel).
        return found && id != 0 && (hasChildNodes || rootHasWindows || id == stableID);
    }
};

bool ParseHexID(std::string_view line, uint32_t& outID) {
    constexpr std::string_view marker = "ID=0x";
    const size_t markerPosition = line.find(marker);
    if (markerPosition == std::string_view::npos)
        return false;
    const char* first = line.data() + markerPosition + marker.size();
    const char* last = line.data() + line.size();
    uint32_t value = 0;
    const auto result = std::from_chars(first, last, value, 16);
    if (result.ec != std::errc{} || result.ptr == first || value == 0)
        return false;
    outID = value;
    return true;
}

PersistedDockSpaceInfo InspectPersistedDockSpace(std::string_view settings) {
    PersistedDockSpaceInfo info;
    info.hasDockedWindowReferences = settings.find("DockId=0x") != std::string_view::npos;

    std::istringstream lines{std::string(settings)};
    std::string line;
    bool inDockingSection = false;
    bool inspectingDockSpace = false;
    while (std::getline(lines, line)) {
        if (line == "[Docking][Data]") {
            inDockingSection = true;
            continue;
        }
        if (!inDockingSection)
            continue;
        if (!line.empty() && line.front() == '[')
            break;

        const size_t firstText = line.find_first_not_of(" \t");
        if (firstText == std::string::npos)
            continue;
        const std::string_view entry(line.data() + firstText, line.size() - firstText);
        if (entry.rfind("DockSpace ", 0) == 0) {
            if (info.found)
                break;
            info.found = ParseHexID(entry, info.id);
            info.rootHasWindows = entry.find("Selected=0x") != std::string_view::npos;
            inspectingDockSpace = info.found;
        } else if (inspectingDockSpace && entry.rfind("DockNode ", 0) == 0) {
            info.hasChildNodes = true;
        }
    }
    return info;
}

uint32_t StableDockSpaceID() {
#if defined(MYENGINE_ENABLE_IMGUI)
    // Do not derive the editor root ID from the host window. ImGui may change
    // a window seed across versions, which would orphan the persisted dock tree.
    return ImHashStr(kDockSpaceName, 0, 0);
#else
    return 0;
#endif
}

void AppendWarning(std::string& warning, std::string_view message) {
    if (!warning.empty())
        warning += "; ";
    warning.append(message.data(), message.size());
}

void SetError(std::string* error, std::string message) {
    if (error)
        *error = std::move(message);
}

nlohmann::json ToJson(const EditorLayoutConfig& config) {
    nlohmann::json panels = nlohmann::json::array();
    for (const auto& panel : config.panels) {
        panels.push_back({{"id", panel.panelID}, {"title", panel.title}, {"area", panel.area}});
    }
    return {{"version", config.version},
            {"toolbarHeightRatio", config.toolbarHeightRatio},
            {"leftWidthRatio", config.leftWidthRatio},
            {"rightWidthRatio", config.rightWidthRatio},
            {"bottomHeightRatio", config.bottomHeightRatio},
            {"panels", panels}};
}

bool RatioValid(float value) {
    return value > 0.01f && value < 0.95f;
}
} // namespace

EditorLayoutConfig EditorLayoutConfig::CreateDefault() {
    EditorLayoutConfig config;
    config.panels = {{"toolbar", "Toolbar", "top"},         {"sceneHierarchy", "Scene Outliner", "left"},
                     {"viewport", "Scene View", "center"},  {"gameViewport", "Game View", "center"},
                     {"inspector", "Inspector", "right"},   {"assetBrowser", "Asset Browser", "bottomLeft"},
                     {"log", "Log Output", "bottomCenter"}, {"profiler", "Profiler", "bottomCenter"}};
    return config;
}

bool EditorLayoutConfig::Validate(std::string* error) const {
    if (version != 1) {
        SetError(error, "unsupported editor layout version");
        return false;
    }
    if (!RatioValid(toolbarHeightRatio) || !RatioValid(leftWidthRatio) || !RatioValid(rightWidthRatio) ||
        !RatioValid(bottomHeightRatio)) {
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

bool EditorLayoutConfig::LoadFromFile(const std::filesystem::path& path, EditorLayoutConfig& config,
                                      std::string* error) {
    if (error)
        error->clear();
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
            if (!item.is_object())
                continue;
            loaded.panels.push_back({item.value("id", std::string{}), item.value("title", std::string{}),
                                     item.value("area", std::string{})});
        }
        if (!loaded.Validate(error))
            return false;
        config = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        SetError(error, "failed to parse editor layout config: " + std::string(exception.what()));
        return false;
    }
}

bool EditorLayoutConfig::SaveToFile(const std::filesystem::path& path, const EditorLayoutConfig& config,
                                    std::string* error) {
    if (error)
        error->clear();
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

void EditorLayoutManager::OpenProject(const std::filesystem::path& projectRoot, EditorProjectState& state,
                                      const std::vector<std::unique_ptr<EditorPanel>>& panels) {
    m_ProjectRoot = projectRoot;
    m_ConfigPath = m_ProjectRoot / "Config" / "EditorLayout.default.json";
    m_Config = EditorLayoutConfig::CreateDefault();
    m_LastWarning.clear();
    m_ProjectOpen = true;
    m_UserIniLoaded = false;
    m_DockSpaceID = StableDockSpaceID();
    m_ApplyDefaultNextFrame = state.imguiLayoutIni.empty();
    bool loadUserIni = !state.imguiLayoutIni.empty();

    if (loadUserIni) {
        const PersistedDockSpaceInfo persisted = InspectPersistedDockSpace(state.imguiLayoutIni);
        if (persisted.IsUsable(m_DockSpaceID)) {
            // Keep the persisted root ID so layouts authored before an ImGui
            // upgrade are attached to this frame's host instead of becoming an
            // orphaned, fixed-size tree in the top-left corner.
            m_DockSpaceID = persisted.id;
        } else if (persisted.hasDockedWindowReferences || persisted.found) {
            loadUserIni = false;
            m_ApplyDefaultNextFrame = true;
            AppendWarning(m_LastWarning, "discarded orphaned editor dock layout; using the default layout");
        }
    }

    if (std::filesystem::is_regular_file(m_ConfigPath)) {
        std::string error;
        if (!EditorLayoutConfig::LoadFromFile(m_ConfigPath, m_Config, &error)) {
            AppendWarning(m_LastWarning, error + "; using built-in editor layout");
            m_Config = EditorLayoutConfig::CreateDefault();
        }
    } else {
        std::string error;
        if (!EditorLayoutConfig::SaveToFile(m_ConfigPath, m_Config, &error)) {
            AppendWarning(m_LastWarning, error);
        }
    }
    if (!m_LastWarning.empty())
        Logger::Warn("[Editor] ", m_LastWarning);

    for (const auto& panel : panels) {
        if (!panel)
            continue;
        const auto found = state.panelVisibility.find(panel->GetID());
        if (found != state.panelVisibility.end())
            panel->SetVisible(found->second);
    }
    if (loadUserIni)
        LoadUserIni(state);
}

void EditorLayoutManager::CloseProject() {
    m_ProjectRoot.clear();
    m_ConfigPath.clear();
    m_ProjectOpen = false;
    m_UserIniLoaded = false;
    m_ApplyDefaultNextFrame = false;
    m_DockSpaceID = 0;
}

void EditorLayoutManager::LoadUserIni(const EditorProjectState& state) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_UserIniLoaded || state.imguiLayoutIni.empty())
        return;
    ImGui::LoadIniSettingsFromMemory(state.imguiLayoutIni.data(), state.imguiLayoutIni.size());
    m_UserIniLoaded = true;
#else
    (void)state;
#endif
}

void EditorLayoutManager::BeginDockSpace(const std::vector<std::unique_ptr<EditorPanel>>& panels, float reservedTop,
                                         float reservedBottom) {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!m_ProjectOpen)
        return;
    m_ReservedTop = std::max(0.0f, reservedTop);
    m_ReservedBottom = std::max(0.0f, reservedBottom);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 dockPos{viewport->WorkPos.x, viewport->WorkPos.y + m_ReservedTop};
    const ImVec2 dockSize{viewport->WorkSize.x,
                          std::max(1.0f, viewport->WorkSize.y - m_ReservedTop - m_ReservedBottom)};
    ImGui::SetNextWindowPos(dockPos);
    ImGui::SetNextWindowSize(dockSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin("Editor DockSpace Host", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspaceID = static_cast<ImGuiID>(m_DockSpaceID ? m_DockSpaceID : StableDockSpaceID());
    ImGui::DockSpace(dockspaceID, {0.0f, 0.0f}, ImGuiDockNodeFlags_PassthruCentralNode);
    if (m_ApplyDefaultNextFrame) {
        ApplyDefaultLayout(panels);
        m_ApplyDefaultNextFrame = false;
    }
    ImGui::End();
#else
    (void)panels;
#endif
}

std::string EditorLayoutManager::FindWindowName(const std::vector<std::unique_ptr<EditorPanel>>& panels,
                                                const std::string& panelID) const {
    for (const auto& panel : panels) {
        if (panel && panel->GetID() == panelID)
            return panel->GetStableWindowName();
    }
    return {};
}

void EditorLayoutManager::ApplyDefaultLayout(const std::vector<std::unique_ptr<EditorPanel>>& panels) {
#if defined(MYENGINE_ENABLE_IMGUI)
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceID = static_cast<ImGuiID>(m_DockSpaceID ? m_DockSpaceID : StableDockSpaceID());
    ImGui::DockBuilderRemoveNode(dockspaceID);
    ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
    const ImVec2 dockPos{viewport->WorkPos.x, viewport->WorkPos.y + m_ReservedTop};
    const ImVec2 dockSize{viewport->WorkSize.x,
                          std::max(1.0f, viewport->WorkSize.y - m_ReservedTop - m_ReservedBottom)};
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
    ImGuiID leftBottomID =
        ImGui::DockBuilderSplitNode(leftColumnID, ImGuiDir_Down, bottomRatio, nullptr, &leftColumnID);
    ImGuiID bottomCenterID = ImGui::DockBuilderSplitNode(mainID, ImGuiDir_Down, bottomRatio, nullptr, &mainID);

    const auto dockToArea = [&](const EditorPanelLayoutNode& node) {
        const std::string windowName = FindWindowName(panels, node.panelID);
        if (windowName.empty())
            return;
        ImGuiID target = mainID;
        if (node.area == "top")
            target = topID;
        else if (node.area == "left")
            target = leftColumnID;
        else if (node.area == "right")
            target = rightID;
        else if (node.area == "bottomLeft")
            target = leftBottomID;
        else if (node.area == "bottomCenter")
            target = bottomCenterID;
        ImGui::DockBuilderDockWindow(windowName.c_str(), target);
    };

    for (const auto& panel : m_Config.panels)
        dockToArea(panel);
    std::unordered_set<std::string> configuredPanels;
    for (const auto& panel : m_Config.panels)
        configuredPanels.insert(panel.panelID);
    for (const auto& panel : panels) {
        if (!panel || configuredPanels.find(panel->GetID()) != configuredPanels.end())
            continue;
        const std::string area = panel->GetDefaultDockArea();
        if (area.empty())
            continue;
        dockToArea({panel->GetID(), panel->GetTitle(), area});
    }
    ImGui::DockBuilderFinish(dockspaceID);
#else
    (void)panels;
#endif
}

void EditorLayoutManager::SaveCurrentLayout(EditorProjectState& state) const {
#if defined(MYENGINE_ENABLE_IMGUI)
    size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    state.imguiLayoutIni.assign(data ? data : "", size);
#else
    (void)state;
#endif
}

void EditorLayoutManager::ResetToDefault(EditorProjectState& state) {
    state.imguiLayoutIni.clear();
    m_ApplyDefaultNextFrame = true;
}

void EditorLayoutManager::LoadDefaultLayout(EditorProjectState& state) {
    ResetToDefault(state);
}
