#pragma once

#include <queue>
#include <string>

enum class EventType {
    None = 0,
    Quit,
    KeyDown,
    KeyUp,
    WindowResize,
    MouseButtonDown,
    MouseButtonUp,
    MouseMove
};

// ---- Keyboard event payload ----
struct KeyEvent {
    int  scancode = 0;   // SDL_Scancode (physical key)
    int  keycode  = 0;   // SDL_Keycode  (logical key)
    bool repeat   = false;
};

// ---- Window resize payload ----
struct ResizeEvent {
    int width  = 0;
    int height = 0;
};

// ---- Mouse button payload ----
struct MouseButtonEvent {
    int  button = 0;   // 1=left 2=middle 3=right
    int  x      = 0;
    int  y      = 0;
};

// ---- Mouse move payload ----
struct MouseMoveEvent {
    int x    = 0;
    int y    = 0;
    int relX = 0;
    int relY = 0;
};

struct Event {
    EventType type    = EventType::None;
    bool      handled = false;

    union {
        KeyEvent         key;
        ResizeEvent      resize;
        MouseButtonEvent mouseButton;
        MouseMoveEvent   mouseMove;
    };

    Event() : key{} {}
};

class EventQueue {
public:
    void Push(const Event& event);
    bool Empty() const;
    Event Pop();

private:
    std::queue<Event> m_Queue;
};
