# GpuCommandList

The command surface includes rendering scopes, logical resource transitions, graphics
and compute pipeline/bind-group binding, viewport/scissor, draw/dispatch and copy
operations. RenderGraph owns attachment transitions and rendering scopes; pass callbacks
normally issue only pipeline, resource and draw/dispatch commands.

Graphics state is immutable and supplied through `GraphicsPipelineDesc`; passes must not
assemble blend, rasterizer or depth state through legacy immediate setters. The command
surface also supports buffer and texture region copies, indirect draws, UAV barriers,
and timestamp writes/resolution. `BeginRendering` accepts up to eight color attachments
and honors `Load`, `Clear`, `Discard`, `Store` and store-discard operations.

## 角色

RHI 命令列表抽象对象。

## 关键职责

- 记录绘制与资源操作命令
- 提交前作为中间命令容器
- 由具体后端映射到原生命令缓冲
