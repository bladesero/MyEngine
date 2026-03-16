#include "Engine.h"
#include "Logger.h"
#include "Time.h"
#include "Window.h"
#include "../Input/Input.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <thread>

Engine::Engine(EngineConfig config)
    : m_Config(std::move(config)) {}

void Engine::SetWindow(IWindow* window) {
    m_Window = window;
}

void Engine::Init() {
    Logger::Info("Init Engine: ", m_Config.appName);
    Time::Reset();
    m_Running = true;
}

void Engine::Shutdown() {
    Logger::Info("Shutdown Engine");
    m_Running = false;
}

void Engine::PushLayer(Layer* layer) {
    m_Layers.PushLayer(layer);
}

void Engine::PushEvent(const Event& event) {
    m_EventQueue.Push(event);
}

void Engine::RequestQuit() {
    Event quit;
    quit.type = EventType::Quit;
    PushEvent(quit);
}

void Engine::RunLoop() {
    if (!m_Running) {
        Logger::Warn("RunLoop called before Init, auto-initializing.");
        Init();
    }

    const float targetFrameSeconds =
        1.0f / static_cast<float>(std::max(1, m_Config.targetFps));
    Logger::Info("Main loop started, target FPS = ", m_Config.targetFps);

    while (m_Running) {
        const auto frameStart = Time::Clock::now();

        Time::Tick();
        PollPlatformEvents();
        DispatchEvents();
        UpdateLayers();
        RenderLayers();
        // NOTE: SwapBuffers is intentionally NOT called here.
        // Each render layer (e.g. TriangleLayer via IRenderContext::EndFrame,
        // or GameLayer via SDL_RenderPresent) is responsible for presenting.
        // This avoids a double-present when a D3D11 context is active.

        // Optional auto-quit (useful for headless tests / demos).
        if (m_Config.autoQuitAfterSeconds > 0.0f &&
            Time::TotalSeconds() > m_Config.autoQuitAfterSeconds) {
            RequestQuit();
        }

        SleepForFrameRate(targetFrameSeconds, frameStart);
    }

    Shutdown();
}

// --------------------------------------------------------------------------
// Private helpers
// --------------------------------------------------------------------------

void Engine::PollPlatformEvents() {
    Input::Flush();   // advance prev-frame snapshot

    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent)) {
        switch (sdlEvent.type) {
        case SDL_EVENT_QUIT: {
            RequestQuit();
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            Event e;
            e.type          = EventType::KeyDown;
            e.key.scancode  = static_cast<int>(sdlEvent.key.scancode);
            e.key.keycode   = static_cast<int>(sdlEvent.key.key);
            e.key.repeat    = sdlEvent.key.repeat != 0;
            Input::OnKeyDown(e.key.scancode);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_KEY_UP: {
            Event e;
            e.type          = EventType::KeyUp;
            e.key.scancode  = static_cast<int>(sdlEvent.key.scancode);
            e.key.keycode   = static_cast<int>(sdlEvent.key.key);
            e.key.repeat    = false;
            Input::OnKeyUp(e.key.scancode);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_WINDOW_RESIZED: {
            if (m_Window) {
                Event e;
                e.type          = EventType::WindowResize;
                e.resize.width  = sdlEvent.window.data1;
                e.resize.height = sdlEvent.window.data2;
                PushEvent(e);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            Event e;
            e.type                = EventType::MouseButtonDown;
            e.mouseButton.button  = sdlEvent.button.button;
            e.mouseButton.x       = static_cast<int>(sdlEvent.button.x);
            e.mouseButton.y       = static_cast<int>(sdlEvent.button.y);
            Input::OnMouseButton(e.mouseButton.button, true);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            Event e;
            e.type                = EventType::MouseButtonUp;
            e.mouseButton.button  = sdlEvent.button.button;
            e.mouseButton.x       = static_cast<int>(sdlEvent.button.x);
            e.mouseButton.y       = static_cast<int>(sdlEvent.button.y);
            Input::OnMouseButton(e.mouseButton.button, false);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            Event e;
            e.type            = EventType::MouseMove;
            e.mouseMove.x     = static_cast<int>(sdlEvent.motion.x);
            e.mouseMove.y     = static_cast<int>(sdlEvent.motion.y);
            e.mouseMove.relX  = static_cast<int>(sdlEvent.motion.xrel);
            e.mouseMove.relY  = static_cast<int>(sdlEvent.motion.yrel);
            Input::OnMouseMove(e.mouseMove.x, e.mouseMove.y,
                               e.mouseMove.relX, e.mouseMove.relY);
            PushEvent(e);
            break;
        }
        default:
            break;
        }
    }
}

void Engine::DispatchEvents() {
    while (!m_EventQueue.Empty()) {
        Event event = m_EventQueue.Pop();

        if (event.type == EventType::Quit) {
            Logger::Info("Quit event received");
            m_Running = false;
            event.handled = true;
        }

        // Top-most layer handles event first.
        auto& layers = m_Layers.GetLayers();
        for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
            (*it)->OnEvent(event);
            if (event.handled) {
                break;
            }
        }
    }
}

void Engine::UpdateLayers() {
    const float dt = Time::DeltaSeconds();
    for (Layer* layer : m_Layers.GetLayers()) {
        layer->OnUpdate(dt);
    }
}

void Engine::RenderLayers() {
    for (Layer* layer : m_Layers.GetLayers()) {
        layer->OnRender();
    }
}

void Engine::SleepForFrameRate(float targetFrameSeconds,
                               Time::Clock::time_point frameStart) {
    const auto frameEnd = Time::Clock::now();
    const std::chrono::duration<float> frameCost = frameEnd - frameStart;
    const float sleepSeconds = targetFrameSeconds - frameCost.count();
    if (sleepSeconds > 0.0f) {
        std::this_thread::sleep_for(std::chrono::duration<float>(sleepSeconds));
    }
}
