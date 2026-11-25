# MyEngine native BUI editor

This is the first native BUI migration slice. `MyEngineBuiEditor` opens a
MyEngine project without starting the legacy editor. It constructs BUI kit
widgets directly: the ImGui compatibility bindings are disabled, and the target
does not link the engine, SDL, Dear ImGui, ImGuizmo, or imnodes.

The workbench reads the project manifest, opens its startup scene, and lists
other scenes in `Content/Scenes`. Select an actor in the hierarchy to inspect
its ID, activation state, transform and component types. You can switch scenes
or reload the active file. This is a **read-only view of serialized data**:
prefab instance roots show the prefab reference, but the tool does not
instantiate prefab children, run scripts or write back to the project.

## Build and run

Prerequisites: CMake 3.20+, Git, and a compiler that supports C++20 for BUI.
Builds fetch pinned revisions of BUI and nlohmann/json. On Linux, install X11
development headers and use an X11/XWayland session for the desktop window;
the headless tests do not need a display. BUI supplies its own window and UI
rendering; it does not render the engine's scene.

From the repository root:

```sh
cmake -S tools/bui-editor -B build/bui-editor
cmake --build build/bui-editor --config Release
./build/bui-editor/MyEngineBuiEditor --project .
ctest --test-dir build/bui-editor -C Release --output-on-failure
```

With a multiconfig generator, the executable will usually be
`build/bui-editor/Release/MyEngineBuiEditor` (append `.exe` on Windows).
`--self-test --project .` renders offscreen, drives BUI pointer input, and
checks the hierarchy/inspector and scene picker against this repo's sample
project. Negative tests exercise a nonexistent project and a malformed cyclic scene.

## Integration boundary

`xmake run MyEngineEditor` remains the full, ImGui-based production editor.
This workbench does not change the Player, graphics backends, binary exports,
or existing editor packaging. The native BUI workbench is built separately.
The next integration should bind BUI widgets to MyEngine's existing editor
commands and scene model instead of adding a second scene writer. For the
embedded viewport, keep the engine's one-present-per-frame ownership and add
a tested scene-color composite/input bridge; BUI's low-level render sink does
not capture native kit widgets. Port the viewport and gizmo, remaining panels,
and node editor before removing the old target or its third-party packages.

Upstream BUI is GPL-3.0-or-later. This branch does not vendor BUI, distribute
a linked executable, or change MyEngine's licensing. Review distribution
obligations before shipping a combined build.
