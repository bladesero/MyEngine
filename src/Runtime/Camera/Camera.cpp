#include "Camera.h"
#include "Math/Mat4Inverse.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

// --------------------------------------------------------------------------
// LookAt / setters
// --------------------------------------------------------------------------

void Camera::LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    m_Position = eye;
    m_Target = target;
    m_Up = up;
    m_ViewDirty = true;

    // Sync fly-mode Euler angles from the new direction.
    Vec3 fwd = (target - eye).Normalized();
    m_Pitch = std::asin(std::max(-0.999f, std::min(0.999f, fwd.y))) * kRad2Deg;
    m_Yaw = std::atan2(fwd.x, -fwd.z) * kRad2Deg;
}

void Camera::SetPosition(const Vec3& pos) {
    m_Position = pos;
    m_ViewDirty = true;
}
void Camera::SetTarget(const Vec3& t) {
    m_Target = t;
    m_ViewDirty = true;
}
void Camera::SetUp(const Vec3& up) {
    m_Up = up;
    m_ViewDirty = true;
}

// --------------------------------------------------------------------------
// Derived direction vectors
// --------------------------------------------------------------------------

Vec3 Camera::GetForward() const {
    return (m_Target - m_Position).Normalized();
}
Vec3 Camera::GetRight() const {
    return m_Up.Cross(GetForward()).Normalized();
}
Vec3 Camera::GetCamUp() const {
    return GetForward().Cross(GetRight()).Normalized();
}

// --------------------------------------------------------------------------
// Perspective setters
// --------------------------------------------------------------------------

void Camera::SetFovY(float deg) {
    m_FovYDeg = deg;
    if (m_ProjMode == ProjectionMode::Perspective)
        RebuildProj();
}
void Camera::SetAspect(float aspect) {
    m_Aspect = aspect;
    if (m_ProjMode == ProjectionMode::Perspective)
        RebuildProj();
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
    m_FovYDeg = fovYDeg;
    m_Aspect = aspect;
    m_ZNear = zNear;
    m_ZFar = zFar;
    m_ProjMode = ProjectionMode::Perspective;
    RebuildProj();
}

// --------------------------------------------------------------------------
// Orthographic setters
// --------------------------------------------------------------------------

void Camera::SetOrtho(float width, float height, float zNear, float zFar) {
    m_OrthoW = width;
    m_OrthoH = height;
    m_OrthoNear = zNear;
    m_OrthoFar = zFar;
    m_ProjMode = ProjectionMode::Orthographic;
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
    if (m_ViewDirty)
        RebuildView();
    return m_View;
}

Mat4 Camera::GetViewProj() const {
    if (m_ViewDirty)
        RebuildView();
    return m_View * m_Proj;
}

bool Camera::IsVisible(const AABB& worldBounds) const {
    const Vec3 center = worldBounds.Center();
    const float radius = worldBounds.Radius();
    const Vec3 relative = center - m_Position;
    const Vec3 forward = GetForward();
    const Vec3 right = GetRight();
    const Vec3 up = GetCamUp();
    const float depth = relative.Dot(forward);

    const float nearPlane = m_ProjMode == ProjectionMode::Perspective ? m_ZNear : m_OrthoNear;
    const float farPlane = m_ProjMode == ProjectionMode::Perspective ? m_ZFar : m_OrthoFar;
    if (depth + radius < nearPlane || depth - radius > farPlane)
        return false;

    const float horizontal = std::fabs(relative.Dot(right));
    const float vertical = std::fabs(relative.Dot(up));
    if (m_ProjMode == ProjectionMode::Perspective) {
        const float halfHeight = (std::max)(0.0f, depth) * std::tan(m_FovYDeg * kDeg2Rad * 0.5f);
        const float halfWidth = halfHeight * m_Aspect;
        return horizontal <= halfWidth + radius && vertical <= halfHeight + radius;
    }

    return horizontal <= m_OrthoW * 0.5f + radius && vertical <= m_OrthoH * 0.5f + radius;
}

