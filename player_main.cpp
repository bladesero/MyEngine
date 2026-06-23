#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Assets/AssetManager.h"
#include "Core/Application.h"
#include "Core/Logger.h"
#include "Core/Platform.h"
#include "Game/SceneRenderLayer.h"
#include "Input/Input.h"
#include "Project/CookedProjectCache.h"
#include "Project/ProjectConfig.h"
#include "Project/RuntimeDependencies.h"
#include "Renderer/IRenderContext.h"
#include "Miscs/IconsManager.h"

#include <filesystem>
#include <memory>
#include <string>

class PlayerApp : public Application {
public:
    PlayerApp(ApplicationConfig cfg, std::filesystem::path projectRoot,
              std::string sceneOverride)
        : Application(cfg)
        , m_Backend(cfg.backend)
        , m_ProjectRoot(std::move(projectRoot))
        , m_SceneOverride(std::move(sceneOverride)) {}

protected:
    bool OnInit() override {
        IconsManager::Get().ApplyWindowIcon(GetWindow(), IconsManager::kPlayerIcon);
        std::string error;
        const bool allowMissingManifest = !m_SceneOverride.empty();
        if (!m_Project.Open(m_ProjectRoot, allowMissingManifest, &error)) {
            Logger::Error("[Player] ", error);
            return false;
        }
        std::filesystem::path scenePath;
        bool resolved = m_SceneOverride.empty()
            ? m_Project.ResolveStartupScene(scenePath, &error)
            : m_Project.ResolveScenePath(m_SceneOverride, scenePath, true, &error);
        if (!resolved) {
            const std::filesystem::path archive = m_Project.GetRoot() / "Content.pak";
            CookedProjectMount mount;
            if (std::filesystem::is_regular_file(archive) &&
                CookedProjectCache::Prepare(m_Project.GetRoot(), mount, &error) &&
                m_Project.Open(mount.projectRoot, false, &error)) {
                Logger::Info("[Player] Mounted cooked Content: ", archive.string(),
                             mount.rebuilt ? " (cache rebuilt)" : " (cache reused)");
                resolved = m_SceneOverride.empty()
                    ? m_Project.ResolveStartupScene(scenePath, &error)
                    : m_Project.ResolveScenePath(m_SceneOverride, scenePath, true, &error);
            }
        }
        if (!resolved) {
            Logger::Error("[Player] ", error);
            return false;
        }
        AssetManager::Get().SetProjectRoot(m_Project.GetRoot());
        LoadProjectInputConfig();

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
        return false;
#endif

        if (!m_RenderContext->Init(&GetWindow())) return false;

        auto& window = GetWindow();
        auto* sceneLayer = new SceneRenderLayer(
            m_RenderContext.get(), window.GetWidth(), window.GetHeight());
        sceneLayer->SetPresentEnabled(true);
        if (!sceneLayer->LoadScene(scenePath.string())) {
            Logger::Error("[Player] Failed to load startup scene: ", scenePath.string());
            delete sceneLayer;
            return false;
        }
        GetEngine().PushLayer(sceneLayer);
        if (!sceneLayer->BeginPlay()) {
            Logger::Error("[Player] Failed to enter Play mode");
            return false;
        }
        Logger::Info("[Player] Running startup scene: ", scenePath.string());
        return true;
    }

    void OnShutdown() override {
        if (m_RenderContext) {
            m_RenderContext->Shutdown();
            m_RenderContext.reset();
        }
        AssetManager::Get().SetProjectRoot({});
    }

private:
    void LoadProjectInputConfig() {
        std::string error;
        std::filesystem::path inputConfig;
        if (!m_Project.ResolveInputConfigPath(inputConfig, false, &error)) {
            Logger::Warn("[Player] Input config path invalid: ", error,
                         "; using default input map");
            Input::SetDefaultActionMap();
            return;
        }
        if (!std::filesystem::is_regular_file(inputConfig)) {
            Logger::Warn("[Player] Input config not found: ", inputConfig.string(),
                         "; using default input map");
            Input::SetDefaultActionMap();
            return;
        }
        if (!Input::LoadActionMapFromFile(inputConfig, &error)) {
            Logger::Warn("[Player] Failed to load input config: ", error,
                         "; using default input map");
            return;
        }
        Logger::Info("[Player] Loaded input config: ", inputConfig.string());
    }

