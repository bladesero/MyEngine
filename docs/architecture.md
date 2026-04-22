# MyEngine 架构总览

## 分层结构

当前主干可按职责划分为：

1. **Core**：应用生命周期、主循环、事件、输入、日志、时间、内存服务
2. **Assets**：资源抽象、导入、缓存、统计与预算
3. **Scene**：Actor/Component 组织、序列化、运行时更新
4. **Renderer**：渲染上下文抽象、Pass 组织、后端实现（D3D11/D3D12/Metal）
5. **Game / Editor**：业务层与编辑器层

## 关键运行链路

1. `Application::Run()` 创建窗口并驱动引擎
2. `Engine::Init()` 初始化 `MemoryService`
3. `Engine::RunLoop()` 每帧执行：
   - `MemoryService::FrameBegin()`（重置帧线性内存）
   - `Time::Tick()`
   - 平台事件采集与分发
   - Layer 更新与渲染
4. `Engine::Shutdown()` 输出内存统计并关闭服务

## 内存子系统（当前实现）

- `GeneralHeapAllocator`：通用堆分配，支持统计、可选 tracking、可选 guard
- `LinearAllocator`：帧内线性分配，`Reset()` 回收
- `PoolAllocator<T>`：定长对象池（模板）
- `AllocTracker`：按 tag 统计、泄漏输出、聚合报表
- `MemoryService`：统一入口，提供 Scene actor 计数与预算告警

## Scene 与 AssetManager 首批接入

- Scene：在 Actor 创建/销毁/清空时同步 `MemoryService` 计数，支持软预算告警
- AssetManager：维护 CPU 内存粗估算（总量 + 按类型），支持预算告警与统计输出

## 依赖方向（建议）

- `Core -> (Assets / Scene / Renderer) -> Game`
- `Editor` 依赖 Runtime，不反向侵入 Runtime 核心层

## 文档维护约定

- 新增类时，在 `docs/classes/` 增加同名文档
- 改动模块关系或生命周期流程时，同步更新本文件
