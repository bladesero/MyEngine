# Workspace and Publishing

## Editor workspace

Starting `MyEngineEditor` without `--project` displays the project selector.
It opens existing manifests, creates projects with a default startup scene, and
stores up to ten recent project roots in the user's MyEngine workspace file.
**Project Settings** edits the project name and publish output directory; the
target is fixed to `windows-x64`. Publishing rejects unsaved scene changes and
reports its final output or error in the Editor.

## Platform support contract

The supported publish target matrix is intentionally narrow:

| Target | Support level | Runtime backend | Shader compiler |
| --- | --- | --- | --- |
| `windows-x64` | release-ready | D3D11, D3D12; optional Vulkan when built with `--vulkan=y` | D3DCompile/FXC, Slang for Vulkan |
| `macos-arm64` | experimental, unverified on this host | Metal | Slang |
| Linux | future target only | none in-repository | none |

Linux may define `MYENGINE_PLATFORM_LINUX`, but this repository does not ship a
Linux GPU `IRenderContext`; do not advertise Linux Player GPU support yet.

## Cook and publish

`ProjectPublisher` builds a dependency graph rooted at every scene, validates
referenced models, materials, textures, shaders, scripts and external model
files, and applies one canonical containment policy to Content paths. It still
cooks all runtime `Content/` files; dependency analysis is a correctness gate,
not a pruning pass.

The Editor **Validate Project** action runs the same `ProjectValidator` used by
publishing. Its deterministic report separates errors and warnings and retains
asset/referrer paths for navigation. Missing or shadowed startup scenes, broken
cook references, unsafe absolute paths, malformed runtime AngelScript, and cook
limits block publishing; individually oversized assets are warnings by default.
Startup validation requires the physical scene inside the selected project, so
a same-named file in the Editor working directory cannot satisfy the check.

The v2 `CookManifest` records the engine/build/content/archive compatibility
contract, required backends, project identity and SHA-256 hashes. Windows
packages require exactly `d3d11,d3d12` for normal builds and
`d3d11,d3d12,vulkan` when `MYENGINE_ENABLE_VULKAN` is compiled in; macOS
packages require exactly `metal`.
A Windows PE collector recursively resolves non-system imports from the build
output or the x64 MSVC Redistributable and writes `RuntimeDependencies.json`.
The staged package is fully re-read and extracted before the previous package
is replaced.

Use the Editor **Publish** action or `tools/publish.ps1`. The standalone Player
first validates local package files from its executable directory: if
`RuntimeDependencies.json` is present, its SHA-256 must match `CookManifest.json`
and every listed runtime DLL must exist with the expected size and hash. This
happens before project content or startup scenes are loaded, so an incomplete
package fails with a non-zero exit code instead of falling back to DLLs from an
external path. The Player also rejects malformed or incompatible manifests,
unsafe paths, mismatched SHA-256 hashes, corrupt entries and trailing PAK data.
`CookedProjectCache` is isolated by `projectId/archiveHash`, checks free space,
uses atomic staging, retains three versions per project within a 20 GiB budget,
and rebuilds missing or modified cached files automatically.

`tools/release-smoke.ps1` builds and publishes an isolated project, checks the
package dependency manifest, launches D3D11 and D3D12, and verifies corrupt
archive, missing scene, missing direct/transitive DLL, tampered dependency
manifest, and invalid-config failures.
Pass `-Vulkan` to build with `--vulkan=y`, require Vulkan shader payloads in the
manifest, and launch the published Player with `--backend vulkan`.
