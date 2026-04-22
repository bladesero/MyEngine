# Scene

## 角色

Actor 集合管理器。

## 关键职责

- 创建/销毁/查找 Actor
- 延迟销毁队列与帧内刷新
- 遍历更新所有 Actor
- 维护 ID 到 Actor 的索引

## 内存接入

- 创建/销毁/清空时同步 `MemoryService` 的 scene actor 计数与预算监控
