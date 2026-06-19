# D3D12Context

## 角色

`IRenderContext` 的 D3D12 实现。

## 关键职责

- 初始化 D3D12 设备/队列/交换链
- 管理命令列表与资源状态转换
- 执行帧同步与 Present

## Device-removal diagnostics

- DRED automatic breadcrumbs, breadcrumb contexts, and page-fault tracking are enabled before device creation.
- Set `MYENGINE_D3D12_DEBUG_LAYER=1` for the Direct3D 12 Debug Layer in Debug builds.
- Set `MYENGINE_D3D12_GPU_VALIDATION=1` for the Debug Layer plus GPU-based validation in Debug builds.
- On the first failed DX12 operation, the context records `GetDeviceRemovedReason()`, emits DRED data, marks the device lost, and rejects later frame/resource creation work.

## Fence-based deferred release

- D3D12 resource wrappers transfer their COM objects to an RHI-owned deferred-release queue when destroyed.
- Objects released while recording are attached to that frame's submitted fence; objects released between frames are attached to the most recently submitted fence.
- SRV/UAV, sampler, RTV, and DSV descriptor leases use the same queue, so descriptor slots are recycled only after GPU completion.
- Completed batches are collected at `BeginFrame`; shutdown waits for the GPU and drains the queue. RenderGraph and render-pass scheduling do not participate in resource retirement.
