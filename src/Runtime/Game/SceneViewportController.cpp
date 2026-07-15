#include "Game/SceneViewportController.h"

#include "Audio/AudioEngine.h"
#include "Input/Input.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kSceneViewFovY = 60.0f;
constexpr float kSceneViewNear = 0.1f;
constexpr float kSceneViewFar = 1000.0f;
constexpr float kSceneViewOrthoNear = -1000.0f;
constexpr float kSceneViewOrthoFar = 1000.0f;
} // namespace

SceneViewport::SceneViewport(IRHIDevice* device, IRHIFrameContext* frameContext, IRHIReadbackService* readbackService)
    : RenderViewport(device, frameContext, readbackService) {
}

void SceneViewport::Initialize(int width, int height) {
    RenderViewport::Initialize(width, height);
    m_Camera.SetCameraMode(CameraMode::Fly);
    m_Camera.LookAt({0.0f, 0.0f, -4.0f}, {0.0f, 0.0f, 0.0f});
    m_Camera.SetPerspective(kSceneViewFovY, GetAspect(), kSceneViewNear, kSceneViewFar);
}

void SceneViewport::OnUpdate(float dt) {
    if (!IsInputEnabled()) {
        m_RmbDown = false;
        AudioEngine::Get().SetListenerTransform(m_Camera.GetPosition(), m_Camera.GetForward(), m_Camera.GetCamUp());
        return;
    }

    const float moveSpeed = 6.0f;
    if (Input::IsKeyDown(SDL_SCANCODE_W))
        m_Camera.MoveForward(moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_S))
        m_Camera.MoveForward(-moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_D))
        m_Camera.MoveRight(moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_A))
        m_Camera.MoveRight(-moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_Q))
        m_Camera.MoveUp(moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_E))
        m_Camera.MoveUp(-moveSpeed * dt);

    if (Input::IsMousePressed(3))
        m_RmbDown = true;
    if (Input::IsMouseReleased(3))
        m_RmbDown = false;
    if (m_RmbDown) {
        const int rx = Input::GetMouseRelX();
        const int ry = Input::GetMouseRelY();
        if (rx != 0 || ry != 0) {
            constexpr float lookSens = 0.25f;
            m_Camera.Rotate(-static_cast<float>(rx) * lookSens, -static_cast<float>(ry) * lookSens);
        }
    }

    const SDL_JoystickID pad = Input::GetPrimaryGamepadId();
    if (pad != 0 && Input::IsGamepadConnected(pad)) {
        const float lx = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        const float ly = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        const float rx = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
        const float ry = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
        const float rt = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        const float lt = Input::GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);

        constexpr float kStickDead = 0.15f;
        if (std::fabs(lx) > kStickDead || std::fabs(ly) > kStickDead) {
            m_Camera.MoveRight(lx * moveSpeed * dt);
            m_Camera.MoveForward(-ly * moveSpeed * dt);
        }
        if (std::fabs(rx) > kStickDead || std::fabs(ry) > kStickDead) {
            m_Camera.Rotate(-rx * 120.0f * dt, -ry * 120.0f * dt);
        }

        const float dolly = rt - lt;
        if (std::fabs(dolly) > 0.05f)
            m_Camera.MoveForward(dolly * 4.0f * dt);
    }

    AudioEngine::Get().SetListenerTransform(m_Camera.GetPosition(), m_Camera.GetForward(), m_Camera.GetCamUp());
}

void SceneViewport::OnWindowResize(int width, int height) {
    RenderViewport::OnWindowResize(width, height);
    if (IsOrthographic())
        ApplyOrthographicForCurrentAspect();
}

void SceneViewport::SetViewportRect(int x, int y, int width, int height) {
    RenderViewport::SetViewportRect(x, y, width, height);
    if (IsOrthographic())
        ApplyOrthographicForCurrentAspect();
}

void SceneViewport::SetInputEnabled(bool enabled) {
    if (IsInputEnabled() == enabled)
        return;
    RenderViewport::SetInputEnabled(enabled);
    if (!enabled)
        m_RmbDown = false;
}

