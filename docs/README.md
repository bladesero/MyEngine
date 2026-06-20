# MyEngine 文档索引

本目录基于当前代码结构整理，包含：

- `architecture.md`：整体架构与模块依赖
- `classes/`：按模块拆分的类文档（每个类单独文件）

## 快速入口

- [架构总览](./architecture.md)
- [类文档索引](./classes/INDEX.md)

## 测试运行

- `xmake run MyEngineTests --help`：查看测试参数说明
- `xmake run MyEngineTests --list`：列出全部注册测试，格式为 `Module::Test`
- `xmake run MyEngineTests --module Project`：只运行 `Project` 模块测试
- `xmake run MyEngineTests --test TestWorkspaceCookAndPublish`：只运行单条测试
