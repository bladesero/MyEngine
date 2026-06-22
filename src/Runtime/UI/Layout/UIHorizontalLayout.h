#pragma once

#include "UI/Layout/UILayoutGroup.h"

class UIHorizontalLayout final : public UILayoutGroup {
public:
    const char* GetRmlClass() const { return "ui-layout-horizontal"; }
};
