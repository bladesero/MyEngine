#include "Physics/CollisionShapes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

float Component(const Vec3& value, size_t axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

float ProjectRadius(const OrientedBox& box, const Vec3& axis)
{
    return std::fabs(axis.Dot(box.axes[0])) * box.halfExtents.x +
           std::fabs(axis.Dot(box.axes[1])) * box.halfExtents.y +
           std::fabs(axis.Dot(box.axes[2])) * box.halfExtents.z;
}

bool TestAxis(const OrientedBox& a, const OrientedBox& b, const Vec3& rawAxis,
              const Vec3& delta, float& bestDepth, Vec3& bestNormal)
{
    const float lengthSq = rawAxis.LengthSq();
    if (lengthSq < 1e-8f) return true;
    const Vec3 axis = rawAxis / std::sqrt(lengthSq);
    const float distance = std::fabs(delta.Dot(axis));
    const float overlap = ProjectRadius(a, axis) + ProjectRadius(b, axis) - distance;
    if (overlap <= 0.0f) return false;
    if (overlap < bestDepth) {
        bestDepth = overlap;
        bestNormal = delta.Dot(axis) >= 0.0f ? axis : axis * -1.0f;
    }
    return true;
}

Vec3 ClosestPoint(const OrientedBox& box, const Vec3& point)
{
    Vec3 result = box.center;
    const Vec3 delta = point - box.center;
    for (size_t axis = 0; axis < 3; ++axis) {
        const float extent = Component(box.halfExtents, axis);
        const float distance = std::clamp(delta.Dot(box.axes[axis]), -extent, extent);
        result += box.axes[axis] * distance;
    }
    return result;
}

Vec3 ClosestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& point)
{
    const Vec3 ab = b - a;
    const float denom = ab.LengthSq();
    if (denom < 1e-8f) return a;
    const float t = std::clamp((point - a).Dot(ab) / denom, 0.0f, 1.0f);
    return a + ab * t;
}

void ClosestSegmentPoints(const Vec3& p1, const Vec3& q1,
                          const Vec3& p2, const Vec3& q2,
                          Vec3& c1, Vec3& c2)
{
    const Vec3 d1 = q1 - p1;
    const Vec3 d2 = q2 - p2;
    const Vec3 r = p1 - p2;
    const float a = d1.Dot(d1);
    const float e = d2.Dot(d2);
    const float f = d2.Dot(r);
    float s = 0.0f;
    float t = 0.0f;
    if (a <= 1e-8f && e <= 1e-8f) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= 1e-8f) {
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = d1.Dot(r);
        if (e <= 1e-8f) {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = d1.Dot(d2);
            const float denom = a * e - b * b;
            if (denom != 0.0f) s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

bool CollideSpheres(const Vec3& centerA, float radiusA,
                    const Vec3& centerB, float radiusB,
                    ContactManifold& contact)
{
    const Vec3 delta = centerA - centerB;
    const float distanceSq = delta.LengthSq();
    const float radius = radiusA + radiusB;
    if (distanceSq >= radius * radius) return false;
    const float distance = std::sqrt((std::max)(distanceSq, 1e-12f));
    contact.normal = distanceSq > 1e-12f ? delta / distance : Vec3::Up();
    contact.depth = radius - distance;
    contact.point = centerB + contact.normal * radiusB;
    return true;
}

} // namespace

AABB ComputeBounds(const OrientedBox& box)
{
    const Vec3 extent = {
        std::fabs(box.axes[0].x) * box.halfExtents.x +
            std::fabs(box.axes[1].x) * box.halfExtents.y +
            std::fabs(box.axes[2].x) * box.halfExtents.z,
        std::fabs(box.axes[0].y) * box.halfExtents.x +
            std::fabs(box.axes[1].y) * box.halfExtents.y +
            std::fabs(box.axes[2].y) * box.halfExtents.z,
        std::fabs(box.axes[0].z) * box.halfExtents.x +
            std::fabs(box.axes[1].z) * box.halfExtents.y +
            std::fabs(box.axes[2].z) * box.halfExtents.z,
    };
    return AABB::FromCenterHalfExtents(box.center, extent);
}

AABB ComputeBounds(const SphereShape& sphere)
{
    return AABB::FromCenterHalfExtents(sphere.center, Vec3(sphere.radius));
}

AABB ComputeBounds(const CapsuleShape& capsule)
{
    AABB bounds;
    bounds.min = {
        (std::min)(capsule.pointA.x, capsule.pointB.x) - capsule.radius,
        (std::min)(capsule.pointA.y, capsule.pointB.y) - capsule.radius,
        (std::min)(capsule.pointA.z, capsule.pointB.z) - capsule.radius,
    };
    bounds.max = {
        (std::max)(capsule.pointA.x, capsule.pointB.x) + capsule.radius,
        (std::max)(capsule.pointA.y, capsule.pointB.y) + capsule.radius,
        (std::max)(capsule.pointA.z, capsule.pointB.z) + capsule.radius,
    };
    return bounds;
}

bool Collide(const OrientedBox& a, const OrientedBox& b, ContactManifold& contact)
{
    const Vec3 delta = a.center - b.center;
    float bestDepth = std::numeric_limits<float>::max();
    Vec3 bestNormal = Vec3::Up();
    for (size_t i = 0; i < 3; ++i) {
        if (!TestAxis(a, b, a.axes[i], delta, bestDepth, bestNormal) ||
            !TestAxis(a, b, b.axes[i], delta, bestDepth, bestNormal)) return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            if (!TestAxis(a, b, a.axes[i].Cross(b.axes[j]),
                          delta, bestDepth, bestNormal)) return false;
        }
    }
    contact.normal = bestNormal;
    contact.depth = bestDepth;
    contact.point = (a.center + b.center) * 0.5f;
    return true;
}

