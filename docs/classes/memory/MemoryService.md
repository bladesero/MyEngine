# MemoryService

## 角色

内存子系统统一入口（单例）。

## 关键职责

- 管理 `GeneralHeapAllocator` 与 `LinearAllocator(FrameArena)`
- 提供 `Allocate/Free` 统一调用路径
- 每帧 `FrameBegin()` 重置帧线性内存
- 维护 Scene actor 计数与预算告警

## 统计与输出

- Shutdown 时可输出 allocator 统计与泄漏摘要（按宏开关）
