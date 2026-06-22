#pragma once

#include "UI/Widgets/UIWidget.h"

class UIProgressBar final : public UIWidget {
public:
    float value = 0.0f;
    float maxValue = 1.0f;
    const char* GetRmlTag() const { return "progress"; }
};
