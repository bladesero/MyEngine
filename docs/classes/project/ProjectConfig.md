# ProjectConfig

## Role

Runtime-owned project manifest used by both Editor and Player.

## Contract

- Opens `MyEngine.project.json` from a project root.
- Stores schema `version`, project `name`, and a project-relative `startupScene`.
- Accepts startup scenes only under `Content/` and can require the scene file to exist.
- Editor may open a project without a manifest and creates it when assigning the first startup scene.
- Player requires a valid manifest unless `--scene` supplies an explicit override.
- Stores publish output directory and target settings under the `publish` object;
  the supported target is currently `windows-x64` only.
- Published projects provide `Content.pak` and the shared `CookManifest` schema.
  Player verifies both and uses `CookedProjectCache` to atomically install,
  reuse, or repair a hash-versioned cache before resolving the startup scene.
