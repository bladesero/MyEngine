#include "Physics/CapsuleColliderComponent.h"

#include "Scene/Actor.h"

#include <algorithm>
#include <cmath>

void CapsuleColliderComponent::SetRadius(float radius)
{
    m_Radius = (std::max)(0.001f, std::fabs(radius));
}

void CapsuleColliderComponent::SetHalfHeight(float halfHeight)
{
    m_HalfHeight = (std::max)(0.0f, std::fabs(halfHeight));
}

CapsuleShape CapsuleColliderComponent::GetWorldShape() const
{
    CapsuleShape shape;
    if (!GetOwner()) {
        shape.pointA = Vec3(0.0f, -m_HalfHeight, 0.0f);
        shape.pointB = Vec3(0.0f, m_HalfHeight, 0.0f);
        shape.radius = m_Radius;
        return shape;
    }
    const Mat4 world = GetOwner()->GetWorldMatrix();
    const Vec3 upVector = world.TransformDir(Vec3::Up());
    const float upScale = upVector.Length();
    const Vec3 axis = upScale > 1e-6f ? upVector / upScale : Vec3::Up();
    const float radialScale = (std::max)(
        world.TransformDir(Vec3::Right()).Length(),
        world.TransformDir(Vec3::Forward()).Length());
    const Vec3 center = GetOwner()->GetWorldPosition();
    const Vec3 extent = axis * (m_HalfHeight * upScale);
    shape.pointA = center - extent;
    shape.pointB = center + extent;
    shape.radius = m_Radius * radialScale;
    return shape;
}

void CapsuleColliderComponent::Serialize(nlohmann::json& data) const
{
    SerializeCollider(data);
    data["radius"] = m_Radius;
    data["halfHeight"] = m_HalfHeight;
}

void CapsuleColliderComponent::Deserialize(const nlohmann::json& data)
{
    DeserializeCollider(data);
    SetRadius(data.value("radius", 0.5f));
    SetHalfHeight(data.value("halfHeight", 0.5f));
}
