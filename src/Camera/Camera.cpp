#include "Camera.h"
#include <algorithm>
#include <cmath>

// --------------------------------------------------------------------------
// LookAt / setters
// --------------------------------------------------------------------------

void Camera::LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    m_Position    = eye;
    m_Target      = target;
    m_Up          = up;
    m_ViewDirty   = true;

    // Sync fly-mode Euler angles from the new direction.
    Vec3 fwd = (target - eye).Normalized();
    m_Pitch = std::asin(std::max(-0.999f, std::min(0.999f, fwd.y))) * kRad2Deg;
    m_Yaw   = std::atan2(fwd.x, -fwd.z) * kRad2Deg;
}

void Camera::SetPosition(const Vec3& pos) { m_Position = pos; m_ViewDirty = true; }
void Camera::SetTarget  (const Vec3& t)   { m_Target   = t;   m_ViewDirty = true; }
void Camera::SetUp      (const Vec3& up)  { m_Up       = up;  m_ViewDirty = true; }

// --------------------------------------------------------------------------
// Derived direction vectors
// --------------------------------------------------------------------------

Vec3 Camera::GetForward() const { return (m_Target - m_Position).Normalized(); }
Vec3 Camera::GetRight()   const { return GetForward().Cross(m_Up).Normalized(); }
Vec3 Camera::GetCamUp()   const { return GetRight().Cross(GetForward()); }

// --------------------------------------------------------------------------
// Perspective setters
// --------------------------------------------------------------------------

void Camera::SetFovY(float deg) {
    m_FovYDeg = deg;
    if (m_ProjMode == ProjectionMode::Perspective) RebuildProj();
}
void Camera::SetAspect(float aspect) {
    m_Aspect = aspect;
    if (m_ProjMode == ProjectionMode::Perspective) RebuildProj();
}
void Camera::SetNear(float zNear) {
    m_ZNear = zNear;
    RebuildProj();
}
void Camera::SetFar(float zFar) {
    m_ZFar = zFar;
    RebuildProj();
}
void Camera::SetPerspective(float fovYDeg, float aspect, float zNear, float zFar) {
    m_FovYDeg  = fovYDeg;
    m_Aspect   = aspect;
    m_ZNear    = zNear;
    m_ZFar     = zFar;
    m_ProjMode = ProjectionMode::Perspective;
    RebuildProj();
}

// --------------------------------------------------------------------------
// Orthographic setters
// --------------------------------------------------------------------------

void Camera::SetOrtho(float width, float height, float zNear, float zFar) {
    m_OrthoW    = width;
    m_OrthoH    = height;
    m_OrthoNear = zNear;
    m_OrthoFar  = zFar;
    m_ProjMode  = ProjectionMode::Orthographic;
    RebuildProj();
}

// --------------------------------------------------------------------------
// Projection mode
// --------------------------------------------------------------------------

void Camera::SetProjectionMode(ProjectionMode mode) {
    m_ProjMode = mode;
    RebuildProj();
}

// --------------------------------------------------------------------------
// Matrices
// --------------------------------------------------------------------------

const Mat4& Camera::GetView() const {
    if (m_ViewDirty) RebuildView();
    return m_View;
}

Mat4 Camera::GetViewProj() const {
    if (m_ViewDirty) RebuildView();
    return m_View * m_Proj;
}

// --------------------------------------------------------------------------
// Orbit-mode controls
// --------------------------------------------------------------------------

void Camera::Orbit(float yawDeg, float pitchDeg) {
    const float yawRad   = yawDeg   * kDeg2Rad;
    const float pitchRad = pitchDeg * kDeg2Rad;

    Vec3  dir    = m_Position - m_Target;
    float radius = dir.Length();
    if (radius < 1e-6f) radius = 1.0f;
    dir = dir.Normalized();

    float theta = std::atan2(dir.x, dir.z) + yawRad;
    float phi   = std::asin(std::clamp(dir.y, -0.99f, 0.99f)) + pitchRad;
    phi         = std::clamp(phi, -kPi * 0.499f, kPi * 0.499f); // avoid gimbal

    m_Position = m_Target + Vec3{
        radius * std::cos(phi) * std::sin(theta),
        radius * std::sin(phi),
        radius * std::cos(phi) * std::cos(theta)
    };
    m_ViewDirty = true;
}

void Camera::Dolly(float delta) {
    Vec3 dir    = (m_Position - m_Target).Normalized();
    float dist  = (m_Position - m_Target).Length();
    // Prevent camera from passing through target.
    float newDist = std::max(0.01f, dist + delta);
    m_Position    = m_Target + dir * newDist;
    m_ViewDirty   = true;
}

void Camera::Pan(float rightDelta, float upDelta) {
    Vec3 r = GetRight();
    Vec3 u = GetCamUp();
    Vec3 offset = r * rightDelta + u * upDelta;
    m_Position += offset;
    m_Target   += offset;
    m_ViewDirty = true;
}

// --------------------------------------------------------------------------
// Fly-mode controls
// --------------------------------------------------------------------------

void Camera::MoveForward(float delta) {
    Vec3 fwd    = GetForward();
    m_Position += fwd * delta;
    m_Target   += fwd * delta;
    m_ViewDirty = true;
}

void Camera::MoveRight(float delta) {
    Vec3 r      = GetRight();
    m_Position += r * delta;
    m_Target   += r * delta;
    m_ViewDirty = true;
}

void Camera::MoveUp(float delta) {
    m_Position.y += delta;
    m_Target.y   += delta;
    m_ViewDirty   = true;
}

void Camera::Rotate(float yawDeg, float pitchDeg) {
    m_Yaw   += yawDeg;
    m_Pitch += pitchDeg;
    m_Pitch  = std::clamp(m_Pitch, -89.0f, 89.0f);

    float yawRad   = m_Yaw   * kDeg2Rad;
    float pitchRad = m_Pitch * kDeg2Rad;

    Vec3 fwd {
        std::cos(pitchRad) * std::sin(yawRad),
        std::sin(pitchRad),
        -std::cos(pitchRad) * std::cos(yawRad)
    };
    m_Target    = m_Position + fwd.Normalized();
    m_ViewDirty = true;
}

// --------------------------------------------------------------------------
// Private: Rebuild
// --------------------------------------------------------------------------

void Camera::RebuildView() const {
    m_View      = Mat4::LookAt(m_Position, m_Target, m_Up);
    m_ViewDirty = false;
}

void Camera::RebuildProj() {
    if (m_ProjMode == ProjectionMode::Perspective) {
        m_Proj = Mat4::Perspective(m_FovYDeg * kDeg2Rad, m_Aspect, m_ZNear, m_ZFar);
    } else {
        m_Proj = Mat4::Ortho(
            -m_OrthoW * 0.5f,  m_OrthoW * 0.5f,
            -m_OrthoH * 0.5f,  m_OrthoH * 0.5f,
             m_OrthoNear,       m_OrthoFar);
    }
}
