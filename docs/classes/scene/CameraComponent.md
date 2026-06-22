# CameraComponent

## Role

Scene component that marks an actor as a render camera. `GameViewport` uses the
first active, enabled component with `isMainCamera=true` as the runtime preview
camera.

## Responsibilities

- Serialize camera projection settings with the scene.
- Build a `Camera` from the owner actor world transform and viewport aspect.
- Provide a fallback-friendly main-camera flag for Editor and Player rendering.
