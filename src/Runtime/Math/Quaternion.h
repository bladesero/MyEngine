#pragma once

#include "Math/Vector3.h"
#include <algorithm>
#include <cmath>

struct Mat4;

namespace Math {

// Unit quaternion (x, y, z, w), Hamilton product, matches LH Y-up + row-vector Mat4
// in this project (see Quat::ToMat4 in EngineMath.h).
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static constexpr Quat Identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    // Axis must be non-zero; normalized internally. Angle in radians (right-hand
    // around axis; consistent with Mat4::Rotation(axis, angle)).
    static Quat FromAxisAngle(const Vec3& axis, float angleRad);

    // Radians. Intrinsic order: roll (Z) -> pitch (X) -> yaw (Y), i.e. q = qy * qx * qz.
    static Quat FromEulerRad(float pitch, float yaw, float roll);

    float LengthSq() const { return x * x + y * y + z * z + w * w; }
    float Length() const { return std::sqrt(LengthSq()); }

    Quat Normalized() const {
        const float len = Length();
        return len > 1e-8f ? Quat{x / len, y / len, z / len, w / len} : Identity();
    }

    Quat Conjugate() const { return {-x, -y, -z, w}; }

    // Inverse for unit quaternion (same as conjugate).
    Quat Inverse() const {
        const float ls = LengthSq();
        if (ls < 1e-16f)
            return Identity();
        const float inv = 1.0f / ls;
        return {-x * inv, -y * inv, -z * inv, w * inv};
    }

    Quat operator*(const Quat& o) const {
        return {
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z,
        };
    }

    Quat& operator*=(const Quat& o) {
        *this = *this * o;
        return *this;
    }

    bool operator==(const Quat& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }

    float Dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    // Rotates vector v (same convention as v * R from ToMat4()).
    Vec3 Rotate(const Vec3& v) const {
        const Vec3 qv(x, y, z);
        const Vec3 t = qv.Cross(v) * 2.0f;
        return v + t * w + qv.Cross(t);
    }

    Mat4 ToMat4() const;

    static Quat FromMat4(const Mat4& m);
};

inline float Dot(const Quat& a, const Quat& b) {
    return a.Dot(b);
}

inline Quat Slerp(const Quat& a, const Quat& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float cosHalf = a.Dot(b);
    Quat b2 = b;
    if (cosHalf < 0.0f) {
        cosHalf = -cosHalf;
        b2 = {-b.x, -b.y, -b.z, -b.w};
    }
    if (cosHalf > 0.9995f) {
        const Quat r = {
            a.x + t * (b2.x - a.x),
            a.y + t * (b2.y - a.y),
            a.z + t * (b2.z - a.z),
            a.w + t * (b2.w - a.w),
        };
        return r.Normalized();
    }
    const float half = std::acos(cosHalf);
    const float invSin = 1.0f / std::sin(half);
    const float k0 = std::sin((1.0f - t) * half) * invSin;
    const float k1 = std::sin(t * half) * invSin;
    return {
        k0 * a.x + k1 * b2.x,
        k0 * a.y + k1 * b2.y,
        k0 * a.z + k1 * b2.z,
        k0 * a.w + k1 * b2.w,
    };
}

inline Quat Quat::FromAxisAngle(const Vec3& axis, float angleRad) {
    const float lenSq = axis.LengthSq();
    if (lenSq < 1e-16f)
        return Identity();
    const float invLen = 1.0f / std::sqrt(lenSq);
    const float hx = angleRad * 0.5f;
    const float s = std::sin(hx);
    const float c = std::cos(hx);
    return {axis.x * invLen * s, axis.y * invLen * s, axis.z * invLen * s, c};
}

inline Quat Quat::FromEulerRad(float pitch, float yaw, float roll) {
    const Quat qx = FromAxisAngle(Vec3::Right(), pitch);
    const Quat qy = FromAxisAngle(Vec3::Up(), yaw);
    const Quat qz = FromAxisAngle(Vec3::Forward(), roll);
    return (qy * qx * qz).Normalized();
}

} // namespace Math
