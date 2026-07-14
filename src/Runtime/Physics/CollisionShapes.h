#pragma once

#include "Core/EngineMath.h"

#include <array>

struct OrientedBox {
    Vec3 center = Vec3::Zero();
    Vec3 halfExtents = Vec3(0.5f);
    std::array<Vec3, 3> axes = {Vec3::Right(), Vec3::Up(), Vec3::Forward()};
};

struct SphereShape {
    Vec3 center = Vec3::Zero();
    float radius = 0.5f;
};

struct CapsuleShape {
    Vec3 pointA = Vec3(0.0f, -0.5f, 0.0f);
    Vec3 pointB = Vec3(0.0f, 0.5f, 0.0f);
    float radius = 0.5f;
};

struct ContactManifold {
    Vec3 normal = Vec3::Up(); // points from shape B toward shape A
    float depth = 0.0f;
    Vec3 point = Vec3::Zero();
};

AABB ComputeBounds(const OrientedBox& box);
AABB ComputeBounds(const SphereShape& sphere);
AABB ComputeBounds(const CapsuleShape& capsule);

bool Collide(const OrientedBox& a, const OrientedBox& b, ContactManifold& contact);
bool Collide(const SphereShape& a, const SphereShape& b, ContactManifold& contact);
bool Collide(const SphereShape& sphere, const OrientedBox& box, ContactManifold& contact);
bool Collide(const CapsuleShape& a, const CapsuleShape& b, ContactManifold& contact);
bool Collide(const CapsuleShape& capsule, const SphereShape& sphere, ContactManifold& contact);
bool Collide(const CapsuleShape& capsule, const OrientedBox& box, ContactManifold& contact);
