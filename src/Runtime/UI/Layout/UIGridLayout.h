#pragma once

#include "UI/Layout/UILayoutGroup.h"

class UIGridLayout final : public UILayoutGroup {
public:
    int columns = 1;
    int rows = 1;
    const char* GetRmlClass() const { return "ui-layout-grid"; }
};