bool Collide(const SphereShape& a, const SphereShape& b, ContactManifold& contact)
{
    return CollideSpheres(a.center, a.radius, b.center, b.radius, contact);
}

bool Collide(const SphereShape& sphere, const OrientedBox& box, ContactManifold& contact)
{
    const Vec3 closest = ClosestPoint(box, sphere.center);
    if (CollideSpheres(sphere.center, sphere.radius, closest, 0.0f, contact)) return true;
    if ((closest - sphere.center).LengthSq() > 1e-12f) return false;

    float bestDistance = std::numeric_limits<float>::max();
    Vec3 normal = Vec3::Up();
    const Vec3 local = sphere.center - box.center;
    for (size_t axis = 0; axis < 3; ++axis) {
        const float extent = Component(box.halfExtents, axis);
        const float coordinate = local.Dot(box.axes[axis]);
        const float distance = extent - std::fabs(coordinate);
        if (distance < bestDistance) {
            bestDistance = distance;
            normal = box.axes[axis] * (coordinate >= 0.0f ? 1.0f : -1.0f);
        }
    }
    contact.normal = normal;
    contact.depth = sphere.radius + bestDistance;
    contact.point = sphere.center - normal * sphere.radius;
    return true;
}

bool Collide(const CapsuleShape& a, const CapsuleShape& b, ContactManifold& contact)
{
    Vec3 pointA;
    Vec3 pointB;
    ClosestSegmentPoints(a.pointA, a.pointB, b.pointA, b.pointB, pointA, pointB);
    return CollideSpheres(pointA, a.radius, pointB, b.radius, contact);
}

bool Collide(const CapsuleShape& capsule, const SphereShape& sphere,
             ContactManifold& contact)
{
    const Vec3 point = ClosestPointOnSegment(
        capsule.pointA, capsule.pointB, sphere.center);
    return CollideSpheres(point, capsule.radius, sphere.center, sphere.radius, contact);
}

bool Collide(const CapsuleShape& capsule, const OrientedBox& box,
             ContactManifold& contact)
{
    bool hit = false;
    ContactManifold best;
    best.depth = 0.0f;
    for (int sample = 0; sample <= 8; ++sample) {
        const float t = static_cast<float>(sample) / 8.0f;
        SphereShape sphere;
        sphere.center = Vec3::Lerp(capsule.pointA, capsule.pointB, t);
        sphere.radius = capsule.radius;
        ContactManifold candidate;
        if (Collide(sphere, box, candidate) && candidate.depth > best.depth) {
            best = candidate;
            hit = true;
        }
    }
    if (hit) contact = best;
    return hit;
}
