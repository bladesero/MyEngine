#pragma once

#include "Core/EngineMath.h"

#include <cstdint>
#include <memory>

class Scene;
class Actor;

struct RaycastHit {
    Actor* actor = nullptr;
    Vec3 point = Vec3::Zero();
    Vec3 normal = Vec3::Up();
    float distance = 0.0f;
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void SetGravity(const Vec3& gravity);
    const Vec3& GetGravity() const { return m_Gravity; }
    void Clear();
    void Step(Scene& scene, float deltaSeconds);
    bool Raycast(const Scene& scene, const Ray& ray, float maxDistance,
                 uint32_t layerMask, RaycastHit& hit) const;

private:
    class Impl;
    std::unique_ptr<Impl> m_Impl;
    Vec3 m_Gravity = Vec3(0.0f, -9.81f, 0.0f);
    float m_Accumulator = 0.0f;
    float m_FixedDelta = 1.0f / 60.0f;
};
