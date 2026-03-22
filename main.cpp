#include "src/Core/Application.h"
#include "src/Renderer/IRenderContext.h"
#include "src/Game/SceneRenderLayer.h"
#include "src/Editor/EditorLayer.h"
#include "src/Core/Logger.h"

#include <memory>
#include <string>

// --------------------------------------------------------------------------
// MyApp  –  bootstraps the D3D11 context and pushes SceneRenderLayer
//           (scene with MeshRenderer: cube + default material).
// --------------------------------------------------------------------------
class MyApp : public Application {
public:
    explicit MyApp(ApplicationConfig cfg)
        : Application(std::move(cfg))
        , m_Backend(cfg.backend) {}

protected:
    void OnInit() override {
        switch (m_Backend) {
        case RenderBackend::D3D12:
            m_RenderContext = CreateD3D12Context();
            break;
        case RenderBackend::D3D11:
        default:
            m_RenderContext = CreateD3D11Context();
            break;
        }

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
    RenderBackend m_Backend = RenderBackend::D3D11;
};

int main(int argc, char** argv) {
    ApplicationConfig cfg;
    cfg.window.title     = "MyEngine Editor – Scene + MeshRenderer";
    cfg.window.width     = 1280;
    cfg.window.height    = 720;
    cfg.window.vsync     = false;  // D3D11 vsync handled by SwapChain Present(1,0)
    cfg.engine.appName   = "MyEngine";
    cfg.engine.targetFps = 60;

    // Optional: --backend d3d11 | d3d12
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) {
            const std::string b = argv[++i];
            if (b == "d3d11" || b == "11") cfg.backend = RenderBackend::D3D11;
            else if (b == "d3d12" || b == "12") cfg.backend = RenderBackend::D3D12;
            else Logger::Warn("Unknown backend: ", b, " (use d3d11/d3d12)");
        }
        else if (arg.rfind("--backend=", 0) == 0) {
            const std::string b = arg.substr(std::string("--backend=").size());
            if (b == "d3d11" || b == "11") cfg.backend = RenderBackend::D3D11;
            else if (b == "d3d12" || b == "12") cfg.backend = RenderBackend::D3D12;
            else Logger::Warn("Unknown backend: ", b, " (use d3d11/d3d12)");
        }
    }

    MyApp app(cfg);
    return app.Run();
}



