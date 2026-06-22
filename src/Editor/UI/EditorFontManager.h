#pragma once

#include <array>
#include <filesystem>
#include <string>

class EditorImGuiBackend;
struct ImFont;

namespace Editor::UI {

enum class EditorFontRole {
    UI = 0,
    UIEmphasis,
    Log,
    Icon,
    Count,
};

struct EditorFontConfig {
    std::filesystem::path uiRegularPath = "Inter-Regular.ttf";
    std::filesystem::path uiSemiBoldPath = "Inter-SemiBold.ttf";
    std::filesystem::path logRegularPath = "JetBrainsMono-Regular.ttf";
    std::filesystem::path iconSolidPath = "FontAwesome-Free-Solid-900.ttf";
    float baseUIFontSize = 13.0f;
    float baseLogFontSize = 13.0f;
    float baseIconFontSize = 13.0f;
};

class EditorFontManager {
public:
    void SetFontRoot(std::filesystem::path root);
    const std::filesystem::path& GetFontRoot() const { return m_FontRoot; }
    void MarkDirty() { m_Dirty = true; }
    bool RebuildIfNeeded(float effectiveScale, EditorImGuiBackend* backend);

    ImFont* GetFont(EditorFontRole role) const;
    bool HasFont(EditorFontRole role) const;
    bool WasRebuilt() const { return m_LastRebuilt; }
    const std::string& GetLastWarning() const { return m_LastWarning; }

    static ImFont* GetActiveFont(EditorFontRole role);
    static bool HasActiveFont(EditorFontRole role);
    static const EditorFontConfig& GetDefaultConfig();

private:
    std::filesystem::path Resolve(const std::filesystem::path& path) const;
    ImFont* LoadFont(EditorFontRole role,
                     const std::filesystem::path& path,
                     float sizePixels,
                     bool mergeIcons);
    void SetWarning(std::string message);
    static size_t Index(EditorFontRole role) { return static_cast<size_t>(role); }

    std::filesystem::path m_FontRoot;
    EditorFontConfig m_Config;
    std::array<ImFont*, static_cast<size_t>(EditorFontRole::Count)> m_Fonts{};
    bool m_Dirty = true;
    bool m_LastRebuilt = false;
    std::string m_LastWarning;
};

} // namespace Editor::UI
