# P0/P1 production foundation validation - 2026-07-12

Implemented the first small-game production closure increment on the current
uncommitted worktree.

## P0 scene transition stability

- Scene read and JSON parse/dependency discovery are separate owned tasks.
- Preload, budgeted main-thread instantiation, GPU upload fence, and atomic
  activation are observable stages with monotonic progress.
- GPU fences capture a queue boundary and are completed by render-thread upload
  processing or explicit queue clear; later uploads do not extend an older fence.
- Cancellation/failure releases dependency pins and keeps the active scene.
- Activation now transfers dependency pins to the candidate Scene. Scene
  destruction invalidates its generation-backed lifetime token before teardown
  and releases the pins; SceneManager destruction also joins owned read/parse
  tasks and releases any untransferred dependencies.
- Runtime UI creates an engine-level loading/error document even when the World
  has no project Canvas. Scene stages drive progress; retry/dismiss/cancel keep
  the prior safe World intact. Player now starts that World and asynchronously
  requests startup content, making a missing startup scene recoverable.
- WorldZone scopes now own actors, idempotent dependency pins, cancellable tasks,
  stable names, statistics, and generation guards. Teardown tests prove task
  cancellation, actor removal, pin restoration, and token invalidation; queued
  AngelScript/UI callbacks now require a live Scene guard before invocation.

## P1 save-game foundation

- SaveGame schema v3 adds display/scene/time/screenshot/build metadata.
- Slots are listed deterministically and report validity plus backup presence.
- Last-known-good backups can be inspected and explicitly restored after primary
  corruption; v1 fixtures migrate consecutively through v2 to v3.
- Player selects an SDL per-project preference directory, avoiding writes to a
  published installation; Runtime tests/tools retain an explicit root override.
- AngelScript exposes slot listing and backup inspection/restoration.
- Autosaves fill a bounded 1-16 slot ring and overwrite the oldest file without
  a multi-file rename transaction; checkpoint IDs cannot escape the save root.
- A 20-transition stress case repeatedly supersedes in-flight scene requests
  and finishes with the expected world and no asset/GPU queue residue.

## P1 runtime user-settings foundation

- Schema v1 persists display, graphics, audio, input, and accessibility groups
  outside the project/install directory with validation and v0 migration.
- Transactional writes retain a last-known-good backup; corrupt primaries can
  be recovered without replacing that backup with corrupt bytes.
- Player applies resolution, window mode, frame limit, VSync, and backend at
  startup with project defaults < user settings < command-line precedence.
- AngelScript can read, replace, and reset the settings JSON. Applying audio,
  input, and accessibility values to their live systems remains follow-up work.
- Schema v2 adds a validated user action-map override. Runtime conflict/rebind,
  persist/reset, last-active-device glyph mode, sensitivity, inversion, global
  gamepad dead zone, and vibration scaling are exposed to AngelScript; the
  project action map remains the reset baseline.

## Executed gates

- `xmake build MyEngineTests`: PASS.
- `xmake run MyEngineTests`: PASS, 175/175 tests.
- `tools/smoke.ps1`: PASS, full debug build and 175/175 tests.

Game-flow closure evidence:

- Runtime `GameFlowController` defines stable Boot/MainMenu/Loading/Gameplay/
  Paused/GameOver states and reason-owned nested pause semantics.
- Scene, audio, and gameplay action gating consume one flow snapshot; DLL-boundary
  input access is covered by `TestGameFlowPauseOwnershipContract`.
- SceneLayer and AngelScript pause/load entry points use the same controller;
  failed loads restore the previous stable flow state.

Audio production evidence:

- Master plus Music/Effects/Voice/UI buses apply persisted Player settings and
  expose live AngelScript control and diagnostics.
- GameFlow pause only suspends buses whose policy opts in; UI voices remain
  available while gameplay is paused.
- AudioSource serializes bus, streaming, priority, concurrency group, and limit;
  deterministic admission/stealing is tested without requiring an audio device.

