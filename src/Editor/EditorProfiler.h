#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct EditorProfilerEvent {
    std::string category;
    std::string name;
    double durationMs = 0.0;
    std::string details;
    uint64_t frameIndex = 0;
    double timestampSeconds = 0.0;
};

class EditorProfiler {
public:
    explicit EditorProfiler(size_t capacity = 512);

    void RecordEvent(std::string category, std::string name, double durationMs, std::string details = {},
                     uint64_t frameIndex = 0);
    std::vector<EditorProfilerEvent> Snapshot() const;
    void Clear();
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }
    void SetCapacity(size_t capacity);
    size_t Capacity() const { return m_Capacity; }

private:
    size_t m_Capacity = 512;
    bool m_Enabled = true;
    std::deque<EditorProfilerEvent> m_Events;
};
