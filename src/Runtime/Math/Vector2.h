#pragma once

#include <cmath>
#include <algorithm>

namespace Math
{
    struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float vx, float vy) : x(vx), y(vy) {}
    constexpr Vec2(float v) : x(v), y(v) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s)       const { return {x * s, y * s}; }
    Vec2 operator/(float s)       const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x+=o.x; y+=o.y; return *this; }

    float Dot(const Vec2& o)  const { return x*o.x + y*o.y; }
    float LengthSq()          const { return x*x + y*y; }
    float Length()            const { return std::sqrt(LengthSq()); }
    Vec2  Normalized()        const {
        float l = Length();
        return l > 1e-8f ? Vec2{x/l, y/l} : Vec2{};
    }

    static Vec2 Lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
    };
}