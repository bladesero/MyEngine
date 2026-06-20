# Workspace and Publishing

## Editor workspace

Starting `MyEngineEditor` without `--project` displays the project selector.
It opens existing manifests, creates projects with a default startup scene, and
stores up to ten recent project roots in the user's MyEngine workspace file.
**Project Settings** edits the project name and publish output directory; the
target is fixed to `windows-x64`. Publishing rejects unsaved scene changes and
reports its final output or error in the Editor.

## Cook and publish

`ProjectPublisher` builds a dependency graph rooted at every scene, validates
referenced models, materials, textures, shaders, scripts and external model
files, and applies one canonical containment policy to Content paths. It still
cooks all runtime `Content/` files; dependency analysis is a correctness gate,
not a pruning pass.

The v2 `CookManifest` records the engine/build/content/archive compatibility
contract, required D3D backends, project identity and SHA-256 hashes. A Windows
PE collector recursively resolves non-system imports from the build output or
the x64 MSVC Redistributable and writes `RuntimeDependencies.json`. The staged
package is fully re-read and extracted before the previous package is replaced.

Use the Editor **Publish** action or `tools/publish.ps1`. The standalone Player
rejects malformed or incompatible manifests, unsafe paths, mismatched SHA-256
hashes, missing runtime dependencies, corrupt entries and trailing PAK data.
`CookedProjectCache` is isolated by `projectId/archiveHash`, checks free space,
uses atomic staging, retains three versions per project within a 20 GiB budget,
and rebuilds missing or modified cached files automatically.

`tools/release-smoke.ps1` builds and publishes an isolated project, checks the
package dependency manifest, launches D3D11 and D3D12, and verifies corrupt
archive, missing scene, missing direct/transitive DLL, tampered dependency
manifest, and invalid-config failures.
