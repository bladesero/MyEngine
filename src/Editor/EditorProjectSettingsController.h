#pragma once

class EditorLayer;

class EditorProjectSettingsController {
public:
    static void Open(EditorLayer& layer);
    static void Draw(EditorLayer& layer);

private:
    static void DrawProjectSettingsTab(EditorLayer& layer);
    static void DrawGraphicsSettingsTab(EditorLayer& layer);
    static void DrawGameplayInputSettingsTab(EditorLayer& layer);
    static void DrawLayoutSettingsTab(EditorLayer& layer);
    static void DrawAppearanceSettingsTab(EditorLayer& layer);
};
