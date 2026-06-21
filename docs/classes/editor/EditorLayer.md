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
