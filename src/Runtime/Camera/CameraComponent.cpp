#include "Camera/CameraComponent.h"

#include "Camera/Camera.h"
#include "Scene/Actor.h"

#include <algorithm>

namespace {
nlohmann::json VecToJson(const Vec3& value)
{
    return nlohmann::json::array({value.x, value.y, value.z});
}

Vec3 VecFromJson(const nlohmann::json& value, const Vec3& fallback)
{
    if (!value.is_array() || value.size() != 3) return fallback;
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}
} // namespace

void CameraComponent::SetFovYDegrees(float value)
{
    m_FovYDegrees = std::clamp(value, 1.0f, 179.0f);
}

void CameraComponent::SetNearClip(float value)
{
    m_NearClip = (std::max)(0.001f, value);
    if (m_FarClip <= m_NearClip) m_FarClip = m_NearClip + 0.001f;
}

void CameraComponent::SetFarClip(float value)
{
    m_FarClip = (std::max)(m_NearClip + 0.001f, value);
}

Camera CameraComponent::BuildCamera(const Actor& owner, float aspect) const
{
    Camera camera;
    const Vec3 position = owner.GetWorldPosition();
    const Mat4 world = owner.GetWorldMatrix();
    const Vec3 forward = world.TransformDir(Vec3::Forward()).Normalized();
    const Vec3 up = world.TransformDir(Vec3::Up()).Normalized();
    camera.LookAt(position, position + forward, up);
    camera.SetPerspective(m_FovYDegrees, aspect > 0.0f ? aspect : 1.0f,
                          m_NearClip, m_FarClip);
    return camera;
}

void CameraComponent::Serialize(nlohmann::json& data) const
{
    data["isMainCamera"] = m_IsMainCamera;
    data["fovYDegrees"] = m_FovYDegrees;
    data["nearClip"] = m_NearClip;
    data["farClip"] = m_FarClip;
    data["clearColor"] = VecToJson(m_ClearColor);
}

void CameraComponent::Deserialize(const nlohmann::json& data)
{
    SetMainCamera(data.value("isMainCamera", true));
    SetFovYDegrees(data.value("fovYDegrees", 60.0f));
    SetNearClip(data.value("nearClip", 0.1f));
    SetFarClip(data.value("farClip", 1000.0f));
    if (data.contains("clearColor")) {
        SetClearColor(VecFromJson(data["clearColor"], m_ClearColor));
    }
}
