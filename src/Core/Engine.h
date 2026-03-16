#pragma once

#include "Event.h"
#include "LayerStack.h"
#include "Time.h"
#include <string>

struct EngineConfig {
    std::string appName = "MinimalEngine";
    int targetFps = 60;
    float autoQuitAfterSeconds = 5.0f;
};

class Engine {
public:
    explicit Engine(EngineConfig config);

    void Init();
    void Shutdown();
    void PushLayer(Layer* layer);
    void PushEvent(const Event& event);
    void RequestQuit();
    void Run();

private:
    void PollPlatformEvents();
    void DispatchEvents();
    void UpdateLayers();
    void RenderLayers();
    static void SleepForFrameRate(float targetFrameSeconds,
                                  Time::Clock::time_point frameStart);

    EngineConfig m_Config;
    bool m_Running = false;
    EventQueue m_EventQueue;
    LayerStack m_Layers;
};
