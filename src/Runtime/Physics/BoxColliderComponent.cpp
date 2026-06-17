#include "Physics/BoxColliderComponent.h"

#include "Scene/Actor.h"

#include <algorithm>
#include <cmath>

void BoxColliderComponent::SetHalfExtents(const Vec3& halfExtents)
{
    m_HalfExtents = {
        std::max(0.001f, std::fabs(halfExtents.x)),
        std::max(0.001f, std::fabs(halfExtents.y)),
        std::max(0.001f, std::fabs(halfExtents.z)),
    };
}

Vec3 BoxColliderComponent::GetWorldHalfExtents() const
{
    if (!GetOwner()) return m_HalfExtents;
    const Mat4 world = GetOwner()->GetWorldMatrix();
    return {
        std::fabs(world.m[0][0]) * m_HalfExtents.x +
            std::fabs(world.m[1][0]) * m_HalfExtents.y +
            std::fabs(world.m[2][0]) * m_HalfExtents.z,
        std::fabs(world.m[0][1]) * m_HalfExtents.x +
            std::fabs(world.m[1][1]) * m_HalfExtents.y +
            std::fabs(world.m[2][1]) * m_HalfExtents.z,
        std::fabs(world.m[0][2]) * m_HalfExtents.x +
            std::fabs(world.m[1][2]) * m_HalfExtents.y +
            std::fabs(world.m[2][2]) * m_HalfExtents.z,
    };
}

OrientedBox BoxColliderComponent::GetWorldShape() const
{
    OrientedBox box;
    box.halfExtents = m_HalfExtents;
    if (!GetOwner()) return box;

    const Mat4 world = GetOwner()->GetWorldMatrix();
    const Vec3 transformedAxes[3] = {
        world.TransformDir(Vec3::Right()),
        world.TransformDir(Vec3::Up()),
        world.TransformDir(Vec3::Forward()),
    };
    box.center = GetOwner()->GetWorldPosition();
    for (size_t axis = 0; axis < 3; ++axis) {
        const float scale = transformedAxes[axis].Length();
        box.axes[axis] = scale > 1e-6f
            ? transformedAxes[axis] / scale
            : (axis == 0 ? Vec3::Right() : (axis == 1 ? Vec3::Up() : Vec3::Forward()));
        if (axis == 0) box.halfExtents.x *= scale;
        else if (axis == 1) box.halfExtents.y *= scale;
        else box.halfExtents.z *= scale;
    }
    return box;
}

AABB BoxColliderComponent::GetWorldBounds() const
{
    return ComputeBounds(GetWorldShape());
}

void BoxColliderComponent::Serialize(nlohmann::json& data) const
{
    SerializeCollider(data);
    data["halfExtents"] = nlohmann::json::array({
        m_HalfExtents.x, m_HalfExtents.y, m_HalfExtents.z
    });
}

void BoxColliderComponent::Deserialize(const nlohmann::json& data)
{
    DeserializeCollider(data);
    if (!data.contains("halfExtents")) return;
    const auto& value = data["halfExtents"];
    if (value.is_array() && value.size() == 3) {
        SetHalfExtents({ value[0].get<float>(), value[1].get<float>(), value[2].get<float>() });
    }
}