Window-focus evidence:

- SDL focus gained/lost events enter the Runtime event queue and SceneLayer
  applies the versioned Player `pauseWhenUnfocused` preference.
- `WindowInactive` is an independent pause reason; focus restoration preserves
  user/system/editor owners, and starting Play while unfocused remains paused.
- UserSettings v3 persists the policy and includes an explicit v2-to-v3
  migration with backup-recovery coverage.

Project validation evidence:

- Editor **Validate Project**, Publisher, and Cooker share one structured
  `ProjectValidator` report layered over the cook dependency graph.
- Tests cover broken references, unsafe absolute paths, malformed runtime
  scripts, oversized warnings, missing startup scenes, and stable locations.
- Startup checks require a physical file under the selected project, preventing
  a same-named scene in the Editor working directory from shadowing a blocker.

Runtime screen evidence:

- `RuntimeUIScreenStack` validates stable IDs and preserves focus across nested
  MainMenu/Pause/Settings/GameOver modal screens independently of Rml documents.
- Keyboard, gamepad, and pointer routing is modal; Pause/Resume atomically gates
  gameplay input through GameFlow, and script APIs can enter menu/game-over flow.
- Standard Settings mixer changes apply live and are persisted through the
  versioned transactional user-settings store; integration coverage reloads
  the saved value and verifies focus restoration.
- UI scale now drives Rml density-independent pixels; engine-standard screens
  are constrained to validated normalized safe-area insets and deterministically
  switch to a narrow layout. Integration coverage verifies live/persisted scale,
  subtitle preference, exact safe dimensions, and invalid-inset rejection.
- Engine fallback faces use stable process-lifetime backing storage, avoiding
  invalidation across repeated UISystem creation. Layout/font diagnostics are
  available to C++ and AngelScript through `UI::GetDiagnosticsJson()`.
- Runtime subtitles now use a bounded 32-cue stable-ID queue with deterministic
  priority, preemption/resume, expiry, overflow accounting, safe-area Rml
  presentation, live enable/scale/high-contrast accessibility, and
  `UI::ShowSubtitle/ClearSubtitles/GetSubtitleJson` script APIs. Focused tests
  cover invalid cues, same-ID replacement, preemption with remaining duration,
  bounded overflow, live presentation suppression/restoration, and script use.
- `reduceCameraShake` is backed by a Runtime DLL-owned accessibility state and
  attenuates actual `GameplayFeedbackComponent` shake offsets to 25 percent.
  The DLL boundary test prevents Editor/Test consumers from observing a stale
  header-local copy of the setting.
- Project flow-screen visuals now load from versioned
  `Content/Config/RuntimeScreens.ui.json`. Checked-in MainMenu, Pause, Settings,
  and GameOver Rml documents preserve stable engine action IDs and focus/modal
  semantics. Integration coverage proves the custom Pause document becomes
  active, path traversal is rejected, and an Rml document missing the required
  action contract produces an observable generated-screen fallback.
- Input glyphs now distinguish keyboard/mouse, Xbox, PlayStation, and Nintendo
  families through SDL controller types. The checked-in versioned atlas maps
  canonical sources to SVG sprites and English/Chinese labels; tests prove
  live keyboard/gamepad switching, localized action resolution, future-version
  rejection, and cook retention of the SVG dependency. Runtime screens consume
  the same resolver for Select/Back hints, while scripts expose family,
  action/source descriptors, and locale selection.

Additive streaming evidence:

- `WorldZoneStreamer` runs in paused-capable PreUpdate and supports distance,
  portal, OR/AND triggers, hysteresis, stable priority, and bounded transitions.
- Async read/parse tasks and dependency pins belong to generation-backed zones;
  actor creation is incremental and invalid local relationships roll back all
  actors and pins from the attempted generation.
- Integration coverage exercises jitter resistance, high-speed teleport,
  paused portal progress, per-frame actor budgets, mid-instantiation cancel,
  cross-zone-parent rejection, and final zero actor/zone/pin residue.

