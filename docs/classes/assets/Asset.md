# Asset / AssetHandle

## Asset

资源基类，统一资源标识、路径、名称、类型与状态。

### 关键职责

- 提供 `AssetID`（路径哈希）
- 提供 `AssetType` / `AssetState`
- 作为 Texture/Mesh/Material/Model 的基类

## AssetHandle\<T\>

类型安全资源句柄（基于 `shared_ptr` 的轻封装）。

### 关键职责

- 统一外部资源访问语义
- 支持 `IsValid()` 与便捷运算符访问
