# AssetManager

## 角色

资源缓存与加载中心（单例）。

## 关键职责

- 按扩展名分派 loader
- 缓存与查询资源（按路径/ID/类型）
- 内置默认资源创建（白贴图、基础网格、默认材质）
- 资源卸载与 GC（`UnloadUnreferenced`）
- 维护 CPU 内存估算与预算告警

## 统计相关

- `GetEstimatedAssetCpuBytes()`
- `GetEstimatedAssetCpuBytesByType()`
- `SetAssetCpuBudgetBytes()`
- `LogAssetMemorySummary()`

## Project and model sub-assets

- Project-owned paths serialize relative to the project root, for example
  `Content/Models/character.gltf`.
- Imported mesh, material, and embedded texture identities append a stable
  fragment such as `#mesh` or `#material-0`.
- Resolving a fragment on a cold cache first loads the source model, then returns
  the registered sub-asset. This allows scene mesh and material references to
  survive an Editor restart.
- Legacy imported material paths stored as `__builtin__/Name` are recovered by
  matching the material name within the model referenced by the component mesh.
