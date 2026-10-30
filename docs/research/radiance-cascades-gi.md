# Radiance Cascades GI 在 MyEngine 的落地研究

> 状态：**研究方案，未实现、未编译、未运行验证**。本分支只新增本文，不修改渲染器、着色器或现有场景。
>
> 调研日期：2026-10-30。结论基于下列固定提交的源码与一手论文；文中预算、工期和验收阈值是建议，不是实测结果。

## 1. 结论先行

建议先做 **有界世界空间、漫反射、单次间接反弹的 3D Radiance Cascades（RC）原型**：

1. **落在现有 Modern Deferred 内，不新增第三套 Renderer。** 复用 GBuffer、RenderGraph、GPU Scene、读回、逐 pass 时间戳和时域提交机制。
2. **首版不依赖硬件光追。** 用保守占据体素、材料/发光数据和软件 DDA 求 radiance interval；RC 数据先放 structured buffer 或 2D 图集，避开现有 RHI 尚无原生 3D texture 描述的问题。
3. **先证明“屏幕外发光体照亮可见表面”和“墙挡得住光”。** 第一个可运行场景只含静态不透明盒体、恒定材质与发光板，不靠 SSGI 或环境光伪装成功。
4. **不要把 ddgitest 直接合入，也不要把 2D 教程改一个 z 轴就当完成。** 旧 DDGI 分支可借鉴数据上传和调试组织，但并非 RC，也没有满足这里需要的材质辐射场与可见性合并。
5. **先保留昂贵的正确性参考模式，再谈实时。** 使用未级联长射线参考以及逐邻居 corrected merge；低成本合并若过不了薄墙/视差测试，宁可保留实验状态，不靠时间滤波掩盖漏光。
6. **2026 年已有面向 3D 的 Split Radiance Cascades。** 把稀疏世界空间 probes + ray splitting 作为后续扩展路线，而不是宣称 3D RC 仍无解；也不把其研究代码当作可直接嵌入本引擎的 SDK。[Split RC 论文](https://arxiv.org/abs/2607.20384v2)

**最小交付定义：** 一个可开关的 `RadianceCascades` 间接漫反射模式、一个自包含测试场景、一组 CPU/GPU 数值读回和 debug views、固定设备上的耗时/显存报告，以及能力不足时明确的回退原因。不是多反弹、全场景、全平台生产级 GI。

## 2. 分支、依赖与证据边界

| 项目 | 本次复核结果 |
|---|---|
| 默认分支 | `master` = `30384548344312c426131ce77a5b31ddc193227b` |
| Linux/Vulkan 基线 | `linux-vulkan-player-editor` = `01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0` |
| Linux PR | [#6](https://github.com/bladesero/MyEngine/pull/6)，复核时 open / draft / 未合并 |
| 本研究分支 | `research/radiance-cascades-gi`，从上述 Linux 当前头创建 |
| 历史实验，只读参考 | `ddgitest` = `5c7695740b866eff7adf1e77962b3ecd60181596` |
| 仓库规则 | 已读根 `AGENTS.md`；该基线树中未发现 `.agents/skills/` |
| 执行条件 | 本次无可用且获授权的构建执行器；通过 GitHub 连接器读源码、创建分支和文档 |

`01ef0aa` 是已报告 Linux 验证的 `e09985c` 的子提交，内容是三处 Metal 源文件的 clang-format 修正。**不能把父提交的运行结果自动记成子提交或本研究的运行结果。** 当前头的 [CI run 41036878964](https://github.com/bladesero/MyEngine/actions/runs/41036878964) 在本次复核时为 completed / action_required；不是“CI 已全绿”。

文档 PR 应暂时指向 `linux-vulkan-player-editor`，这样 diff 只有本文，不夹带整批 Linux 改动。Linux PR 合并后再人工复核祖先关系并调整研究 PR base；**本次不合并、不改 master、不改 Linux 或 ddgitest 分支**。

下文“已有”只表示在指定提交读到实现/API，**不代表相应能力已在所有驱动上运行通过**。所有“拟新增”文件和符号都尚不存在。

## 3. 算法选择：分清四件事

| 路线 | 本项目如何使用 | 不可混淆的边界 |
|---|---|---|
| 原始 RC / penumbra criterion | 距离分段，近处高空间/低角度分辨率，远处低空间/高角度分辨率；保存辐射与透射率并从远到近合并 | 是光传输表示/复用方法，不自动提供场景求交、材质评估或无限反弹 |
| 2D / flatland / screen-space RC | 可做算法直觉和屏幕空间对照，不作为本项目最终世界空间 GI | 作者原稿中 PoE2 路径是适合固定相机的 flatland-like 屏幕空间方案，不能据此保证任意 3D 相机无屏外缺失。[原稿](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex) |
| Holographic RC | 研究方向性复用，不作为此次 3D 性能依据 | 论文明确针对 **2D**；其二维场景耗时不能外推本引擎。[HRC](https://arxiv.org/abs/2505.02041) |
| Split RC | 后续稀疏化候选：世界空间哈希 probes，从可见表面发射射线，按命中距离分配贡献 | 2026-07-22 提交、2026-10-11 修订的 3D 漫反射研究；需要额外的稀疏分配、累积/归一化和稳定性工程。[论文](https://arxiv.org/abs/2607.20384v2) |

一手理论参考是 Osborne / Sannikov 的正式论文，尤其 §2 的 intervals / penumbra criterion 与 Appendix A 的 corrected merge。本文的工程参数不是论文保证；几何过细、空间采样过疏仍会产生漏光。[正式论文](https://doi.org/10.1093/rasti/rzae062)

### 决策矩阵

| 方案 | 初始工程量 | 屏外几何 | 跨后端阻力 | 决策 |
|---|---:|---|---|---|
| 在现有 SSGI 上做屏幕空间 RC | 低到中 | 不完整 | 低 | 仅可选对照，不作为目标交付 |
| 有界稠密 world-space RC + 软件体素遍历 | 中 | 支持纳入体积的完整几何 | 中，主要是 compute/格式/同步 | **首选正确性基线** |
| 直接做硬件 RT RC | 中到高 | 支持 AS 覆盖的几何 | 高，当前 Vulkan/Metal 未接通同等 RT 能力 | 第二阶段替换 trace backend |
| 直接移植 Split RC | 高 | 世界空间缓存，采样受可见表面驱动 | 高，额外原子/哈希/RT 依赖 | 在基线可比较后单独 spike |
| 直接合入 ddgitest | 表面上低、实际高 | 取决于 SDF sidecar | 分支年代/API 差异大 | 不采用 |

## 4. 现有代码的准确落点

以下链接固定到 `01ef0aa`，避免后续分支移动改变证据。

| 现有文件 / 符号 | 已有能力 | 对 RC 的意义 / 限制 |
|---|---|---|
| [Renderer.cpp](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/Renderer.cpp#L1003-L1095)，`Renderer::RenderScene` | Modern 路径依次组织 AS、Depth、兼容 GBuffer、HiZ、GPU GBuffer、clustered lighting、screen-space effects | 可在 GBuffer 之后 resolve RC，并在 tone mapping / TAA 之前按明确契约合成 |
| [ModernDeferredPipeline.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/ModernDeferredPipeline.h#L89-L174) | `Prepare`、`AddClusteredLightingPasses`、`AddScreenSpaceEffects`、`AddTemporalPostProcess`、`CommitTemporalFrame` / `AbortTemporalFrame` | 新增 RC 子系统与 pass 构建接口，不把所有算法继续堆在这个大 cpp 中 |
| [RenderGraph.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/RenderGraph.h)，[RenderGraph.cpp](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/RenderGraph.cpp#L1235-L1326) | compute pass、buffer/texture import、SRV/UAV 声明、依赖验证、UAV barrier、池和预算 | 足够承载 Trace → Merge → Resolve；不是缺一整套 framegraph。跨帧 persistent 资源仍需单独预算和状态管理 |
| [GpuSceneDatabase.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/GpuSceneDatabase.h)，[cpp](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/GpuSceneDatabase.cpp#L365-L473) | geometry arena、object/material/light buffers、脏上传、场景遍历 | 可以接静态 Standard 不透明几何；当前会把蒙皮、自定义 code/graph/parent material 等放在 compatibility 路径，透明也不纳入此对象流 |
| [ModernGBuffer.hlsl](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/EngineContent/Shaders/ModernGBuffer.hlsl) | albedo / normal / material / emissive / velocity，当前与上一帧矩阵 | 足够做表面世界位置重建、材质响应、降采样/重投影；**GBuffer 不是屏外场景数据库** |
| [RHI/RHITypes.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/RHI/RHITypes.h) | structured buffer、RGBA16F/32F、SRV/UAV、2D array、compute capabilities | `RHITextureDesc` 没有 depth / texture dimension 字段；不能直接声明已支持 `Texture3D` |
| [VulkanContext.cpp](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/Backends/Vulkan/VulkanContext.cpp#L3159-L3171) | `CreateTexture` 使用 `VK_IMAGE_TYPE_2D`、depth=1 | 首版 buffer/2D atlas 是最小变更；原生 3D image 将涉及多个后端、上传、view、读回和测试 |
| [IRHIDevice.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/RHI/IRHIDevice.h)、[GpuCommandList.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/RHI/GpuCommandList.h) | compute/dispatch、AS 创建/构建、timestamp API | 多个虚函数默认返回空或 no-op；有接口不等于 backend 已实现，必须检查 capability、资源创建和实测结果 |
| [Vulkan GetCapabilities](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/Backends/Vulkan/VulkanContext.cpp#L3540-L3567)、[Metal GetCapabilities](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/Backends/Metal/MetalContext.mm#L1577-L1602) | 宣告 compute/storage 等能力；没有把 `accelerationStructures` / `inlineRayQueries` 设为 true | 当前跨平台 RC 不能把 ray query 当必选项 |
| [ModernDeferredPipeline.cpp](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/ModernDeferredPipeline.cpp#L875-L1012)、[ModernRayTracing.hlsli](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/EngineContent/Shaders/ModernRayTracing.hlsli#L190-L252) | `EnsureRayTracingScene`、BLAS/TLAS、`ModernRTTrace`、alpha-test candidate | 后续 RT trace backend 可复用；需解除 RC 更新与现有 screen-effect mask 的不恰当耦合，不能只把 RTDiffuse 开关改名字 |
| [ModernScreenSpace.hlsl](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/EngineContent/Shaders/ModernScreenSpace.hlsl#L745-L814) | bilateral upsample、时域/à-trous；`CSEffectsComposite` 将 SSGI 乘材质响应与 `GI_REFERENCE_SCALE=2` | 可借鉴滤波，但 RC 的 E/π 与 alpha 契约不同，**不可直接塞入 SSGI 纹理并沿用 ×2** |
| [ClusteredDeferred.hlsl](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/EngineContent/Shaders/ClusteredDeferred.hlsl#L236-L250)、[PBR_BRDF.hlsli](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/EngineContent/Shaders/PBR_BRDF.hlsli#L60-L84) | 已把环境 SH / local SH volumes 和 specular IBL 加进 HDR；环境 diffuse 参数约定为 E/π | RC 要显式选择/替换 diffuse provider，避免环境、烘焙 SH、SSGI、RTDiffuse 与 RC 重复记账 |
| [ProbeLightingSystem.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/ProbeLightingSystem.h)、[ProbeBakeRenderer.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/ProbeBakeRenderer.h) | 加载/上传既有 reflection / SH lighting assets，GPU probe bake API | 可作现有回退或对照；不是动态 RC cache，不能覆盖其资产格式来偷换含义 |
| [PostProcessComponent.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/PostProcessComponent.h)、[Renderer.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/Renderer.h) | SSGI / RT replacement 配置、序列化入口、debug view enum | 需新增互斥模式与明确 diagnostics；保持老场景默认行为不变 |
| [RenderPath.h](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/src/Runtime/Renderer/RenderPath.h#L41-L50) | Modern 需要 compute、storage、indirect、bindless≥4096 等 | 第一版 renderer 集成受 **Modern 整条路径** 门槛限制，不等于 RC 算法本身需要 bindless。Classic / D3D11 先回退，不宣称已覆盖 |

### 缺口优先级

- **P0：** interval/merge 数学基准、场景占据与辐射源、inside-solid/边界策略、资源布局与读回、exclusive diffuse 合成、能力回退。
- **P1：** 场景/材质/光照版本失效、局部重建、时间滤波、多 viewport 生命周期、预算/性能量化。
- **P2：** 原生 3D texture、稀疏 clipmap、硬件 RT、Split RC、蒙皮/透明/任意 Shader Graph、漫反射多反弹。
- 不需要先重写 RHI、RenderGraph、材质系统或整个 deferred renderer。

## 5. ddgitest 能借什么，不能借什么

只读检查固定到 [ddgitest 提交](https://github.com/bladesero/MyEngine/tree/5c7695740b866eff7adf1e77962b3ecd60181596)：

- `src/Runtime/Renderer/SceneSdfClipmap.h/.cpp`：`SceneSdfClipmapBuilder::Build`，三层，每层 64³ SDF / 16³ probes；依赖 mesh SDF/voxel sidecar。围绕 scene bounds 构建，不是摄像机滚动、增量更新的现代 clipmap。重建有 level × voxel × contributor 的 CPU 遍历。
- `src/Runtime/Renderer/DDGIPass.cpp`：`PrepareGraphResources` / `EnsureBuffers` / `Execute` 展示 metadata、SDF、occupancy、probe SH buffer 的上传与 compute bindings。
- `EngineContent/Shaders/DDGIProbeUpdate.hlsl`：固定 12 个方向、16 次步进上限、`DDGI_SURFACE_ALBEDO=0.45` 与 `DDGI_BOUNCE_SCALE=0.45`，写九个 SH 系数；没有 RC 的距离分段方向层级与远近透射合并。
- 该实验不是材质正确的彩色反弹：着色源主要是固定灰色反射与 directional light，没有本方案需要的通用 emissive/albedo 表面数据流。也没有完成这里要求的动态可见性/距离统计/时域有效性。
- 可借鉴：buffer 线性索引、诊断信息、资源导入思想。需要重新设计：trace 输入、源辐射、合并、probe visibility、版本失效、预算。**本分支不 cherry-pick、不覆盖旧实验、不认领它的运行通过。**

## 6. 最小可运行 3D 设计

### 6.1 范围与场景契约

- 单个固定世界 AABB；例：16 m × 16 m × 16 m，128³ occupancy，voxel edge = 0.125 m。
- 第一场景是生成的静态盒体/平面，材质为不透明、非金属、常量线性 albedo 与 emissive。无需仓库缺失的 sample mesh。
- 首先仅 emissive → diffuse receiver；再加一盏 directional light 的“直接照明表面 → receiver”一次反弹。
- 不支持透明透射、折射、镜面级联、参与介质、蒙皮更新。对未纳入的遮挡物必须显示 unsupported count / coverage warning，不可静默让其穿光。
- 首版固定 AABB 与 probe grid，避免相机移动时把整个世界缓存也拖动。world-space 缓存按 scene/version 归属；表面 resolve history 按 viewport/camera 归属。
- 输入从完整合格 scene extraction 获取，不从相机 frustum / HiZ 可见 draw list 获取。否则“世界空间”仍会丢屏外光源。

### 6.2 几何与源辐射：先确保有东西可追踪

拟新增 `RadianceSceneVolume`，首版用 CPU 保守 triangle-box overlap 生成 occupancy + surface material 记录，上传一次；tiny fixture 可直接由解析盒体生成体素作为参考。不是“每个 voxel center 在三角形里才算占据”，后者会漏掉薄墙。

每个表面体素至少保留 occupancy、surface normal、linear albedo、emissive/source radiance。多个表面落入同一 voxel 时采用稳定规则并标记冲突，避免浮点原子导致不确定结果。首版用 CPU 决定规则，不额外依赖浮点 atomic 或 64-bit atomic。

辐射源定义：

```text
Lo_source = Le + (rho_diffuse / pi) * E_direct
```

- 只含发光与直接照明，**不读取当前帧 RC 已合成 HDR**；不引入隐含反馈反弹。
- directional direct injection 用世界空间 occupancy trace 求 shadow visibility；当前相机 CSM 不能保证离屏体素的完整阴影覆盖。
- 发光面辐射不额外乘一个“到 probe 的 inverse-square”；几何衰减来自立体角积分。point light 的 direct irradiance 计算则应遵守其物理衰减。
- source 数据必须遵循 [rendering-math-conventions.md](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/docs/rendering-math-conventions.md) 与现有材质颜色约定，禁止把显示用 sRGB/HDR tone-mapped 结果当线性辐射。
- SDF 可以后续加速空域跳跃，但必须是保守距离下界；旧 sidecar 不能直接获得安全大步长的保证。首版 DDA 更容易建立不漏 cell 的测试。

### 6.3 Interval ABI

拟用以下逻辑 ABI，实际 GPU packing 单独测试：

```text
RadianceInterval:
    rgb J = 沿该有限线段积累、已按沿途透射加权的入射辐射
    a   T = 剩余透射率，范围 [0,1]，不是 opacity
Aux:
    hitDistance / valid / truncated（独立元数据，不挤占 T）
```

空段为 `(J=0,T=1)`；命中不透明表面为 `(J=Lo_source,T=0)`。本阶段没有半透明材质。unknown/invalid 需要沿仍有透射的路径传播：若近段已知且 T=0，未知远段不应抹掉已知近段贡献；若近段 T>0，远段未知不能伪装成完整无遮挡结果。近段 a 与远段 b 的合成为：

```text
Compose(a,b) = (a.J + a.T * b.J, a.T * b.T)
```

单位测试先证明 identity、opaque blocker、顺序不可交换、在相同连续射线上结合律成立。**该结合律不能证明不同 probe 的空间插值无误差。**

trace 接口建议固定为 `TraceRadianceInterval(origin, direction, tMin, tMax, sceneVersion)`，便于以后替换 voxel DDA、SDF、硬件 RayQuery；RC 层不直接依赖某个后端 AS 类型。

### 6.4 明确一个可算预算的稠密层级

以下是研究用起始参数，非“最优值”：

| 层 i | probe grid | probe spacing | 每 probe 方向数 | 射线区间（r0=2 m） | interval 数 |
|---|---:|---:|---:|---|---:|
| 0 | 32³ | 0.5 m | 24 = 6×2² | [0, 2) m | 786,432 |
| 1 | 16³ | 1 m | 96 = 6×4² | [2, 4) m | 393,216 |
| 2 | 8³ | 2 m | 384 = 6×8² | [4, 8) m | 196,608 |
| 3 | 4³ | 4 m | 1,536 = 6×16² | [8, 16] m | 98,304 |
| 合计 | | | | 最大追踪范围 16 m | **1,474,560** |

在同一有界体积内，`s_i=s0*2^i`、`P_i≈P0/8^i`、`D_i=D0*4^i`，所以每层存储 `S_i=P_i*D_i=S0/2^i`。球面角度有两个维度，不能套二维“方向数只翻倍”的假设。

采用六面嵌套方向网格便于 parent → 4 child 映射。一般分辨率下各 face texel 的立体角不相等，必须预计算/积分 solid-angle weights，检查总和约为 4π。禁止简单平均所有 cubemap texels 后称为能量守恒。

**已知参数冲突，不能隐藏：** 若 base 24 个方向取每个 2×2 face texel 的中心，因对称性这一级各 texel 的 Ω 都是 π/6；但轴向 normal 的中心采样 cosine sum 约为 3.42013，比 π 高 **8.866%**。因此这张表只是初始存储/trace 量级，**不是已满足 §10 的 2% 积分门槛的质量配置**。P0a 必须选择更好的 quadrature / 增加 base 方向数，或按 §6.6 对每个 angular bin 积分 clamped cosine。增加方向数会改变所有 interval 与显存数字，须重算 §8，不能沿用 105 MiB 预算。不能用一个经验性全局缩放代替不同法线方向上的积分检查。

**边界与有限范围：** 16 m 射程小于 16 m 立方体的对角线；到 tMax 仍在体积中表示“远处未知/截断”，不是天空。首版封闭室内测试将 tail 辐射设为零并记录 truncated fraction。只有确实离开“边界之外已知为空”的测试体积，才可使用规定的环境边界。开放世界必须额外建覆盖/clipmap，不能在每级未命中时重复加天空。

### 6.5 Trace 与 Merge：正确性优先

1. 每个 probe / direction 只追踪自己的有限区间，写 raw intervals。
2. 从最远层向最近层构建 merged intervals；每个 pass 读上层 SRV、读本层 raw，写本层另一份 UAV。
3. 不在同一 dispatch 里让不同 workgroups 原地读写相互依赖的 cascade。图中每层有显式依赖与 UAV→SRV 转换；不使用不必要的 `ManualResourceTransitions` 绕过验证。
4. 同方向、八邻居三线性插值是**近似低成本模式**，会有视差、重复遮挡或漏光。不能先把八个 far 结果平均，再用一次近段追踪就声称得到 corrected merge。
5. 参考/高质量模式按 Appendix A 的思路：针对每个上级空间邻居及对应细分方向，重接下级区间起点到该上级 child interval 起点，分别追踪并合并，再按空间与立体角权重归一。三维有八邻居，论文指出除最远层外，对应待追踪射线数量增至约八倍；**这不是 GPU 耗时必然八倍的保证**。这部分额外 trace **没有包含在上表的 raw interval 数中**。[Appendix A](https://doi.org/10.1093/rasti/rzae062)
6. 重接区间的几何端点、方向和长度要在 CPU 参考实现中显式构造，不能只用一个“更大 bias”代替。保留每段可视化。
7. 最终表面 → probe 的短段可见性、inside-solid probe 拒绝、normal bias 是**另外一层**保护，不替代 corrected merge。spacing 不满足 penumbra 条件仍可能失败；通过误差和漏光测试决定是否允许低成本模式。
8. 每层记录 invalid / inside / truncated / max-step-hit 计数。DDA 达到步数上限返回 unknown，不返回无遮挡。

第一次 GPU 调试采用更小的 8³ base probes、2–3 层和 32³ voxels；先与 CPU 全方向长射线参考在预定误差门槛内一致，再扩大到表格配置。固定方向单帧就应能成像；时间滤波不是算法成立的前提。

### 6.6 表面 Resolve 与能量契约

对可见表面位置 x、法线 n，得到：

```text
E(x,n)   = sum_k L(x,omega_k) * max(dot(n,omega_k),0) * deltaOmega_k
B(x,n)   = E(x,n) / pi
Ldiffuse = kD * linearAlbedo * B(x,n)
```

上式是中心方向 quadrature 近似；对 §6.4 的低角度网格，另一个可验证的选择是对每个 angular bin 计算 `w_k(n)=integral_bin max(dot(n,omega),0) dOmega`，使用 `E≈sum_k L_k*w_k(n)`，**此时不能再乘 deltaOmega_k**。这能消除常量 radiance 场的中心 cosine 积分偏差，但不消除 bin 内实际辐射变化/小光源 aliasing；权重实现、成本和误差都要独立测试。

- `RCResolve.rgb = B = E/pi`，不包含接收表面的 albedo，不包含 exposure/gamma，不提前乘 art intensity。
- 在 x 周围采样 probe 前做 inside-solid / surface-to-probe visibility 检查，拒绝穿墙候选；有效权重重新归一。无可靠候选时返回 invalid + 约定 fallback，而不是将陈旧亮值继续保留。
- 首版直接对 directional data 做积分，SH 压缩作为可选优化；若以后转 SH，先明确存的是 radiance SH 还是 cosine-convolved E/π，避免再次卷积。
- debug/滤波需要区分 `confidence` 与 luminance second moment：现有 SSGI/RTDiffuse 的 alpha 是二阶矩，RC interval alpha 是 T，resolve alpha 可是 confidence。三者不能共用 ABI。
- 可复用 temporal rejection / bilateral / à-trous 的结构，但要新建 RC 常量与资源契约。已有 `GI_REFERENCE_SCALE=2` 不适用于这个归一化结果。

### 6.7 与现有光照互斥，防止重复加亮

拟增加 `IndirectDiffuseMode { Existing, RadianceCascades }`（后续可细分 Existing 中 SSGI / RTDiffuse / probes），默认 Existing，旧场景不变。

测试模式中：

- 保留 direct lighting、emissive、specular environment / SSR 的独立路径。
- RC 覆盖区禁用既有 SSGI/RTDiffuse，禁用同一接收点的 local baked SH diffuse；不要让 RC 与它们同时作为完整 diffuse GI 相加。
- 首个封闭箱场景环境 diffuse 设零，保证观察到的亮度来自 RC。
- 后续若把天空纳入 RC，则由 RC 替换被覆盖区的 environment diffuse；coverage 之外回退原 environment/baked provider。使用明确的有效性/边界过渡，不把两套完整结果相加。
- 如果另选“只追加局部反弹，保留 environment diffuse”模式，必须把环境排除出 RC 求解域并作为独立、命名清晰的近似模式测试；不默认开启。
- 合成点在 linear HDR、TAA/tone mapping 之前；曝光改变不应重建世界 radiance cache。

### 6.8 生命周期与时域更新

- P0 每次光照/几何变动全量重算，不引入 atlas history；先排除 temporal 掩盖的错误。
- 版本至少包含 scene identity/generation、几何 transform、mesh revision、material/emissive、light、volume origin/extent、probe layout、trace 参数与 shader version。不能仅 hash mesh 路径。
- 光源开关和遮挡物删除需要使旧亮值失效；dirty bounds 包含旧/新 AABB。普通 shadow/camera history reset 不是 world cache invalidation 的替代。
- P1 可在稳定 world cache 上做分摊更新与 surface resolve history；若分四帧更新必须公开最坏响应延迟，并优先处理脏区。
- 与现有 `CommitTemporalFrame` / `AbortTemporalFrame` 一致：图执行失败不能推进历史 ping、场景版本或“已更新”标记；GPU 失败后需使可能部分写入的 RC 缓存失效并全量重建，不能仅回滚 CPU 标志。
- viewport resize/camera cut 只清对应表面 history；scene change / volume rebase 清世界缓存。Editor Scene / Game View 不共享 camera history。
- 资源释放遵守在途提交生命周期；不要在热重载或体积 resize 时提前销毁旧 descriptors/buffers。Vulkan 不复用仍被排队 command 引用的可变 bind group。

## 7. 拟议 RenderGraph 与文件拆分

```text
Scene extraction + dirty version
        |
RC scene volume upload / direct-source injection
        |
RCTrace[0..N-1]                 GBuffer + depth + velocity
        |                               |
RCMerge[N-1 -> 0] -----------------------+
        |
RCResolve (E/pi + validity; half-res optional)
        |
RC temporal/filter (P1; own ABI)
        |
Direct/emissive/specular + selected indirect diffuse
        |
Existing TAA -> tone mapping -> ScreenUI
```

| 拟新增/修改 | 责任 | 首版刻意不做 |
|---|---|---|
| 新增 `src/Runtime/Renderer/RadianceCascadeLayout.h` | checked sizes、index mapping、direction hierarchy、interval ranges、packing ABI | 不依赖 Editor |
| 新增 `RadianceSceneVolume.h/.cpp` | 合格场景采集、保守体素化、源辐射、version/dirty 数据 | 不直接复制旧 SDF builder |
| 新增 `RadianceCascades.h/.cpp` | persistent 资源、capability/fallback、Trace/Merge/Resolve pass 构建、stats、commit/abort | 不成为新 renderer backend |
| 新增 `EngineContent/Shaders/RadianceCascadeCommon.hlsli`、`RadianceCascadeTrace.hlsl`、`RadianceCascadeMerge.hlsl`、`RadianceCascadeResolve.hlsl` 与对应 `.shader` | 显式入口、统一数学与布局、debug modes | 不与现有 SSGI macro 强行合并 |
| 修改 `EngineShaderCatalog.h` / 相关 prewarm 路径 | 加载新 shader descriptor、热重载版本处理 | 不手写平台特定加载旁路 |
| 修改 `ModernDeferredPipeline.*`、`Renderer.cpp` | 接入子系统、GBuffer 输入、互斥合成与 diagnostics | 不改 Forward / Classic 默认行为 |
| 修改 `ClusteredDeferred.hlsl` 或抽出的 diffuse composition helper | 拆清 environment diffuse 与 specular，以替换 diffuse provider | 不再次乘 π 或经验 ×2 |
| 后续修改 `PostProcessComponent.*`、Editor inspector/debug menu | 模式持久化、参数边界、fallback reason、旧场景默认兼容 | 第一检查点不先做大量 UI |
| 新增 `tests/RadianceCascadesTests.cpp`、自包含 fixture 工具/场景 | 数学、索引、CPU oracle、GPU readback、回归 | 不依赖缺失的项目示例资源 |

源码由现有 `myengine.module` 递归发现；不新增 Runtime 内部库目标，不手写 source array。若导出 Runtime API 发生变化，使用 `MYENGINE_RUNTIME_API`，并复核 ABI baseline / RuntimeLinkProbe。研究文档本身不改变模块 DAG，因此本次无需改 `design.md`；真正实现引入新结构时再更新。

## 8. 预算：明确假设，不制造“实时已达标”

### 8.1 存储

上表 1,474,560 intervals：

| 存储方式 | 一套 | raw + merged 两套 | 再加整套 history |
|---|---:|---:|---:|
| RGBA16F texture，或显式 `uint2` half packing，8 B/interval | 11.25 MiB | 22.5 MiB | 33.75 MiB |
| `StructuredBuffer<float4>`，16 B/interval | 22.5 MiB | 45 MiB | 67.5 MiB |

**不要把 float4 structured buffer 当成 8 字节。** FP16 要实测动态范围、溢出与 dark radiance 精度；CPU oracle / correctness mode 保留 FP32。

一个可核算的例子（不含现有 renderer 自身开销）：

- 128³ voxels，每 voxel：occupancy uint 4 B + source float4 16 B + packed normal 4 B + packed albedo/material 4 B = **56 MiB**。
- packed raw + merged = **22.5 MiB**。
- 1080p 的半分辨率为 960×540；RGBA16F 每张约 **3.96 MiB**。保守预留六张（resolve、history、filter/debug 工作面）约 **23.73 MiB**。
- 独立 R32 validity/work mask 半分辨率约 **1.98 MiB**；另预留 metadata/flags 1 MiB。
- 上述 persistent 约 **105.2 MiB**。若完整 staging 再保留一个 56 MiB volume，峰值约 **161.2 MiB**；再留整套 packed atlas history 则约 **172.5 MiB**。
- 这些只是逻辑字节数：不含 GPU allocation alignment、driver descriptors、读回队列、额外 hit-distance atlas、corrected-merge 临时存储、场景 mesh、现有 GBuffer。**hit-distance 若每 interval R32 再需 5.625 MiB/套**，必须在最终台账计入。

建议该起始 profile 的新增 persistent 目标 ≤128 MiB、已计入 staging/额外统计的峰值硬预算 ≤192 MiB；若实际超出就缩配置或减少副本，不静默占满 RenderGraph 全局池。FP32 验证 profile 可单独提高预算但不称 production preset。

这是“同体积每级 probes 数量缩小”的预算，**不适用于每级都固定 32³ 的扩大范围 clipmap，也不适用于稀疏 Split RC**。persistent imported buffers 未必由 transient budget 覆盖，RC 必须自行报告总 bytes 与峰值。

### 8.2 工作量

- 全更新至少 1.47M raw intervals；若每段平均 16 / 32 个 voxel traversal，约 23.6M / 47.2M cell visits，尚未计入 shadow injection、merge、resolve、corrected rays。
- corrected merge 的三维八邻居追踪可能成为主成本；不能把“raw sample 数递减”推导成“每级固定时间”或“1080p 肯定 60 FPS”。
- 先分别记录 voxel rebuild CPU ms、upload bytes、injection / trace / merge / resolve / filter GPU ms、每级 cells/ray、truncation 和有效 probes 数。
- 物理 GPU 型号/驱动/分辨率/场景未确定，**现在不能给可信 FPS**。建议 P0 选择并记录一台目标设备；P1 再以 1080p、新增 GI p95 ≤4 ms 作为讨论用目标，不达标时分析瓶颈，而不是删正确性门槛。
- llvmpipe 适合有限的功能诊断，不作为 GPU 性能依据。官方 2D 论文性能、别的引擎视频和本仓库旧 smoke 都不能代替本方案实测。
- 所有 CPU/GPU buffer size 运算用 checked 64-bit；最终创建前检查 `RHIBufferDesc::size` 的 32-bit 范围、纹理尺寸上限和 dispatch 限制。

## 9. 分阶段落地与退出门槛

估时以熟悉本仓库的一名图形工程师、可用物理 GPU、无需新增美术资产为前提；是排期建议，不是承诺。

| 阶段 | 预计 | 具体产物 | 才能进入下一阶段的证据 |
|---|---:|---|---|
| P0a 数学/布局 | 1–2 人日 | interval CPU reference、solid-angle weights、packing/index tests、tiny scene | 合并、边界、索引无误；无溢出；常量球面场积分通过 |
| P0b 世界 trace | 2–3 人日 | 保守体素输入、DDA、GPU interval readback、inside/truncated debug | GPU 与同一体素 CPU oracle 一致；屏外 geometry 未被裁掉；薄墙 occupancy 正确 |
| P0c 级联合并 | 2–4 人日 | far→near passes、corrected reference mode、direct long-ray comparison | 单帧发光/遮挡成立，漏光达标，GPU validation 无新增错误 |
| P0d Renderer 集成 | 2–3 人日 | RCResolve E/π、互斥 diffuse provider、mode/fallback、固定场景 | 开关能回到原画面；无双倍环境/SSGI；曝光无关；Editor/Player 都可观察 |
| P1 可用性/性能 | 3–5 人日 | 版本失效、轻量时域、预算、配置/调试、至少两后端验证 | 动态开关响应、相机/viewport 历史安全、实测时间/显存和失败回退 |
| P2 单独评审 | 不纳入首版 | HW RT trace、稀疏体积/Split RC、动态蒙皮、多反弹 | 用相同场景/质量目标证明优化收益，不破坏 oracle/回退 |

合计首个可比较 P0/P1 约 **10–17 人日**；若必须修后端能力、shader compiler 或资产问题则重新估时。第一个提交应是 P0a + tiny fixture，不是先堆全套 UI 和开放世界 clipmap。

**停止/转向条件：**

- 薄墙或 corrected merge 在 tiny scene 都不成立：退回几何/区间几何端点/权重，不加 temporal。
- 在确定质量下 dense working set/trace 开销无法满足目标：保留其作为 oracle，单独评估稀疏化或硬件 RT，不凭感觉降低到有明显漏光的配置。
- 当前硬件不支持 Modern：明确回退现有路径；若用户要求 Classic/D3D11 覆盖，另立独立 compute integration 范围。
- 未支持材质/蒙皮占主要场景：先补 coverage，不展示只照亮少量 Standard geometry 的画面当完整 GI。

## 10. 验收矩阵：结果必须可重复

下列阈值是**拟定工程门槛**；首次执行应保存固定场景、相机、曝光、版本和数值基准。不要仅凭截图“看起来变亮”。

### 10.1 自动数学与布局

- `Compose(identity,x)=x`；opaque near 使 far contribution 为零；同一连续射线分段与整段结果 FP32 误差 ≤1e-5（简单解析 fixture）。
- 球面权重总和与 4π 的相对误差 ≤1e-4；对数个法线的 cosine integral 与 π 误差 ≤2%（低分辨率初始角网格不达标则提高/改善 quadrature）。
- 恒定 radiance L 下 B≈L；纯 Lambert 归一化 fixture 固定 kD=1，此时 receiver 输出≈rho×L；实际 PBR 接入另按引擎 Fresnel/diffuse split 比较。发光翻倍、结果线性翻倍，无额外 inverse-square 或 π。
- 每层首尾 index、parent/child、face seam、正负轴、interval 边界去重、NaN/Inf/zero direction、overflow 都有测试。
- FP16 packing round trip 记录误差与溢出范围；不能用 1e-5 阈值要求全部 FP16 HDR 数据。

### 10.2 GPU 与几何 oracle

- 32³/64³ tiny voxel scene 的 GPU DDA 命中/未命中、首个 hit cell、T 与 CPU voxel oracle 一致；hit distance 误差按 cell epsilon 定义。
- 另用 triangle/解析盒体 long-ray reference 区分 **voxelization 误差** 与 **RC merge 误差**。不要只对相同错误的两个体素实现比较。
- 固定无噪声 diffuse 场景与高质量 long-ray reference 比较 B；先规定排除背景/低信号分母的 ROI，建议相对 L1 ≤10%，同时单列最大/95 分位误差，不只报平均。
- 黑暗隔间：被完全遮挡 ROI 的平均 B 不超过无遮挡对照 ROI 的 1%；分别测 0.5、1、2 voxel 厚墙、斜墙和贴墙 probes。半 voxel 失败必须作为体素分辨率限制披露，不允许用平均指标掩盖。
- 单独渲染各层 raw J/T、merged J/T、probe occupancy、source volume、short-segment visibility、RC-only E/π、truncated/invalid、diffuse provider 与历史年龄。

### 10.3 场景与交互

1. **关环境/关 SSGI 的黑箱 + 发光板**：不开 RC 时间接区域黑，开 RC 后有方向正确的光；关闭发光后清除旧光。
2. **彩色 Lambert 受直射后反弹**：红/绿墙让邻近白墙出现对应色偏；不依赖 fixed-grey bounce 常量。
3. **屏外光源**：相机转开/发光板离屏，可见接收面的照明保持；与 SSGI 对照明确区别。
4. **薄墙、门缝、L 形拐角**：漏光指标达标；关闭 corrected merge 的差异可量化。
5. **移动/删除遮挡物与发光体**：P0 全更新下一成功帧生效；P1 分摊更新记录最大帧延迟（建议 ≤4 帧），没有永久残留。
6. **camera cut、FOV、resize、Scene/Game View 切换、Play/Stop、场景切换、shader hot reload、失败后重试**：无跨相机历史污染、越界、陈旧缓存。
7. **金属/透明/蒙皮/custom shader**：未支持的覆盖有明确诊断；纯金属不获得额外 diffuse lobe。
8. **天空/边界/远距**：tMax 截断不等于 sky；环境只积分一次；体积边界没有突然爆亮。

### 10.4 后端与回归

参考现有 [RendererTests.cpp](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/tests/RendererTests.cpp) 的 `TestRenderGraphComputePassTypeAndUavBarriers`、`TestRenderGraphComputeBufferDependencies`、`TestModernTemporalHistoryCommitAndAbort`、Slang compile contracts 添加测试；字符串/source-contract tests 不能替代 GPU readback。

真正实现后至少执行：

```text
xmake f -m debug
xmake build MyEngine.Architecture
xmake build MyEngineRuntimeLinkProbe
xmake run MyEngineTests
xmake run MyEngineRuntimeLinkProbe
```

并按 `AGENTS.md`、`docs/linux-vulkan.md` 和平台脚本执行实际 Editor/Player/着色器/读回测试。记录 Vulkan validation 与 D3D12 debug layer 错误；最终声称支持的 Metal 路径必须独立编译并实机验证。Windows ABI 导出改变时更新对应有序 baseline。检查 shader cooking / Content.pak，而不只测试 Editor 的 loose files。

预算回归包含开关 RC 的 GPU p50/p95、CPU rebuild 峰值、cold start、steady state、新增 persistent/peak bytes；长运行至少覆盖场景热重载和多次 viewport 重建。GPU timestamp 不可用时标为 unavailable，不拿 CPU frame time 冒充 GPU pass time。

## 11. 后续方向与主要风险

| 风险 | 具体应对 |
|---|---|
| probe / voxel 太稀导致漏光 | conservative geometry、corrected merge、短段可见性、薄墙量化；不能只调 bias |
| direction hierarchy 错配 / cubemap seam | 明确 parent-child 与立体角测试；参考模式画出 ray endpoints |
| 缺少完整屏外材质/几何 | 建 coverage 清单与 unsupported count；先限制静态 Standard，避免静默跳过 |
| 世界缓存与多个 viewport 交叉污染 | scene/version 与 viewport history 分离；成功执行后 commit |
| DDA / merge 成本超预算 | 每级统计，先减场景/验证配置；随后 occupancy hierarchy、dirty tiles、packed data，再比较稀疏/RT |
| 纹理格式/Slang 后端行为不同 | 创建前检测、实际读回、绑定反射验证；不能仅看 bool capability |
| 多反弹能量膨胀 | 首版不做反馈；以后单独 source/history ping-pong、rho≤1、收敛/稳定性测试，不读正在写的缓存 |
| “GI”重复记账 | 只选择一个覆盖区 diffuse provider，保留独立 specular；统一 E/π 和 alpha ABI |
| 覆盖边界假天空 | unknown/truncated 与真正 domain exit 分开；必要时扩大体积或回退 |
| 旧实验移植引入不兼容 | 只借思想，逐文件重新适配当前 RHI/材质/ABI，不批量 cherry-pick |

### Split RC / RT 下一步如何评估

- 保持 `TraceRadianceInterval` 与 cache/layout 解耦，先用相同 tiny/reference 场景比较软件 trace 与当前可用的 D3D12 RayQuery 路径；Vulkan/Metal RT 是独立 backend 工作项。
- 本次 Split RC 证据范围是论文摘要、版本记录及作者参考代码；PDF 未完整抓取/通读，也未运行作者 demo，**不声称复现其全部方法或性能**。
- Split RC 的原作者 [Chandra 实现](https://github.com/entropylost/chandra) 可作算法与实验参考；其 Rust/Keter、本地依赖、浮点原子、64-bit keys 与 RT 资源要求不等价于本仓库 C++/HLSL RHI 能力。移植前固定上游 SHA、检查许可证和依赖，不复制整套引擎。
- 稀疏版本必须量化 hash occupancy/load、碰撞/溢出、probe eviction、相机新显露区域、累积归一化、原子竞争和最坏显存。不能只比较平均 FPS。
- 第一阶段保留下来的 dense/long-ray oracle 正是后续评估 sparse quality 的基线；不因选择 Split RC 就丢弃验收场景。

## 12. 本研究已完成与尚未完成

**已完成：**

- 复核 master / Linux PR / ddgitest 的当前引用，创建独立研究分支；
- 阅读当前渲染器、RHI、RenderGraph、shader、GPU Scene、probe 系统与相关测试/CI；
- 区分 2D、原始 RC、3D dense baseline 与 2026 Split RC；
- 给出可执行的阶段设计、代码落点、数学/存储契约、预算和验收矩阵；
- 只提交研究文档，无 renderer/shader/scene 改动。

**未完成 / 不作声称：**

- 没有实现 RC，没有运行新 shader，没有图像/数值 readback；
- 没有编译或执行本研究分支，没有物理 GPU 性能结果；
- 没有确认目标机器、跨驱动支持、生产场景覆盖或所有 CI 通过；
- 没有合并任何 PR，也没有更改已有分支或 workflow。

当前 CI 文件 [.github/workflows/windows-ci.yml](https://github.com/bladesero/MyEngine/blob/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0/.github/workflows/windows-ci.yml) 没有 Linux lane，debug-smoke 仍被禁用，实 GPU lane 为手动/master-only。因此即使文档 PR 的常规构建完成，也不能当作 RC 运行验收。

## 参考资料（均为一手来源）

1. Alexander Sannikov，原始 RC 原稿：[源码](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/RadianceCascades.tex)、[PDF](https://github.com/Raikiri/RadianceCascadesPaper/blob/main/out_latexmk2/RadianceCascades.pdf)。作者 README 说明该稿未投稿到其模板所对应的期刊，本文不称其为已发表 JCGT 论文。
2. C. M. J. Osborne / A. Sannikov，Radiance cascades: a novel high-resolution formal solution for multidimensional non-LTE radiative transfer，RASTI：[DOI / 正文与 Appendix A](https://doi.org/10.1093/rasti/rzae062)。
3. Rouli Freeman / Alexander Sannikov / Adrian Margel，Holographic Radiance Cascades for **2D** Global Illumination：[arXiv](https://arxiv.org/abs/2505.02041)。
4. Rouli Freeman / Alexander Sannikov / Joe Pitt-Francis，Split Radiance Cascades: Real-Time Global Illumination via Sparse Radiance Probes：[2026 v2](https://arxiv.org/abs/2607.20384v2)、[作者参考实现 Chandra](https://github.com/entropylost/chandra)。
5. 仓库固定源码：[本研究基线 01ef0aa](https://github.com/bladesero/MyEngine/tree/01ef0aa31b3739964b7c6011f4bc66dc7dbca2a0)、[只读 ddgitest 5c76957](https://github.com/bladesero/MyEngine/tree/5c7695740b866eff7adf1e77962b3ecd60181596)。

> 下一步最小动作：按 P0a 先实现 CPU interval / layout tests 和一个无外部资产的发光盒体 fixture；通过后再创建 GPU trace pass。本文是实施依据，不是“GI 已落地”的验收报告。
