# Input

## Role

Frame-local raw input state plus project-configurable gameplay action mapping.

## Contract

- `Engine::PollPlatformEvents` feeds keyboard, mouse, and gamepad state into `Input`.
- Raw APIs remain available for editor tooling and low-level gameplay queries:
  `IsKeyDown`, `IsMousePressed`, `GetGamepadAxis`, and related transition helpers.
- Project action maps sit above raw input and are loaded from
  `Content/Config/Input.input.json` by default.
- Action maps support `Button`, `Axis1D`, and `Axis2D` entries with stable source
  strings such as `Keyboard/W`, `Mouse/DeltaX`, `Gamepad/South`, and
  `Gamepad/LeftX`.
- Button bindings are OR-combined. Axis bindings pick the value with the largest
  absolute magnitude per axis, then clamp to `[-1, 1]`; each binding may set
  `scale`, `scaleX`, `scaleY`, and `deadZone`.
- `Input::LoadActionMapFromFile` installs a project map. On failure the caller
  should fall back to `Input::SetDefaultActionMap`.
- Lua scripts can query semantic input through `Input.action_down`,
  `Input.action_pressed`, `Input.action_released`, `Input.axis`, and `Input.axis2`.
