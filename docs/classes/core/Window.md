# IWindow / SDLWindow

## IWindow

窗口与平台交互抽象接口。

### 关键职责

- 初始化/关闭窗口与图形上下文绑定
- 提供尺寸、句柄、缓冲交换等能力

## SDLWindow

`IWindow` 的 SDL3 实现。

### 关键职责

- 调用 SDL API 管理 OS 窗口
- 为渲染后端提供窗口宿主对象
