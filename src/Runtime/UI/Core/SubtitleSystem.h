#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SubtitleCue {
    std::string stableId;
    std::string speaker;
    std::string text;
    float durationSeconds = 2.0f;
    int priority = 0;
};

struct SubtitleState {
    bool visible = false;
    std::string stableId;
    std::string speaker;
    std::string text;
    float remainingSeconds = 0.0f;
    int priority = 0;
    size_t queued = 0;
    uint64_t dropped = 0;
};

class SubtitleSystem {
public:
    static constexpr size_t MaxQueuedCues = 32;

    bool Enqueue(SubtitleCue cue, std::string* error = nullptr);
    void Update(float unscaledDeltaSeconds);
    void Clear();
    const SubtitleState& GetState() const { return m_State; }

private:
    struct QueuedCue {
        SubtitleCue cue;
        uint64_t sequence = 0;
    };
    static bool ComesBefore(const QueuedCue& a, const QueuedCue& b);
    void ActivateNext();
    void RefreshState();

    std::vector<QueuedCue> m_Queue;
    QueuedCue m_Active;
    bool m_HasActive = false;
    float m_Remaining = 0.0f;
    uint64_t m_NextSequence = 1;
    uint64_t m_Dropped = 0;
    SubtitleState m_State;
};
