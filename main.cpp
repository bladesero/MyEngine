#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Assets/AssetManager.h"
#include "Core/Application.h"
#include "Core/Platform.h"
#include "Renderer/IRenderContext.h"
#include "Game/SceneRenderLayer.h"
#include "Editor/EditorLayer.h"
#include "Core/Logger.h"

#include <memory>
#include <filesystem>
#include <string>

namespace {
std::filesystem::path GetExecutableDirectory(const char* argv0) {
    if (const char* basePath = SDL_GetBasePath()) {
        return std::filesystem::path(basePath).lexically_normal();
    }
    if (argv0 && *argv0) {
        return std::filesystem::absolute(argv0).parent_path().lexically_normal();
    }
    return std::filesystem::current_path();
}
}

// --------------------------------------------------------------------------
// MyApp  –  bootstraps the platform render context and pushes layers.
//
//  Windows : D3D11 (default) or D3D12 (--backend d3d12)
//  macOS   : Metal
// --------------------------------------------------------------------------
class MyApp : public Application {
public:
    MyApp(ApplicationConfig cfg, std::filesystem::path projectRoot,
          EditorAutomationConfig automation)
        : Application(cfg)
        , m_Backend(cfg.backend)
        , m_ProjectRoot(std::move(projectRoot))
        , m_Automation(std::move(automation)) {}

protected:
    bool OnInit() override {
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
        return false;
#endif

        if (!m_RenderContext->Init(&GetWindow())) {
            return false; // error already logged
        }

        auto& win = GetWindow();
        auto* sceneLayer = new SceneRenderLayer(m_RenderContext.get(),
                                                win.GetWidth(), win.GetHeight());
        sceneLayer->SetPresentEnabled(false); // editor will present after ImGui overlay
        GetEngine().PushLayer(sceneLayer);
        GetEngine().PushLayer(new EditorLayer(
            sceneLayer, &win, &GetEngine(), std::move(m_ProjectRoot), std::move(m_Automation)));
        return true;
    }

    void OnShutdown() override {
        if (m_RenderContext) {
            m_RenderContext->Shutdown();
            m_RenderContext.reset();
        }
    }

private:
    std::unique_ptr<IRenderContext> m_RenderContext;
    RenderBackend m_Backend = kDefaultRenderBackend;
    std::filesystem::path m_ProjectRoot;
    EditorAutomationConfig m_Automation;
};

static int RunEditor(int argc, char* argv[]) {
    ApplicationConfig cfg;
    cfg.window.title     = "MyEngine Editor – Scene + MeshRenderer";
    cfg.window.width     = 1920;
    cfg.window.height    = 1080;
    cfg.window.vsync     = false;
    cfg.engine.appName   = "MyEngine";
    cfg.engine.targetFps = 60;
    std::filesystem::path projectRoot;
    EditorAutomationConfig automation;
    const std::filesystem::path executableDirectory = GetExecutableDirectory(argv[0]);
    const std::filesystem::path bundledEngineContent =
        (executableDirectory / "EngineContent").lexically_normal();
    std::error_code ec;
    if (std::filesystem::is_directory(bundledEngineContent, ec) && !ec) {
        AssetManager::Get().SetEngineContentRoot(bundledEngineContent);
    }

    // Optional: --backend d3d11 | d3d12  (Windows only)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) {
            projectRoot = argv[++i];
        }
        else if (arg.rfind("--project=", 0) == 0) {
            projectRoot = arg.substr(std::string("--project=").size());
        }
        else if (arg == "--create-project" && i + 1 < argc) {
            automation.createProjectRoot = argv[++i];
            projectRoot = automation.createProjectRoot;
        }
        else if (arg.rfind("--create-project=", 0) == 0) {
            automation.createProjectRoot = arg.substr(std::string("--create-project=").size());
            projectRoot = automation.createProjectRoot;
        }
        else if (arg == "--project-name" && i + 1 < argc) {
            automation.projectName = argv[++i];
        }
        else if (arg.rfind("--project-name=", 0) == 0) {
            automation.projectName = arg.substr(std::string("--project-name=").size());
        }
        else if (arg == "--publish-project") {
            automation.publishProject = true;
        }
        else if (arg == "--backend" && i + 1 < argc) {
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

    if (automation.Enabled()) {
        cfg.engine.autoQuitAfterSeconds = 20.0f;
    }

    MyApp app(cfg, std::move(projectRoot), std::move(automation));
    return app.Run();
}

int main(int argc, char* argv[]) {
    return SDL_RunApp(argc, argv, RunEditor, nullptr);
}
