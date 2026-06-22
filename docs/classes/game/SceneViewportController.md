# SceneViewportController

## Role

`SceneViewportController` owns scene camera state, viewport bounds, editor
viewport input gating, camera movement, and screen-ray construction.

## Responsibilities

- Initialize and update the fly camera used by scene rendering.
- Track either the full window viewport or the editor Scene View rectangle.
- Apply keyboard, mouse, and gamepad movement when viewport input is enabled.
- Build picking rays from window-space screen coordinates.
- Sync the active camera transform to the audio listener.
