# IRenderContext / IRHIContext

## 角色

渲染后端抽象接口，屏蔽 API 差异（D3D11/D3D12/Metal）。

## 关键职责

- 资源创建（纹理、缓冲、shader）
- 命令提交与帧边界控制
- 与窗口交换链联动
