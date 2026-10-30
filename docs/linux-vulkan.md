# Linux Vulkan build and validation

Linux uses the Vulkan backend by default. The runtime uses SDL3's Vulkan surface
creation, supporting the X11 or Wayland driver chosen by SDL. Windows keeps its
existing native Win32 Vulkan surface path; macOS keeps Metal.

## Requirements

- xmake 3.0.7 or newer and a C++17 compiler
- Vulkan loader development library (`libvulkan-dev` on Debian/Ubuntu)
- Vulkan 1.3-capable graphics driver with dynamic rendering, plus an X11/Wayland
  session. Mesa lavapipe can exercise correctness in software; it does not prove
  native GPU performance or vendor-driver behavior.
- Slang 2026.19 was validated; use a recent compiler supporting `SV_VulkanInstanceID`, needed to preserve
  the `firstInstance` object index in GPU-driven draws. The old 2025.6.3 bundle
  does not support that semantic. Set `MYENGINE_SLANGC` to the full `slangc` path
  when it is not on `PATH`.

Validated compiler download: [Slang v2026.19](https://github.com/shader-slang/slang/releases/tag/v2026.19),
`slang-2026.19-linux-x86_64.tar.gz`, SHA-256
`bd6cfc47b7353b2cffa36a866b1584749f76f4b2dbebf6dda26bc3fb40dc3c0e`.

The xmake package recipes resolve SDL3, Vulkan headers and the other C++ libraries.
Shared SDL and Slang libraries are copied beside the built applications on Linux.
If Slang was installed outside the system, its shared-library directory may also
need to be on `LD_LIBRARY_PATH`.

## Build

```sh
tools/build-linux.sh
# or:
xmake f -p linux -m debug --vulkan=y -y
xmake build MyEnginePlayer
xmake build MyEngineEditor
xmake build MyEngineTests
xmake build MyEngineRuntimeLinkProbe
xmake run MyEngineRuntimeLinkProbe
```

`MYENGINE_BUILD_MODE=release tools/build-linux.sh` selects release mode.
When a Vulkan SDK is in a custom directory, expose its loader library using the
compiler's standard `LIBRARY_PATH`/linker configuration.

## Reproducible diagnostic fixture

Create a new disposable project without changing the repository's original scene:

```sh
python3 tools/create-linux-vulkan-fixture.py ../MyEngineVulkanDiagnostic
xmake run MyEnginePlayer --project ../MyEngineVulkanDiagnostic --backend vulkan
xmake run MyEnginePlayer --project ../MyEngineVulkanDiagnostic --backend vulkan \
  --scene Content/Scenes/VulkanPBRDiagnostic.scene.json
```

The first scene tests geometry, red/green colors, a checker texture, a camera,
sky and a visible HUD. The PBR scene adds physically lit materials and shadow
rendering. The generator refuses to overwrite an existing directory. It does not
invent or replace the missing original monkey model.

For a separate GPU-driven deferred fixture:

```sh
python3 tools/create-linux-vulkan-fixture.py ../MyEngineVulkanDeferred --render-path deferred
xmake run MyEnginePlayer --project ../MyEngineVulkanDeferred --backend vulkan
```

This option removes forward-only custom shader references from the generated
materials, preserving their colors, textures, metallic and roughness properties.
It selects native static materials for the indirect GBuffer path. Forward-only
code shaders such as `Mesh`/`ShadowedMainPass` do not provide a GBuffer pass and
correctly produce an error material if used unchanged in a deferred scene.
Run these commands from the engine checkout so development EngineContent resolves.

## Run and check

```sh
xmake run MyEnginePlayer --project /path/to/project --backend vulkan
xmake run MyEngineEditor --project /path/to/project --backend vulkan
xmake run MyEngineTests
```

With the Khronos validation layer installed, set
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` during smoke testing. Check both
rendered pixels and validation output; successful startup alone does not prove
correct geometry, material bindings or UI composition.

The default repository scene references `Content/Mesh/monkey.obj`, which is not
tracked in the repository. Restore that original asset before claiming the scene
is complete. A diagnostic scene with authored primitives, textures, lighting,
a camera and visible UI can verify the renderer independently of the missing model.

Linux publishing is not implemented. The Editor marks the Publish action
unavailable rather than invoking the Windows publisher. Windows DXBC/DXIL and
publishing tests require their supported host toolchains; `TestProjectFPSContentSmoke`
also requires the separately populated `Builds/ProjectFPS` project.

Pipeline cache files are stored under `$XDG_CACHE_HOME/MyEngine`, falling back to
`$HOME/.cache/MyEngine`. Cache files are driver/device-specific and atomically replaced.
