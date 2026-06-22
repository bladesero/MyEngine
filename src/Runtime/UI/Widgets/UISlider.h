#pragma once

#include "UI/Widgets/UIWidget.h"

class UISlider final : public UIWidget {
public:
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value = 0.0f;
    const char* GetRmlTag() const { return "input"; }
};
