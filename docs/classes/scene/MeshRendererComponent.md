# MeshRendererComponent

## 角色

网格渲染组件，桥接 Scene 与 Renderer。

## 关键职责

- 持有 `MeshHandle` 与 `MaterialHandle`
- 为渲染阶段提供可绘制资源引用
- 参与序列化/反序列化
