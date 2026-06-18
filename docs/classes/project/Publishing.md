# Workspace and Publishing

## Editor workspace

Starting `MyEngineEditor` without `--project` displays the project selector.
It opens existing manifests, creates projects with a default startup scene, and
stores up to ten recent project roots in the user's MyEngine workspace file.
**Project Settings** edits the project name and publish output directory; the
target is fixed to `windows-x64`. Publishing rejects unsaved scene changes and
reports its final output or error in the Editor.

## Cook and publish

`ProjectPublisher` preflights the startup scene, Content root, target, project
manifest, Player, runtime DLL, and SDL DLL. It cooks all runtime `Content/`
files into deterministic `Content.pak` and writes the Runtime-owned
`CookManifest` contract with file sizes and FNV-1a hashes. Installation uses a
staging and backup transaction, so a failed publish preserves the previous
successful package.

Use the Editor **Publish** action or `tools/publish.ps1`. The standalone Player
rejects malformed manifests, unsafe paths, mismatched archive hashes, and
corrupt entries. `CookedProjectCache` uses a per-archive cross-process lock and
staging directory to install the cache atomically; missing or modified cached
files are rebuilt automatically.

`tools/release-smoke.ps1` builds and publishes an isolated project, checks the
package allowlist, launches D3D11 and D3D12, and verifies corrupt archive,
missing scene, missing DLL, and invalid-config failures.
