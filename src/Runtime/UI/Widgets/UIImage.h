#pragma once

#include "UI/Widgets/UIWidget.h"

#include <utility>

class UIImage final : public UIWidget {
public:
    using UIWidget::UIWidget;
    const std::string& GetSource() const { return m_Source; }
    void SetSource(std::string source) { m_Source = std::move(source); }
    const char* GetRmlTag() const { return "img"; }

private:
    std::string m_Source;
};
