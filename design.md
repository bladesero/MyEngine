MyEngine/
├── CMakeLists.txt                  ← FetchContent 拉取 SDL3，分三个 target
├── main.cpp                        ← MyApp : Application，OnInit 推 GameLayer
└── src/
    ├── Core/
    │   ├── Application.h/.cpp      ← 🆕 用户继承的入口抽象
    │   ├── Window.h/.cpp           ← 🆕 IWindow 接口 + SDLWindow 实现
    │   ├── Engine.h/.cpp           ← ♻️ 持有 IWindow*，真正 SDL 事件轮询
    │   ├── Event.h/.cpp            ← ♻️ 新增 KeyEvent/ResizeEvent/MouseEvent
    │   ├── Layer / LayerStack      ← 不变
    │   ├── Logger.h                ← 不变
    │   └── Time.h/.cpp             ← 不变
    └── Game/
        ├── GameLayer.h/.cpp        ← ♻️ OnRender 用 SDL 渲染彩色背景，ESC 退出

# 配置（选择你喜欢的生成器）
cmake -S . -B build -G "Ninja"        # 或 "Visual Studio 17 2022"

# 编译
cmake --build build

# 运行
.\build\MyEngine.exe

分层设计说明：

库/目标	职责
MyEngineCore (static lib)	Time / Event / Layer / LayerStack / Engine
MyEngineGame (static lib)	GameLayer，依赖 Core
MyEngine (executable)	只含 main.cpp，链接以上两个库
Logger.h 因为包含模板函数，保持为纯头文件（header-only）。

关键设计
抽象	职责
IWindow	平台无关接口：Init/Shutdown/SwapBuffers/GetWidth/GetHeight
SDLWindow	SDL3 实现：创建窗口 + Renderer，vsync 支持
Application	生命周期入口：Run() = 建窗→OnInit()→Engine::RunLoop()→OnShutdown()→销毁窗口
Engine	通过 SDL_PollEvent 将 SDL 事件翻译为内部 Event 并派发给 LayerStack