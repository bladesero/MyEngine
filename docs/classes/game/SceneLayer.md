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
# Asynchronous scene transitions

Runtime requests are prepared as a `SceneLoadPlan`: file I/O, JSON parsing,
validation, and dependency discovery run on a worker, while Actor and Component
construction remains on the main thread under a per-frame budget. The current
world is paused but remains renderable until the candidate world is complete.
Failed, cancelled, and superseded requests leave the current world unchanged.
