# MyEngine Rendering Math Conventions

This document is for code-generation models and external assistants such as DeepSeek, Composer, or other LLM coding agents. Treat it as the required checklist before changing rendering, camera, shadow, post-process, picking, gizmo, or shader math.

If this document conflicts with the implementation, the implementation wins. Verify against these authoritative files first:

- `src/Runtime/Core/EngineMath.h`
- `src/Runtime/Scene/Transform.h`
- `src/Runtime/Camera/Camera.cpp`
- `src/Runtime/Renderer/MainPass.cpp`
- `src/Runtime/Renderer/ShadowPass.cpp`
- `EngineContent/Shaders/*.hlsl`

## Non-Negotiable Rules

1. MyEngine uses **row vectors**: positions and directions multiply on the left.
2. Matrix application order is left to right: `v * A * B` applies `A` first, then `B`.
3. Camera/view/projection math is **left-handed**: visible camera-space depth is `+Z`.
4. Clip-space depth follows D3D convention: normalized depth is `[0, 1]`.
5. HLSL matrix constants must use `row_major`, and shader code should use `mul(rowVector, matrix)`.
6. Do not introduce OpenGL-style `-Z forward`, column-vector multiplication, or depth `[-1, 1]` unless the whole path is explicitly converted.

## Matrix Storage And Multiplication

### CPU Mat4

`Mat4` stores `m[row][col]` and transforms vectors as row vectors:

```cpp
Vec4 clip = matrix.Transform(Vec4(x, y, z, w)); // equivalent to v * matrix
```

Translation lives in the last row:

```cpp
m[3][0] = tx;
m[3][1] = ty;
m[3][2] = tz;
```

Correct combined transform examples:

```cpp
Mat4 world = local * parentWorld;
Mat4 mvp = world * viewProj;
Mat4 viewProj = view * proj;
Mat4 lightMvp = world * lightViewProj;
```

Incorrect patterns:

```cpp
Mat4 mvp = viewProj * world;        // wrong for this engine
float4 clip = mul(matrix, position); // wrong unless you intentionally transposed everything
```

### HLSL Constants

Use row-major matrices and row-vector multiplication:

```hlsl
cbuffer PerDraw : register(b0)
{
    row_major float4x4 g_World;
    row_major float4x4 g_ViewProj;
};

float4 worldPos = mul(float4(localPos, 1.0f), g_World);
float4 clipPos  = mul(worldPos, g_ViewProj);
```

Do not switch to column-vector `mul(matrix, vector)` style in one shader. That silently breaks world, camera, shadow, skinning, and gizmo consistency.

## Local Transform Convention

`Transform::GetLocalMatrix()` returns:

```cpp
S * Ry * Rx * Rz * T
```

For row vectors this applies:

1. scale
2. yaw around Y
3. pitch around X
4. roll around Z
5. translation

Euler storage is:

```cpp
rotation = Vec3{ pitchDegrees, yawDegrees, rollDegrees };
```

Important consequence: if a mesh is in the local `XY` plane with normal `+Z`, rotating it to a horizontal `XZ` ground plane must preserve the intended normal direction. For the built-in Quad:

- `rotation.x = -90` maps local normal `+Z` to world `+Y`.
- `rotation.x = +90` maps local normal `+Z` to world `-Y`.

Using the wrong sign can make direct lighting and shadow reception appear broken because `NdotL` becomes zero.

## Camera And Projection

The camera uses a left-handed look-at matrix:

- camera forward is `target - eye`
- view-space forward is `+Z`
- near/far distances are positive values along camera forward
- perspective projection produces D3D depth `[0, 1]`

Correct view-projection path:

```cpp
Mat4 view = camera.GetView();
Mat4 proj = camera.GetProj();
Mat4 viewProj = view * proj;
```

Correct shader path:

```hlsl
float4 clip = mul(float4(worldPos, 1.0f), g_ViewProj);
float3 ndc = clip.xyz / clip.w;
```

Do not generate frustum corners with negative camera-space depth. For this engine, visible perspective frustum corners use positive `z`:

```cpp
Vec3 nearCorner = { xNear, yNear, splitNear };
Vec3 farCorner  = { xFar,  yFar,  splitFar  };
```

