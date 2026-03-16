#include "src/Core/Application.h"
#include "src/Renderer/IRenderContext.h"
#include "src/Game/TriangleLayer.h"

#include <memory>

// --------------------------------------------------------------------------
// MyApp  –  bootstraps the D3D11 context and pushes TriangleLayer.
// --------------------------------------------------------------------------
class MyApp : public Application {
public:
    explicit MyApp(ApplicationConfig cfg)
        : Application(std::move(cfg)) {}

protected:
    void OnInit() override {
        m_RenderContext = CreateD3D11Context();
        if (!m_RenderContext->Init(&GetWindow())) {
            return; // error already logged
        }

        auto& win = GetWindow();
        GetEngine().PushLayer(
            new TriangleLayer(m_RenderContext.get(),
                              win.GetWidth(), win.GetHeight()));
    }

    void OnShutdown() override {
        if (m_RenderContext) m_RenderContext->Shutdown();
    }

private:
    std::unique_ptr<IRenderContext> m_RenderContext;
};

int main() {
    ApplicationConfig cfg;
    cfg.window.title     = "MyEngine – DX11 Triangle";
    cfg.window.width     = 1280;
    cfg.window.height    = 720;
    cfg.window.vsync     = false;  // D3D11 vsync handled by SwapChain Present(1,0)
    cfg.engine.appName   = "MyEngine";
    cfg.engine.targetFps = 60;

    MyApp app(cfg);
    return app.Run();
}



