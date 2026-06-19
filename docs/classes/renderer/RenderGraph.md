# RenderGraph

## Role

`RenderGraph` is the backend-independent frame scheduler. A pass declares texture reads,
color/depth writes, buffer reads, or texture/buffer UAV read/write access in setup, then
records only RHI commands in its execute callback.

## Contract

- `Compile()` rejects invalid handles, duplicate declarations and reads of uninitialized
  transient textures, then derives a stable dependency order.
- `Execute()` creates and reuses transient textures/buffers, emits state transitions, opens rendering
  scopes for declared attachments and invokes callbacks.
- D3D12 maps transitions to resource barriers. D3D11 treats states as logical states and
  unbinds SRVs before a resource becomes an output.
- New pass code must not include backend headers, cast RHI objects to backend types or
  access native handles. `tools/check-rhi-boundaries.ps1` enforces this rule.

## Migration status

`ShadowPass`, `PostProcessPass` and `EnvironmentPass` use backend-opaque RHI resources and
commands. Compute SH projection uses a generic structured buffer, UAV binding and async
readback ticket. Editor scene output is a `GpuTextureView`; only the backend ImGui adapter
converts that view to `ImTextureID`. The boundary check has no render-pass allowlist.
