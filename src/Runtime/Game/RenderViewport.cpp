#include "Game/RenderViewport.h"

#include <algorithm>

RenderViewport::RenderViewport(IRHIDevice* device,
                               IRHIFrameContext* frameContext,
                               IRHIReadbackService* readbackService)
    : m_RenderExecution(device, frameContext, readbackService)
{}

void RenderViewport::Initialize(int width, int height)
{
    m_VpX = 0;
    m_VpY = 0;
    m_VpW = (std::max)(1, width);
    m_VpH = (std::max)(1, height);
}

void RenderViewport::OnWindowResize(int width, int height)
{
    if (width <= 0 || height <= 0 || m_UseExternalViewportRect) return;
    m_VpX = 0;
    m_VpY = 0;
    m_VpW = width;
    m_VpH = height;
    UpdateCameraAspect(GetCamera());
}

void RenderViewport::Render(Scene& scene, bool presentToSwapchain,
                            const UIDrawList* uiDrawList)
{
    ResolveFrameCamera(scene);
    m_RenderExecution.Render(scene, GetCamera(), *this, presentToSwapchain, uiDrawList);
}

void RenderViewport::SetViewportRect(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    m_UseExternalViewportRect = true;
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
    UpdateCameraAspect(GetCamera());
}

void RenderViewport::SetInputEnabled(bool enabled)
{
    m_InputEnabled = enabled;
}

void RenderViewport::GetViewportRect(int& outX, int& outY, int& outW, int& outH) const
{
    outX = m_VpX;
    outY = m_VpY;
    outW = m_VpW;
    outH = m_VpH;
}

float RenderViewport::GetAspect() const
{
    return m_VpH > 0 ? static_cast<float>(m_VpW) / static_cast<float>(m_VpH) : 1.0f;
}

bool RenderViewport::BuildRayFromScreen(float screenX, float screenY,
                                        Math::Ray& outRay) const
{
    if (m_VpW <= 0 || m_VpH <= 0) return false;
    if (screenX < static_cast<float>(m_VpX) || screenY < static_cast<float>(m_VpY) ||
        screenX > static_cast<float>(m_VpX + m_VpW) ||
        screenY > static_cast<float>(m_VpY + m_VpH)) {
        return false;
    }

    const float ndcX =
        ((screenX - static_cast<float>(m_VpX)) / static_cast<float>(m_VpW)) * 2.0f - 1.0f;
    const float ndcY =
        (1.0f - ((screenY - static_cast<float>(m_VpY)) / static_cast<float>(m_VpH))) * 2.0f - 1.0f;
    return GetCamera().BuildRayFromNdc(ndcX, ndcY, outRay);
}

GpuTextureView* RenderViewport::GetOutputView() const
{
    return m_RenderExecution.GetOutputView();
}

void RenderViewport::ReleaseFrameResources()
{
    m_RenderExecution.ReleaseFrameResources();
}

void RenderViewport::UpdateCameraAspect(Camera& camera)
{
    if (m_VpW > 0 && m_VpH > 0) camera.SetAspect(GetAspect());
}
