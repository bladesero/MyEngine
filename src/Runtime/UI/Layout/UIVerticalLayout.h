#pragma once

#include "UI/Layout/UILayoutGroup.h"

class UIVerticalLayout final : public UILayoutGroup {
public:
    const char* GetRmlClass() const { return "ui-layout-vertical"; }
};
