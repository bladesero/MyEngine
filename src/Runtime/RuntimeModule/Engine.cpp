#include "Core/Engine.h"
#include "Core/Logger.h"
#include "Core/EngineTime.h"
#include "Audio/AudioEngine.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/TypeRegistry.h"
#include "RuntimeModule/RuntimeModule.h"
#include "Core/Memory/MemoryService.h"
#include "Core/Window.h"
#include "Core/CrashHandler.h"
#include "Input/Input.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <exception>
#include <thread>

namespace {

bool IsEngineWindowEvent(const SDL_Event& event, SDL_WindowID engineWindowID) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.windowID == engineWindowID;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return event.window.windowID == engineWindowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.windowID == engineWindowID;
    case SDL_EVENT_MOUSE_MOTION:
        return event.motion.windowID == engineWindowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.windowID == engineWindowID;
    case SDL_EVENT_TEXT_INPUT:
        return event.text.windowID == engineWindowID;
    default:
        return true;
    }
}

} // namespace

Engine::Engine(EngineConfig config) : m_Config(std::move(config)) {
}

void Engine::SetWindow(IWindow* window) {
    m_Window = window;
}

void Engine::Init() {
    InitializeMyEngineRuntimeModules();
    std::string registryError;
    if (!TypeRegistry::Get().Freeze(&registryError))
        Logger::Error("TypeRegistry freeze failed: ", registryError);
    MemoryService::Get().Init();
    Logger::Info("Init Engine: ", m_Config.appName);
    AudioEngine::Get().Init();
    Time::Reset();
    m_Running = true;
    m_ExitCode = 0;
}

void Engine::Shutdown() {
    AudioEngine::Get().Shutdown();
    MemoryService::Get().Shutdown();
    Logger::Info("Shutdown Engine");
    m_Running = false;
}

void Engine::ClearLayers() {
    m_Layers.Clear();
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

    const float targetFrameSeconds = 1.0f / static_cast<float>(std::max(1, m_Config.targetFps));
    Logger::Info("Main loop started, target FPS = ", m_Config.targetFps);

    while (m_Running) {
        const auto frameStart = Time::Clock::now();

        MemoryService::Get().FrameBegin();

        Time::Tick();
        PollPlatformEvents();
        DispatchEvents();
        const auto updateStart = Time::Clock::now();
        UpdateLayers();
        AudioEngine::Get().Update();
        const auto renderStart = Time::Clock::now();
        RenderLayers();
        const auto renderEnd = Time::Clock::now();
        if (m_FatalHealthCheck) {
            if (const auto failure = m_FatalHealthCheck(); failure && !failure->empty()) {
                const std::string report = CrashHandler::WriteDiagnosticReport(*failure);
                Logger::Error("[Engine] Fatal runtime health failure: ", *failure, "; report=", report);
                m_ExitCode = 3;
                m_Running = false;
            }
        }
        // NOTE: SwapBuffers is intentionally NOT called here.
        // Each render layer (e.g. TriangleLayer via IRenderContext::EndFrame,
        // or GameLayer via SDL_RenderPresent) is responsible for presenting.
        // This avoids a double-present when a D3D11 context is active.

        // Optional auto-quit (useful for headless tests / demos).
        if (m_Config.autoQuitAfterSeconds > 0.0f && Time::TotalSeconds() > m_Config.autoQuitAfterSeconds) {
            RequestQuit();
        }

        const std::chrono::duration<float, std::milli> updateCost = renderStart - updateStart;
        const std::chrono::duration<float, std::milli> renderCost = renderEnd - renderStart;
        const std::chrono::duration<float, std::milli> frameCost = renderEnd - frameStart;
        m_FrameStats.frameNumber = Time::FrameCount();
        m_FrameStats.updateMs = updateCost.count();
        m_FrameStats.renderMs = renderCost.count();
        m_FrameStats.frameMs = frameCost.count();
        m_FrameStats.renderer = FrameStatsProvider::GetRendererStats();
        m_FrameStats.resources = FrameStatsProvider::GetResourceStats();
        m_FrameStats.smoothedFrameMs = m_FrameStats.smoothedFrameMs == 0.0f
                                           ? m_FrameStats.frameMs
                                           : m_FrameStats.smoothedFrameMs * 0.9f + m_FrameStats.frameMs * 0.1f;
        m_StatsAccumulator += Time::DeltaSeconds();
        ++m_StatsFrames;
        if (m_StatsAccumulator >= 0.5f) {
            m_FrameStats.fps = static_cast<float>(m_StatsFrames) / m_StatsAccumulator;
            m_StatsAccumulator = 0.0f;
            m_StatsFrames = 0;
        }

        SleepForFrameRate(targetFrameSeconds, frameStart);
    }

    Shutdown();
}