This is especially important for CSM. Using `-splitNear` / `-splitFar` fits cascades behind the camera and creates shadow gaps at split boundaries.

## Clip, NDC, UV, And Depth

After clip projection:

```hlsl
float3 ndc = clip.xyz / clip.w;
```

NDC ranges:

- `x`: `[-1, 1]`
- `y`: `[-1, 1]`
- `z`: `[0, 1]`

Texture UV conversion for projected shadow/post-process sampling:

```hlsl
float2 uv = float2(ndc.x * 0.5f + 0.5f,
                   -ndc.y * 0.5f + 0.5f);
```

Reject projected shadow samples outside the texture/depth range:

```hlsl
if (uv.x < 0.0f || uv.x > 1.0f ||
    uv.y < 0.0f || uv.y > 1.0f ||
    ndc.z < 0.0f || ndc.z > 1.0f) {
    return 1.0f; // fully lit
}
```

Do not use OpenGL depth `[-1, 1]` checks in engine shaders.

## Normals And Tangents

Current shaders transform normals and tangents with the world matrix as directions:

```hlsl
float3 normalW = normalize(mul(float4(localNormal, 0.0f), g_World).xyz);
```

This works correctly for uniform scale and most current demo content. If non-uniform scale support is required, introduce and upload a proper inverse-transpose normal matrix consistently across CPU constants and shaders. Do not patch only one shader.

For normal maps:

```hlsl
float3 tangent = normalize(tangentW - normalW * dot(tangentW, normalW));
float3 bitangent = normalize(cross(normalW, tangent));
```

Keep tangent-space reconstruction consistent with row-vector world-space normals.

## Directional Lights And Shadows

Directional light direction convention:

- `LightComponent::GetDirection()` is the direction the light travels / points.
- Main lighting commonly uses `L = normalize(-direction)` for the vector from surface to light.
- Shadow light view uses an eye at `center - direction * distance`, looking toward the scene center.

Projected shadow transform:

```cpp
Mat4 lightViewProj = lightView * lightProj;
```

Shader:

```hlsl
float4 lightClip = mul(float4(worldPos, 1.0f), g_LightViewProj);
```

Depth comparison uses D3D-style `[0, 1]` projected depth. Bias should be applied to projected depth before comparison:

```hlsl
float compareDepth = projectedDepth - bias;
```

## CSM Rules

CSM is the easiest place to introduce subtle math bugs. Follow these rules exactly.

### Split Selection

Split distances are positive distances along camera forward, not OpenGL negative view-space Z:

```hlsl
float3 toCamera = worldPos - g_CameraPosition.xyz;
float viewDepth = abs(dot(toCamera, g_CameraForward.xyz));
```

Current cascade selection is direct:

```hlsl
uint cascade = 0;
if (viewDepth > g_CascadeSplits.y) {
    cascade = 2;
} else if (viewDepth > g_CascadeSplits.x) {
    cascade = 1;
}
```

Do not add cross-cascade lerp unless both cascades are guaranteed to cover the same receiver region. A naive lerp causes visible bright gaps where the old cascade is clipped before the next cascade contributes.

### Frustum Corner Generation

Correct LH camera-space corners:

```cpp
const Vec3 corners[8] = {
    { -nearHalfW,  nearHalfH, splitNear },
    {  nearHalfW,  nearHalfH, splitNear },
    { -nearHalfW, -nearHalfH, splitNear },
    {  nearHalfW, -nearHalfH, splitNear },
    { -farHalfW,   farHalfH,  splitFar  },
    {  farHalfW,   farHalfH,  splitFar  },
    { -farHalfW,  -farHalfH,  splitFar  },
    {  farHalfW,  -farHalfH,  splitFar  },
};
```

Wrong:

```cpp
{ x, y, -splitNear };
{ x, y, -splitFar  };
```

That fits the cascade behind the camera in this engine.

### Cascade Matrix Upload

If `ShadowPass` creates `N` cascades, `Renderer` and `MainPass` must upload all `N` matrices. Do not generate 3 cascades and upload only 2.

Minimum consistency checklist:

