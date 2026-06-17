#pragma once

#include "Event.h"
#include "LayerStack.h"
#include "PlatformEventBridge.h"
#include "EngineTime.h"
#include "FrameStats.h"
#include <string>
#include <unordered_set>

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
    void ClearLayers();

    // Full blocking loop (used by Application::Run).
    void RunLoop();

    void PushLayer(Layer* layer);
    void PushEvent(const Event& event);
    void RequestQuit();
    const FrameStats& GetFrameStats() const { return m_FrameStats; }

    // Optional platform bridge for raw platform events.
    void SetPlatformEventBridge(IPlatformEventBridge* bridge) { m_PlatformEventBridge = bridge; }

private:
    void PollPlatformEvents();   // translates SDL_Event → our Event
    void DispatchEvents();
    void UpdateLayers();
    void RenderLayers();
    bool IsLayerFaulted(const Layer* layer) const;
    void MarkLayerFaulted(const Layer* layer, const char* phase, const char* message);
    static void SleepForFrameRate(float targetFrameSeconds,
                                  Time::Clock::time_point frameStart);

    EngineConfig m_Config;
    IWindow*     m_Window  = nullptr;
    bool         m_Running = false;
    EventQueue   m_EventQueue;
    LayerStack   m_Layers;
    IPlatformEventBridge* m_PlatformEventBridge = nullptr;
    FrameStats m_FrameStats;
    float m_StatsAccumulator = 0.0f;
    uint32_t m_StatsFrames = 0;
    std::unordered_set<const Layer*> m_FaultedLayers;
};
