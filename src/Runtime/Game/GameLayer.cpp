#include "GameLayer.h"
#include "../Core/Logger.h"
#include "../Core/EngineTime.h"

#include <SDL3/SDL.h>
#include <cmath>

// Simple HSV→RGB helper (S=1, V=1)
static void HsvToRgb(float h, Uint8& r, Uint8& g, Uint8& b) {
    h = std::fmod(h, 360.0f);
    const float s = 1.0f, v = 1.0f;
    const int i = static_cast<int>(h / 60.0f) % 6;
    const float f = h / 60.0f - static_cast<int>(h / 60.0f);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    float fr, fg, fb;
    switch (i) {
    case 0:
        fr = v;
        fg = t;
        fb = p;
        break;
    case 1:
        fr = q;
        fg = v;
        fb = p;
        break;
    case 2:
        fr = p;
        fg = v;
        fb = t;
        break;
    case 3:
        fr = p;
        fg = q;
        fb = v;
        break;
    case 4:
        fr = t;
        fg = p;
        fb = v;
        break;
    default:
        fr = v;
        fg = p;
        fb = q;
        break;
    }
    r = static_cast<Uint8>(fr * 255);
    g = static_cast<Uint8>(fg * 255);
    b = static_cast<Uint8>(fb * 255);
}

GameLayer::GameLayer(SDL_Renderer* renderer) : Layer("GameLayer"), m_Renderer(renderer) {
}

void GameLayer::OnAttach() {
    Logger::Info(Name(), " attached");
}

void GameLayer::OnDetach() {
    Logger::Info(Name(), " detached");
}

void GameLayer::OnEvent(Event& event) {
    switch (event.type) {
    case EventType::KeyDown:
        Logger::Info(Name(), " KeyDown scancode=", event.key.scancode, " repeat=", event.key.repeat ? "yes" : "no");
        // ESC → quit
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            Event quit;
            quit.type = EventType::Quit;
            // We can't call Engine directly, so we just mark handled;
            // the application can wire up a quit callback if desired.
            // For demo purposes: re-push a Quit through the queue is
            // handled at the Engine level from SDL_EVENT_QUIT.
            // Here we use the layer's event slot to signal quit.
            event.type = EventType::Quit;
            event.handled = false; // let Engine catch it
        }
        break;
    case EventType::WindowResize:
        Logger::Info(Name(), " WindowResize ", event.resize.width, "x", event.resize.height);
        break;
    default:
        break;
    }
}

void GameLayer::OnUpdate(float dt) {
    // Cycle hue at 30 degrees per second.
    m_Hue = std::fmod(m_Hue + 30.0f * dt, 360.0f);

    m_SecondAccumulator += dt;
    ++m_FrameInSecond;

    if (m_SecondAccumulator >= 1.0f) {
        Logger::Info("Frame=", Time::FrameCount(), ", FPS=", m_FrameInSecond, ", dt=", dt,
                     ", total=", Time::TotalSeconds(), "s");
        m_SecondAccumulator = 0.0f;
        m_FrameInSecond = 0;
    }
}

void GameLayer::OnRender() {
    if (!m_Renderer)
        return;

    Uint8 r, g, b;
    HsvToRgb(m_Hue, r, g, b);
    SDL_SetRenderDrawColor(m_Renderer, r, g, b, 255);
    SDL_RenderClear(m_Renderer);
}
