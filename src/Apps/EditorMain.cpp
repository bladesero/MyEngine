#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Assets/AssetManager.h"
#include "Core/Application.h"
#include "Core/Platform.h"
#include "Renderer/IRenderContext.h"
#include "Game/SceneRenderLayer.h"
#include "Editor/EditorLayer.h"
#include "Editor/EditorWorkspace.h"
#include "Core/Logger.h"
#include "Project/ProjectConfig.h"
#include "Miscs/IconsManager.h"
#include "Renderer/RenderBackendRegistry.h"

#include <memory>
#include <filesystem>
#include <string>
#include <sstream>

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

bool ApplyBackendValue(const std::string& value, ApplicationConfig& cfg) {
    const auto backend = ParseRenderBackend(value);
    if (!backend) {
        Logger::Warn("Unknown backend: ", value, " (use ", AvailableRenderBackendValues(), ")");
        return false;
    }
    if (!IsBackendCompiled(*backend)) {
        Logger::Warn("Backend '", RenderBackendToProjectValue(*backend),
                     "' is not compiled in; available: ", AvailableRenderBackendValues());
        return false;
    }
    cfg.backend = *backend;
    return true;
}

bool ApplyVSyncValue(const std::string& value, ApplicationConfig& cfg) {
    if (value == "on" || value == "true" || value == "1") {
        cfg.window.vsync = true;
        return true;
    }
    if (value == "off" || value == "false" || value == "0") {
        cfg.window.vsync = false;
        return true;
    }
    Logger::Warn("Unknown vsync value: ", value, " (use on/off)");
    return false;
}

void ApplyProjectBackend(const std::filesystem::path& projectRoot, ApplicationConfig& cfg) {
    auto tryApply = [&cfg](const std::filesystem::path& root) {
        ProjectConfig project;
        std::string error;
        if (!project.Open(root, false, &error)) {
            Logger::Warn("[App] Failed to read project graphics settings: ", error);
            return false;
        }
        if (ApplyBackendValue(project.GetGraphicsSettings().backend, cfg)) {
            Logger::Info("[App] Using project graphics backend '", project.GetGraphicsSettings().backend, "' from ",
                         root.string());
        } else {
            cfg.backend = kDefaultRenderBackend;
            Logger::Warn("[App] Project graphics backend '", project.GetGraphicsSettings().backend,
                         "' is unavailable; falling back to ", RenderBackendToProjectValue(kDefaultRenderBackend));
        }
        return true;
    };

    if (!projectRoot.empty()) {
        tryApply(projectRoot);
        return;
    }

    EditorWorkspace workspace;
    std::string error;
    if (!workspace.Load(&error)) {
        Logger::Warn("[App] Failed to load editor workspace: ", error);
        return;
    }
    for (const auto& recent : workspace.GetRecentProjects()) {
        if (tryApply(recent))
            return;
    }
}
} // namespace

