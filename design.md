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

新增文件结构
src/
├── Renderer/
│   ├── IRenderContext.h       ← 渲染后端接口 (CreateVertexBuffer/Shader/Draw…)
│   ├── D3D11Context.h/.cpp    ← DX11 实现
│   └── TriangleShader.h       ← 内联 HLSL (POSITION+COLOR → MVP变换)
├── Input/
│   ├── Input.h/.cpp           ← 静态快照 Input 类 (IsKeyDown/IsKeyPressed 等)
├── Camera/
│   └── Camera.h               ← Vec3/Mat4 + Camera (Perspective/Ortho/Orbit/Dolly)
└── Game/
    ├── TriangleLayer.h/.cpp   ← 🆕 主演示层，替代 GameLayer
    ├── GameLayer.h/.cpp       ← 保留（SDL 渲染路径）

架构关系
Application::Run()
  └─ Engine::RunLoop()
       ├─ PollPlatformEvents()  → SDL_PollEvent → Input::OnKey/Mouse* + PushEvent
       ├─ DispatchEvents()      → Layer::OnEvent (WindowResize 更新 Camera + Viewport)
       ├─ UpdateLayers()        → TriangleLayer::OnUpdate (键盘旋转/轨道相机)
       └─ RenderLayers()        → TriangleLayer::OnRender
              ├─ 构建 MVP = Model(Y旋转) * Camera.GetViewProj()
              ├─ D3D11Context::BeginFrame (ClearRTV)
              ├─ BindShader / BindVertexBuffer / SetVSConstants(MVP)
              ├─ Draw(3)
              └─ EndFrame → SwapChain::Present

变更概览
新增：Math.h （原 Camera.h 里的 math 重构到此）
类型	内容
Vec2	+, -, *, /, +=, Dot, Length, Normalized
Vec3	全套运算符 + Cross, Lerp, Zero/One/Up/Forward/Right 静态常量
Vec4	基础运算, XYZ(), FromVec3()
Mat4	*, *=, Transform, TransformDir/Point, Transposed, Data()
工厂：Identity, Translation, Scale, Rotation(axis/X/Y/Z), LookAt, Perspective, Ortho
常量	kPi, kTwoPi, kDeg2Rad, kRad2Deg

重构：Camera.h + 新增 Camera.cpp
新增能力	接口
完整 LookAt	LookAt(eye, target, up)
独立调节 FOV	SetFovY(deg)
独立调节宽高比	SetAspect(aspect) — 窗口 resize 时只调此项
独立近远平面	SetNear / SetFar
正交投影	SetOrtho(w, h, near, far)
切换投影模式	SetProjectionMode(Perspective\|Orthographic)
切换运动模式	SetCameraMode(Orbit\|Fly)
方向向量	GetForward / GetRight / GetCamUp
平移（orbit）	Pan(rightDelta, upDelta)
自由飞行	MoveForward / MoveRight / MoveUp / Rotate
Orbit 限制	pitch 被 clamp 避免 gimbal lock，dolly 防止穿透目标