bool Camera::BuildRayFromNdc(float ndcX, float ndcY, Ray& outRay) const {
    const Mat4& proj = GetProj();
    const Vec4 nearProbe = proj.Transform(Vec4(0.0f, 0.0f, 1.0f, 1.0f));
    const Vec4 farProbe = proj.Transform(Vec4(0.0f, 0.0f, 2.0f, 1.0f));
    const bool reversed =
        (nearProbe.w > 1e-8f && farProbe.w > 1e-8f) && ((nearProbe.z / nearProbe.w) > (farProbe.z / farProbe.w));

    const float zNear = reversed ? (1.0f - FLT_EPSILON) : 0.0f;
    const float zFar = reversed ? 0.0f : (1.0f - FLT_EPSILON);

    Mat4 invVP{};
    if (!Mat4Invert(GetViewProj(), invVP)) {
        return false;
    }

    Vec4 ray0 = invVP.Transform(Vec4(ndcX, ndcY, zNear, 1.0f));
    Vec4 ray1 = invVP.Transform(Vec4(ndcX, ndcY, zFar, 1.0f));
    if (std::fabs(ray0.w) < 1e-8f || std::fabs(ray1.w) < 1e-8f) {
        return false;
    }

    ray0 = ray0 * (1.0f / ray0.w);
    ray1 = ray1 * (1.0f / ray1.w);

    const Vec3 p0 = ray0.XYZ();
    const Vec3 p1 = ray1.XYZ();
    Vec3 dir = p1 - p0;
    const float len = dir.Length();
    if (len < 1e-8f) {
        return false;
    }

    outRay.origin = p0;
    outRay.direction = dir * (1.0f / len);
    return true;
}

// --------------------------------------------------------------------------
// Orbit-mode controls
// --------------------------------------------------------------------------

void Camera::Orbit(float yawDeg, float pitchDeg) {
    const float yawRad = yawDeg * kDeg2Rad;
    const float pitchRad = pitchDeg * kDeg2Rad;

    Vec3 dir = m_Position - m_Target;
    float radius = dir.Length();
    if (radius < 1e-6f)
        radius = 1.0f;
    dir = dir.Normalized();

    float theta = std::atan2(dir.x, dir.z) + yawRad;
    float phi = std::asin(std::clamp(dir.y, -0.99f, 0.99f)) + pitchRad;
    phi = std::clamp(phi, -kPi * 0.499f, kPi * 0.499f); // avoid gimbal

    m_Position = m_Target + Vec3{radius * std::cos(phi) * std::sin(theta), radius * std::sin(phi),
                                 radius * std::cos(phi) * std::cos(theta)};
    m_ViewDirty = true;
}

void Camera::Dolly(float delta) {
    Vec3 dir = (m_Position - m_Target).Normalized();
    float dist = (m_Position - m_Target).Length();
    // Prevent camera from passing through target.
    float newDist = std::max(0.01f, dist + delta);
    m_Position = m_Target + dir * newDist;
    m_ViewDirty = true;
}

void Camera::Pan(float rightDelta, float upDelta) {
    Vec3 r = GetRight();
    Vec3 u = GetCamUp();
    Vec3 offset = r * rightDelta + u * upDelta;
    m_Position += offset;
    m_Target += offset;
    m_ViewDirty = true;
}

// --------------------------------------------------------------------------
// Fly-mode controls
// --------------------------------------------------------------------------

void Camera::MoveForward(float delta) {
    Vec3 fwd = GetForward();
    m_Position += fwd * delta;
    m_Target += fwd * delta;
    m_ViewDirty = true;
}

void Camera::MoveRight(float delta) {
    Vec3 r = GetRight();
    m_Position += r * delta;
    m_Target += r * delta;
    m_ViewDirty = true;
}

void Camera::MoveUp(float delta) {
    m_Position.y += delta;
    m_Target.y += delta;
    m_ViewDirty = true;
}

void Camera::Rotate(float yawDeg, float pitchDeg) {
    m_Yaw += yawDeg;
    m_Pitch += pitchDeg;
    m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);

    float yawRad = m_Yaw * kDeg2Rad;
    float pitchRad = m_Pitch * kDeg2Rad;

    Vec3 fwd{std::cos(pitchRad) * std::sin(yawRad), std::sin(pitchRad), -std::cos(pitchRad) * std::cos(yawRad)};
    m_Target = m_Position + fwd.Normalized();
    m_ViewDirty = true;
}

// --------------------------------------------------------------------------
// Private: Rebuild
// --------------------------------------------------------------------------

void Camera::RebuildView() const {
    m_View = Mat4::LookAt(m_Position, m_Target, m_Up);
    m_ViewDirty = false;
}

void Camera::RebuildProj() {
    if (m_ProjMode == ProjectionMode::Perspective) {
        m_Proj = Mat4::Perspective(m_FovYDeg * kDeg2Rad, m_Aspect, m_ZNear, m_ZFar);
    } else {
        m_Proj =
            Mat4::Ortho(-m_OrthoW * 0.5f, m_OrthoW * 0.5f, -m_OrthoH * 0.5f, m_OrthoH * 0.5f, m_OrthoNear, m_OrthoFar);
    }
}
