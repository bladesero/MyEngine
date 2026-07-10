# AngelScript Editor Extensions

Editor AngelScript is for project extensions, not for replacing core Editor UI.
C++ owns the dock hosts, Scene Outliner, Asset Browser, Viewport, Inspector
core sections, selection, undo/redo, asset registry, and project state.

Project scripts live under `Content/Editor/Scripts/**/*.as`. Engine defaults live
under `EngineContent/Editor/Scripts/**/*.as`.

## Entry Point

```angelscript
void RegisterEditor(EditorRegistry@ registry)
{
    registry.ToolPanel("project.audit", "Project Audit", Right, "DrawProjectAudit");
    registry.MenuItem("Tools/Project Audit", "DrawProjectAuditMenu");
    registry.ToolbarItem("project.audit.run", 100, "DrawProjectAuditToolbar");
    registry.InspectorSection("ScriptComponent", 100, "DrawScriptInspector");
    registry.AssetContextMenu("*", "DrawAssetContext");
    registry.ActorContextMenu("DrawActorContext");
}
```

Core panel ids are reserved and rejected for project tool panels:
`toolbar`, `viewport`, `gameViewport`, `sceneHierarchy`, `inspector`,
`assetBrowser`, `log`, and `profiler`.

`PanelBody` is engine/debug-only. Do not use it for project extensions.

## Safe APIs

Scripts can draw through `UI::*` and can mutate only through controlled
operators:

- `Selection::*`
- `Commands::*` (`CreateActor`, `DeleteActor`, `RenameActor`,
  `SetActorActive`, `SetActorTag`, `SetActorLayer`, `MoveActor`,
  `ExecuteAction`)
- `Assets::*`
- `Transaction::*`
- `Components::*`
- `Validation::*`
- `Project::*`

Scripts must not receive or store C++ object pointers. Player does not load the
editor domain.

## Config

`Config/EditorScripts.json` defaults to allowing append-only extensions and
disallowing core overrides. The important switches are:

```json
{
  "enabled": true,
  "corePanelMode": "cppOnly",
  "allowProjectAppend": true,
  "allowProjectOverrideCore": false,
  "enableToolPanels": true,
  "enableMenuExtensions": true,
  "enableInspectorExtensions": true,
  "enableContextMenuExtensions": true
}
```

Use `Tools/Reload Editor Scripts` to force a reload while the Editor is running.
Compile errors, rejected registrations, and callback failures are logged with
the `[EditorScript]` prefix.
