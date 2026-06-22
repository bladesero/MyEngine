#pragma once

#include "Core/EngineMath.h"
#include "Scene/Component.h"

class Actor;
class Camera;

class CameraComponent final : public Component {
public:
    const char* GetTypeName() const override { return "Camera"; }

    bool IsMainCamera() const { return m_IsMainCamera; }
    void SetMainCamera(bool value) { m_IsMainCamera = value; }

    float GetFovYDegrees() const { return m_FovYDegrees; }
    void SetFovYDegrees(float value);

    float GetNearClip() const { return m_NearClip; }
    void SetNearClip(float value);

    float GetFarClip() const { return m_FarClip; }
    void SetFarClip(float value);

    const Vec3& GetClearColor() const { return m_ClearColor; }
    void SetClearColor(const Vec3& value) { m_ClearColor = value; }

    Camera BuildCamera(const Actor& owner, float aspect) const;

    void Serialize(nlohmann::json& data) const override;
    void Deserialize(const nlohmann::json& data) override;

private:
    bool m_IsMainCamera = true;
    float m_FovYDegrees = 60.0f;
    float m_NearClip = 0.1f;
    float m_FarClip = 1000.0f;
    Vec3 m_ClearColor = {0.12f, 0.12f, 0.18f};
};
