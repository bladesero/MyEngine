#pragma once

#include <cmath>
#include <cstdint>

#include "Math/Vector4.h"

namespace Math {

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    Vec4 ToVec4() const { return {r, g, b, a}; }

    static Color FromVec4(const Vec4& v) { return {v.x, v.y, v.z, v.w}; }

    static Color Lerp(const Color& a, const Color& b, float t) {
        return {
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t,
        };
    }

    // Simple gamma-style sRGB 8-bit to linear RGBA in 0..1.
    static Color FromSRGB8(uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa = 255) {
        static constexpr float kInv = 1.0f / 255.0f;
        const auto toLin = [](uint8_t c) { return std::pow(static_cast<float>(c) * kInv, 2.2f); };
        return {toLin(rr), toLin(gg), toLin(bb), static_cast<float>(aa) * kInv};
    }
};

} // namespace Math
