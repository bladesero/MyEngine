#pragma once

#include "UI/Widgets/UIWidget.h"

class UIScrollView final : public UIWidget {
public:
    using UIWidget::UIWidget;
    const char* GetRmlTag() const { return "div"; }
    const char* GetRmlClass() const { return "ui-scroll-view"; }
};
