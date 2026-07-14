#pragma once

#include <cmath>
#include <algorithm>

#include "Math/Vector3.h"

namespace Math {
struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    // 构造
    constexpr Vec4() = default;
    constexpr Vec4(float v) : x(v), y(v), z(v), w(v) {}
    constexpr Vec4(float vx, float vy, float vz, float vw) : x(vx), y(vy), z(vz), w(vw) {}
    constexpr Vec4(float vx, float vy, float vz) : x(vx), y(vy), z(vz), w(1.0f) {}
    constexpr Vec4(float vx, float vy) : x(vx), y(vy), z(0.0f), w(1.0f) {}

    // 静态常量
    static Vec4 Zero() { return {0, 0, 0, 1}; }
    static Vec4 One() { return {1, 1, 1, 1}; }
    static Vec4 Up() { return {0, 1, 0, 1}; }
    static Vec4 Forward() { return {0, 0, 1, 1}; }
    static Vec4 Right() { return {1, 0, 0, 1}; }

    // 运算符重载
    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Vec4 operator/(float s) const { return {x / s, y / s, z / s, w / s}; }
    Vec4 operator+(float s) const { return {x + s, y + s, z + s, w + s}; }
    Vec4 operator-(float s) const { return {x - s, y - s, z - s, w - s}; }
    Vec4& operator+=(const Vec4& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        w += o.w;
        return *this;
    }
    Vec4& operator-=(const Vec4& o) {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        w -= o.w;
        return *this;
    }
    Vec4& operator*=(float s) {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }
    Vec4& operator/=(float s) {
        x /= s;
        y /= s;
        z /= s;
        w /= s;
        return *this;
    }

    // 数学方法
    float Dot(const Vec4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    float LengthSq() const { return x * x + y * y + z * z + w * w; }
    float Length() const { return std::sqrt(LengthSq()); }
    Vec4 Normalized() const {
        float l = Length();
        return l > 1e-8f ? Vec4{x / l, y / l, z / l, w / l} : Vec4{};
    }

    Vec3 XYZ() const { return {x, y, z}; }

    static Vec4 FromVec3(const Vec3& v, float ww = 1.0f) { return {v.x, v.y, v.z, ww}; }
};
} // namespace Math
