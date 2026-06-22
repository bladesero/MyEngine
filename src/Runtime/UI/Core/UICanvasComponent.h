#pragma once

#include "Scene/Component.h"
#include "UI/Core/UICanvas.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

class UICanvasComponent final : public Component {
public:
    UICanvasComponent();
    ~UICanvasComponent() override;

    const char* GetTypeName() const override { return "UICanvas"; }
    int GetExecutionOrder() const override { return 100; }

    UICanvas& GetCanvas() { return *m_Canvas; }
    const UICanvas& GetCanvas() const { return *m_Canvas; }

    bool LoadDocument(const std::string& path);
    bool Reload();

    const std::string& GetDocumentPath() const { return m_Canvas->GetDocumentPath(); }
    void SetDocumentPath(std::string path) { m_Canvas->SetDocumentPath(std::move(path)); }
    const std::vector<std::string>& GetStylePaths() const { return m_Canvas->GetStylePaths(); }
    void SetStylePaths(std::vector<std::string> paths) { m_Canvas->SetStylePaths(std::move(paths)); }
    const std::vector<std::string>& GetDefaultFontPaths() const { return m_Canvas->GetDefaultFontPaths(); }
    void SetDefaultFontPaths(std::vector<std::string> paths) { m_Canvas->SetDefaultFontPaths(std::move(paths)); }

    bool IsVisible() const { return m_Canvas->IsVisible(); }
    void SetVisible(bool visible) { m_Canvas->SetVisible(visible); }
    bool IsInteractive() const { return m_Canvas->IsInteractive(); }
    void SetInteractive(bool interactive) { m_Canvas->SetInteractive(interactive); }
    int GetSortOrder() const { return m_Canvas->GetSortOrder(); }
    void SetSortOrder(int order) { m_Canvas->SetSortOrder(order); }
    UIInputMode GetInputMode() const { return m_Canvas->GetInputMode(); }
    void SetInputMode(UIInputMode mode) { m_Canvas->SetInputMode(mode); }

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    std::unique_ptr<UICanvas> m_Canvas;
};
