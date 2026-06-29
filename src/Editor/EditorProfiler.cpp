#include "Editor/EditorProfiler.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {
double NowSeconds()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
}

EditorProfiler::EditorProfiler(size_t capacity)
    : m_Capacity(std::max<size_t>(1, capacity))
{
}

void EditorProfiler::RecordEvent(std::string category, std::string name,
                                 double durationMs, std::string details,
                                 uint64_t frameIndex)
{
    if (!m_Enabled) return;
    while (m_Events.size() >= m_Capacity) m_Events.pop_front();
    EditorProfilerEvent event;
    event.category = std::move(category);
    event.name = std::move(name);
    event.durationMs = durationMs;
    event.details = std::move(details);
    event.frameIndex = frameIndex;
    event.timestampSeconds = NowSeconds();
    m_Events.push_back(std::move(event));
}

std::vector<EditorProfilerEvent> EditorProfiler::Snapshot() const
{
    return {m_Events.begin(), m_Events.end()};
}

void EditorProfiler::Clear()
{
    m_Events.clear();
}

void EditorProfiler::SetCapacity(size_t capacity)
{
    m_Capacity = std::max<size_t>(1, capacity);
    while (m_Events.size() > m_Capacity) m_Events.pop_front();
}
