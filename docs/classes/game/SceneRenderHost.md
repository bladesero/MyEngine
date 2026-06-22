# SceneRenderHost

## Role

`SceneRenderHost` binds a `Scene`, `Camera`, `Renderer`, and `IRenderContext`
for high-level scene rendering. It is intentionally above the low-level
renderer backends and below `SceneRenderLayer`.

## Responsibilities

- Own the runtime `Renderer` instance used by a scene layer.
- Resize renderer targets when the viewport size changes.
- Resize the swapchain on window resize.
- Set the active command-list viewport before rendering.
- Render to the swapchain for Player or offscreen scene color for Editor.
