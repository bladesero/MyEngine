#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Core/EngineMath.h"
#include "Physics/CollisionShapes.h"
#include "Physics/ColliderComponent.h"

class MYENGINE_RUNTIME_API BoxColliderComponent final : public ColliderComponent {
public:
    const char* GetTypeName() const override { return "BoxCollider"; }

    const Vec3& GetHalfExtents() const { return m_HalfExtents; }
    void SetHalfExtents(const Vec3& halfExtents);
    Vec3 GetWorldHalfExtents() const;
    OrientedBox GetWorldShape() const;
    AABB GetWorldBounds() const;

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    Vec3 m_HalfExtents = Vec3(0.5f);
};
