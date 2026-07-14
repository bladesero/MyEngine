#pragma once

#include "Math/Vector3.h"

namespace Math {

// origin + direction; direction is expected to be unit length.
struct Ray {
    Vec3 origin{};
    Vec3 direction{0.0f, 0.0f, 1.0f};

    Vec3 At(float t) const { return origin + direction * t; }

    float DistanceSqToPoint(const Vec3& p) const {
        const Vec3 closest = ClosestPoint(p);
        const Vec3 d = p - closest;
        return d.Dot(d);
    }

    Vec3 ClosestPoint(const Vec3& p) const {
        const Vec3 v = p - origin;
        float t = v.Dot(direction);
        if (t < 0.0f)
            t = 0.0f;
        return origin + direction * t;
    }
};

} // namespace Math
