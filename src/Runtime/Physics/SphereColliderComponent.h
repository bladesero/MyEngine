#pragma once

#include "Physics/CollisionShapes.h"
#include "Physics/ColliderComponent.h"

class SphereColliderComponent final : public ColliderComponent {
public:
    const char* GetTypeName() const override { return "SphereCollider"; }
    float GetRadius() const { return m_Radius; }
    void SetRadius(float radius);
    SphereShape GetWorldShape() const;
    AABB GetWorldBounds() const { return ComputeBounds(GetWorldShape()); }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    float m_Radius = 0.5f;
};
