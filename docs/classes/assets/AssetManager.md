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
