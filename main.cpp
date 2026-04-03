#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Core/Application.h"
#include "Core/Platform.h"
#include "Renderer/IRenderContext.h"
#include "Game/SceneRenderLayer.h"
#include "Editor/EditorLayer.h"
#include "Core/Logger.h"

#include <memory>
#include <string>

// --------------------------------------------------------------------------
// MyApp  –  bootstraps the platform render context and pushes layers.
//
//  Windows : D3D11 (default) or D3D12 (--backend d3d12)
//  macOS   : Metal
// --------------------------------------------------------------------------
class MyApp : public Application {
public:
    explicit MyApp(ApplicationConfig cfg)
        : Application(std::move(cfg))
        , m_Backend(cfg.backend) {}

protected:
    void OnInit() override {
#ifdef MYENGINE_PLATFORM_WINDOWS
        switch (m_Backend) {
        case RenderBackend::D3D12:
            m_RenderContext = CreateD3D12Context();
            break;
        case RenderBackend::D3D11:
        default:
            m_RenderContext = CreateD3D11Context();
            break;
        }
#elif defined(MYENGINE_PLATFORM_MACOS)
        m_RenderContext = CreateMetalContext();
#else
        Logger::Error("[App] No render backend available on this platform");
        return;
#endif

        if (!m_RenderContext->Init(&GetWindow())) {
            return; // error already logged
        }

        auto& win = GetWindow();
        auto* sceneLayer = new SceneRenderLayer(m_RenderContext.get(),
                                                win.GetWidth(), win.GetHeight());
        sceneLayer->SetPresentEnabled(false); // editor will present after ImGui overlay
        GetEngine().PushLayer(sceneLayer);
        GetEngine().PushLayer(new EditorLayer(sceneLayer, &win, &GetEngine()));
    }

    void OnShutdown() override {
        if (m_RenderContext) m_RenderContext->Shutdown();
    }

private:
    std::unique_ptr<IRenderContext> m_RenderContext;
    RenderBackend m_Backend = kDefaultRenderBackend;
};

static int RunEditor(int argc, char* argv[]) {
    ApplicationConfig cfg;
    cfg.window.title     = "MyEngine Editor – Scene + MeshRenderer";
    cfg.window.width     = 1920;
    cfg.window.height    = 1080;
    cfg.window.vsync     = false;
    cfg.engine.appName   = "MyEngine";
    cfg.engine.targetFps = 60;

    // Optional: --backend d3d11 | d3d12  (Windows only)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) {
            const std::string b = argv[++i];
#ifdef MYENGINE_PLATFORM_WINDOWS
            if      (b == "d3d11" || b == "11") cfg.backend = RenderBackend::D3D11;
            else if (b == "d3d12" || b == "12") cfg.backend = RenderBackend::D3D12;
            else Logger::Warn("Unknown backend: ", b, " (use d3d11/d3d12)");
#else
            Logger::Warn("--backend flag ignored: not on Windows (got: ", b, ")");
#endif
        }
        else if (arg.rfind("--backend=", 0) == 0) {
            const std::string b = arg.substr(std::string("--backend=").size());
#ifdef MYENGINE_PLATFORM_WINDOWS
            if      (b == "d3d11" || b == "11") cfg.backend = RenderBackend::D3D11;
            else if (b == "d3d12" || b == "12") cfg.backend = RenderBackend::D3D12;
            else Logger::Warn("Unknown backend: ", b, " (use d3d11/d3d12)");
#else
            Logger::Warn("--backend flag ignored: not on Windows (got: ", b, ")");
#endif
        }
    }

    MyApp app(cfg);
    return app.Run();
}

int main(int argc, char* argv[]) {
    return SDL_RunApp(argc, argv, RunEditor, nullptr);
}
