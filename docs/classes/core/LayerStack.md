# LayerStack

## 角色

维护 Layer 顺序容器，支持按栈顺序分发更新/渲染/事件。

## 关键职责

- 存储 Layer 指针集合
- 提供 push/pop（按项目实现）
- 供 `Engine` 以正序更新、逆序事件分发
