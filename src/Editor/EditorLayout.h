#pragma once

struct EditorPanelRect {
    float x = 0, y = 0, width = 0, height = 0;
};
struct EditorMainRects {
    EditorPanelRect toolbar, outliner, viewport, inspector, assetBrowser, log;
};
class EditorLayout {
public:
    static EditorMainRects Compute(float x, float y, float width, float height);
};
