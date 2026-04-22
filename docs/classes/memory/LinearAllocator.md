# LinearAllocator

## 角色

线性（bump）分配器，面向帧临时内存。

## 关键职责

- `Init(capacity)` 申请一段连续内存
- `Allocate(size, alignment)` 仅前移指针
- `Reset()` 一次性回收整段

## 特性

- 分配极快
- 不支持单块 `Free`
- 适合渲染临时缓冲、临时容器等场景
