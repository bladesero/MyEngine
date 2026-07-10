# Editor Editing Workflows

This document records the editor editing contract. Core editor panels remain C++
owned; scripts can append tools and context actions, but must not replace core
editing behavior.

## Global Actions

| Action ID | Default shortcut | Expected route |
| --- | --- | --- |
| `edit.delete` | Delete | `EditorCommandOperator::DeleteSelection` |
| `edit.duplicate` | Ctrl+D | actor subtree duplicate or asset duplicate |
| `edit.rename` | F2 | focused panel inline rename |
| `edit.copy` | Ctrl+C | `EditorCommandOperator::CopySelection` |
| `edit.paste` | Ctrl+V | `EditorCommandOperator::PasteSelection` |
| `edit.selectAll` | Ctrl+A | focused editor selection scope |
| `view.frameSelected` | F | `EditorViewportOperator::FrameSelected` |
| `asset.validate` | menu | `EditorImportService::RefreshValidation` and Asset Browser registry refresh |
| `asset.open` | menu | `EditorAssetOperator::OpenAsset` through focused Asset Browser or current asset selection |
| `asset.reveal` | menu | `EditorAssetOperator::RevealAsset` for current asset or focused folder |
| `asset.createFolder` | menu | `EditorAssetOperator::CreateFolder` in the focused Asset Browser folder |
| `asset.createMaterial` | menu | `EditorAssetOperator::CreateAssetFromTemplate` using the material template; from `Content` root this targets `Content/Materials` |
| `asset.createTexture` | menu | `EditorAssetOperator::CreateAssetFromTemplate` using the texture settings template; from `Content` root this targets `Content/Textures` |
| `asset.createPrefab` | menu | `EditorAssetOperator::CreateAssetFromTemplate` using a prefab template with matching asset metadata UUID; from `Content` root this targets `Content/Prefabs` |
| `asset.createAngelScript` | menu | `EditorAssetOperator::CreateAssetFromTemplate` using the AngelScript template |
| `asset.createLua` | menu | `EditorAssetOperator::CreateAssetFromTemplate` using the Lua template |
| `asset.createShader` | menu | `EditorAssetOperator::CreateAssetFromTemplate` using the shader descriptor template |
| `asset.createUI` | menu | `EditorAssetOperator::CreateAssetFromTemplate` using the UI document template |
| `asset.createScene` | menu | `EditorAssetOperator::CreateAssetFromTemplate` using the scene template |
| `asset.move` | menu | `EditorAssetOperator::MoveAsset` into the focused Asset Browser folder |
| `asset.rename` | menu | focused Asset Browser inline rename |
| `hierarchy.expandAll` | menu | Scene Outliner expands every visible tree node |
| `hierarchy.collapseAll` | menu | Scene Outliner collapses every visible tree node |
| `hierarchy.createEmptyParent` | menu | `EditorCommandOperator::CreateEmptyParent` wraps selected actor in an undoable empty parent |
| `hierarchy.unparent` | menu | `EditorCommandOperator::UnparentActor` moves selected actor to the parent scope |
| `hierarchy.moveUp` | menu | `EditorCommandOperator::MoveActorUp` moves selected actor before its previous sibling |
| `hierarchy.moveDown` | menu | `EditorCommandOperator::MoveActorDown` moves selected actor after its next sibling |
| `hierarchy.selectParent` | menu | selection-only jump to selected actor parent |
| `hierarchy.selectPreviousSibling` | menu | selection-only jump to previous sibling |
| `hierarchy.selectNextSibling` | menu | selection-only jump to next sibling |

## Command And Dirty Rules

- Actor create, delete, duplicate, move, rename, active/tag/layer edits, copy,
  and paste must go through `EditorCommandOperator` or `EditorUndoUtil`.
  Creating a child actor from the Scene Outliner must use
  `EditorCommandOperator::CreateChildActor`, so creation and parenting are one
  undoable command instead of separate create and move commands.
  Multi-actor duplicate and delete are single scene snapshot commands and
  should collapse selected descendants when their selected parent subtree is
  already included by the operation.
  Actor copy stores one or more selected root subtrees in the editor clipboard;
  paste creates those roots in one scene snapshot command, preserving subtree
  components and selecting the last pasted root.
