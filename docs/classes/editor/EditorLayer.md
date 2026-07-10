# EditorLayer

## Role

ImGui/ImGuizmo editor shell that coordinates panels, actions, project settings,
workspace preferences, and scene editing against `SceneRenderLayer`.

## Contract

- Owns the editor ImGui frame and renders after the scene layer.
- Registers editor actions in `EditorActionRegistry`; toolbar buttons and
  shortcuts execute through the same action path.
- Stores editor shortcuts in `EditorWorkspace` as user preferences, not in the
  project manifest or gameplay input config.
- Provides the Settings modal with Project, Gameplay Input, and Shortcuts tabs.
- Keeps scene-changing operations routed through `EditorCommandStack` when they
  need undo/redo support.
- Keeps core panels as C++ hosts. Panels may expose extension points, but core
  Scene Outliner, Inspector, Asset Browser, viewport, log, and profiler behavior
  must not depend on project scripts.
- Routes production edit operations through `EditorOperators`: global actions,
  panel context menus, and scripted extension points should share command,
  selection, asset, component, prefab, and transaction semantics.
