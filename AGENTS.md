# MyEngine — Codex context

**中文**：本文件供 Codex / 自动化助手快速对齐仓库约定；**架构细节、目录树与模块依赖**以根目录 [`design.md`](./design.md) 为准（中文，含 Mermaid 图）。实现与文档冲突时以 **`xmake.lua` 与源码** 为准。

---

## What this is

- **C++17** game/engine codebase: **SDL3** windowing and events, **ImGui** + **ImGuizmo** editor UI, scene graph (**Scene** / **Actor** / components), **JSON** scene serialization (**nlohmann_json**), rendering via **IRenderContext** (D3D11/D3D12 on Windows, Metal on macOS).
- **Single shared library** `MyEngineRuntime` (`runtime` basename) contains almost all engine code **including** `EditorLayer`; executables only add `main.cpp` / `player_main.cpp` / tests and link the DLL.

---

## Build and run

- **Prerequisites**: [xmake](https://xmake.io/) ≥ **2.8.0**, C++17 toolchain (MSVC on Windows, Clang on macOS). On Windows, **PowerShell** is used for HLSL embedding (see below).
- **Smoke check** (recommended before/after engine changes):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\smoke.ps1
```

This checks that `xmake` itself starts, configures debug mode, builds, and runs `MyEngineTests`.
- **Configure and build** (from repo root):

```bash
xmake f -m debug    # or release
xmake
```

- **Run** (working directory should be project root; `set_rundir` is `$(projectdir)` for Editor/Player):

```bash
xmake run MyEngineEditor    # editor: SceneRenderLayer + EditorLayer
xmake run MyEnginePlayer    # no editor UI; SceneRenderLayer only
xmake run MyEngineTests     # unit tests
```

Running the Editor without `--project` opens the project selector. Pass
`--project <directory>` to open a project directly. The selector can create a
project and keeps a per-user recent-project workspace list. Use **Project
Settings** to edit the project name and Windows publish output; publishing is
rejected while the current scene has unsaved changes.

`MyEnginePlayer` reads `MyEngine.project.json` from the project root and loads its
`startupScene`. Use `--project <directory>` to select another project or
`--scene Content/Scenes/Other.scene.json` to override the configured scene.

Publish a standalone project directory with cooked `Content.pak`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish.ps1 -Project . -Mode release
```

Run the isolated release/package/Player acceptance gate with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\release-smoke.ps1
```

The output path is controlled by `publish.outputDirectory` and `publish.target`
in `MyEngine.project.json`. The package contains Player/runtime binaries,
`Content.pak`, `CookManifest.json`, and the project manifest. Player validates
the manifest and archive, then atomically installs or repairs a hash-versioned
runtime cache. Publishing currently supports Windows x64 only.

- **Windows GPU backend** (Editor/Player): optional ` --backend d3d11` or ` --backend d3d12` (see `main.cpp` / `player_main.cpp`).

---

## Where to look

| Topic | Location |
|--------|----------|
| Full layout, targets, dependency layers | [`design.md`](./design.md) |
| Targets, packages, file lists, defines | `xmake.lua` |
| App loop and layers | `src/Runtime/Core/Application.cpp`, `Engine.cpp` |
| Scene + rendering | `src/Runtime/Game/SceneRenderLayer.*`, `src/Runtime/Renderer/` |
| Editor UI | `src/Editor/EditorLayer.*` |
| Entry points | `main.cpp` (editor), `player_main.cpp` (player) |

---

## Conventions for changes

1. **New `.cpp` files** under `src/Runtime/` or `src/Editor/` must be added to **`xmake.lua`** → target `MyEngineRuntime` → `add_files(...)`, unless they are header-only.
2. **Public headers** for consumers of the DLL: under `src/Runtime/`; `add_headerfiles` already globs `src/Runtime/(**.h)`.
3. **Dependency direction**: prefer **Core → Scene/Assets/Camera → Renderer → Game**; **do not** make `Renderer` depend on `Editor`. Editor sits beside Game and uses `SceneRenderLayer*`.
4. **Math**: row-major `Mat4`, left-handed, Y-up; align with `EngineMath.h` and existing shaders.
5. **Resources**: `Content/` is copied next to binaries (`copy_game_content`); keep paths consistent with `AssetManager` / serialization.
6. **SDL3**: package must stay **shared** (`add_requireconfs` for libsdl3) to avoid duplicate SDL symbols with ImGui.

---

## Windows: HLSL → embedded bytecode

- Shader sources live under `EngineContent/Shaders` or project `Content/Shaders`. Windows publishing cooks every `.shader` descriptor to D3D11/D3D12 bytecode in `Content.pak`; HLSL/HLSLI files are excluded.
- That script invokes **dxc** to compile `.hlsl` sources and generates **`build/hlsl_generated/`** (e.g. `ShaderBytecodeWindows.cpp` + headers). Editing HLSL or the embed script may require a **clean rebuild** of the runtime target.
- If build fails with missing generated files, ensure PowerShell can run and **DirectX Shader Compiler (dxc)** is on `PATH` (typical with Windows SDK).

---

## Editor vs Player (rendering)

- **Editor** (`main.cpp`): pushes `SceneRenderLayer` then `EditorLayer`; `SceneRenderLayer::SetPresentEnabled(false)` so **ImGui draws after** the 3D pass and presentation happens once.
- **Player** (`player_main.cpp`): only `SceneRenderLayer` with `SetPresentEnabled(true)`.

---

## Testing

- Add or extend tests in `tests/EngineTests.cpp`; run `xmake run MyEngineTests`. Tests link `MyEngineRuntime` and may use `AssetManager`, `SceneSerializer`, etc.

---

## Platform gaps

- **Linux**: `MYENGINE_PLATFORM_LINUX` may be defined, but there is **no** `IRenderContext` implementation in-repo yet; do not assume a GPU backend exists there.

---

## Documentation policy

- **Do not** duplicate long architecture prose here; update **`design.md`** when structure or dependencies change materially.
- Keep this file **short**: build commands, file-add workflow, HLSL/embed note, and dependency rules.