    std::unique_ptr<IRenderContext> m_RenderContext;
    RenderBackend m_Backend = kDefaultRenderBackend;
    std::filesystem::path m_ProjectRoot;
    std::string m_SceneOverride;
    ProjectConfig m_Project;
};

static bool ParseBackend(const std::string& value, ApplicationConfig& cfg) {
#ifdef MYENGINE_PLATFORM_WINDOWS
    if (value == "d3d11" || value == "11") cfg.backend = RenderBackend::D3D11;
    else if (value == "d3d12" || value == "12") cfg.backend = RenderBackend::D3D12;
    else {
        Logger::Error("Unknown backend: ", value, " (use d3d11/d3d12)");
        return false;
    }
#else
    Logger::Warn("--backend flag ignored on this platform (got: ", value, ")");
#endif
    return true;
}

static std::filesystem::path ResolveExecutableDirectory() {
    if (const char* basePath = SDL_GetBasePath()) {
        return std::filesystem::absolute(std::filesystem::path(basePath)).lexically_normal();
    }
    std::error_code ec;
    return std::filesystem::absolute(std::filesystem::current_path(), ec).lexically_normal();
}

static void ApplyProjectBackend(const std::filesystem::path& projectRoot,
                                ApplicationConfig& cfg) {
#ifdef MYENGINE_PLATFORM_WINDOWS
    ProjectConfig project;
    std::string error;
    if (!project.Open(projectRoot, false, &error)) {
        Logger::Warn("[Player] Failed to read project graphics settings: ", error);
        return;
    }
    if (!ParseBackend(project.GetGraphicsSettings().backend, cfg)) {
        cfg.backend = kDefaultRenderBackend;
    }
#else
    (void)projectRoot;
    (void)cfg;
#endif
}

static int RunPlayer(int argc, char* argv[]) {
    ApplicationConfig cfg;
    cfg.window.title = "MyEngine Player";
    cfg.window.width = 1280;
    cfg.window.height = 720;
    cfg.window.vsync = true;
    cfg.engine.appName = "MyEnginePlayer";
    cfg.engine.targetFps = 60;

    std::filesystem::path projectRoot = std::filesystem::current_path();
    std::string sceneOverride;
    bool backendOverridden = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" || arg == "--scene" || arg == "--backend") {
            if (i + 1 >= argc) {
                Logger::Error("Missing value for ", arg);
                return 2;
            }
            const std::string value = argv[++i];
            if (arg == "--project") projectRoot = value;
            else if (arg == "--scene") sceneOverride = value;
            else {
                if (!ParseBackend(value, cfg)) return 2;
                backendOverridden = true;
            }
        } else if (arg.rfind("--project=", 0) == 0) {
            projectRoot = arg.substr(std::string("--project=").size());
        } else if (arg.rfind("--scene=", 0) == 0) {
            sceneOverride = arg.substr(std::string("--scene=").size());
        } else if (arg.rfind("--backend=", 0) == 0) {
            if (!ParseBackend(arg.substr(std::string("--backend=").size()), cfg)) return 2;
            backendOverridden = true;
        } else {
            Logger::Error("Unknown argument: ", arg);
            return 2;
        }
    }

    std::string packageError;
    if (!RuntimeDependencyManifest::ValidatePackage(
            ResolveExecutableDirectory(), &packageError)) {
        Logger::Error("[Player] ", packageError);
        return 1;
    }

    if (!backendOverridden) {
        ApplyProjectBackend(projectRoot, cfg);
    }

    PlayerApp app(cfg, std::move(projectRoot), std::move(sceneOverride));
    return app.Run();
}

int main(int argc, char* argv[]) {
    return SDL_RunApp(argc, argv, RunPlayer, nullptr);
}
