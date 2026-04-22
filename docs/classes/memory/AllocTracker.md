# AllocTracker

## 角色

分配统计与泄漏诊断组件。

## 关键职责

- 记录 live allocations（可选 tracking）
- 聚合按 `AllocTag` 的 bytes/count/calls
- 输出泄漏 TopN 与汇总统计

## 相关开关

- `MYENGINE_MEM_STATS`
- `MYENGINE_MEM_TRACKING`
- `MYENGINE_MEM_GUARD`（由分配器配合）
