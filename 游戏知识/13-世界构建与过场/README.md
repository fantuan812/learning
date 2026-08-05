# 13 世界构建与过场

> 适用范围：UE 客户端 · 大世界构建与影视演出
> 版本基准：UE 5.8（关键 API 已对照本机引擎源码逐条验证）

## 分类简介

「13-世界构建与过场」是知识库中"大世界内容创作 + 影视级演出"方向的分类。它承接 01-引擎基础（对象模型、Actor/Component 生命周期、关卡系统）与 12-引擎源码分析（资源加载等底层机制），聚焦引擎级三大系统：

- **Landscape 地形系统**：从地形创建与雕刻、高度图数据组织、材质层（Layer Blend）权重混合、Landscape Spline 道路河流，到运行时高度查询/修改、LOD 与烘焙性能控制，以及 UE5 大世界下与 World Partition 的深度集成；
- **Foliage 植被与实例化渲染**：Foliage 模式绘制、`AInstancedFoliageActor` 与 `FFoliageInfo` 的数据组织、ISM/HISM（`UHierarchicalInstancedStaticMeshComponent`）实例化渲染原理、植被 LOD 与剔除策略、运行时动态生成植被，以及与 Mass 群集方案的选型对比；
- **过场与影视 Sequencer**：LevelSequence/MovieScene 资产架构（轨道/片段/键帧/绑定）、Possessable/Spawnable 绑定机制、CineCamera 电影镜头参数、子序列与模板序列工作流、MovieSceneCapture 电影渲染输出，以及运行时播放控制（`UMovieSceneSequencePlayer`）。

与前几个分类"讲机制"的定位不同，本分类偏"讲系统 + 讲创作流程"：既解释每个系统在引擎中的数据结构与工作管线（对照 UE 5.8 本机源码），也给出可直接上手的编辑器操作步骤与 C++/蓝图示例。

## 文件列表

| 文件 | 一句话简介 |
| --- | --- |
| [01-Landscape地形系统.md](./01-Landscape地形系统.md) | Landscape 地形创建与雕刻、高度图编码（LandscapeDataAccess）、材质 Layer Blend 权重混合、Landscape Spline、运行时高度查询/修改、LOD 与烘焙、World Partition 配合。 |
| [02-植被Foliage与实例化渲染.md](./02-植被Foliage与实例化渲染.md) | Foliage 绘制与数据流、AInstancedFoliageActor / FFoliageInfo、ISM 与 HISM 实例化渲染原理、LOD 与剔除、运行时生成植被、与 Mass 群集对比。 |
| [03-过场与影视Sequencer.md](./03-过场与影视Sequencer.md) | Sequencer 资产架构（轨道/片段/键帧/绑定）、Possessable/Spawnable、CineCamera 电影镜头、子序列与模板序列、MovieSceneCapture 渲染、运行时播放控制。 |

## 学习顺序建议

1. **先读 01-Landscape**：地形是开放世界的"地基"，植被与过场都依赖"世界里有东西可放、有场景可拍"；
2. **再读 02-Foliage**：植被是地形之上数量最大的内容层，理解实例化渲染原理后才能正确评估性能与选型；
3. **最后读 03-Sequencer**：过场是"消费"前面所有场景资产的演出层，需要理解绑定与播放架构才能做运行时控制。

速查路径：

- 想快速搭一个可跑的大世界原型：01 → 02，03 的渲染细节可后补；
- 正在做过场/演出系统：直接精读 03，遇到"镜头里没东西"回 01/02 查地形与植被；
- 正在做运行时世界修改（挖坑/铺路/种树）：精读 01 的运行时章节 + 02 的运行时生成章节；
- 正在做大世界性能优化：01 的 LOD/烘焙章节 + 02 的剔除章节 + 03 的渲染输出章节。

## 与 01-引擎基础 08/09（关卡流送 / WorldPartition）的关系

本分类的地形与植被大量依赖 UE5 大世界特性（World Partition、ISM 分区、HLOD），这些特性的底层加载/分区机制规划在 **01-引擎基础 08-关卡流送与加载、09-WorldPartition 大世界分区** 两篇中讲解：前者讲 Level Streaming 的加载规则与生命周期，后者讲 WP 的数据分区、Streaming Cell 加载与 HLOD 生成。两边是"机制层"与"应用层"的分工：

