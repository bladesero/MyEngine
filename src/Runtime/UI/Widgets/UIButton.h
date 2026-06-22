#pragma once

#include "UI/Widgets/UIWidget.h"

class UIButton final : public UIWidget {
public:
    using UIWidget::UIWidget;
    const char* GetRmlTag() const { return "button"; }
};
