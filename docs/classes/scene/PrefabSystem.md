# Prefab System

`PrefabSystem` stores reusable Actor subtrees in `*.prefab.json` assets. A Scene stores only the instance root placement, asset path, asset UUID, and instance overrides; source nodes are reconstructed as one deferred Scene creation batch.

## Asset format

- `version`: format version, currently `1`.
- `uuid`: must match the adjacent `.meta` file.
- `rootLocalId`: stable identifier of the source root.
- `nodes`: preorder Actor records containing `localId`, `parentLocalId`, local Transform, active state, and stable component order.

Local IDs identify source objects across instances and source updates. Runtime Actor IDs and `ActorHandle` generations are never serialized into the Prefab asset.

Nested Prefabs, source-node deletion overrides, variants, and sibling reordering are intentionally unsupported in version 1.

## Instance and override semantics

`PrefabSystem::Instantiate` reserves all Actor handles, queues every component and parent relationship, then commits through the Scene command buffer. Components therefore deserialize and attach only after the complete template structure is available.

Overrides are field-level JSON Pointer operations plus structural operations for component add/remove and added Actor subtrees. The instance root Transform is placement data stored by the Scene and is not an override of the source root Transform.

- `CaptureOverrides` diffs an instance against its source.
- `ApplyAll` writes the instance subtree back to the source and refreshes all matching instances.
- `RevertAll` clears one instance's overrides and refreshes matching instances.
- `Unpack` removes source linkage while preserving the live Actor subtree.
- `RefreshInstances` recreates matching instances while preserving root Actor IDs, outer parents, placement, and per-instance overrides.

## Editor and cook integration

The Asset Browser recognizes `.prefab.json`, supports drag/drop into the Scene View and Scene Outliner, and the Outliner can create a Prefab from the selected subtree. The Inspector exposes Apply All, Revert All, Unpack, and Select Source on instance roots.

Cook dependency validation traverses Scene-to-Prefab-to-component asset references. It rejects missing or unsafe paths, missing metadata, UUID mismatches, malformed Prefab headers, and nested Prefab references.

## Scene record

```json
{
  "id": 42,
  "parentID": 0,
  "transform": { "position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] },
  "prefabInstance": {
    "asset": "Content/Prefabs/Vehicle.prefab.json",
    "uuid": "...",
    "overrides": []
  }
}
```
