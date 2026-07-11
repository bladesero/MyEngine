# ShaderManager

## 角色

着色器管理器。

## 关键职责

- 统一 shader 加载/编译/缓存
- 为渲染通道提供 shader 查询接口

## Runtime cache policy

`ShaderManager` keeps its in-process GPU shader cache, but uncooked source
shaders first consult `ShaderCacheService`. In Editor mode the service can
compile missing project or engine shaders into the current project's
`Library/<platform>/<uuid>/` and then load the cooked artifact. Player switches
to `RuntimeCookedOnly`, so missing cooked artifacts fail clearly instead of
running a runtime compiler.
