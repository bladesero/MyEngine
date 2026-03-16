#include "Engine.h"
#include "Logger.h"
#include "Time.h"

#include <algorithm>
#include <thread>

Engine::Engine(EngineConfig config)
    : m_Config(std::move(config)) {}

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

void Engine::Run() {
    if (!m_Running) {
        Logger::Warn("Run called before Init, auto-initializing.");
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
        SleepForFrameRate(targetFrameSeconds, frameStart);
    }

    Shutdown();
}

void Engine::PollPlatformEvents() {
    if (Time::TotalSeconds() > m_Config.autoQuitAfterSeconds) {
        RequestQuit();
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
