#include "Editor/UI/EditorUIScaleManager.h"

#include "Core/Window.h"

#include <cmath>

#include <SDL3/SDL.h>

namespace Editor::UI {
namespace {
bool NearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= 0.001f;
}
}

void EditorUIScaleManager::Initialize(IWindow* window, float userScale)
{
    m_Window = window;
    m_UserScale = ClampUserScale(userScale);
    m_PlatformScale = QueryPlatformScale();
    m_EffectiveScale = ComputeEffectiveScale(m_PlatformScale, m_UserScale);
    m_FontManager.MarkDirty();
}

bool EditorUIScaleManager::BeginFrame(EditorImGuiBackend* backend)
{
    const float platformScale = QueryPlatformScale();
    const bool scaleChanged = ApplyScale(platformScale, m_UserScale);
    if (scaleChanged) m_FontManager.MarkDirty();
    const bool fontsChanged = m_FontManager.RebuildIfNeeded(m_EffectiveScale, backend);
    return scaleChanged || fontsChanged;
}

bool EditorUIScaleManager::SetUserScale(float userScale)
{
    return ApplyScale(m_PlatformScale, ClampUserScale(userScale));
}

void EditorUIScaleManager::SetPlatformScaleForTesting(float platformScale)
{
    m_UseTestingPlatformScale = true;
    ApplyScale(platformScale, m_UserScale);
}

float EditorUIScaleManager::ComputeEffectiveScale(float platformScale, float userScale)
{
    const float safePlatform = platformScale > 0.0f ? platformScale : 1.0f;
    return std::clamp(safePlatform * ClampUserScale(userScale), 0.5f, 4.0f);
}

float EditorUIScaleManager::QueryPlatformScale() const
{
    if (m_UseTestingPlatformScale) return m_PlatformScale;
    if (!m_Window || !m_Window->GetSDLWindow()) return 1.0f;
    const float scale = SDL_GetWindowDisplayScale(m_Window->GetSDLWindow());
    return scale > 0.0f ? scale : 1.0f;
}

bool EditorUIScaleManager::ApplyScale(float platformScale, float userScale)
{
    const float clampedUser = ClampUserScale(userScale);
    const float safePlatform = platformScale > 0.0f ? platformScale : 1.0f;
    const float effective = ComputeEffectiveScale(safePlatform, clampedUser);
    const bool changed = !NearlyEqual(m_PlatformScale, safePlatform) ||
        !NearlyEqual(m_UserScale, clampedUser) ||
        !NearlyEqual(m_EffectiveScale, effective);
    m_PlatformScale = safePlatform;
    m_UserScale = clampedUser;
    m_EffectiveScale = effective;
    if (changed) m_FontManager.MarkDirty();
    return changed;
}

} // namespace Editor::UI
