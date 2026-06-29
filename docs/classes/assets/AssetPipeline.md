# Asset Pipeline

Editor imports external model and texture sources into `SourceAssets/`. Identity
is stored in source-controlled `.meta` sidecars. Runtime code only reads metadata;
it never creates or repairs it.

`AssetImportService` writes platform artifacts under
`Library/<platform>/<uuid>/`. Artifact cache keys include the source hash,
settings, importer name/version, engine build ID and target platform. `Library`
and `.myengine/AssetDatabase.json` are local generated data and are not committed.

`AssetDatabase` is a versioned, atomically replaced JSON index containing source
and artifact paths, hashes, import state, diagnostics and forward dependencies.
It also provides reverse-reference queries. Failed reimports retain the previous
artifact record and publish diagnostics instead of replacing valid runtime data.
When `SourceAssets/`, `Library/`, or `.myengine/AssetDatabase.json` exists,
publish preflight treats the database as the imported-asset source of truth and
rejects missing sources, missing or tampered artifacts, unknown dependencies,
dependency cycles, and non-ready import states.

`AssetManager` consumes source or imported artifacts and accepts explicit UUID
registration for generated artifacts. Asset changes are synchronously reported
through `AssetChangedEvent`; hot reload verifies content hashes after timestamp
changes before replacing an in-memory asset.
