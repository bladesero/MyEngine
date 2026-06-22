# Runtime UI / RmlUi

Runtime UI lives under `src/Runtime/UI` and uses RmlUi as the retained-mode
document, style, layout, event, and animation engine. MyEngine wrappers keep the
scene/component, asset, input, and render contracts portable.

## Runtime Contract

- `UICanvasComponent` is the serialized scene entry point. It stores
  project-relative RML, RCSS, font paths, visibility, interactivity, sort order,
  and input mode.
- `UISystem` owns RmlUi initialization, the shared in-game context, font loading,
  event forwarding, context update, and draw-list collection.
- `RmlAssetLoader` resolves RML, RCSS, fonts, and texture paths through
  `AssetManager`, including project-relative and `Content/Engine` paths.
- `RmlRenderInterface` converts RmlUi compiled geometry and generated textures
  into `UIDrawList` commands backed by MyEngine RHI buffers and textures.
- `ScreenUIPass` renders the draw list as the final screen-space pass after scene
  composite.

## Boundaries

- Runtime UI does not include or depend on `src/Editor`.
- Editor UI remains ImGui. Editor support is limited to asset discovery and
  `UICanvasComponent` inspector editing.
- The first supported canvas space is screen space. World-space UI should be
  added as a separate projection policy without changing the component
  serialization contract.
