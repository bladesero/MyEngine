#pragma once

#include "Event.h"
#include "LayerStack.h"
#include "PlatformEventBridge.h"
#include "EngineTime.h"
#include <string>

// Forward-declare to avoid pulling Window.h (and SDL) into every TU.
class IWindow;
union SDL_Event;

struct EngineConfig {
    std::string appName             = "MinimalEngine";
    int         targetFps           = 60;
    float       autoQuitAfterSeconds = -1.0f; // negative = run forever
};

class Engine {
public:
    explicit Engine(EngineConfig config);

    // Called by Application after window is ready.
    void SetWindow(IWindow* window);

    void Init();
    void Shutdown();

    // Full blocking loop (used by Application::Run).
    void RunLoop();

    void PushLayer(Layer* layer);
    void PushEvent(const Event& event);
    void RequestQuit();

    // Optional platform bridge for raw platform events.
    void SetPlatformEventBridge(IPlatformEventBridge* bridge) { m_PlatformEventBridge = bridge; }

private:
    void PollPlatformEvents();   // translates SDL_Event → our Event
    void DispatchEvents();
    void UpdateLayers();
    void RenderLayers();
    static void SleepForFrameRate(float targetFrameSeconds,
                                  Time::Clock::time_point frameStart);

    EngineConfig m_Config;
    IWindow*     m_Window  = nullptr;
    bool         m_Running = false;
    EventQueue   m_EventQueue;
    LayerStack   m_Layers;
    IPlatformEventBridge* m_PlatformEventBridge = nullptr;
};
