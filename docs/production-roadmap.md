# MyEngine production hardening roadmap

Target baseline: Windows x64 single-player 3D production using D3D11/D3D12.
Vulkan and macOS remain experimental until they pass the same conformance gates.

## Definition of done

A milestone is complete only when its implementation, migration/compatibility
tests, clean build, smoke gate, failure-path tests, and relevant runtime launch
gate all pass. Static source checks alone do not close an item.

The project owner may close a milestone with an explicitly documented exception.
An exception is not a PASS and must retain the missing evidence and future
follow-up in the validation record.

## Sprint 1 - stable baseline

- [x] Central engine and persistent-format version declarations.
- [x] Immutable project, scene, prefab, input, and save compatibility fixtures.
- [x] Scheduler dependency ordering, cycle rejection, and phase timing metrics.
- [x] Scheduler metrics exposed to the native and scripted profiler surfaces.
- [x] Windows hosted CI for debug smoke and release deliverable builds.
- [x] **Accepted exception:** hosted workflow baseline evidence is deferred; the
      current-worktree debug smoke passed locally on 2026-07-12.
- [x] **Accepted exception:** `40c754ee` remains the already-pushed combined
      TypeRegistry/Scheduler implementation; shared `master` is not rewritten.

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
- [x] **Accepted exception:** real D3D11/D3D12 driver/device removal on the
      isolated runner is deferred; the supervised harness is implemented but
      has not produced real-removal evidence.
- [x] Add a supervised real-device-loss lab harness that records immutable
      evidence and rejects the engine's synthetic injection path.
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
- [x] The GPU job is restricted in workflow code to the canonical repository
      and `master`, uses read-only token permissions, non-persistent checkout
      credentials, pinned Actions, and a documented host runbook.
- [x] **Accepted exception:** configuration and rejection testing of the
      `myengine-gpu-production` GitHub environment is deferred.
- [x] **Accepted exception:** first self-hosted GPU soak evidence is deferred;
      the equivalent local 60-second dual-backend gate passed.
- [x] Complete backend-neutral resource/resize/readback/pipeline/descriptor
      conformance coverage for the two stable backends.

Exit evidence: both stable backends pass conformance and launch gates, every
package/crash is traceable to archived symbols and inputs, and the soak gate shows
no sustained CPU/GPU resource growth.

Sprint 1-3 and M4 are closed for the current milestone as of 2026-07-12. The
five checked exception entries above record deferred evidence, not successful
remote or real-device execution.

## M5 - performance budgets and runtime scalability

Keep the production target on Windows x64 single-player D3D11/D3D12. Platform
expansion remains lower priority than proving stable frame pacing, bounded
resource growth, and deterministic background work on the supported target.

### M5.1 Performance budgets and evidence

- [x] Add a backend-neutral runtime performance budget evaluator with warmup,
      minimum sample count, p50/p95/p99/max frame time, p95 GPU time, peak
      working-set growth from the post-warmup baseline, dropped fixed ticks,
      deterministic violations, and JSON output.
- [x] Add a Player CLI capture mode that feeds real `FrameStats`, scheduler
      dropped ticks, available GPU timings, process working set, build
      provenance, scene, backend, and resolution into the evaluator; retain raw
      post-warmup samples and transactionally write schema-v1 JSON.
- [x] Add backend-neutral GPU adapter/driver identity to the RHI contract and
      include adapter, driver, vendor/device IDs, VRAM, revision, and software
      status in Player performance reports.
- [x] Store project/profile budgets in a versioned config with v0 migration and
      future-version rejection. Command-line warmup/minimum overrides apply only
      when explicitly provided and are recorded in the report.
- [x] Add representative cold-start, warm gameplay, scene transition, and
      stress profiles. Compare only like-for-like hardware classes and retain
      raw samples with the summary. `tools/performance-gates.ps1` executes all
      four profiles on one adapter and rejects hardware/profile drift. Profile
      v3 also budgets initial-scene Ready and maximum reload latency from live
      SceneManager transitions.
- [x] Add the first checked-in `desktop-60fps` / `desktop-discrete` profile and
      include/validate project Config JSON files in the cook dependency closure.
- [x] Make the dual-backend release smoke validate real Player performance
      reports and fail on insufficient samples or budget violations.

### M5.2 Background work ownership

- [x] Add a bounded Runtime `TaskService` with stable task names, high/normal/low
      FIFO priority queues, cooperative cancellation tokens, typed shared
      results, exception propagation, scope-owned cancel/join, shutdown drain,
      and observable submitted/completed/cancelled/failed/queued statistics.
- [x] Move SceneManager background scene read/parse preparation from unowned
      `std::async` to a `TaskScope`; replacement/cancel requests signal the old
      task and SceneManager destruction joins outstanding work.
