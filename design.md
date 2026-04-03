# MyEngine 架构说明

## 仓库布局

```
MyEngine/
├── xmake.lua              # 构建：MyEngineRuntime（共享库）、MyEngineEditor、MyEnginePlayer、MyEngineTests
├── main.cpp               # 编辑器入口：SDL3 + Application，推入 SceneRenderLayer 与 EditorLayer
├── player_main.cpp        # 运行时入口：仅 SceneRenderLayer（无 ImGui），适合发行/全屏演示
├── design.md              # 本文档
├── tests/EngineTests.cpp  # 单元测试（序列化、Transform、Input、资源导入）
└── src/
    ├── Runtime/Core/      # Application、Engine、Window、Event、Layer、Time、Logger
    ├── Runtime/Renderer/  # IRenderContext（RHI）、D3D11/D3D12（Windows）、Metal（macOS）、MainPass/ShadowPass
    ├── Runtime/Assets/    # AssetManager、Mesh/Material/Texture/Model
    ├── Runtime/Scene/     # Scene、Actor、Transform、组件、SceneSerializer（JSON）
    ├── Runtime/Camera/    # Camera（透视/正交、轨道/飞行）
    ├── Runtime/Input/     # Input 快照
    ├── Runtime/Game/      # SceneLayer、SceneRenderLayer、TriangleLayer、GameLayer
    └── Editor/            # EditorLayer（ImGui：工具栏、Outliner、Scene View、Inspector、日志）
```

## 构建与运行

```bash
xmake f -m debug   # 或 release / development
xmake

xmake run MyEngineEditor   # 编辑器（Windows 可选 --backend d3d11 | d3d12）
xmake run MyEnginePlayer   # 无 UI 场景渲染
xmake run MyEngineTests      # 单元测试
```

## 目标与依赖

| 目标 | 说明 |
|------|------|
| `MyEngineRuntime` | 共享库：引擎、渲染、场景、编辑器 UI 层（`MYENGINE_ENABLE_IMGUI`） |
| `MyEngineEditor` | 可执行文件，链接 `MyEngineRuntime`，入口 `main.cpp` |
| `MyEnginePlayer` | 可执行文件，链接 `MyEngineRuntime`，入口 `player_main.cpp`，不推 EditorLayer |
| `MyEngineTests` | 仅头文件 + 测试，不启动 GPU |

第三方库（见 `xmake.lua`）：SDL3、ImGui（含各后端）、nlohmann_json、stb、tinyobjloader。

## 运行时架构

```
Application::Run()
  └─ Engine::RunLoop()
       ├─ PollPlatformEvents()  → SDL_PollEvent → Input + 事件队列
       ├─ DispatchEvents()       → Layer::OnEvent
       ├─ UpdateLayers()         → Layer::OnUpdate
       └─ RenderLayers()        → Layer::OnRender（顺序：先 SceneRenderLayer，后 EditorLayer）
```

- **SceneRenderLayer**：持有 `IRenderContext`、`Camera`、`Renderer`；按视口矩形渲染 `MeshRendererComponent`；可与编辑器共享「仅渲染、不 Present」，由 EditorLayer 在 ImGui 之后 `EndFrame`。
- **EditorLayer**：ImGui 面板、文件对话框（SDL3）、视口拾取与简易操作（与 `SceneRenderLayer` 协作）。

## 渲染后端

- **Windows**：`CreateD3D11Context()` / `CreateD3D12Context()`（`main.cpp` / `player_main.cpp` 中 `--backend`）。
- **macOS**：`CreateMetalContext()`。
- **Linux**：当前无 GPU 后端实现；`MYENGINE_PLATFORM_LINUX` 仅编译定义，需后续补充 OpenGL/Vulkan 与 `IRenderContext` 实现。

## 资源与场景

- **AssetManager**：按扩展名加载纹理/模型；内置白/黑/法线、立方体等；`GetByPath` / `Load` / `Register`。
- **序列化**：`SceneSerializer` 将场景保存为 JSON；`MeshRendererComponent` 保存 mesh/material 路径，反序列化时从 `AssetManager` 解析。

## 数学约定

- 行主序 `Mat4`，左手坐标系，Y 向上，与 D3D 深度 0..1 及 HLSL `mul(vector, matrix)` 风格一致（见 `EngineMath.h`）。

## 与旧版文档的差异

早期版本曾以 `TriangleLayer` + 多静态库拆分为文档主线；当前主线为 **SceneRenderLayer + Renderer + EditorLayer**，并以 **MyEnginePlayer** 作为无编辑器运行时。若文档与代码不一致，以代码与 `xmake.lua` 为准。
