#pragma once

#include "Engine.h"
#include "Window.h"
#include "Platform.h"
#include <memory>

// --------------------------------------------------------------------------
// ApplicationConfig  –  combines window + engine settings
// --------------------------------------------------------------------------
enum class RenderBackend : uint8_t {
    D3D11  = 0,
    D3D12  = 1,
    Metal  = 2,
    Vulkan = 3,
};

// Choose a sensible compile-time default for each platform.
#ifdef MYENGINE_PLATFORM_MACOS
inline constexpr RenderBackend kDefaultRenderBackend = RenderBackend::Metal;
#else
inline constexpr RenderBackend kDefaultRenderBackend = RenderBackend::D3D11;
#endif

struct ApplicationConfig {
    WindowConfig  window;
    EngineConfig  engine;
    RenderBackend backend = kDefaultRenderBackend;
};

// --------------------------------------------------------------------------
// Application  –  user-facing base class
//
// Usage:
//   class MyApp : public Application {
//   protected:
//       bool OnInit() override { PushLayer(new GameLayer()); return true; }
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
    virtual bool OnInit()     { return true; }
    virtual void OnShutdown() {}

    // Convenience forwarders so subclasses don't need to reach into Engine.
    void PushLayer(Layer* layer);

private:
    ApplicationConfig         m_Config;
    std::unique_ptr<IWindow>  m_Window;
    Engine                    m_Engine;
};
