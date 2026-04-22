# IAllocator

## 角色

可替换分配器抽象接口。

## 关键职责

- `Allocate(size, alignment, tag, file, line)`
- `Free(ptr, file, line)`
- `Name()` 用于诊断输出

## 当前实现

- `GeneralHeapAllocator` 实现该接口
