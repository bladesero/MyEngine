#pragma once

// Forward declaration to avoid forcing SDL includes in all users.
union SDL_Event;

// Platform event bridge interface.
// Implementations can observe raw platform events (e.g. ImGui SDL backend).
class IPlatformEventBridge {
public:
    virtual ~IPlatformEventBridge() = default;
    virtual void OnSDLEvent(const SDL_Event& event) = 0;
};
