#include "Game/SceneViewportController.h"

#include "Audio/AudioEngine.h"
#include "Input/Input.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cmath>

void SceneViewportController::Initialize(int width, int height)
{
    m_VpX = 0;
    m_VpY = 0;
    m_VpW = width;
    m_VpH = height;
    m_Camera.SetCameraMode(CameraMode::Fly);
    m_Camera.LookAt({ 0.0f, 0.0f, -4.0f }, { 0.0f, 0.0f, 0.0f });
    if (m_VpH > 0) {
        m_Camera.SetPerspective(60.0f,
            static_cast<float>(m_VpW) / static_cast<float>(m_VpH),
            0.1f,
            1000.0f);
    }
}

void SceneViewportController::OnUpdate(float dt)
{
    if (!m_InputEnabled) {
        m_RmbDown = false;
        AudioEngine::Get().SetListenerTransform(
            m_Camera.GetPosition(), m_Camera.GetForward(), m_Camera.GetCamUp());
        return;
    }

    const float moveSpeed = 6.0f;
    if (Input::IsKeyDown(SDL_SCANCODE_W)) m_Camera.MoveForward( moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_S)) m_Camera.MoveForward(-moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_D)) m_Camera.MoveRight( moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_A)) m_Camera.MoveRight(-moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_Q)) m_Camera.MoveUp( moveSpeed * dt);
    if (Input::IsKeyDown(SDL_SCANCODE_E)) m_Camera.MoveUp(-moveSpeed * dt);

    if (Input::IsMousePressed(3))  { m_RmbDown = true; }
    if (Input::IsMouseReleased(3)) { m_RmbDown = false; }
    if (m_RmbDown) {
        const int rx = Input::GetMouseRelX(), ry = Input::GetMouseRelY();
        if (rx != 0 || ry != 0) {
            const float lookSens = 0.25f;
            m_Camera.Rotate(-static_cast<float>(rx) * lookSens,
                            -static_cast<float>(ry) * lookSens);
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
        if (std::fabs(dolly) > 0.05f) {
            m_Camera.MoveForward(dolly * 4.0f * dt);
        }
    }

    AudioEngine::Get().SetListenerTransform(
        m_Camera.GetPosition(), m_Camera.GetForward(), m_Camera.GetCamUp());
}

void SceneViewportController::OnWindowResize(int width, int height)
{
    if (width <= 0 || height <= 0 || m_UseEditorViewport) {
        return;
    }
    m_VpX = 0;
    m_VpY = 0;
    m_VpW = width;
    m_VpH = height;
    m_Camera.SetAspect(static_cast<float>(m_VpW) / static_cast<float>(m_VpH));
}

void SceneViewportController::SetEditorViewportRect(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    m_UseEditorViewport = true;

    const int clampedX = (std::max)(0, x);
    const int clampedY = (std::max)(0, y);
    if (m_VpX == clampedX && m_VpY == clampedY &&
        m_VpW == width && m_VpH == height) {
        return;
    }

    m_VpX = clampedX;
    m_VpY = clampedY;
    m_VpW = width;
    m_VpH = height;
    m_Camera.SetAspect(static_cast<float>(m_VpW) / static_cast<float>(m_VpH));
}

void SceneViewportController::SetInputEnabled(bool enabled)
{
    m_InputEnabled = enabled;
    if (!enabled) {
        m_RmbDown = false;
    }
}

void SceneViewportController::GetViewportRect(int& outX, int& outY, int& outW, int& outH) const
{
    outX = m_VpX;
    outY = m_VpY;
    outW = m_VpW;
    outH = m_VpH;
}

bool SceneViewportController::BuildRayFromScreen(float screenX, float screenY, Math::Ray& outRay) const
{
    if (m_VpW <= 0 || m_VpH <= 0) {
        return false;
    }
    if (screenX < static_cast<float>(m_VpX) || screenY < static_cast<float>(m_VpY) ||
        screenX > static_cast<float>(m_VpX + m_VpW) || screenY > static_cast<float>(m_VpY + m_VpH)) {
        return false;
    }

    const float mox = ((screenX - static_cast<float>(m_VpX)) / static_cast<float>(m_VpW)) * 2.0f - 1.0f;
    const float moy = (1.0f - ((screenY - static_cast<float>(m_VpY)) / static_cast<float>(m_VpH))) * 2.0f - 1.0f;

    return m_Camera.BuildRayFromNdc(mox, moy, outRay);
}
