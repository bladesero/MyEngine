# SceneSerializer

## 角色

场景序列化器（JSON）。

## 关键职责

- `Scene -> JSON` 保存
- `JSON -> Scene` 加载
- 处理 Actor、Transform、Component、父子关系恢复
# Load plans

`BuildLoadPlan` is the worker-safe parsing boundary and does not construct
Runtime objects. `InstantiateLoadPlan` is main-thread-only and constructs bounded
Actor batches before restoring hierarchy. Synchronous loads use the same path
with an unbounded batch.
