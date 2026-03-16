#pragma once

#include <queue>
#include <string>

enum class EventType {
    None = 0,
    Quit,
    KeyDown,
    KeyUp,
    WindowResize
};

struct Event {
    EventType type = EventType::None;
    bool handled = false;
    int a = 0;
    int b = 0;
    std::string text{};
};

class EventQueue {
public:
    void Push(const Event& event);
    bool Empty() const;
    Event Pop();

private:
    std::queue<Event> m_Queue;
};
