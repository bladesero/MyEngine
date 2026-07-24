#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Physics/CollisionShapes.h"
#include "Physics/ColliderComponent.h"

class MYENGINE_RUNTIME_API CapsuleColliderComponent final : public ColliderComponent {
public:
    const char* GetTypeName() const override { return "CapsuleCollider"; }
    float GetRadius() const { return m_Radius; }
    float GetHalfHeight() const { return m_HalfHeight; }
    void SetRadius(float radius);
    void SetHalfHeight(float halfHeight);
    CapsuleShape GetWorldShape() const;
    AABB GetWorldBounds() const { return ComputeBounds(GetWorldShape()); }
    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    float m_Radius = 0.5f;
    float m_HalfHeight = 0.5f;
};
