#pragma once

#include "Core/EngineMath.h"
#include <map>
#include <utility>

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
    void SetGravity(const Vec3& gravity) { m_Gravity = gravity; }
    const Vec3& GetGravity() const { return m_Gravity; }

    void Step(Scene& scene, float deltaSeconds);
    bool Raycast(const Scene& scene, const Ray& ray, float maxDistance,
                 uint32_t layerMask, RaycastHit& hit) const;

private:
    void Substep(Scene& scene, float deltaSeconds);

    Vec3 m_Gravity = Vec3(0.0f, -9.81f, 0.0f);
    float m_Accumulator = 0.0f;
    float m_FixedDelta = 1.0f / 60.0f;
    std::map<std::pair<uint64_t, uint64_t>, bool> m_ActivePairs;
};