- Component add, remove, JSON replacement, and property edits must go through
  `EditorComponentOperator` or an equivalent command helper. Multi-actor
  component removal is a single scene snapshot command and is only available
  for component types present on every selected actor.
- Prefab apply, revert, and unpack must go through `EditorPrefabOperator`.
  `Apply All` must undo both the scene snapshot and the prefab file content.
  The Prefab override list should show category, target, property, preview,
  visible status/diagnostic text, and per-row Apply/Revert actions. Unsupported
  override rows, including persisted overrides from newer/unknown prefab
  schema versions, stay visible but must keep Apply/Revert disabled. The list also
  provides a text filter, diagnostics-only toggle, ready/blocked counts,
  disabled-action tooltips, and collapsible category groups for large
  instances; these controls only affect display and must not mutate prefab
  overrides or dirty the scene. Apply/Revert button failures
  should surface an Inspector-local error message as well as the runtime log,
  while successful operations may show a short success message. `Select Source`
  must route through `EditorSelectionOperator` and may request Inspector focus;
  it is selection UI state only and must not dirty the scene.
- Asset create, delete, duplicate, rename, move, folder operations, refresh, and
  open/reveal dispatch must go through `EditorAssetOperator`. Asset copy/paste
  uses `EditorCommandOperator` for clipboard ownership and
  `EditorAssetOperator::CopyAssetToFolder` for the actual file/meta/registry
  update; Asset Browser supplies the focused folder as the paste target. Rename
  and move operations must refuse to overwrite an existing target file, folder,
  or target `.meta` so asset identity is never silently reused.
- Panel code may read models and collect UI intent, but persistent side effects
  should live in operators or commands.
- Scene View camera actions such as frame selected, axis-frame, orbit, and
  projection toggle should route through `EditorViewportOperator`.

## Observability

- Editing operations should be profiler-visible without spamming normal logs.
  Successful user edits record profiler events; failed operator/service
  requests also write a warning log and, where a panel has local status UI,
  should surface a panel-local message.
- `EditorCommand` records command-stack `Execute`, `Undo`, and `Redo` events.
  Event details include the command name, success state, and whether the
  command dirtied the scene or an external resource.
- `EditorAsset` records asset operator requests such as create, delete,
  duplicate, rename, move, copy, template creation, open, reveal, reimport, and
  import-settings updates. Event details should include the source path,
  target path or folder, template/type, and failure counts when relevant.
- `EditorPrefab` records prefab apply, revert, unpack, create, instantiate, and
  per-override apply/revert requests. Event details should include actor id,
  prefab path when available, override index, override path, and success state.
- `EditorImport` records import-service work: import, reimport, reimport with
  settings, reimport-all, and validate. Event details should include source
  path, artifact path, asset uuid, success state, issue counts, and aggregate
  failure counts.
- `AssetRegistry Refresh` remains owned by `EditorAssetRegistry` under the
  broader `Editor` profiler category. Runtime serializers, importers, and
  asset data models must not depend on `EditorProfiler`; Editor services and
  operators own profiling and user-facing diagnostics.

## Panel Boundaries

- Scene Outliner owns row drawing, inline rename UI, drag/drop hit testing, and
  tree expansion state. Selection, duplicate, delete, active toggle, actor
  unparenting, sibling reorder, and actor move/drop must call operators.
  Create Child Actor must route through
  `EditorCommandOperator::CreateChildActor`.
  Create Empty Parent must route through
  `EditorCommandOperator::CreateEmptyParent`; the operator owns the scene
  snapshot command because the operation combines actor creation and
  reparenting. Unparent/Move Up/Move Down must route through
  `EditorCommandOperator::UnparentActor`, `MoveActorUp`, and `MoveActorDown`.
  UI preset creation (`UI Canvas`, `Button`, `Text`, and layout actors) must
  route through `EditorCommandOperator::CreateUIActor`; the panel only shows
  menu entries and checks whether the selected parent can host UI children.
  Ctrl-toggle and Shift-range selection must route through
  `EditorSelectionOperator` so C++ UI, shortcuts, context-menu actions,
  select-all, and future extension facades share the same selection semantics.
  Select Subtree and Select Children must also route through
  `EditorSelectionOperator::SelectActorSubtree`; Select Subtree includes the
  root actor and Select Children excludes it. Both are selection-only
  operations and must not dirty the scene or enter the command stack.
  Select Parent, Select Previous Sibling, and Select Next Sibling are also
  selection-only actions routed through `EditorSelectionOperator::SelectActor`;
  they must not dirty the scene or enter the command stack.
  Text search plus tag, layer, and component-type filters are panel-local view
  state. They share the same subtree match cache used by drawing, select-all,
  and shift-range ordering. Direct row matches may be highlighted, while parent
  rows that only remain visible because a descendant matched should not be
  treated as direct matches. These filters must not dirty the scene.
  Expand/collapse-all is panel-local UI state and must not dirty the scene.