// --------------------------------------------------------------------------
// MyApp bootstraps the platform render context and pushes layers.
//
//  Windows : D3D11 (default), D3D12, or optional Vulkan
//  macOS   : Metal
// --------------------------------------------------------------------------
class MyApp : public Application {
public:
    MyApp(ApplicationConfig cfg, std::filesystem::path projectRoot, EditorAutomationConfig automation)
        : Application(cfg), m_Backend(cfg.backend), m_VSyncEnabled(cfg.window.vsync),
          m_ProjectRoot(std::move(projectRoot)), m_Automation(std::move(automation)) {}

protected:
    bool OnInit() override {
        IconsManager::Get().ApplyWindowIcon(GetWindow(), IconsManager::kEditorIcon);
        m_RenderContext = CreateRenderContext(m_Backend);
        if (!m_RenderContext) {
            Logger::Error("[App] Render backend '", RenderBackendToProjectValue(m_Backend),
                          "' is unavailable in this build; available: ", AvailableRenderBackendValues());
            return false;
        }

        if (!m_RenderContext->Init(&GetWindow())) {
            return false; // error already logged
        }
        m_RenderContext->SetVSyncEnabled(m_VSyncEnabled);
        GetEngine().SetFatalHealthCheck([this]() -> std::optional<std::string> {
            if (!m_RenderContext || !m_RenderContext->IsDeviceLost())
                return std::nullopt;
            const RHIDeviceLossInfo info = m_RenderContext->GetDeviceLossInfo();
            std::ostringstream message;
            message << "RHI device lost backend=" << RenderBackendToProjectValue(m_Backend)
                    << " reason=" << RHIDeviceLossReasonName(info.reason) << " nativeCode=" << info.nativeCode
                    << " generation=" << info.deviceGeneration << " diagnostic=" << info.diagnostic;
            return message.str();
        });

        auto& win = GetWindow();
        auto* sceneLayer = new SceneRenderLayer(m_RenderContext.get(), win.GetWidth(), win.GetHeight());
        sceneLayer->SetPresentEnabled(false); // editor will present after ImGui overlay
        GetEngine().PushLayer(sceneLayer);
        GetEngine().PushLayer(
            new EditorLayer(sceneLayer, &win, &GetEngine(), std::move(m_ProjectRoot), std::move(m_Automation)));
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
    bool m_VSyncEnabled = false;
    std::filesystem::path m_ProjectRoot;
    EditorAutomationConfig m_Automation;
};

static int RunEditor(int argc, char* argv[]) {
    ApplicationConfig cfg;
    cfg.window.title = "MyEngine Editor - Scene + MeshRenderer";
    cfg.window.width = 1920;
    cfg.window.height = 1080;
    cfg.window.vsync = false;
    cfg.engine.appName = "MyEngine";
    cfg.engine.targetFps = 60;
    std::filesystem::path projectRoot;
    EditorAutomationConfig automation;
    bool backendOverridden = false;
    const std::filesystem::path executableDirectory = GetExecutableDirectory(argv[0]);
    const std::filesystem::path bundledEngineContent = (executableDirectory / "EngineContent").lexically_normal();
    std::error_code ec;
    if (std::filesystem::is_directory(bundledEngineContent, ec) && !ec) {
        AssetManager::Get().SetEngineContentRoot(bundledEngineContent);
    }

    // Optional: --backend d3d11 | d3d12 | vulkan  (Windows only)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) {
            projectRoot = argv[++i];
        } else if (arg.rfind("--project=", 0) == 0) {
            projectRoot = arg.substr(std::string("--project=").size());
        } else if (arg == "--create-project" && i + 1 < argc) {
            automation.createProjectRoot = argv[++i];
            projectRoot = automation.createProjectRoot;
        } else if (arg.rfind("--create-project=", 0) == 0) {
            automation.createProjectRoot = arg.substr(std::string("--create-project=").size());
            projectRoot = automation.createProjectRoot;
        } else if (arg == "--project-name" && i + 1 < argc) {
            automation.projectName = argv[++i];
        } else if (arg.rfind("--project-name=", 0) == 0) {
            automation.projectName = arg.substr(std::string("--project-name=").size());
        } else if (arg == "--publish-project") {
            automation.publishProject = true;
        } else if (arg == "--backend" && i + 1 < argc) {
            const std::string b = argv[++i];
            backendOverridden = ApplyBackendValue(b, cfg);
        } else if (arg.rfind("--backend=", 0) == 0) {
            const std::string b = arg.substr(std::string("--backend=").size());
            backendOverridden = ApplyBackendValue(b, cfg);
        } else if (arg == "--vsync" && i + 1 < argc) {
            ApplyVSyncValue(argv[++i], cfg);
        } else if (arg.rfind("--vsync=", 0) == 0) {
            ApplyVSyncValue(arg.substr(std::string("--vsync=").size()), cfg);
        }
    }

    if (!backendOverridden && automation.createProjectRoot.empty()) {
        ApplyProjectBackend(projectRoot, cfg);
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
