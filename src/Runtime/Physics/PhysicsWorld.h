#pragma once

#include "Scene/SceneSubsystems.h"

#include <cstdint>
#include <memory>
#include <vector>

class PhysicsWorld final : public IScenePhysicsSubsystem {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void SetGravity(const Vec3& gravity) override;
    const Vec3& GetGravity() const override { return m_Gravity; }
    void Clear() override;
    void Step(Scene& scene, float deltaSeconds) override;
    void StepFixed(Scene& scene, float fixedDeltaSeconds) override;
    bool Raycast(const Scene& scene, const Ray& ray, float maxDistance, uint32_t layerMask,
                 RaycastHit& hit) const override;
    bool OverlapSphere(const Scene& scene, const Vec3& center, float radius, uint32_t layerMask,
                       std::vector<ActorHandle>& outActors) const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_Impl;
    Vec3 m_Gravity = Vec3(0.0f, -9.81f, 0.0f);
    float m_FixedDelta = 1.0f / 60.0f;
};
