# IconsManager

`IconsManager` is a Runtime utility for app and editor icon assets. It reads SVG files from `EngineContent/Editor/Icons`, rasterizes them to RGBA8 pixels, caches CPU pixels, uploads icon textures through `IRHIDevice`, and can write Windows `.ico` files for executable resources.

## Responsibilities

- Resolve icon names such as `engine-editor`, `play-start`, or `mesh` to SVG files.
- Replace `currentColor`-style SVG paint with the requested `IconColor`.
- Rasterize simple SVG paths, rectangles, and circles into RGBA8 buffers.
- Upload cached icon textures to the active RHI backend for ImGui usage.
- Apply SDL window icons through `IWindow::SetIconFromPixels`.
- Generate multi-size `.ico` files for Editor, Player, and Cooker Windows executables.

## Boundaries

The class lives in Runtime and has no dependency on Editor headers. Editor UI consumes it through `EditorWidgets`, while Player and tooling can use it directly for window or executable icons.
