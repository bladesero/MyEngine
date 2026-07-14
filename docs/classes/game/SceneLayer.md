# SceneLayer

## Role

`SceneLayer` owns scene file lifecycle and runtime simulation state. It keeps
editing and play simulation in separate worlds:

- `EditorWorld`: the persistent editable scene used by load/save, undo, editor
  panels, editor selection, and Scene View's default mode.
- `PlayWorld`: a temporary clone created by `BeginPlay()` and destroyed by
  `StopPlay()`. Editor-only inspection can view it, but changes are not copied
  back to EditorWorld.

## Responsibilities

- Load, create, and save only the EditorWorld.
- Clone EditorWorld into PlayWorld on `BeginPlay()` using scene serialization.
- Update only PlayWorld while running, paused-step, or playing.
- Preserve EditorWorld and dirty state when PlayWorld is stopped.
- Expose explicit world accessors: `GetEditorScene()`, `GetPlayScene()`, and
  `GetSimulationScene()`.

`GetScene()` is retained as a transitional runtime compatibility alias for
`GetSimulationScene()`. Editor code should use `EditorContext`, where
`GetScene()` intentionally means EditorWorld.
## Asynchronous scene transitions

Runtime transitions use explicit read, parse/dependency-discovery, preload,
incremental instantiate, GPU upload, and activation stages. Read/parse and asset
decode run through owned task scopes; Actor and Component construction remains
on the main thread under a per-frame budget. A monotonic upload fence waits only
for work submitted before candidate activation, so later unrelated uploads do
not extend the transition. The current world is paused but remains renderable
until atomic activation. Failed, cancelled, and superseded requests release pins
and leave the current world unchanged.
On successful activation, dependency pins move from the request into the Scene
and remain owned until that World is destroyed. `SceneLifetimeToken` is the
callback guard for work that may complete later: completion code retains the
guard returned by `TryAcquire()` for its entire commit. Scene teardown takes the
exclusive gate and invalidates the token; generations distinguish replacement
Worlds even when a caller reuses the same scene path.

`SceneRenderLayer` owns a highest-level system Rml document for transition UX.
It displays the stable stage name, progress, requested path, and failure detail
without requiring a project-authored Canvas. Escape cancels an active request;
after failure, R or Enter retries the same path/options and Escape dismisses the
error while keeping the current safe World. Player uses this path for its first
startup-scene request as well as later gameplay transitions.