- `ShadowPass::GetCascadeCount()` matches the number rendered.
- `Renderer.cpp` passes that many matrices.
- `MainPass.cpp` copies that many matrices into the shader constant buffer.
- HLSL array size matches the maximum uploaded cascade count.
- `Texture2DArray` depth slices match the maximum cascade count.

### Texel Snapping

Cascade light-space XY bounds may be snapped to shadow-map texels to reduce shimmering:

```cpp
float worldUnitsPerTexel = cascadeDiameter / shadowMapSize;
left   = floor(left   / worldUnitsPerTexel) * worldUnitsPerTexel;
right  = ceil (right  / worldUnitsPerTexel) * worldUnitsPerTexel;
bottom = floor(bottom / worldUnitsPerTexel) * worldUnitsPerTexel;
top    = ceil (top    / worldUnitsPerTexel) * worldUnitsPerTexel;
```

Do not snap only one side or only one cascade. That creates cascade-to-cascade discontinuities.

## SSAO Rules

SSAO reconstructs view-space position from D3D depth and inverse projection. The engine view space is `+Z` forward:

```hlsl
float2 ndc = float2(uv.x * 2.0f - 1.0f,
                    1.0f - uv.y * 2.0f);
float4 clip = float4(ndc, depth, 1.0f);
float4 view = mul(clip, g_InvProjection);
float3 viewPos = view.xyz / view.w;
```

Occlusion comparisons must respect LH depth:

- closer to camera means smaller positive `z`
- farther from camera means larger positive `z`

Do not port SSAO formulas that assume right-handed `-Z` view space without converting the sign tests.

## Post-Process And Editor Offscreen

The runtime can render to an offscreen scene color, then composite FXAA/SSAO/tone mapping. Editor offscreen previews must display the composited SRV, not the raw pre-composite scene color. Otherwise SSAO and FXAA look like they do not work even when the passes executed correctly.

Checklist:

- Main pass writes HDR scene color and depth.
- SSAO reads offscreen depth and writes AO texture.
- Composite reads scene color and SSAO.
- Editor preview reads composite output.

## RHI Usage Constraints

The renderer is moving through an RHI abstraction. Do not bypass it casually. The same rendering feature may need to run on D3D11, D3D12, and Metal.

Authoritative RHI files:

- `src/Runtime/Renderer/IRenderContext.h`
- `src/Runtime/Renderer/RHI/IRHIDevice.h`
- `src/Runtime/Renderer/RHI/IRHIFrameContext.h`
- `src/Runtime/Renderer/RHI/IRHIReadbackService.h`
- `src/Runtime/Renderer/RHI/IEditorImGuiRHIInterop.h`
- `src/Runtime/Renderer/RHI/GpuCommandList.h`
- `src/Runtime/Renderer/RHI/GpuBuffer.h`
- `src/Runtime/Renderer/RHI/GpuShader.h`
- `src/Runtime/Renderer/RHI/GpuTexture.h`
- `src/Runtime/Renderer/RHI/GpuSwapChain.h`
- `src/Runtime/Renderer/D3D11Context.*`
- `src/Runtime/Renderer/D3D12Context.*`

### Backend-Agnostic Code Must Use RHI Types

In backend-agnostic renderer code, prefer the smallest RHI interface needed:

```cpp
IRHIDevice* device;              // resource creation and capabilities
IRHIFrameContext* frameContext;  // frame boundary and command-list access
GpuCommandList* cmd = frameContext->GetGraphicsCommandList();
GpuBuffer* buffer;
GpuShader* shader;
GpuTexture* texture;
GpuSwapChain* swapChain;
```

`IRenderContext` remains as a compatibility facade for existing wiring and
backend factories. Do not add new backend-agnostic features to the facade when
they fit `IRHIDevice`, `IRHIFrameContext`, `IRHIReadbackService`, or
`GpuCommandList`.

Render passes should not store or fetch a command list from a context. Pass
constructors receive the smallest service they need, usually `IRHIDevice` plus
an optional `IRHIReadbackService`, and their execution path receives an explicit
`GpuCommandList&` from `Renderer` / `RenderGraph`.

