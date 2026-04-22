# Engine

## 角色

主循环与运行时调度核心。

## 关键职责

- 初始化/关闭引擎状态
- 每帧执行：时间、事件、更新、渲染
- 管理 `LayerStack` 与 `EventQueue`
- 对接 `MemoryService`（Init/Shutdown/FrameBegin）

## 关键接口

- `Init()`, `Shutdown()`, `RunLoop()`
- `PushLayer(Layer*)`
- `PushEvent(const Event&)`, `RequestQuit()`
