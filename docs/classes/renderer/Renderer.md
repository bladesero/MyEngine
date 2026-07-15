# Renderer

## Optional features

`RendererFeatureMask` controls optional Shadows, SSAO, and ScreenUI graph
branches. The default is `All`. Disabled branches are not prepared, imported,
or registered in RenderGraph; disabling Shadows also releases the renderer's
cached shadow graph resources through normal deferred RHI lifetime handling.

## 角色

渲染系统调度器。

## 关键职责

- 管理渲染上下文与 pass 顺序
- 组织主渲染流程（阴影/主通道等）
- 对 Scene 提供统一渲染入口
