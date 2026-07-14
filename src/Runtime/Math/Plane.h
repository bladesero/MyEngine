#pragma once

#include <cmath>

#include "Math/Ray.h"

namespace Math {

// Unit normal n and distance d: dot(n, p) == d for points on the plane
// (signed distance from origin along n is d).
struct Plane {
    Vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;

    static Plane FromPointNormal(const Vec3& point, const Vec3& n) {
        const Vec3 nn = n.Normalized();
        Plane p;
        p.normal = nn;
        p.distance = nn.Dot(point);
        return p;
    }

    float SignedDistance(const Vec3& p) const { return normal.Dot(p) - distance; }

    // Returns false if ray is parallel to the plane or intersection is behind the ray origin.
    bool IntersectRay(const Ray& ray, float& tOut) const {
        const float denom = normal.Dot(ray.direction);
        static constexpr float kEps = 1e-6f;
        if (std::abs(denom) < kEps)
            return false;
        const float t = (distance - normal.Dot(ray.origin)) / denom;
        if (t < 0.0f)
            return false;
        tOut = t;
        return true;
    }
};

} // namespace Math
