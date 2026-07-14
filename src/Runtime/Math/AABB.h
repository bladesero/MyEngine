#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "Math/Ray.h"
#include "Math/Vector3.h"

namespace Math {

struct AABB {
    Vec3 min{};
    Vec3 max{};

    static AABB FromCenterHalfExtents(const Vec3& c, const Vec3& half) {
        AABB b;
        b.min = c - half;
        b.max = c + half;
        return b;
    }

    void Expand(const Vec3& p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }

    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Extents() const { return (max - min) * 0.5f; }
    float Radius() const { return Extents().Length(); }

    void Merge(const AABB& o) {
        min.x = std::min(min.x, o.min.x);
        min.y = std::min(min.y, o.min.y);
        min.z = std::min(min.z, o.min.z);
        max.x = std::max(max.x, o.max.x);
        max.y = std::max(max.y, o.max.y);
        max.z = std::max(max.z, o.max.z);
    }

    bool Contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }

    bool Intersects(const AABB& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y && min.z <= o.max.z &&
               max.z >= o.min.z;
    }

    // Slab ray-AABB; returns false if ray misses. tMin/tMax are distances along ray (tMin <= tMax).
    bool IntersectRay(const Ray& ray, float& tMin, float& tMax) const {
        tMin = 0.0f;
        tMax = std::numeric_limits<float>::max();

        for (int i = 0; i < 3; ++i) {
            const float o = i == 0 ? ray.origin.x : (i == 1 ? ray.origin.y : ray.origin.z);
            const float d = i == 0 ? ray.direction.x : (i == 1 ? ray.direction.y : ray.direction.z);
            const float bmin = i == 0 ? min.x : (i == 1 ? min.y : min.z);
            const float bmax = i == 0 ? max.x : (i == 1 ? max.y : max.z);

            static constexpr float kEps = 1e-8f;
            if (std::abs(d) < kEps) {
                if (o < bmin || o > bmax)
                    return false;
                continue;
            }
            const float invD = 1.0f / d;
            float t0 = (bmin - o) * invD;
            float t1 = (bmax - o) * invD;
            if (t0 > t1)
                std::swap(t0, t1);
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMax < tMin)
                return false;
        }
        return true;
    }
};

} // namespace Math