void SceneViewport::FrameDirection(SceneViewDirection direction, const Vec3& target, float distance) {
    distance = (std::max)(0.1f, distance);

    Vec3 offset{0.0f, 0.0f, -distance};
    Vec3 up = Vec3::Up();
    switch (direction) {
    case SceneViewDirection::Front:
        offset = {0.0f, 0.0f, -distance};
        up = Vec3::Up();
        break;
    case SceneViewDirection::Back:
        offset = {0.0f, 0.0f, distance};
        up = Vec3::Up();
        break;
    case SceneViewDirection::Left:
        offset = {-distance, 0.0f, 0.0f};
        up = Vec3::Up();
        break;
    case SceneViewDirection::Right:
        offset = {distance, 0.0f, 0.0f};
        up = Vec3::Up();
        break;
    case SceneViewDirection::Top:
        offset = {0.0f, distance, 0.0f};
        up = Vec3::Forward();
        break;
    case SceneViewDirection::Bottom:
        offset = {0.0f, -distance, 0.0f};
        up = Vec3::Forward() * -1.0f;
        break;
    }

    m_Camera.SetCameraMode(CameraMode::Fly);
    m_Camera.LookAt(target + offset, target, up);
    if (IsOrthographic()) {
        m_OrthographicWidth = (std::max)(m_OrthographicWidth, distance * 2.0f);
        ApplyOrthographicForCurrentAspect();
    }
}

void SceneViewport::FrameTarget(const Vec3& target, float radius) {
    radius = (std::max)(0.1f, radius);
    const float distance = (std::max)(4.0f, radius * 4.0f);
    Vec3 forward = m_Camera.GetForward();
    if (forward.Length() < 1e-5f)
        forward = Vec3::Forward();
    Vec3 up = std::fabs(forward.Dot(Vec3::Up())) > 0.98f ? Vec3::Forward() : Vec3::Up();

    m_Camera.SetCameraMode(CameraMode::Fly);
    m_Camera.LookAt(target - forward.Normalized() * distance, target, up);
    if (IsOrthographic()) {
        m_OrthographicWidth = (std::max)(radius * 2.0f, 2.0f);
        ApplyOrthographicForCurrentAspect();
    }
}

void SceneViewport::OrbitAroundFocus(const Vec3& target, float yawDegrees, float pitchDegrees) {
    Vec3 eye = m_Camera.GetPosition();
    Vec3 forward = (target - eye).Normalized();
    Vec3 up = std::fabs(forward.Dot(Vec3::Up())) > 0.98f ? Vec3::Forward() : Vec3::Up();

    m_Camera.SetCameraMode(CameraMode::Fly);
    m_Camera.SetTarget(target);
    m_Camera.Orbit(yawDegrees, pitchDegrees);

    eye = m_Camera.GetPosition();
    forward = (target - eye).Normalized();
    up = std::fabs(forward.Dot(Vec3::Up())) > 0.98f ? Vec3::Forward() : Vec3::Up();
    m_Camera.LookAt(eye, target, up);
    if (IsOrthographic())
        ApplyOrthographicForCurrentAspect();
}

void SceneViewport::ToggleProjectionMode() {
    SetProjectionMode(IsOrthographic() ? ProjectionMode::Perspective : ProjectionMode::Orthographic);
}

void SceneViewport::SetProjectionMode(ProjectionMode mode) {
    if (mode == ProjectionMode::Orthographic) {
        ApplyOrthographicForCurrentAspect();
    } else {
        m_Camera.SetPerspective(kSceneViewFovY, GetAspect(), kSceneViewNear, kSceneViewFar);
    }
}

bool SceneViewport::IsOrthographic() const {
    return m_Camera.GetProjectionMode() == ProjectionMode::Orthographic;
}

void SceneViewport::ApplyOrthographicForCurrentAspect() {
    const float aspect = (std::max)(0.001f, GetAspect());
    m_OrthographicWidth = (std::max)(0.1f, m_OrthographicWidth);
    m_Camera.SetOrtho(m_OrthographicWidth, m_OrthographicWidth / aspect, kSceneViewOrthoNear, kSceneViewOrthoFar);
}
