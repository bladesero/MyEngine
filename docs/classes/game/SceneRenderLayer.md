# SceneRenderLayer

## Role

`SceneRenderLayer` is the scene runtime facade used by both Editor and Player.
It still owns the active `SceneLayer` lifecycle, but it delegates viewport,
rendering, and default-scene setup to narrower runtime helpers.

## Responsibilities

- Drive `SceneLayer` edit/play/pause/step lifecycle.
- Forward editor camera, picking-ray, viewport-rect, and input-enable requests
  to `SceneViewport`.
- Resolve Game View camera state through `GameViewport` and scene
  `CameraComponent` data.
- Schedule Scene/Game viewport renders through each viewport's
  `ViewportRenderExecution`.
- Schedule Scene/Game only when their latest ImGui dock tab was actually
  visible. Activity is collected and committed once per ImGui frame so a
  temporarily cleared candidate does not reset Scene View mouse capture; only
  the final inactive state disables viewport and Game UI input.
- Own the Material Preview dirty/realtime policy. Static graphs submit one frame
  after invalidation; graphs with a reachable `Time` node submit continuously
  only while the Shader Graph panel is active.
- Route Scene View to EditorWorld by default, or to `GetSimulationScene()` when
  the editor enables PlayWorld inspection.
- Route Game View to `GetSimulationScene()` (EditorWorld in edit mode,
  PlayWorld during play/pause/step).
- Resize the swapchain once on window resize; per-viewport render targets are
  resized by each viewport execution.
- Call the legacy `DefaultSceneFactory::PopulateIfEmpty` hook after EditorWorld
  scene load.

## Collaborators

- `SceneViewport`: editor camera setup, editor viewport rect, input, rays, and
  audio listener transform, plus its own render execution/output view.
- `GameViewport`: main-camera resolution and fallback runtime preview camera.
- `ViewportRenderExecution`: per-viewport `Renderer`, scene color view,
  command-list viewport, and scene render submission.
- `DefaultSceneFactory`: legacy no-op hook for future explicit scene templates.

Editor code should prefer `EditorContext::GetSceneViewport()` /
`GetGameViewport()` for camera/ray and render output access. `GetSceneLayer()`
remains transitional for scene lifecycle and compatibility.
`SetSceneViewportUsesSimulationScene()` is a runtime-safe switch used by the
editor; it stores no PlayWorld pointer, so stopping play cannot leave a dangling
Scene View render target.
