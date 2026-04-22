# GeneralHeapAllocator

## 角色

通用堆分配器，实现 `IAllocator`。

## 关键职责

- 通用分配/释放
- 与 `AllocTracker` 对接做按 tag 统计
- 支持 debug guard（尾哨兵）与 tracking

## 典型用途

- 非帧临时、生命周期不规则的堆对象
