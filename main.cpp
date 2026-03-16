#include "src/Core/Engine.h"
#include "src/Game/GameLayer.h"

int main() {
    EngineConfig config;
    config.appName = "MyEngine-Core-Minimal";
    config.targetFps = 60;
    config.autoQuitAfterSeconds = 5.0f;

    Engine engine(config);
    engine.Init();
    engine.PushLayer(new GameLayer());
    engine.Run();
    return 0;
}
