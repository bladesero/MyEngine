#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Core/Application.h"
#include "Core/Logger.h"
#include "Core/Platform.h"
#include "Game/SceneRenderLayer.h"
#include "Renderer/IRenderContext.h"

#include <memory>
#include <string>

// --------------------------------------------------------------------------
// Player  –  runs the scene without the editor / ImGui overlay.
//  Windows : D3D11 (default) or D3D12 (--backend d3d12)
//  macOS   : Metal
//  Linux   : no GPU backend in this repo yet (see design.md).
// --------------------------------------------------------------------------
class PlayerApp : public Application {
public:
    explicit PlayerApp(ApplicationConfig cfg)
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
        Logger::Error("[Player] No render backend available on this platform");
        return;
#endif

        if (!m_RenderContext->Init(&GetWindow())) {
            return;
        }

        auto& win = GetWindow();
        auto* sceneLayer = new SceneRenderLayer(
            m_RenderContext.get(),
            win.GetWidth(),
            win.GetHeight());
        sceneLayer->SetPresentEnabled(true);
        GetEngine().PushLayer(sceneLayer);
        sceneLayer->BeginPlay();
    }

    void OnShutdown() override {
        if (m_RenderContext) {
            m_RenderContext->Shutdown();
            m_RenderContext.reset();
        }
    }

private:
    std::unique_ptr<IRenderContext> m_RenderContext;
    RenderBackend                   m_Backend = kDefaultRenderBackend;
};

static int RunPlayer(int argc, char* argv[]) {
    ApplicationConfig cfg;
    cfg.window.title     = "MyEngine Player";
    cfg.window.width     = 1280;
    cfg.window.height    = 720;
    cfg.window.vsync     = true;
    cfg.engine.appName   = "MyEnginePlayer";
    cfg.engine.targetFps = 60;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) {
            const std::string b = argv[++i];
#ifdef MYENGINE_PLATFORM_WINDOWS
            if      (b == "d3d11" || b == "11") cfg.backend = RenderBackend::D3D11;
            else if (b == "d3d12" || b == "12") cfg.backend = RenderBackend::D3D12;
            else Logger::Warn("Unknown backend: ", b, " (use d3d11/d3d12)");
#else
            Logger::Warn("--backend flag ignored on this platform (got: ", b, ")");
#endif
        } else if (arg.rfind("--backend=", 0) == 0) {
            const std::string b = arg.substr(std::string("--backend=").size());
#ifdef MYENGINE_PLATFORM_WINDOWS
            if      (b == "d3d11" || b == "11") cfg.backend = RenderBackend::D3D11;
            else if (b == "d3d12" || b == "12") cfg.backend = RenderBackend::D3D12;
            else Logger::Warn("Unknown backend: ", b, " (use d3d11/d3d12)");
#else
            Logger::Warn("--backend flag ignored on this platform (got: ", b, ")");
#endif
        }
    }

    PlayerApp app(cfg);
    return app.Run();
}

int main(int argc, char* argv[]) {
    return SDL_RunApp(argc, argv, RunPlayer, nullptr);
}
