# Runtime UI / RmlUi

Runtime UI lives under `src/Runtime/UI` and uses RmlUi as the retained-mode
document, style, layout, event, and animation engine. MyEngine wrappers keep the
scene/component, asset, input, and render contracts portable.

## Runtime Contract

- `UICanvasComponent` is the serialized scene entry point. It supports
  `AssetDocument` mode for hand-authored `.rml/.rcss` files and `ActorTree`
  mode for UI authored as a normal Scene Actor subtree.
- `UIRectTransformComponent`, widget components, and layout components are
  serialized Runtime components. They are authoring facades; RmlUi remains the
  retained DOM, layout, and render backend.
- `UIActorTreeBuilder` converts a canvas actor subtree into an in-memory RML
  document. `UISystem` reloads that document only when the subtree signature
  changes.
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
- Editor UI remains ImGui. Scene Outliner can create a UI Canvas and child
  UI actors, while Inspector sections edit the Runtime UI components and Game
  View previews the generated RmlUi document.
- The first supported canvas space is screen space. World-space UI should be
  added as a separate projection policy without changing the component
  serialization contract.
