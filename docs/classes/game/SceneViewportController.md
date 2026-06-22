# SceneViewportController

## Role

`SceneViewportController` owns scene camera state, viewport bounds, editor
viewport input gating, camera movement, and screen-ray construction.

## Responsibilities

- Initialize and update the fly camera used by scene rendering.
- Track either the full window viewport or the editor Scene View rectangle.
- Apply keyboard, mouse, and gamepad movement when viewport input is enabled.
- Frame the editor camera to fixed directions for Scene View overlay controls.
- Orbit the editor camera around a supplied focus point for axis-gizmo dragging.
- Toggle the Scene View camera between perspective and orthographic projection.
- Build picking rays from window-space screen coordinates.
- Sync the active camera transform to the audio listener.

## Editor Controls

`SceneViewport` exposes `FrameDirection`, `OrbitAroundFocus`,
`ToggleProjectionMode`, and `IsOrthographic` for editor-only viewport chrome.
These APIs only affect the Scene View editor camera. They do not mutate scene
data, Game View, the main `CameraComponent`, or PlayWorld simulation state.
