#include "UI/Core/SubtitleSystem.h"

#include <algorithm>
#include <cmath>

bool SubtitleSystem::ComesBefore(const QueuedCue& a, const QueuedCue& b) {
    if (a.cue.priority != b.cue.priority)
        return a.cue.priority > b.cue.priority;
    return a.sequence < b.sequence;
}

bool SubtitleSystem::Enqueue(SubtitleCue cue, std::string* error) {
    if (cue.stableId.empty() || cue.text.empty() || !std::isfinite(cue.durationSeconds) ||
        cue.durationSeconds <= 0.0f || cue.durationSeconds > 300.0f) {
        if (error)
            *error = "subtitle id, text, or duration is invalid";
        return false;
    }
    cue.priority = std::clamp(cue.priority, -1000, 1000);
    if (m_HasActive && m_Active.cue.stableId == cue.stableId) {
        m_Active.cue = std::move(cue);
        m_Remaining = m_Active.cue.durationSeconds;
        RefreshState();
        if (error)
            error->clear();
        return true;
    }
    for (QueuedCue& queued : m_Queue) {
        if (queued.cue.stableId == cue.stableId) {
            queued.cue = std::move(cue);
            std::stable_sort(m_Queue.begin(), m_Queue.end(), ComesBefore);
            RefreshState();
            if (error)
                error->clear();
            return true;
        }
    }

    QueuedCue incoming{std::move(cue), m_NextSequence++};
    if (m_HasActive && incoming.cue.priority > m_Active.cue.priority) {
        m_Active.cue.durationSeconds = m_Remaining;
        m_Queue.push_back(std::move(m_Active));
        m_Active = std::move(incoming);
        m_Remaining = m_Active.cue.durationSeconds;
    } else if (!m_HasActive) {
        m_Active = std::move(incoming);
        m_HasActive = true;
        m_Remaining = m_Active.cue.durationSeconds;
    } else {
        m_Queue.push_back(std::move(incoming));
    }
    std::stable_sort(m_Queue.begin(), m_Queue.end(), ComesBefore);
    if (m_Queue.size() > MaxQueuedCues) {
        m_Queue.pop_back();
        ++m_Dropped;
    }
    RefreshState();
    if (error)
        error->clear();
    return true;
}

void SubtitleSystem::Update(float unscaledDeltaSeconds) {
    if (!m_HasActive || !std::isfinite(unscaledDeltaSeconds) || unscaledDeltaSeconds <= 0.0f)
        return;
    float remainingDelta = unscaledDeltaSeconds;
    while (m_HasActive && remainingDelta > 0.0f) {
        if (remainingDelta < m_Remaining) {
            m_Remaining -= remainingDelta;
            remainingDelta = 0.0f;
        } else {
            remainingDelta -= m_Remaining;
            m_HasActive = false;
            m_Remaining = 0.0f;
            ActivateNext();
        }
    }
    RefreshState();
}

void SubtitleSystem::Clear() {
    m_Queue.clear();
    m_HasActive = false;
    m_Remaining = 0.0f;
    RefreshState();
}

void SubtitleSystem::ActivateNext() {
    if (m_Queue.empty())
        return;
    m_Active = std::move(m_Queue.front());
    m_Queue.erase(m_Queue.begin());
    m_HasActive = true;
    m_Remaining = m_Active.cue.durationSeconds;
}

void SubtitleSystem::RefreshState() {
    m_State.visible = m_HasActive;
    m_State.stableId = m_HasActive ? m_Active.cue.stableId : std::string{};
    m_State.speaker = m_HasActive ? m_Active.cue.speaker : std::string{};
    m_State.text = m_HasActive ? m_Active.cue.text : std::string{};
    m_State.remainingSeconds = m_HasActive ? m_Remaining : 0.0f;
    m_State.priority = m_HasActive ? m_Active.cue.priority : 0;
    m_State.queued = m_Queue.size();
    m_State.dropped = m_Dropped;
}
