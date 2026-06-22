#pragma once

#include "Editor/UI/EditorStyleTokens.h"

#include <string>

namespace Editor::UI {

struct EditorTheme {
    std::string id = "dark";
    std::string displayName = "Dark";
    EditorStyleTokens tokens;
};

class EditorThemeManager {
public:
    void Initialize(std::string themeID = "dark");
    void SetThemeID(std::string themeID);
    void Apply(float effectiveScale);

    const std::string& GetThemeID() const { return m_Theme.id; }
    const EditorTheme& GetTheme() const { return m_Theme; }
    const EditorStyleTokens& GetTokens() const { return m_Theme.tokens; }

    static EditorTheme CreateDefaultTheme();
    static std::string NormalizeThemeID(const std::string& themeID);
    static float ScaleValue(float value, float effectiveScale);

private:
    EditorTheme m_Theme;
};

} // namespace Editor::UI
