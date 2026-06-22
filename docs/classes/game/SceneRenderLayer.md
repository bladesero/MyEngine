# SceneRenderLayer

## Role

`SceneRenderLayer` is the scene runtime facade used by both Editor and Player.
It still owns the active `SceneLayer` lifecycle, but it delegates viewport,
rendering, and default-scene setup to narrower runtime helpers.

## Responsibilities

- Drive `SceneLayer` edit/play/pause/step lifecycle.
- Forward camera, picking-ray, viewport-rect, and input-enable requests to
  `SceneViewportController`.
- Forward renderer output, swapchain resize, and present/offscreen behavior to
  `SceneRenderHost`.
- Call the legacy `DefaultSceneFactory::PopulateIfEmpty` hook after scene load.

## Collaborators

- `SceneViewportController`: camera setup, editor viewport rect, input, rays,
  and audio listener transform.
- `SceneRenderHost`: `IRenderContext`, `Renderer`, scene color view, swapchain
  resize, command-list viewport, and scene render submission.
- `DefaultSceneFactory`: legacy no-op hook for future explicit scene templates.

Editor code should prefer `EditorContext::GetSceneViewport()` and
`EditorContext::GetSceneRenderHost()` when it only needs camera/ray or render
output access. `GetSceneLayer()` remains transitional for scene lifecycle and
compatibility.