- Inspector owns section layout and field widgets. Actor base fields such as
  name, active, tag, layer, and static/editor flags must commit editor
  commands. Component field changes should commit property commands; fallback
  snapshot transactions are allowed for complex sections. Multi-actor selection
  v1 shows a summary and batches actor base fields with unambiguous semantics:
  active, tag, layer, and static.
  It supports multi-actor Transform position, rotation, and scale editing
  through `EditorCommandOperator::SetActorsPosition`,
  `SetActorsRotation`, and `SetActorsScale`, including mixed-value display.
  It also lists common component types, can add a registered component to every
  selected actor through `EditorComponentOperator::AddComponents`, can remove a
  common component from every selected actor through
  `EditorComponentOperator::RemoveComponents`, and can edit shared top-level
  bool, numeric, and Vec3 JSON-array fields through
  `EditorComponentOperator::SetComponentPropertyForActors`.
  Nested component JSON and string editing remain separate workflows.
  Scene Settings edits such as scene name, physics gravity, main camera hint,
  and rendering defaults like ambient intensity must route through
  `EditorCommandOperator` so the scene dirty state and undo/redo behavior match
  normal actor edits.
  Add Component remains a C++ Inspector section and must route all additions
  through `EditorComponentOperator`; the UI provides search, category groups,
  and a short recently-used list so large component registries stay navigable
  without changing undo/dirty semantics. The recently-used list is persisted in
  `EditorWorkspace` panel state under the Inspector panel, so it is editor UI
  preference data and must not dirty the scene, project config, asset database,
  or undo stack.
