#include "Core/Application.h"
#include "Core/Logger.h"
#include "Core/CrashHandler.h"
#include "Assets/AssetManager.h"
#include "Input/Input.h"
#include "Miscs/IconsManager.h"
#include "Renderer/ShaderManager.h"
#include "RuntimeModule/RuntimeModule.h"

Application::Application(ApplicationConfig config)
    : m_Config(std::move(config)), m_Window(std::make_unique<SDLWindow>()), m_Engine(m_Config.engine) {
}

int Application::Run() {
    InitializeMyEngineRuntimeModules();
    CrashHandler::Install(m_Config.engine.appName);
    // 1. Create the OS window.
    if (!m_Window->Init(m_Config.window)) {
        Logger::Error("Failed to create window – aborting.");
        CrashHandler::Uninstall();
        return 1;
    }

    // 2. Hand the window pointer to the engine so it can poll SDL events
    //    and call SwapBuffers at the right time.
    m_Engine.SetWindow(m_Window.get());

    // 3. Let the derived class push layers / load resources.
    const bool initialized = OnInit();

    // 4. Run the main loop (blocks until quit).
    if (initialized) {
        m_Engine.Init();
        m_Engine.RunLoop();
    } else {
        Logger::Error("Application initialization failed.");
    }

    // 5. Release layers before backend teardown so renderer-owned GPU resources
    //    are destroyed while the render context is still alive.
    OnBeforeLayersCleared();
    m_Engine.ClearLayers();
    IconsManager::Get().Clear();
    ShaderManager::Get().Clear();
    AssetManager::Get().Clear();

    // 6. Derived-class teardown.
    OnShutdown();

    // 7. Tear down input state before SDL shuts down.
    Input::Shutdown();

    // 8. Destroy window (also calls SDL_Quit).
    m_Window->Shutdown();
    CrashHandler::Uninstall();
    return initialized ? m_Engine.GetExitCode() : 1;
}

void Application::PushLayer(Layer* layer) {
    m_Engine.PushLayer(layer);
}
