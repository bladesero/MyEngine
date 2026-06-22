#pragma once

#include "Math/Vector2.h"

struct RectTransform {
    Math::Vec2 anchorMin{0.0f, 0.0f};
    Math::Vec2 anchorMax{1.0f, 1.0f};
    Math::Vec2 offsetMin{0.0f, 0.0f};
    Math::Vec2 offsetMax{0.0f, 0.0f};
    Math::Vec2 pivot{0.5f, 0.5f};
};