RenderGraph pass setup must declare resource access. Empty setup callbacks are
rejected unless the pass is explicitly marked with
`RenderGraph::PassFlags::AllowNoResourceAccess`; use that only for temporary
side-effect/compatibility passes while their real resources are being migrated
into the graph.
`Renderer`'s main frame graph path should not register passes with
`AllowNoResourceAccess`; unavailable optional resources should either be omitted
from the graph or produce a clear error before graph execution.

Imported resources may declare a final state through the import overload or
`SetFinalState`. RenderGraph will transition imported textures and buffers to
that final state after all passes execute, which avoids cross-frame state being
left as an undocumented caller assumption.
The D3D offscreen post-process path imports `SceneColor`, `SceneDepth`, `SSAO`,
`SSAOBlur`, and `Composite` resources into RenderGraph; `Main`, SSAO, blur, and
offscreen composite passes declare their reads/writes instead of using empty
compatibility setup callbacks.
When compositing to the swapchain, the current backbuffer is imported as the
Composite pass color target with RenderTarget state ownership for that pass;
the frame context still performs the backend-specific Present transition in
`EndFrame`.
Shadow maps are imported into RenderGraph and declared as depth writes/read
dependencies for `Shadow` and `PrepareMain`. Shadow rendering uses
`RenderGraph::PassFlags::ManualRenderingScope` while cascades and cube faces are
still expressed as backend views rather than first-class graph subresources.
The environment cubemap and SH buffer are also graph resources. Environment
generation uses `ManualRenderingScope | ManualResourceTransitions` because it
still performs per-mip/per-face transitions internally until RenderGraph has a
first-class subresource access model.
RenderGraph supports texture subresource declarations through
`RGTextureSubresource` overloads on `ReadTexture`, `WriteColor`, `WriteDepth`,
and `ReadWriteUAV`. Disjoint mip/layer ranges may be declared independently,
access-local views are created by the graph, and `RenderGraphResources` can
return a matching subresource view for shader binding.
Compile performs conservative liveness analysis. Passes that only write
unobserved transient resources are culled, culled pass names are exposed through
`GetCulledPasses`, and transient resources outside the live graph are not
created. Imported resources, final-state resources, read-only side-effect passes,
and explicit `AllowNoResourceAccess` passes remain live roots.
Transient texture and buffer reuse is descriptor-keyed rather than name-keyed:
same-desc resources can be reused across frames even when their debug names
change, while culled transient resources never enter the reuse pool.

Do not store or pass native D3D objects through general renderer APIs:

```cpp
ID3D11ShaderResourceView* srv; // backend-specific: not allowed in generic APIs
ID3D12Resource* resource;      // backend-specific: not allowed in generic APIs
```

Allowed exception: backend-specific passes may use native handles after explicitly checking the backend:

```cpp
if (auto* d3d11 = dynamic_cast<D3D11Context*>(Context())) {
    ID3D11DeviceContext* dc = d3d11->GetDeviceContext();
}
```

If you add a D3D11 path, either add the equivalent D3D12/Metal path or make the fallback behavior explicit and safe.

### Frame Lifecycle

Only the top-level renderer should own frame boundaries:

```cpp
frameContext->BeginFrame(clearR, clearG, clearB, clearA);
// passes execute here
frameContext->EndFrame();
```

Render passes must not call `BeginFrame()` or `EndFrame()` on their own. A pass may bind render targets, set viewport, draw, and restore state, but it must not present the swap chain.

Current high-level order:

1. process upload queue
2. `BeginFrame`
3. shadow pass
4. main pass
5. post-process passes
6. editor/UI draw
7. `EndFrame` if present is enabled

Do not add a pass that calls `EndFrame()` early. That breaks editor offscreen, post-process compositing, and ImGui.

### Command List Usage

New draw code should use `GpuCommandList` where possible:

```cpp
GpuCommandList* cmd = frameContext->GetGraphicsCommandList();
cmd->BindShader(shader);
cmd->BindVertexBuffer(vertexBuffer);
cmd->BindIndexBuffer(indexBuffer);
cmd->SetVSConstants(&constants, sizeof(constants));
cmd->BindPSTexture(0, texture);
cmd->DrawIndexed(indexCount, indexOffset, baseVertex);
```

