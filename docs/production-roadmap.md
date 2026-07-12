# MyEngine production hardening roadmap

Target baseline: Windows x64 single-player 3D production using D3D11/D3D12.
Vulkan and macOS remain experimental until they pass the same conformance gates.

## Definition of done

A milestone is complete only when its implementation, migration/compatibility
tests, clean build, smoke gate, failure-path tests, and relevant runtime launch
gate all pass. Static source checks alone do not close an item.

## Sprint 1 - stable baseline

- [x] Central engine and persistent-format version declarations.
- [x] Immutable project, scene, prefab, input, and save compatibility fixtures.
- [x] Scheduler dependency ordering, cycle rejection, and phase timing metrics.
- [x] Scheduler metrics exposed to the native and scripted profiler surfaces.
- [x] Windows hosted CI for debug smoke and release deliverable builds.
- [ ] Run the hosted workflow and retain the first green run as baseline evidence.
- [ ] Split the current TypeRegistry/Scheduler work into reviewable commits.

Exit evidence: a clean checkout builds on Windows CI, all compatibility fixtures
load, scheduler ordering is deterministic, and smoke passes.

## Sprint 2 - schema and transactional persistence

Current status:

- [x] Runtime transactional writer with disk flush, validation, atomic replace,
      last-known-good backup, and deterministic fault injection.
- [x] Project, Scene, Prefab, InputActionMap, SaveGame, AssetDatabase,
      CookManifest, and RuntimeDependencies use transactional writes.
- [x] Common consecutive JSON migration registry and missing-step tests.
- [x] SaveGame v1 to v2 migrated through the common registry.
- [x] Complete TypeRegistry migration and enforce the registry audit.
- [x] Route Project, Scene, Prefab, InputActionMap, SaveGame, AssetDatabase,
      CookManifest, and RuntimeDependencies readers through migration registries.
- [x] Add Scene and Prefab format-level failure-injection tests beyond the
      writer contract.

### TypeRegistry completion

Migrate components in this order: render, physics, animation/scripting, gameplay,
navigation/audio/UI. Each descriptor must define a stable type ID, stable property
IDs, schema version, defaults, inspector policy, script policy, prefab policy,
aliases, and migrations. Add a registry audit test that fails for any persistent
component still relying solely on legacy serialization.

Remove direct component serialization from SceneSerializer and Prefab diffing
after all descriptors pass the audit. Keep the reader compatibility path for one
published compatibility window.

### Transactional writes and migrations

Add a Runtime `TransactionalFileWriter` with sibling temporary file, flush,
read-back validation, atomic replace, and optional last-known-good backup. Route
Project, Scene, Prefab, InputActionMap, SaveGame, AssetDatabase, CookManifest,
and RuntimeDependencies through it.

Add a common JSON migration registry using consecutive `N -> N+1` transforms.
Migrations run on temporary JSON and commit only after validation. Add injected
failures for write, validation, and replace stages.

Exit evidence: every persistent format has a version policy, fixtures prove the
compatibility window, and injected failures preserve the previous valid file.

## Sprint 3 - recovery, nested prefabs, and import transactions

### Editor recovery

Add an Editor-only recovery service. It writes project-scoped scene snapshots to
`.myengine/recovery`, records clean/unclean shutdown state, lists recoverable
snapshots at startup, and never dirties or cooks recovery state. Manual saves
delete only the matching recovered revision after the transactional save succeeds.

Current implementation status:

- [x] Editor-only project-scoped recovery service, transactional snapshots,
      clean/unclean session marker, enumeration/read, and matching-revision removal.
- [x] Recovery lifecycle unit test including simulated unclean restart.
- [x] Connect periodic snapshots, successful manual-save cleanup, startup restore UI,
      and Editor shutdown handling to `EditorLayer`/project lifecycle.

### Nested prefab model

Represent nested instances as source prefab UUID/path, stable instance local ID,
root placement, source revision, and local overrides. Do not flatten nested source
data into authoritative nodes. Detect dependency cycles before save/import/cook.
Support property, component, and actor add/remove overrides and preserve local
overrides when a source prefab refreshes. Add three-level nesting, cycle, refresh,
apply/revert, undo/redo, scene round-trip, and cook-closure tests.

Current implementation status:

- [x] Non-flattened nested reference model with stable instance local ID,
      parent local ID, source path/UUID/revision, placement, and local overrides.
