# Application

## 角色

应用入口基类，负责窗口创建、引擎驱动与生命周期钩子编排。

## 关键职责

- 持有 `SDLWindow` 与 `Engine`
- 在 `Run()` 中串联 `OnInit()` / `Engine::RunLoop()` / `OnShutdown()`；`OnInit()` 返回 `false` 时跳过主循环并返回非零退出码
- 暴露 `PushLayer()` 给派生应用

## 典型调用顺序

1. 初始化窗口
2. `OnInit()`（业务层挂 Layer，返回初始化是否成功）
3. `Engine::Init()` + `RunLoop()`
4. `OnShutdown()`
5. 输入与窗口清理