- Asset Browser owns folder/list UI, filtering, and rename/delete confirmation.
  File-system mutations must stay inside `Content/` or `SourceAssets/` and use
  asset commands so `.meta`, registry refresh, and selection update together.
  Asset multi-selection is panel-local UI state: the global primary asset
  selection remains the Inspector target, while Ctrl-click, Shift-click, and
  `edit.selectAll` can collect multiple visible asset rows for batch delete,
  duplicate, move, copy/paste, and context-menu reimport operations. Multi-asset
  copy stores the selected asset paths in `EditorCommandOperator` clipboard
  state. Global and empty-area paste copy each asset into the focused Asset
  Browser folder through `EditorAssetOperator::CopyAssetToFolder`, while an
  asset-row `Paste Into Folder` uses that row asset's parent folder so recursive
  views do not paste into a stale browsing root. Asset-row `Move Selected Here`
  uses the same row parent folder and routes through `MoveSelectedAssetsToFolder`,
  so recursive views can move multi-selected assets without depending on the
  left folder tree selection. Empty-area context menus also expose selected
  asset operations for open, reveal, reimport, copy, move-to-current-folder,
  duplicate, and delete before the folder/create actions, so users can
  right-click whitespace without losing the current selection. Empty-area
  `Open Selected` and `Reveal Selected in Explorer` act on the current primary
  selected asset, while `Move Selected Here` routes through
  `MoveSelectedAssetsToFolder` with the focused Asset Browser folder. Batch reimport skips
  non-imported assets and calls `EditorAssetOperator::Reimport` only for
  selected rows with importer UUIDs. If another panel or command changes the
  primary asset selection, Asset Browser collapses the panel-local selection
  back to that primary asset before handling global actions, so stale multi
  selections cannot move, delete, duplicate, paste, copy, or reimport unrelated
  assets.
  Opening a Scene asset from Asset Browser must show save/discard/cancel UI
  while the current editor scene is dirty. `EditorAssetOperator::OpenAsset`
  still refuses direct dirty scene switches so menu, shortcut, or extension
  paths cannot accidentally discard edits without an explicit caller decision.
  Inspectable assets such as materials, textures, prefabs, models, and audio
  are selected and request Inspector focus. Model, Prefab, and Audio assets use
  dedicated read-only Inspector sections for mesh/material/animation, prefab
  node/component summaries, and clip metadata instead of the generic fallback.
  The Prefab asset section also reports current scene instance count and can
  select matching prefab root instances through
  `EditorPrefabOperator::SelectInstances`. Script, shader, UI, and unknown
  text-like files may open in the external editor instead.
  Text, type, import-state, diagnostics-only, and recursive filters define the
  current asset list view; panel actions such as select-all must honor the same
  filter model as the visible list. Diagnostics include importer record
  diagnostics and `AssetDatabase::ValidateAgainstProject` issues surfaced by
  `EditorAssetRegistry::Refresh`, so the Diagnostics filter can show missing
  sources, stale artifacts, dependency cycles, and other database validation
  problems after `Validate Assets` or a registry refresh. Asset Browser exposes
  `Validate Assets` from its toolbar and empty-area context menu by dispatching
  the global `asset.validate` action, so validation stays owned by
  `EditorLayer::ValidateAssets` and the import service. User-facing validation
  feedback should use `EditorImportService::GetValidationSummaryText` so menus,
  notifications, Asset Browser operation messages, and future validation UI
  share the same report summary. Asset Browser keeps the latest validation
  summary visible next to its operation message and uses
  `EditorImportService::HasValidationIssues` for severity. It may read that
  summary after dispatching the action, but it must not call
  `RefreshValidation` directly. The panel also shows a collapsible diagnostics
  issue list built from the same filtered visible asset set; selecting an issue
  selects that asset and focuses Inspector without mutating files or the
  registry.
  Delete confirmation must query `EditorAssetOperator::FindSceneReferences`
  and `FindProjectSceneReferences` only for the selected assets being confirmed
  and show a warning when current or project scene Actors/components still
  reference those assets. Folder delete confirmation first enumerates contained
  assets through the Asset Browser folder model, then reuses the same warning
  path before calling `EditorAssetOperator::DeleteFolder`. The global
  `edit.delete` action in Asset Browser only opens the relevant asset or folder
  confirmation in the panel message area; it must not delete files immediately,
  hide the confirmation inside a filtered row, or bypass the warning.
  Starting inline asset/folder rename and opening asset/folder delete
  confirmation are mutually exclusive pending states, so F2, Delete, and
  context-menu Rename/Delete cannot leave overlapping editors and confirmations.
  A rename action is consumed only after the panel successfully enters inline
  rename state; failed asset rename setup must return false so EditorLayer can
  continue fallback dispatch. Asset rename setup navigates Asset Browser to the
  selected asset's containing folder and clears browser filters that would hide
  the row, so the inline editor is visible after F2.
  Folder row context-menu actions use the row that opened the popup as the
  target for Refresh, Open, Reveal, Paste, New Folder, Move Here, Rename, and Delete; they must
  not infer the target from a stale previously selected folder. Folder Open
  selects that row, persists the Asset Browser workspace folder state, and
  shows an operation message. Folder-row New Folder creates under that target
  folder, selects the newly created folder, persists workspace state, and shows
  the same operation message as toolbar/menu creation. Folder-row Create
  template items also target that row folder through
  `RequestCreateAssetFromTemplateInFolder`, so recursive views cannot create
  assets in a stale browsing root. Folder-row Paste routes through
  `RequestPasteAssetsToFolder` with the row folder, so copied assets land in
  that directory even when the focused browsing root differs. Global, toolbar,
  and empty-area Create Folder keep the focused folder selected so subsequent
  template creation still uses the same browsing root. Confirmed folder delete
  executes against the pending confirmation path.
  The warning is read-only and does not change the deletion command path:
  confirmed asset deletes still go through `EditorAssetOperator::DeleteAsset`,
  while confirmed folder deletes go through `EditorAssetOperator::DeleteFolder`;
  `.meta`, selection, registry refresh, and undo/redo remain handled by the
  operator/command layer.
  Move and rename operations use the same current/project scene reference query
  before mutating files and append a warning to the Asset Browser operation
  message when Actors/components still point to the old asset path. The global
  `asset.move` action and the folder context `Move Selected Assets Here` item
  both route through the same `MoveSelectedAssetsToFolder` panel helper, which
  delegates file/meta/registry changes to `EditorAssetOperator::MoveAsset`.
  Folder move and rename operations enumerate assets under the folder and build
  the same old-path/new-path retarget requests for each referenced asset. They do not
  rewrite scene references implicitly; explicit reference retargeting remains a
  separate command-backed workflow. `EditorAssetOperator::RetargetSceneReferences`
  rewrites matching references in the currently open scene and commits a
  `Retarget Asset References` scene snapshot command, so the fix is undoable
  and marks the scene dirty like other scene edits.
  `EditorAssetOperator::RetargetProjectSceneReferences` scans project scene
  assets under `Content/`, skips the currently open scene file to avoid double
  mutation, rewrites matching serialized asset-reference strings, and commits a
  resource-only `ModifyAssetsCommand`. That project-scene fix is undoable but
  does not dirty the currently open scene. Asset Browser exposes both paths
  through the same `Retarget References` operation message button after a
  successful move or rename that found current-scene or project-scene
  references. Unrelated asset operations clear the pending retarget request so
  stale old/new path pairs cannot be applied accidentally.
  Copy/paste action hooks
  should keep the
  copied asset in the editor clipboard and paste into the currently selected
  folder rather than guessing from the previous asset selection. Failed asset
  operations such as delete, duplicate, rename, paste, or drag/drop move should
  surface a visible Asset Browser message as well as a log warning. Successful
  create, copy, paste, duplicate, delete, rename, and drag/drop move operations
  should also replace stale errors with a short panel-local success message.
  Open, reveal, refresh, and reimport actions follow the same rule so context
  menu, toolbar, shortcut, and drag/drop paths do not leave the user guessing
  whether the request ran.
  Asset Browser view state, including search text, selected folder, type
  filter, import-state filter, recursive mode, and diagnostics-only mode, is
  persisted in `EditorWorkspace` panel state. It is editor workspace UI state
  and must not dirty the scene, project config, asset database, or undo stack.
