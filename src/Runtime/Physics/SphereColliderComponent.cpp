#include "Physics/SphereColliderComponent.h"

#include "Scene/Actor.h"

#include <algorithm>
#include <cmath>

void SphereColliderComponent::SetRadius(float radius)
{
    m_Radius = (std::max)(0.001f, std::fabs(radius));
}

SphereShape SphereColliderComponent::GetWorldShape() const
{
    SphereShape shape;
    if (!GetOwner()) {
        shape.radius = m_Radius;
        return shape;
    }
    const Mat4 world = GetOwner()->GetWorldMatrix();
    const float scale = (std::max)({
        world.TransformDir(Vec3::Right()).Length(),
        world.TransformDir(Vec3::Up()).Length(),
        world.TransformDir(Vec3::Forward()).Length()
    });
    shape.center = GetOwner()->GetWorldPosition();
    shape.radius = m_Radius * scale;
    return shape;
}

void SphereColliderComponent::Serialize(nlohmann::json& data) const
{
    SerializeCollider(data);
    data["radius"] = m_Radius;
}

void SphereColliderComponent::Deserialize(const nlohmann::json& data)
{
    DeserializeCollider(data);
    SetRadius(data.value("radius", 0.5f));
}