Runtime resource-budget evidence:

- Player owns one coordinator for CPU asset high/low watermarks, bounded stable
  LRU eviction, queued GPU-upload task/byte pressure, and the streamed Actor cap.
- Asset reports identify every released path/byte and distinguish pin, builtin,
  and external-reference blockers. Upload reports retain pending/peak/processed
  bytes plus task/byte/time deferral counts and failure totals.
- WorldZone instantiation pauses at the Actor cap and reports blocked frames;
  recovery resumes after capacity returns. Tests force pressure, verify exact
  eviction/deferral accounting and hysteresis recovery, and confirm the values
  cross the Runtime DLL boundary through `FrameStatsProvider`.
- Common GPU object lifetime accounting records live/peak Buffer and Texture
  bytes plus logical Descriptor objects; D3D11/D3D12 builds exercise the actual
  backend commit sites, while Vulkan/Metal use the same guarded implementation.
- RenderGraph tests prove an over-budget graph is rejected before RHI creation,
  pooled reuse is reported without a second allocation, and a pressured pool is
  deterministically reduced to its configured low watermark.
- D3D12 native descriptor accounting follows resource, sampler, RTV, and DSV
  pool allocation through deferred fence retirement instead of treating lease
  destruction as immediate reuse. Reserved slots, live/peak values, and failed
  allocations are visible in Player reports, FrameStats, and script diagnostics.
- Material texture residency is bounded by a global stable LRU. Pinned assets
  and externally held texture views produce path/byte blockers; eligible entries
  clear their raw asset handle before GPU release and rebuild through the normal
  upload path on next use. Asset lifetime tokens also make cache cleanup safe
  when CPU GC destroys an asset first. A focused test covers both blocker
  classes, eviction, expired CPU assets, low-watermark recovery, and reupload.
- Mesh vertex/index buffers now use lifetime-safe global residency records and
  a configurable active-use grace window. Focused coverage proves an active
  mesh is blocked, becomes evictable when cold, and rebuilds on demand.
- Sustained GPU, descriptor, native-slot, or transient pressure drives a bounded
  two-level quality state. Pressure and healthy hysteresis are tested in both
  directions; texture mip bias and deterministic particle caps are exercised
  as real degradation effects rather than telemetry-only flags.
- `tools/smoke.ps1`: PASS with 177 tests. One transient SDL3.dll copy contention
  was recovered by the script's existing retry.
- `tools/release-smoke.ps1 -SoakSeconds 5 -ReloadIterations 3`: PASS on D3D11
  and D3D12, including performance reports, device-loss, repeated scene load,
  and package failure paths. A stale incremental release ABI was diagnosed from
  its retained crash report; the gate now forcibly rebuilds `MyEngineRuntime`
  before its executable consumers, and the coherently rebuilt package is the
  recorded passing result.
- `git diff --check`: PASS apart from line-ending conversion notices.

After the subtitle/accessibility closure, `tools/smoke.ps1` passes 179/179
tests. The isolated `release-smoke.ps1 -SoakSeconds 3 -ReloadIterations 1`
also passes D3D11/D3D12 launch, performance reports, synthetic device loss,
reload, package corruption, dependency, and invalid-config paths with the new
Runtime exports and generated subtitle document present.

After project-authored flow screens, smoke passes 181/181 tests. The isolated
release gate publishes 65 files (the versioned screen config plus four Rml
documents are included) and passes D3D11/D3D12 launch, performance, device-loss,
reload, corruption, dependency, and invalid-config checks.

After localized device glyphs, smoke passes 182/182 tests. The release package
contains 67 files, including `InputGlyphs.glyph.json` and its SVG atlas, and the
same D3D11/D3D12 release gate passes. Physical/virtual controller execution and
shipping screenshots remain the explicitly accepted milestone exception; the
deterministic SDL-type and simulated activity paths are the local evidence.

The longer 60-second soak was not repeated for this increment; the focused
five-second release regression is not a replacement for the milestone soak.