- [x] Move AssetManager `LoadAsync` and deduplicated `RequestAsync` work into an
      AssetManager-owned task scope. `Clear` cancels/joins before taking the
      cache lock, and SceneManager preload tracking now uses typed task handles.
- [x] Replace subsystem-owned `std::async` calls with a Runtime task service
      that defines priority, cancellation, shutdown, exception propagation,
      affinity, and bounded concurrency. Runtime and Editor source contain no
      remaining `std::async` ownership sites.
- [x] Route SceneManager preload/parse, AssetManager requests, shader cook, and
      Editor import/publish jobs through explicit task scopes. A project close,
      scene cancellation, or engine shutdown must join or cancel every scope.
- [x] Add starvation, cancellation-race, shutdown-with-work, exception, and
      deterministic single-worker tests before enabling higher concurrency.

### M5.3 Streaming and resource lifetime

- [x] Separate scene dependency discovery, CPU decode, runtime object commit,
      GPU upload, and activation into budgeted stages with progress and cancel.
- [x] Add scene/zone streaming ownership so references, actor handles, physics,
      navigation, audio, scripts, and prefab instances cannot outlive a zone.
- [x] Enforce CPU asset, GPU resource, upload-byte, descriptor, actor, and
      transient-frame budgets with hysteresis and observable eviction reasons.
- [x] Enforce the measurable CPU asset, queued GPU-upload byte/task, and streamed
      Actor dimensions through one Player budget controller. CPU LRU reports
      exact evictions plus pin/builtin/reference blockers; upload backlog reports
      pending/peak/processed bytes and task/byte/time deferrals; WorldZone actor
      commits pause at the global cap.
- [x] Track live/peak GPU Buffer and Texture residency across D3D11, D3D12,
      Vulkan, and Metal creation paths with destruction-backed release; track
      logical views/samplers/bind groups as Descriptor pressure. RenderGraph now
      rejects live transient graphs before allocation when byte/resource/view
      limits are exceeded and deterministically trims its persistent pool to a
      low watermark. D3D12 native resource/sampler/RTV/DSV descriptor slots now
      follow allocator lease and deferred-fence release lifetimes, including
      reserved slots, peaks, allocation failures, Player reports, script stats,
      and independent budget hysteresis. Material texture residency uses a
      stable global LRU, preserves pinned and in-flight view references with
      explicit blockers, clears stale asset handles before release, and rebuilds
      evicted textures on demand. Mesh vertex/index residency uses the same
      lifetime-safe policy with an active-use grace window to prevent
      evict/reupload thrashing. Sustained GPU/descriptor/transient pressure
      raises a bounded reversible quality level: future texture uploads skip
      top mips and particle simulation/render counts fall deterministically;
      healthy hysteresis restores quality one level at a time.
- [x] Add repeated transition and constrained-budget stress gates that prove no
      stale callbacks, use-after-free, unbounded queues, or sustained growth.
- [x] Add a deterministic 20-transition replacement/cancellation gate that
      asserts the final world and zero residual asset/GPU-upload queue work.

Exit evidence: checked-in profiles produce provenance-linked JSON reports;
release Player meets frame/resource budgets on both stable backends; all
background work is owned and cancelable; repeated streaming transitions remain
within CPU/GPU memory and frame-time limits.

## P0/P1 small-game production closure

### P0 stability foundation

- [x] Keep the active world renderable while scene read, parse, preload,
      incremental instantiate, GPU upload fence, and atomic activation run.
- [x] Use request-scoped task cancellation, dependency pin release, bounded
      main-thread instantiation, and post-swap asset garbage collection.
- [x] Add scene/zone ownership and repeated constrained-budget transition gates
      proving that CPU/GPU resources and callbacks cannot outlive their world.
- [x] Transfer scene dependency pins into the activated World, release them on
      World/request destruction, and expose generation-backed lifetime tokens;
      the 20-transition gate verifies replacement generations and zero pin,
      async-load, and GPU-queue residue.
- [x] Add stable WorldZone ownership for actors, asset pins, cancellable task
      scopes, generation guards, deterministic teardown/statistics, and migrate
      queued AngelScript/UI callbacks away from unguarded Scene pointer commits.
- [x] Build the higher-level distance/portal-driven additive content streamer
      on this ownership primitive, with load/unload hysteresis, teleport-safe
      deterministic priority, bounded actor/transition budgets, dependency
      pins, pause-safe progress, script control, diagnostics, cancellation, and
      whole-zone rollback for invalid local relationships.
- [x] Add an engine-owned loading/error overlay with stage progress, diagnostics,
      cancel, retry, and safe-world dismissal; Player boots a renderable safe
      World before asynchronously requesting the startup scene, so missing or
      corrupt startup content no longer terminates the process.

### P1 production features

