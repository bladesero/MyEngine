#include "Application.h"
#include "Logger.h"

Application::Application(ApplicationConfig config)
    : m_Config(std::move(config))
    , m_Window(std::make_unique<SDLWindow>())
    , m_Engine(m_Config.engine) {}

int Application::Run() {
    // 1. Create the OS window.
    if (!m_Window->Init(m_Config.window)) {
        Logger::Error("Failed to create window – aborting.");
        return 1;
    }

    // 2. Hand the window pointer to the engine so it can poll SDL events
    //    and call SwapBuffers at the right time.
    m_Engine.SetWindow(m_Window.get());

    // 3. Let the derived class push layers / load resources.
    OnInit();

    // 4. Run the main loop (blocks until quit).
    m_Engine.Init();
    m_Engine.RunLoop();

    // 5. Derived-class teardown.
    OnShutdown();

    // 6. Destroy window (also calls SDL_Quit).
    m_Window->Shutdown();
    return 0;
}

void Application::PushLayer(Layer* layer) {
    m_Engine.PushLayer(layer);
}
