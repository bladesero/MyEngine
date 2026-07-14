#pragma once

#include <algorithm>
#include <functional>

class UITween {
public:
    using UpdateCallback = std::function<void(float)>;

    void Play(float from, float to, float durationSeconds, UpdateCallback callback) {
        m_From = from;
        m_To = to;
        m_Duration = std::max(0.0001f, durationSeconds);
        m_Time = 0.0f;
        m_Playing = true;
        m_Callback = std::move(callback);
    }

    void Pause() { m_Playing = false; }
    void Resume() { m_Playing = true; }
    bool IsPlaying() const { return m_Playing; }

    void Update(float dt) {
        if (!m_Playing || !m_Callback)
            return;
        m_Time = std::min(m_Duration, m_Time + std::max(0.0f, dt));
        const float t = m_Time / m_Duration;
        m_Callback(m_From + (m_To - m_From) * t);
        if (m_Time >= m_Duration)
            m_Playing = false;
    }

private:
    float m_From = 0.0f;
    float m_To = 0.0f;
    float m_Duration = 1.0f;
    float m_Time = 0.0f;
    bool m_Playing = false;
    UpdateCallback m_Callback;
};
