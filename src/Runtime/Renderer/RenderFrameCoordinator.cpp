#include "Renderer/RenderFrameCoordinator.h"

#include "Core/EngineTime.h"
#include "Core/FrameStats.h"
#include "Renderer/GpuUploadQueue.h"
#include "Renderer/RHI/IRHIFrameContext.h"

#include <chrono>

RenderFrameCoordinator::RenderFrameCoordinator(IRHIDevice* device, IRHIFrameContext* frameContext)
    : m_Device(device), m_FrameContext(frameContext) {
}

RenderFrameCoordinator::~RenderFrameCoordinator() {
    EndFrame();
}

void RenderFrameCoordinator::BeginFrame(float r, float g, float b, float a) {
    if (!m_Device || !m_FrameContext)
        return;
    const uint64_t frameNumber = Time::FrameCount();
    if (m_FrameOpen) {
        if (m_FrameNumber == frameNumber)
            return;
        EndFrame();
    }

    const auto uploadStart = std::chrono::steady_clock::now();
    GpuUploadQueue::Get().Process(*m_Device, GpuUploadQueue::Get().GetDefaultBudget());
    const float uploadCpuMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uploadStart).count());
    RendererFrameStats stats = FrameStatsProvider::GetRendererStats();
    stats.uploadQueueCpuMs += uploadCpuMs;
    FrameStatsProvider::SetRendererStats(stats);

    m_FrameContext->BeginFrame(r, g, b, a);
    m_FrameNumber = frameNumber;
    m_FrameOpen = true;
}

void RenderFrameCoordinator::EndFrame() {
    if (!m_FrameOpen || !m_FrameContext)
        return;
    m_FrameContext->EndFrame();
    m_FrameOpen = false;
}