- [x] Recursive instantiation, source refresh with override preservation,
      three-level nesting, scene round-trip, and runtime/Cook cycle rejection.
- [x] Nested references participate in existing property/component/actor override
      capture and prefab apply/revert paths.
- [x] Focused Editor command tests prove nested apply/revert Undo/Redo, including
      restoration of both the source asset and the local instance state.

### Asset import transaction

Stage artifact and database record together. Validate artifact hash and dependency
closure, then atomically promote the artifact and database update. On failure keep
the previous ready artifact/record. Include importer version, settings, platform,
engine build ID, and dependencies in the cache key. Report the invalidation reason.

Current implementation status:

- [x] Importers write to sibling staging artifacts; hash and dependency closure
      are validated before promotion.
- [x] Artifact promotion and AssetDatabase persistence roll back to the previous
      ready artifact/record when validation, promotion, or database save fails.
- [x] Deterministic failure tests cover validation, post-promotion, and
      pre-database-save boundaries and assert that no staging debris remains.

Exit evidence: forced termination cannot corrupt the last valid scene/database,
three-level prefabs survive refresh and cook, and release smoke runs in CI.

## M4 - rendering and release reliability

### RHI conformance

Create a backend-neutral test suite for buffers, textures, views, uploads,
readbacks, render/depth targets, pipeline bindings, descriptor exhaustion,
swapchain resize/minimize/restore, and device-loss state transitions. Run the same
contract against D3D11 and D3D12; Vulkan joins the stable matrix only after parity.

Device loss must surface a stable reason code and diagnostics. Recovery either
recreates the device/swapchain and invalidates GPU caches through one generation
boundary, or exits cleanly with a diagnostic report; partially valid rendering is
not accepted.

### Release provenance and gates

Embed engine version, build ID, commit, configuration, compiler, and shader tool
version into manifests and crash diagnostics. Archive symbols with the build ID.
Generate third-party license inventory and SBOM. Add package signing hooks without
putting private credentials in the repository.

Extend release smoke with package layout/hash checks, D3D11/D3D12 launch, corrupt
archive/manifest/dependency cases, repeated scene load/unload, and a configurable
soak duration. GPU launch/soak runs on a labeled self-hosted Windows runner; hosted
CI continues to run build and headless contracts.

Current implementation status:

- [x] One public-API conformance runner exercises capabilities, structured
      buffers/views/partial updates, texture upload/SRV, render/depth targets,
      shader and graphics-pipeline binding/draw, samplers, swapchain zero-size
      rejection/resize/restore, buffer/texture readback, and descriptor pressure.
- [x] The identical conformance runner passes locally on D3D11 and D3D12 and is
      mandatory in each backend launch performed by release smoke.
- [x] Device loss exposes a stable reason enum, native code, diagnostic, and
      device generation. Editor/Player write a diagnostic report and exit with
      a non-zero code instead of continuing with partially valid rendering.
- [x] Headless tests cover stable device-loss state transitions and reason names.
- [x] Release smoke injects a deterministic device removal after conformance on
      D3D11 and D3D12, asserts exit code 3, and validates provenance, backend,
      stable reason, native code and generation in the diagnostic report.
- [ ] Add a driver/device fault-injection lab on the self-hosted GPU runner to
      prove real D3D11/D3D12 removal reaches the clean-exit path.
- [x] Runtime build info exposes engine version, build ID, commit,
      configuration, compiler, and shader tool contract.
- [x] CookManifest and crash diagnostics embed provenance; CI injects the full
      Git commit SHA.
- [x] CI archives release symbols under commit/run-addressed artifact names.
- [x] Published packages include SPDX SBOM and third-party license inventory.
- [x] Publishing exposes an external signing-tool hook without repository secrets.
- [x] Release smoke validates package layout/hashes, D3D11/D3D12 launch,
      repeated load, configurable soak/resource-growth bounds, and corrupt
      archive/manifest/dependency/project failure cases.
- [x] A labeled self-hosted Windows GPU job runs the dual-backend release gate.
- [ ] Run and retain the first self-hosted GPU soak as baseline evidence.
- [x] Complete backend-neutral resource/resize/readback/pipeline/descriptor
      conformance coverage for the two stable backends.

Exit evidence: both stable backends pass conformance and launch gates, every
package/crash is traceable to archived symbols and inputs, and the soak gate shows
no sustained CPU/GPU resource growth.
