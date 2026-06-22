#include "Editor/UI/EditorFontManager.h"

#include "Editor/EditorImGuiBackend.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

#include <algorithm>
#include <cstdio>
#include <system_error>
#include <utility>

namespace Editor::UI {
namespace {
#if defined(MYENGINE_ENABLE_IMGUI)
constexpr ImWchar kFontAwesomeRanges[] = {0xf000, 0xf8ff, 0};
#endif
std::array<ImFont*, static_cast<size_t>(EditorFontRole::Count)> g_ActiveFonts{};
const EditorFontConfig kDefaultConfig{};
}

void EditorFontManager::SetFontRoot(std::filesystem::path root)
{
    m_FontRoot = std::move(root);
    MarkDirty();
}

bool EditorFontManager::RebuildIfNeeded(float effectiveScale, EditorImGuiBackend* backend)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    m_LastRebuilt = false;
    if (!m_Dirty || !ImGui::GetCurrentContext()) return false;

    m_Fonts.fill(nullptr);
    g_ActiveFonts.fill(nullptr);
    m_LastWarning.clear();

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const float safeScale = effectiveScale > 0.0f ? effectiveScale : 1.0f;
    const float uiSize = m_Config.baseUIFontSize * safeScale;
    const float logSize = m_Config.baseLogFontSize * safeScale;
    const float iconSize = m_Config.baseIconFontSize * safeScale;

    m_Fonts[Index(EditorFontRole::UI)] =
        LoadFont(EditorFontRole::UI, Resolve(m_Config.uiRegularPath), uiSize, false);
    if (!m_Fonts[Index(EditorFontRole::UI)]) {
        ImFontConfig config;
        config.SizePixels = uiSize;
        m_Fonts[Index(EditorFontRole::UI)] = io.Fonts->AddFontDefault(&config);
        SetWarning("UI font missing; using ImGui default font");
    }
    io.FontDefault = m_Fonts[Index(EditorFontRole::UI)];

    m_Fonts[Index(EditorFontRole::Icon)] =
        LoadFont(EditorFontRole::Icon, Resolve(m_Config.iconSolidPath), iconSize, true);
    if (!m_Fonts[Index(EditorFontRole::Icon)]) {
        m_Fonts[Index(EditorFontRole::Icon)] = m_Fonts[Index(EditorFontRole::UI)];
        SetWarning(m_LastWarning.empty()
            ? "Icon font missing; using text icon fallback"
            : m_LastWarning + "; icon font missing; using text icon fallback");
    }

    m_Fonts[Index(EditorFontRole::UIEmphasis)] =
        LoadFont(EditorFontRole::UIEmphasis, Resolve(m_Config.uiSemiBoldPath), uiSize, false);
    if (!m_Fonts[Index(EditorFontRole::UIEmphasis)]) {
        m_Fonts[Index(EditorFontRole::UIEmphasis)] = m_Fonts[Index(EditorFontRole::UI)];
    }

    m_Fonts[Index(EditorFontRole::Log)] =
        LoadFont(EditorFontRole::Log, Resolve(m_Config.logRegularPath), logSize, false);
    if (!m_Fonts[Index(EditorFontRole::Log)]) {
        m_Fonts[Index(EditorFontRole::Log)] = m_Fonts[Index(EditorFontRole::UI)];
        SetWarning(m_LastWarning.empty()
            ? "Log font missing; using UI font"
            : m_LastWarning + "; log font missing; using UI font");
    }

    g_ActiveFonts = m_Fonts;
    io.FontGlobalScale = 1.0f;
    if (backend) backend->RebuildFontTexture();
    m_Dirty = false;
    m_LastRebuilt = true;
    return true;
#else
    (void)effectiveScale;
    (void)backend;
    m_Dirty = false;
    m_LastRebuilt = true;
    return true;
#endif
}

ImFont* EditorFontManager::GetFont(EditorFontRole role) const
{
    const size_t index = Index(role);
    return index < m_Fonts.size() ? m_Fonts[index] : nullptr;
}

bool EditorFontManager::HasFont(EditorFontRole role) const
{
    return GetFont(role) != nullptr;
}

ImFont* EditorFontManager::GetActiveFont(EditorFontRole role)
{
    const size_t index = Index(role);
    return index < g_ActiveFonts.size() ? g_ActiveFonts[index] : nullptr;
}

bool EditorFontManager::HasActiveFont(EditorFontRole role)
{
    return GetActiveFont(role) != nullptr;
}

const EditorFontConfig& EditorFontManager::GetDefaultConfig()
{
    return kDefaultConfig;
}

std::filesystem::path EditorFontManager::Resolve(const std::filesystem::path& path) const
{
    if (path.is_absolute() || m_FontRoot.empty()) return path;
    return (m_FontRoot / path).lexically_normal();
}

ImFont* EditorFontManager::LoadFont(EditorFontRole role,
                                    const std::filesystem::path& path,
                                    float sizePixels,
                                    bool mergeIcons)
{
#if defined(MYENGINE_ENABLE_IMGUI)
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec) return nullptr;
    ImFontConfig config;
    config.SizePixels = sizePixels;
    config.MergeMode = mergeIcons;
    config.PixelSnapH = mergeIcons;
    if (mergeIcons) {
        config.GlyphMinAdvanceX = sizePixels;
    }
    const std::string name = path.filename().string();
    std::snprintf(config.Name, sizeof(config.Name), "%s",
                  name.empty() ? "EditorFont" : name.c_str());
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        path.string().c_str(), sizePixels, &config,
        mergeIcons ? kFontAwesomeRanges : nullptr);
    if (!font) {
        const char* roleName = role == EditorFontRole::Log ? "log" :
            role == EditorFontRole::Icon ? "icon" :
            role == EditorFontRole::UIEmphasis ? "UI emphasis" : "UI";
        SetWarning(m_LastWarning.empty()
            ? std::string("Failed to load ") + roleName + " font: " + path.string()
            : m_LastWarning + "; failed to load " + roleName + " font: " + path.string());
    }
    return font;
#else
    (void)role;
    (void)path;
    (void)sizePixels;
    (void)mergeIcons;
    return nullptr;
#endif
}

void EditorFontManager::SetWarning(std::string message)
{
    m_LastWarning = std::move(message);
}

} // namespace Editor::UI
