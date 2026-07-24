#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "../Core/EngineMath.h"

// ==========================================================================
// Camera
//
// Supports two projection modes: Perspective and Orthographic.
// Supports two movement modes:
//   - Orbit  : position revolves around a fixed target (default)
//   - Fly    : free first-person camera (WASD-style)
//
// All angles are in DEGREES unless the parameter name ends in "Rad".
// ==========================================================================

enum class ProjectionMode { Perspective, Orthographic };
enum class CameraMode { Orbit, Fly };

class MYENGINE_RUNTIME_API Camera {
public:
    // -----------------------------------------------------------------------
    // LookAt / position
    // -----------------------------------------------------------------------
    void LookAt(const Vec3& eye, const Vec3& target, const Vec3& up = Vec3::Up());

    void SetPosition(const Vec3& pos);
    void SetTarget(const Vec3& target);
    void SetUp(const Vec3& up);

    Vec3 GetPosition() const { return m_Position; }
    Vec3 GetTarget() const { return m_Target; }
    Vec3 GetUp() const { return m_Up; }

    // Derived direction vectors (lazily rebuilt).
    Vec3 GetForward() const; // normalised direction from eye to target
    Vec3 GetRight() const;   // world-right (cross product)
    Vec3 GetCamUp() const;   // recomputed camera-up

    // -----------------------------------------------------------------------
    // Perspective settings (independent setters – each marks dirty)
    // -----------------------------------------------------------------------
    void SetFovY(float deg);
    void SetAspect(float aspect);
    void SetNear(float zNear);
    void SetFar(float zFar);
    void SetPerspective(float fovYDeg, float aspect, float zNear = 0.1f, float zFar = 1000.0f);

    float GetFovY() const { return m_FovYDeg; }
    float GetAspect() const { return m_Aspect; }
    float GetNear() const { return m_ZNear; }
    float GetFar() const { return m_ZFar; }
    float GetOrthoWidth() const { return m_OrthoW; }
    float GetOrthoHeight() const { return m_OrthoH; }

    // -----------------------------------------------------------------------
    // Orthographic settings
    // -----------------------------------------------------------------------
    void SetOrtho(float width, float height, float zNear = -1.0f, float zFar = 1.0f);

    // -----------------------------------------------------------------------
    // Projection mode
    // -----------------------------------------------------------------------
    void SetProjectionMode(ProjectionMode mode);
    ProjectionMode GetProjectionMode() const { return m_ProjMode; }

    // -----------------------------------------------------------------------
    // Camera mode
    // -----------------------------------------------------------------------
    void SetCameraMode(CameraMode mode) { m_CamMode = mode; }
    CameraMode GetCameraMode() const { return m_CamMode; }

    // -----------------------------------------------------------------------
    // Matrices
    // -----------------------------------------------------------------------
    const Mat4& GetView() const;
    const Mat4& GetProj() const { return m_Proj; }
    Mat4 GetViewProj() const;
    bool IsVisible(const AABB& worldBounds) const;
    bool BuildRayFromNdc(float ndcX, float ndcY, Ray& outRay) const;

    // -----------------------------------------------------------------------
    // Orbit-mode controls (angles in degrees, distances in world units)
    // -----------------------------------------------------------------------
    // Rotate around target (yaw = horizontal, pitch = vertical).
    void Orbit(float yawDeg, float pitchDeg);

    // Move closer to / farther from target.
    void Dolly(float delta);

    // Slide target + position together (screen-plane translation).
    void Pan(float rightDelta, float upDelta);

    // -----------------------------------------------------------------------
    // Fly-mode controls
    // -----------------------------------------------------------------------
    // Move along camera-local axes.
    void MoveForward(float delta);
    void MoveRight(float delta);
    void MoveUp(float delta);

    // Rotate free camera (yaw around world-Y, pitch around camera-right).
    void Rotate(float yawDeg, float pitchDeg);

private:
    void RebuildView() const;
    void RebuildProj();

    // ---- View state --------------------------------------------------------
    Vec3 m_Position = {0.0f, 0.0f, 0.0f};
    Vec3 m_Target = {0.0f, 0.0f, 0.0f};
    Vec3 m_Up = {0.0f, 1.0f, 0.0f};

    // Fly-mode Euler angles (degrees).
    float m_Yaw = 180.0f; // looking down +Z
    float m_Pitch = 0.0f;

    // ---- Projection state --------------------------------------------------
    ProjectionMode m_ProjMode = ProjectionMode::Perspective;
    CameraMode m_CamMode = CameraMode::Orbit;

    float m_FovYDeg = 60.0f;
    float m_Aspect = 16.0f / 9.0f;
    float m_ZNear = 0.1f;
    float m_ZFar = 1000.0f;

    // Ortho extents.
    float m_OrthoW = 10.0f;
    float m_OrthoH = 10.0f;
    float m_OrthoNear = -1.0f;
    float m_OrthoFar = 1.0f;

    // ---- Cached matrices ---------------------------------------------------
    mutable Mat4 m_View;
    Mat4 m_Proj;
    mutable bool m_ViewDirty = true;
};