- Asset Inspector owns metadata, diagnostics, and import-settings editing for
  selected assets. Import settings are edited as validated JSON and applied
  through `EditorAssetOperator::ReimportWithSettings`; Texture sampler controls
  merge into the existing settings JSON instead of replacing unrelated importer
  keys. These edits are undoable asset/import operations and must not dirty the
  scene. Material Inspector saves through `MaterialModifier` /
  `ModifyAssetCommand`, so disk content, the loaded `AssetManager` material, and
  registry state update consistently across save, undo, and redo. Dependency
  and referencer views are read from `AssetDatabase::GetDependencies` and
  `AssetDatabase::GetReferencers`; their `Select` actions must route through
  `EditorSelectionOperator` and must not modify files, registry records, scene
  dirty state, or undo history. Current scene references are separate from the
  asset database graph and are queried through
  `EditorAssetOperator::FindSceneReferences`, which scans serialized component
  values and prefab source paths in the Inspector scene and can select the
  referencing Actor without mutating files, registry records, scene dirty state,
  or undo history. Project scene references are queried through
  `EditorAssetOperator::FindProjectSceneReferences`, which scans project
  `Content` scene JSON files on disk and reports scene path plus actor/component
  location without opening, saving, dirtying, or switching the current scene.
  The same section should surface unresolved
  dependency UUIDs and current-asset `AssetDatabase::ValidateAgainstProject`
  issues so missing references, stale artifacts, and dependency cycles are
  visible without opening publish logs. The Assets menu `Validate Assets`
  action refreshes the same validation report and Asset Browser registry state,
  then reports the validation summary through the existing Editor result UI.
- Editor AngelScript extension points may append tool panels, menu items,
  toolbar items, Inspector sections, and context menu entries. Mutating callbacks
  must call the same facade/operator APIs as C++ panels.