Older backend classes still expose immediate compatibility wrappers such as
`BindShader`, `SetVSConstants`, and `DrawIndexed`. New backend-agnostic draw
code should go through `GpuCommandList`; do not add new immediate methods to
`IRenderContext`.

### Constants

`SetVSConstants()` currently binds the same constant buffer to VS and PS slot `b0` in D3D11, and the RHI mirrors this behavior for shared per-draw constants.

Implications:

- Keep VS/PS shared constants in one compatible struct unless you add a real RHI API for separate VS/PS constants.
- HLSL `cbuffer` layout must match the CPU struct exactly.
- Constant buffer sizes should be 16-byte aligned by construction.
- Do not assume a call named `SetVSConstants` affects only the vertex shader in this engine.

Correct:

```cpp
ShadowedPerDrawConstants constants{};
cmd->SetVSConstants(&constants, sizeof(constants));
```

Wrong:

```cpp
cmd->SetVSConstants(&vsOnly, sizeof(vsOnly)); // then expect PS b0 to keep old data
```

### Resource Ownership

RHI resources are generally owned by `std::shared_ptr<GpuBuffer>`, `std::shared_ptr<GpuShader>`, or `std::shared_ptr<GpuTexture>`.

Rules:

- Keep shared ownership in assets, passes, or renderer caches.
- Pass raw `Gpu*` pointers only as non-owning references during binding.
- Do not delete native resources manually.
- Do not cache native backend pointers longer than the owning `Gpu*` object lifetime.
- On resize or device loss, release/recreate dependent render targets and descriptors.

Texture upload path:

```cpp
std::shared_ptr<GpuTexture> gpu = context->UploadTexture2D(data, width, height);
```

Do not upload textures from random render code every frame. Use `AssetManager`, `TextureAsset`, or renderer caches.

### Shader Creation

Backend-agnostic code should request shaders through `ShaderManager` instead of directly calling backend compiler APIs:

```cpp
auto handle = ShaderManager::Get().GetOrCreate(path, "VSMain", "PSMain", layout, layoutCount);
GpuShader* shader = handle ? handle->shader.get() : nullptr;
```

Use `CreateShaderFromBytecode()` only inside shader manager/backend infrastructure or carefully isolated code that already has compiled bytecode.

Shader layout rules:

- Mesh shaders need a valid `VertexElement` layout matching `MeshVertex`.
- Fullscreen triangle shaders use `nullptr, 0` layout and `SV_VertexID`.
- Depth-only shadow shaders need the same vertex layout as the geometry they draw.

### Binding Textures And Slots

`BindPSTexture(slot, texture)` is the generic pixel-shader texture path. Slot numbers must match the shader declarations.

Current important slots in the main PBR path:

- `t0`: base color
- `t1`: directional shadow map / CSM texture array
- `t2`: normal map
- `t3`: metallic-roughness map
- `t4`: occlusion map
- `t5`: emissive map
- `t6`: spot shadow map
- `t7`: point shadow cubemap
- `t8`: IBL cubemap

If you change slots, update all of these together:

- HLSL declarations
- `MainPass.cpp` bindings
- backend descriptor/root-signature assumptions
- fallback/null texture binding

Do not bind a render target as SRV while it is still bound for writing. D3D11 often requires explicit unbinding; D3D12 requires correct resource state transitions.

### Render Targets, Depth, And Post-Process

Render targets and depth resources are backend-specific today. Backend-agnostic pass code should not assume D3D11 RTV/DSV types.

Acceptable backend-specific patterns:

```cpp
if (auto* d3d11 = dynamic_cast<D3D11Context*>(Context())) {
    // D3D11-only RT/DSV/SRV path
}

if (auto* d3d12 = dynamic_cast<D3D12Context*>(Context())) {
    // D3D12-only descriptor/resource-state path
}
```

When adding a post-process pass:

1. allocate offscreen color/depth resources on resize
2. write pass output to a render target
3. unbind output before sampling it
4. composite to backbuffer or editor composite texture
5. restore viewport/render-target state if the editor expects it

