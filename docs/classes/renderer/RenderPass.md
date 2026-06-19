# RenderPass

New passes are authored as RenderGraph setup/execute callbacks. They declare all resource
access during setup and use only `GpuCommandList` plus `RenderGraphResources` during
execution. Direct D3D headers, backend casts and native resource handles are forbidden.

## 角色

渲染通道基类。

## 关键职责

- 统一 pass 生命周期/执行接口
- 作为 `MainPass`、`ShadowPass` 等具体通道父类
