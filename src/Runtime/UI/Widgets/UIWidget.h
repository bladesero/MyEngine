#pragma once

#include "UI/Core/UIElement.h"

#include <string>
#include <utility>

class UIWidget : public UIElement {
public:
    using UIElement::UIElement;

    const std::string& GetText() const { return m_Text; }
    void SetText(std::string text) { m_Text = std::move(text); }
    const std::string& GetClassName() const { return m_ClassName; }
    void SetClassName(std::string className) { m_ClassName = std::move(className); }

private:
    std::string m_Text;
    std::string m_ClassName;
};
