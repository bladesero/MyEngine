#pragma once

#include "UI/Core/RectTransform.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
}

enum class UICanvasSpace {
    Screen,
};

enum class UIInputMode {
    None,
    UIOnly,
    GameAndUI,
};

class UICanvas {
public:
    UICanvas() = default;
    ~UICanvas();

    UICanvas(const UICanvas&) = delete;
    UICanvas& operator=(const UICanvas&) = delete;

    void SetContext(Rml::Context* context);
    Rml::Context* GetContext() const { return m_Context; }
    Rml::ElementDocument* GetDocument() const { return m_Document; }

    bool LoadDocument(const std::string& path);
    void CloseDocument();
    bool Reload();

    const std::string& GetDocumentPath() const { return m_DocumentPath; }
    void SetDocumentPath(std::string path) { m_DocumentPath = std::move(path); }
    const std::vector<std::string>& GetStylePaths() const { return m_StylePaths; }
    void SetStylePaths(std::vector<std::string> paths) { m_StylePaths = std::move(paths); }
    const std::vector<std::string>& GetDefaultFontPaths() const { return m_DefaultFontPaths; }
    void SetDefaultFontPaths(std::vector<std::string> paths) { m_DefaultFontPaths = std::move(paths); }

    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool visible);
    bool IsInteractive() const { return m_Interactive; }
    void SetInteractive(bool interactive) { m_Interactive = interactive; }
    int GetSortOrder() const { return m_SortOrder; }
    void SetSortOrder(int order) { m_SortOrder = order; }
    UICanvasSpace GetCanvasSpace() const { return m_CanvasSpace; }
    void SetCanvasSpace(UICanvasSpace space) { m_CanvasSpace = space; }
    UIInputMode GetInputMode() const { return m_InputMode; }
    void SetInputMode(UIInputMode mode) { m_InputMode = mode; }

private:
    Rml::Context* m_Context = nullptr;
    Rml::ElementDocument* m_Document = nullptr;
    std::string m_DocumentPath;
    std::vector<std::string> m_StylePaths;
    std::vector<std::string> m_DefaultFontPaths;
    bool m_Visible = true;
    bool m_Interactive = true;
    int m_SortOrder = 0;
    UICanvasSpace m_CanvasSpace = UICanvasSpace::Screen;
    UIInputMode m_InputMode = UIInputMode::GameAndUI;
};