### D3D12 Descriptor Constraints

D3D12 has explicit descriptor heaps and per-frame command recording constraints. Do not copy D3D11 assumptions into D3D12 code.

Rules:

- Allocate SRV/sampler/RTV/DSV descriptors through `D3D12Context` helpers.
- Use `ResetPostProcessDescriptorAllocators()` for transient post-process descriptors where appropriate.
- Use `PushRenderTarget()` / `PopRenderTarget()` for temporary render target changes when available.
- Ensure resource states are transitioned before rendering or sampling.
- Do not assume a native pointer can be rebound without descriptor setup.
- Do not exceed declared descriptor counts such as `kTextureSlotCount`, `kOffscreenRtvCount`, or `kDsvDescriptorCount`.

### Swap Chain And Resize

Window resize has two layers:

- swap chain resize through `GpuSwapChain`
- renderer/pass resource resize through `Renderer::Resize`

Do both when needed. Resizing the swap chain alone does not recreate shadow maps, post-process textures, editor offscreen targets, or pass-local depth buffers.

Do not let a render pass call swap-chain resize directly. Resize is coordinated by the scene/window layer and renderer.

### ImGui And Editor Interop

ImGui access goes through `IEditorImGuiRHIInterop`:

```cpp
IEditorImGuiRHIInterop* interop;
ImGuiBackendHandles handles = interop->GetImGuiBackendHandles();
```

Do not call `imgui_impl_dx11` / `imgui_impl_dx12` directly outside backend context code. Editor code should stay backend-agnostic.

For editor scene previews, use RHI/public renderer handles. Do not reach into a D3D11/D3D12 context from editor UI just to fetch private render target resources.

### Device Loss

Backends expose:

```cpp
bool IsDeviceLost() const;
const std::string& GetLastDeviceError() const;
```

If adding long-running GPU operations, check and propagate device-loss errors. Do not spin forever on failed presents, failed resize, or failed resource creation.

### RHI Change Checklist

Before changing RHI or backend code, verify:

1. Does the change compile for every enabled backend?
2. Is the generic API expressed in `Gpu*` types, not native D3D types?
3. Does D3D12 have descriptor/resource-state handling equivalent to D3D11 binding?
4. Are resources released and recreated on resize?
5. Are render targets unbound before they are sampled?
6. Are constants bound with the current VS+PS slot `b0` behavior in mind?
7. Does editor offscreen still receive the composited output?
8. Does `xmake run MyEngineTests` and `xmake build` pass?

## ImGuizmo And External Math Libraries

Some external tools assume column-vector or right-handed matrices. Do not feed engine matrices directly without checking the tool convention.

For ImGuizmo, the editor adapts matrices so gizmo interaction remains visually consistent with the engine view-projection. If changing gizmo math, verify:

- camera forward direction
- handedness
- row-vector order
- pivot-space translation
- non-uniform scale handling

## Common Bug Patterns

Avoid these changes unless you are deliberately converting an entire path:

- Using `viewProj * world` instead of `world * viewProj`.
- Using `mul(matrix, vector)` in HLSL while CPU uploads row-vector matrices.
- Treating camera forward as `-Z`.
- Generating CSM corners with negative split depth.
- Uploading fewer cascade matrices than the shadow pass renders.
- Applying cross-cascade lerp when cascades do not overlap in receiver coverage.
- Checking projected depth against `[-1, 1]`.
- Binding the raw offscreen scene color in editor preview after adding a post-process composite stage.
- Rotating the built-in Quad ground with the wrong sign and flipping its normal downward.

## Review Checklist Before Submitting Rendering Math Changes

Before changing rendering math, answer these questions in the PR or commit note:

1. Is the code using row-vector order end to end?
2. Is shader code using `row_major` and `mul(vector, matrix)`?
3. Is camera-space visible depth positive `+Z`?
4. Are depth checks using `[0, 1]`?
5. Are CPU split distances and shader split selection using the same depth metric?
6. Are all generated matrices uploaded to the shader?
7. Is editor offscreen output showing the same composited result as the runtime backbuffer?
8. Did `xmake run MyEngineTests` and `xmake build` pass?