- [x] Add versioned user settings outside the install/project directory for
      display, graphics, audio, input, and accessibility preferences.
- [x] Add the versioned per-user settings store with transactional backup
      recovery, v0 migration, validation, script read/write/reset APIs, and
      Player precedence of project defaults < user settings < command line.
- [x] Apply the persisted audio, input, and accessibility values to live mixer,
      input-device, UI-scale, subtitle, and camera-feedback systems.
- [x] Apply persisted Master/Music/Effects/Voice values to the live audio mixer
      in Player and UserSettings script writes/resets; input preferences and
      action-map overrides are likewise live.
- [x] Productize SaveGame with slots, metadata, autosave/checkpoint policy,
      backup recovery, compatibility diagnostics, and user-data storage.
- [x] SaveGame v3 provides slot metadata/listing, timestamps, build provenance,
      explicit last-known-good backup read/restore, and a configurable storage
      root; Player selects the per-user SDL preference directory.
- [x] Add bounded autosave rings without rename chains, latest-autosave lookup,
      validated checkpoint slots, and matching AngelScript APIs.
- [x] Add runtime input rebinding, conflicts, device glyph switching, gamepad
      reconnect, dead zones, sensitivity, inversion, and vibration settings.
- [x] Add schema-v2 user action-map overrides, typed runtime rebind/conflict
      APIs, project-binding reset, activity-based keyboard/gamepad glyph mode,
      reconnect-safe gamepad slots, live dead-zone/sensitivity/inversion, and
      vibration scaling with AngelScript access.
- [x] Add a versioned localized input-glyph atlas for keyboard/mouse,
      Xbox, PlayStation, and Nintendo families; map SDL controller types,
      hot-switch with the last active device, resolve action/source prompts,
      select the OS locale with deterministic fallback, expose script/UI
      diagnostics, and retain the atlas through cook dependencies.
- [x] **Accepted exception:** shipping settings-screen capture and automated
      reconnect execution against physical/virtual controller backends remain
      deferred workflow evidence for this milestone.
- [x] Add a Boot/MainMenu/Loading/Gameplay/Pause/GameOver flow contract with
      modal UI/input ownership and consistent time/audio pause semantics.
- [x] Add the Runtime `GameFlowController` contract for stable flow states,
      reason-owned nested pause, system/UI/world input ownership, gameplay
      action gating, loading rollback, and synchronized Scene/audio pause;
      SceneLayer and AngelScript pause/load entry points now share it.
- [x] Connect project-authored MainMenu, Pause, Settings, and GameOver screens
      to the contract and add focus restoration.
- [x] Provide engine-standard MainMenu/Pause/Settings/GameOver screens through
      a Runtime-owned modal stack, with restored focus, keyboard/gamepad/mouse
      navigation, GameFlow ownership, script flow entry points, and live
      persisted mixer controls, plus project-authored visual replacement.
      Projects can now provide versioned `RuntimeScreens.ui.json` overrides and
      Rml documents while stable action IDs, modal semantics, settings behavior,
      and GameFlow ownership remain engine-controlled. Invalid documents fall
      back to the generated standard screen with observable diagnostics.
- [x] Route SDL window focus gain/loss through the Runtime event contract and
      apply versioned `pauseWhenUnfocused` Player settings using the independent
      `WindowInactive` pause owner; focus gain cannot release other owners.
- [x] Add reusable runtime screens/widgets, focus navigation, DPI/aspect/safe
      area validation, font fallback, and UI diagnostics.
- [x] Add the reusable `RuntimeUIScreenStack` with stable screen/action IDs,
      duplicate validation, modal roots, wrap navigation, activation/cancel,
      and focus restoration independent of Rml document lifetime.
- [x] Apply live UI scale through the Rml density contract, constrain standard
      screens to normalized safe-area insets, switch to a narrow layout when
      required, load engine fallback faces, expose layout/font diagnostics,
      and persist UI scale, subtitles, and high-contrast controls from the
      standard Settings screen. Shipping resolution screenshots remain in the
      explicitly deferred workflow gate above.
- [x] Add a bounded Runtime subtitle queue with stable IDs, deterministic
      priority/preemption/resume, actual safe-area Rml presentation, live
      subtitle enable/scale/high-contrast settings, diagnostics, and
      AngelScript control. `reduceCameraShake` now attenuates the existing live
      GameplayFeedback camera shake path rather than remaining metadata-only.
- [x] Add Master plus Music/Effects/Voice/UI mixer buses, per-bus game-pause
      policy, streamed AudioSource playback, deterministic concurrency/priority
      voice stealing, fades, live script/settings control, and diagnostics.
- [x] Add one-click project validation for broken references, invalid startup
      state, runtime script errors, absolute paths, oversized assets, and cook
      blockers; Editor and Publisher consume the same structured report.