// --------------------------------------------------------------------------
// Private helpers
// --------------------------------------------------------------------------

void Engine::PollPlatformEvents() {
    Input::Flush(); // advance prev-frame snapshot

    const SDL_WindowID engineWindowID =
        m_Window && m_Window->GetSDLWindow() ? SDL_GetWindowID(m_Window->GetSDLWindow()) : 0;
    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent)) {
        // ImGui owns secondary platform windows used by floating panels and menus, so its bridge must see every event.
        if (m_PlatformEventBridge) {
            m_PlatformEventBridge->OnSDLEvent(sdlEvent);
        }
        // Only the main engine window may mutate engine input, focus, or swap-chain dimensions.
        if (engineWindowID != 0 && !IsEngineWindowEvent(sdlEvent, engineWindowID)) {
            continue;
        }
        switch (sdlEvent.type) {
        case SDL_EVENT_QUIT: {
            RequestQuit();
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            Event e;
            e.type = EventType::KeyDown;
            e.key.scancode = static_cast<int>(sdlEvent.key.scancode);
            e.key.keycode = static_cast<int>(sdlEvent.key.key);
            e.key.repeat = sdlEvent.key.repeat != 0;
            Input::OnKeyDown(e.key.scancode);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_KEY_UP: {
            Event e;
            e.type = EventType::KeyUp;
            e.key.scancode = static_cast<int>(sdlEvent.key.scancode);
            e.key.keycode = static_cast<int>(sdlEvent.key.key);
            e.key.repeat = false;
            Input::OnKeyUp(e.key.scancode);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            if (m_Window) {
                m_Window->RefreshSize();
                Event e;
                e.type = EventType::WindowResize;
                e.resize.width = m_Window->GetWidth();
                e.resize.height = m_Window->GetHeight();
                e.resize.pixelWidth = m_Window->GetPixelWidth();
                e.resize.pixelHeight = m_Window->GetPixelHeight();
                PushEvent(e);
            }
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED: {
            Event e;
            e.type = EventType::WindowFocusGained;
            PushEvent(e);
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST: {
            Event e;
            e.type = EventType::WindowFocusLost;
            PushEvent(e);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            Event e;
            e.type = EventType::MouseButtonDown;
            e.mouseButton.button = sdlEvent.button.button;
            e.mouseButton.x = static_cast<int>(sdlEvent.button.x);
            e.mouseButton.y = static_cast<int>(sdlEvent.button.y);
            Input::OnMouseButton(e.mouseButton.button, true);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            Event e;
            e.type = EventType::MouseButtonUp;
            e.mouseButton.button = sdlEvent.button.button;
            e.mouseButton.x = static_cast<int>(sdlEvent.button.x);
            e.mouseButton.y = static_cast<int>(sdlEvent.button.y);
            Input::OnMouseButton(e.mouseButton.button, false);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            Event e;
            e.type = EventType::MouseMove;
            e.mouseMove.x = static_cast<int>(sdlEvent.motion.x);
            e.mouseMove.y = static_cast<int>(sdlEvent.motion.y);
            e.mouseMove.relX = static_cast<int>(sdlEvent.motion.xrel);
            e.mouseMove.relY = static_cast<int>(sdlEvent.motion.yrel);
            Input::OnMouseMove(e.mouseMove.x, e.mouseMove.y, e.mouseMove.relX, e.mouseMove.relY);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            Event e;
            e.type = EventType::MouseWheel;
            e.mouseWheel.x = sdlEvent.wheel.x;
            e.mouseWheel.y = sdlEvent.wheel.y;
            PushEvent(e);
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            Event e;
            e.type = EventType::TextInput;
            std::strncpy(e.textInput.text, sdlEvent.text.text, sizeof(e.textInput.text) - 1);
            PushEvent(e);
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED: {
            const auto instanceId = static_cast<int>(sdlEvent.gdevice.which);
            Input::OnGamepadAdded(sdlEvent.gdevice.which);

            Event e;
            e.type = EventType::GamepadAdded;
            e.gamepadDevice.instanceId = instanceId;
            PushEvent(e);
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED: {
            const auto instanceId = static_cast<int>(sdlEvent.gdevice.which);
            Input::OnGamepadRemoved(sdlEvent.gdevice.which);

            Event e;
            e.type = EventType::GamepadRemoved;
            e.gamepadDevice.instanceId = instanceId;
            PushEvent(e);
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            const bool down = (sdlEvent.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            Input::OnGamepadButton(sdlEvent.gbutton.which, static_cast<SDL_GamepadButton>(sdlEvent.gbutton.button),
                                   down);

            Event e;
            e.type = down ? EventType::GamepadButtonDown : EventType::GamepadButtonUp;
            e.gamepadButton.instanceId = static_cast<int>(sdlEvent.gbutton.which);
            e.gamepadButton.button = static_cast<int>(sdlEvent.gbutton.button);
            e.gamepadButton.down = down;
            PushEvent(e);
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            Input::OnGamepadAxis(sdlEvent.gaxis.which, static_cast<SDL_GamepadAxis>(sdlEvent.gaxis.axis),
                                 sdlEvent.gaxis.value);

            Event e;
            e.type = EventType::GamepadAxisMotion;
            e.gamepadAxis.instanceId = static_cast<int>(sdlEvent.gaxis.which);
            e.gamepadAxis.axis = static_cast<int>(sdlEvent.gaxis.axis);
            e.gamepadAxis.value = static_cast<int>(sdlEvent.gaxis.value);
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
            Layer* layer = *it;
            if (IsLayerFaulted(layer)) {
                continue;
            }
            try {
                layer->OnEvent(event);
            } catch (const std::exception& e) {
                MarkLayerFaulted(layer, "event", e.what());
                continue;
            } catch (...) {
                MarkLayerFaulted(layer, "event", "unknown exception");
                continue;
            }
            if (event.handled) {
                break;
            }
        }
    }
}

void Engine::UpdateLayers() {
    const float dt = Time::DeltaSeconds();
    for (Layer* layer : m_Layers.GetLayers()) {
        if (IsLayerFaulted(layer)) {
            continue;
        }
        try {
            layer->OnUpdate(dt);
        } catch (const std::exception& e) {
            MarkLayerFaulted(layer, "update", e.what());
        } catch (...) {
            MarkLayerFaulted(layer, "update", "unknown exception");
        }
    }
}

void Engine::RenderLayers() {
    for (Layer* layer : m_Layers.GetLayers()) {
        if (IsLayerFaulted(layer)) {
            continue;
        }
        try {
            layer->OnRender();
        } catch (const std::exception& e) {
            MarkLayerFaulted(layer, "render", e.what());
        } catch (...) {
            MarkLayerFaulted(layer, "render", "unknown exception");
        }
    }
}

bool Engine::IsLayerFaulted(const Layer* layer) const {
    return m_FaultedLayers.count(layer) > 0;
}

void Engine::MarkLayerFaulted(const Layer* layer, const char* phase, const char* message) {
    if (!layer)
        return;
    if (m_FaultedLayers.insert(layer).second) {
        Logger::Error("[Engine] Layer '", layer->Name(), "' failed during ", phase, ": ", message, "; disabling layer");
    }
}

void Engine::SleepForFrameRate(float targetFrameSeconds, Time::Clock::time_point frameStart) {
    const auto frameEnd = Time::Clock::now();
    const std::chrono::duration<float> frameCost = frameEnd - frameStart;
    const float sleepSeconds = targetFrameSeconds - frameCost.count();
    if (sleepSeconds > 0.0f) {
        std::this_thread::sleep_for(std::chrono::duration<float>(sleepSeconds));
    }
}
