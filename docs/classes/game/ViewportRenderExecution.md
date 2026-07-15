# ViewportRenderExecution

## Role

`ViewportRenderExecution` owns the render execution state for one viewport. It
binds a viewport's `Scene`, resolved `Camera`, rectangle, `Renderer`, and narrow
RHI services for a single render submission.

## Responsibilities

- Own one runtime `Renderer` instance per viewport.
- Resize that renderer's offscreen resources when the viewport size changes.
- Set the active command-list viewport before rendering.
- Render either to the swapchain for Player or to offscreen scene color for
  Editor viewport panels.
- Expose the viewport output view through `GetOutputView()`.
- Forward each viewport's `RendererFeatureMask`, allowing specialized runtime
  previews to omit optional passes without introducing an Editor renderer.

`SceneViewport` and `GameViewport` each own their own `ViewportRenderExecution`.
`SceneRenderLayer` schedules those viewport renders but no longer owns separate
render hosts.
