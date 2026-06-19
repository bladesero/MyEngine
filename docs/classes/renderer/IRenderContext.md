# IRenderContext / IRHIContext

`IRHIContext` now derives from `IRHIDevice`. Device resource creation is separated from
frame lifecycle, command-list access and presentation. New code should depend on
`IRHIDevice`, `GpuCommandList`, `GpuSwapChain` or `RenderGraph` at the narrowest required
boundary rather than inspecting the concrete context type.

The former immediate draw wrappers were removed from `IRHIContext`; recording goes through
`GpuCommandList`, while resource creation goes through `IRHIDevice`.

The device creates backend-opaque buffers, textures, subresource views, samplers,
pipelines and reflected named bind groups. `GetBackend()` is permitted for capability
selection only; pass implementations must not branch into native API code.

## 角色

渲染后端抽象接口，屏蔽 API 差异（D3D11/D3D12/Metal）。

## 关键职责

- 资源创建（纹理、缓冲、shader）
- 命令提交与帧边界控制
- 与窗口交换链联动
