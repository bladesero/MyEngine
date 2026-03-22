#pragma once

#include "Engine.h"
#include "Window.h"
#include <memory>

// --------------------------------------------------------------------------
// ApplicationConfig  –  combines window + engine settings
// --------------------------------------------------------------------------
enum class RenderBackend : uint8_t {
    D3D11 = 0,
    D3D12 = 1,
};

struct ApplicationConfig {
    WindowConfig window;
    EngineConfig engine;
    RenderBackend backend = RenderBackend::D3D11;
};

// --------------------------------------------------------------------------
// Application  –  user-facing base class
//
// Usage:
//   class MyApp : public Application {
//   protected:
//       void OnInit()     override { PushLayer(new GameLayer()); }
//       void OnShutdown() override { /* cleanup */ }
//   };
//
//   int main() {
//       MyApp app;
//       return app.Run();
//   }
// --------------------------------------------------------------------------
class Application {
public:
    explicit Application(ApplicationConfig config = {});
    virtual ~Application() = default;

    // Entry point – returns process exit code.
    int Run();

    IWindow&       GetWindow()       { return *m_Window; }
    const IWindow& GetWindow() const { return *m_Window; }

    Engine&       GetEngine()       { return m_Engine; }
    const Engine& GetEngine() const { return m_Engine; }

protected:
    // Override in derived classes to set up layers, resources, etc.
    virtual void OnInit()     {}
    virtual void OnShutdown() {}

    // Convenience forwarders so subclasses don't need to reach into Engine.
    void PushLayer(Layer* layer);

private:
    ApplicationConfig         m_Config;
    std::unique_ptr<IWindow>  m_Window;
    Engine                    m_Engine;
};