| 层面 | 01-引擎基础 08/09（机制层） | 13 本分类（应用层） |
| --- | --- | --- |
| 关注点 | 关卡怎么被加载/卸载、数据怎么分区、HLOD 怎么生成 | 地形与植被怎么做出来、怎么绘制、怎么运行时修改 |
| 视角 | 引擎流程与对象生命周期 | 创作工具与运行时 API |
| 交叉点 | WP 网格单元、StreamingSource、HLOD 代理、LWC | Landscape 的 `APartitionActor` 继承、`AISMPartitionActor` 实例分区、Landscape HLOD、Foliage 按 Cell 加载 |

典型交叉示例：

- 09 篇解释 `UWorldPartition` 如何把关卡拆成 Streaming Cell；01 篇（本分类）说明为什么 `ALandscapeProxy` 在 UE 5.8 中直接继承 `APartitionActor`（源码 `Runtime\Landscape\Classes\LandscapeProxy.h`），以及地形如何以整体 Actor 横跨多个 Cell 而不产生接缝；
- 08/09 篇讲 HLOD 的生成管线；02 篇讲 Foliage 实例如何在分区 Actor（`AISMPartitionActor`）间按 Cell 组织与加载；
- 阅读时对"什么时候加载/卸载"有疑问，先回 01-引擎基础 08/09 篇；对"这块地形数据长什么样、怎么改"有疑问，留在本分类。

底层对照可再延伸至 [12-引擎源码分析](../12-引擎源码分析/README.md) 的 13-资源加载与异步加载源码篇；Landscape 材质与 Foliage 光照表现与 [02-渲染与图形](../02-渲染与图形/README.md) 的 Nanite/Lumen 篇直接相关。

## 前置知识

- 01-引擎基础：UObject / Actor / Component 生命周期（本分类大量涉及组件与 Actor 类型）；
- 基本关卡编辑经验（放置 Actor、材质实例、蓝图）；
- 03-游戏玩法编程 的蓝图与 C++ 协作篇（示例代码阅读前提）。

## 版本与源码基准

- 引擎版本：UE 5.8（本机安装目录 `C:\Program Files\Epic Games\UE_5.8`）；
- 关键源码对照文件（路径相对 `Engine\Source\Runtime`）：
  - Landscape：`Landscape\Classes\Landscape.h`、`Landscape\Classes\LandscapeProxy.h`、`Landscape\Classes\LandscapeComponent.h`、`Landscape\Public\LandscapeDataAccess.h`、`Landscape\Classes\LandscapeSplinesComponent.h`；
  - Foliage：`Foliage\Public\InstancedFoliage.h`、`Foliage\Public\InstancedFoliageActor.h`、`Foliage\Public\FoliageType.h`、`Foliage\Public\FoliageStatistics.h`；`Engine\Classes\Components\HierarchicalInstancedStaticMeshComponent.h`；
  - Sequencer：`LevelSequence\Public\LevelSequence.h`、`LevelSequence\Public\LevelSequencePlayer.h`、`LevelSequence\Public\LevelSequenceActor.h`、`LevelSequence\Public\LevelSequenceDirector.h`；`MovieScene\Public\MovieSceneSequence.h`、`MovieScene\Public\MovieScene.h`、`MovieScene\Public\MovieSceneSection.h`、`MovieScene\Public\MovieSceneSequencePlayer.h`；`CinematicCamera\Public\CineCameraActor.h`、`CinematicCamera\Public\CineCameraComponent.h`；`MovieSceneCapture\Public\MovieSceneCapture.h`、`MovieSceneCapture\Public\MovieSceneCaptureSettings.h`。

文中标注"源码验证"的 API 均来自上述文件，行号以本机 5.8 源码为准；不同小版本行号可能漂移，请以类名/函数名为准。

## 阅读约定

- 代码以 C++ 为主、编辑器操作为辅；所有运行时示例建议放入独立测试工程验证；
- 编辑器专属 API（带 `WITH_EDITOR` 守卫）与运行时 API 会明确区分；
- 官方未公开或版本敏感的运行时修改方案会明确标注风险等级；
- "本机源码"指 `C:\Program Files\Epic Games\UE_5.8`。
