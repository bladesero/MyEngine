#include "GameLayer.h"
#include "../Core/Logger.h"
#include "../Core/Time.h"

GameLayer::GameLayer()
    : Layer("GameLayer") {}

void GameLayer::OnAttach() {
    Logger::Info(Name(), " attached");
}

void GameLayer::OnDetach() {
    Logger::Info(Name(), " detached");
}

void GameLayer::OnEvent(Event& event) {
    if (event.type == EventType::Quit) {
        Logger::Info(Name(), " got quit event");
        event.handled = true;
    }
}

void GameLayer::OnUpdate(float dt) {
    m_SecondAccumulator += dt;
    ++m_FrameInSecond;

    if (m_SecondAccumulator >= 1.0f) {
        Logger::Info(
            "Frame=", Time::FrameCount(),
            ", FPS=", m_FrameInSecond,
            ", dt=", dt,
            ", total=", Time::TotalSeconds(), "s");
        m_SecondAccumulator = 0.0f;
        m_FrameInSecond = 0;
    }
}
