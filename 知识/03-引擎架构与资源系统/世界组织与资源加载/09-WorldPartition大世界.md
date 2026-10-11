---
type: Concept
title: "09 World Partition 大世界"
status: stable
verified: []
maturity: L2
updated: 2026-10-11
sources:
  - id: wp-guide-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine?application_version=5.6
  - id: wp-data-layers-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---data-layers-in-unreal-engine?application_version=5.6
  - id: wp-hlod-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine?application_version=5.6
  - id: wp-ofpa-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/one-file-per-actor-in-unreal-engine?application_version=5.6
  - id: wp-level-instancing-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/level-instancing-in-unreal-engine?application_version=5.6
  - id: wp-source-api-55
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/Components/UWorldPartitionStreamingSourceCo-?application_version=5.5
  - id: wp-data-layer-api-58
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UDataLayerManager/SetDataLayerRuntimeState
  - id: wp-release-50
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5.0-release-notes?application_version=5.0
  - id: actor-lifecycle-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-actor-lifecycle?application_version=5.6
---
# 09 World Partition 大世界

> 知识成熟度：L2。本文静态核对 Epic 公开文档；场景、蓝图接线和结果均为教学设计与纸面预期（PAPER_EXPECTED）。UE、PIE、蓝图/C++ 编译、资产配置、构建、Cook、GPU、网络及性能实验全部 NOT_RUN。
> 版本基准：UE 5.6 官方操作文档，核对日期 2026-10-10。组件 API 的 5.5 页面与 Data Layer Manager 的 5.8 页面只用于标明接口差异，不冒充 5.6 编译证据。未访问本机引擎源码，也未重新核验旧稿声称的 UE 5.8.0、CL 55116800 环境。
> 最后更新：2026-10-11

## 一、先明确问题：一张世界，哪些内容现在值得加载？

设想地图上有相距 1.2 km 的 A、B 两处建筑。玩家在 A 时，通常需要 A 的真实 Actor、碰撞和交互；B 可以暂时不进入运行世界，或只显示远景代理。把全部内容永久加载会扩大内存和更新成本；手工切成很多子关卡则增加区域边界与依赖维护。

World Partition 把“我关心世界的哪里”与“哪些 Actor 应一起流送”连接起来：编辑时在一个持久世界中组织内容，生成运行流送数据时形成 Cell，运行时由 Streaming Source 和规则决定目标 Cell。它提供自动的空间流送组织，但不会替项目决定任务何时可交互、资产是否已经异步初始化、服务器是否允许传送。

本文先解释这条因果链，再搭建只有一个流送源、两个标记物的完整正常例；最后分别加入 Data Layer、预加载、HLOD 和引用关系。传统子关卡的对象状态详见 [08 关卡流送](08-关卡流送LevelStreaming.md)；引擎实现与 Trace 分析放在 [22 World Partition 与 World Streaming 源码](22-WorldPartition与WorldStreaming源码.md)，本文不把源码页的历史核验当作本次证据。

## 二、三个阶段不能混为一谈

### 2.1 编辑组织、流送生成、运行决策

编辑器需要知道“某个 Actor 在哪里、属于什么层、与谁有引用”，才能在不把整张世界都载入编辑器的情况下组织内容。Actor 描述信息承担这种索引职责；它不是已生成、可调用业务方法的 Actor 实例。

生成运行时流送数据时，要结合空间配置、Actor 边界与依赖关系形成可流送的 Cell。运行时查询的是生成后的流送组织与其目标状态。不能据此推导“运行时每帧遍历全部编辑器 ActorDesc”，也不能保证全部描述信息在所有打包配置中以同一形式常驻。

一个 Cell 通常包含多项内容，不是“一个 Actor 对应一个 Cell”。OFPA（One File Per Actor）主要解决编辑文件冲突；它把 Actor 实例数据放进外部文件，Cook 后 Actor 会进入相应 Level 数据。**编辑保存单元与运行流送单元不同**，不能数外部 Actor 文件来推算运行时加载请求数。[OFPA 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/one-file-per-actor-in-unreal-engine?application_version=5.6)明确区分了这两种形态。

### 2.2 空间位置决定候选，配置与依赖决定实际结果

对本篇使用的单个 Runtime Spatial Hash 网格，可按下面的简化模型理解：流送源位置和形状选出附近候选 Cell；空间加载标记、Runtime Data Layer 等条件进一步影响目标；引擎再异步推进实际状态。

- `Is Spatially Loaded = true` 的 Actor 参与空间条件；关闭它只是不再用距离作为该 Actor 的加载前提，仍可能受 Runtime Data Layer 限制
- `Runtime Grid` 表达 Actor 的网格归属；`None` 表示由分区系统选择，不等于“不流送”
- `Cell Size` 与 `Loading Range` 是不同参数：前者控制组织粒度，后者控制源周围的覆盖范围
- 关卡 Actor 之间的引用可能使内容被组合并一起加载；一个巨大 Actor 的边界也可能改变其组织粒度。因此不能只用中心点距离预测每个 Actor 的命运

这些是 [World Partition 5.6 的 Actor、Streaming Sources、Runtime Grid Settings 章节](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine?application_version=5.6)支持的使用契约。旧稿“按 Actor 密度自动细分来保证内存与加载时间”没有本次来源支持，应撤回；固定参数下堆入更多内容，成本并不会自动保持不变。普通网格层级也不能直接等同于“近处完整 Actor、远处 HLOD”的层级。

### 2.3 目标状态、实际状态与业务可用性

Streaming Source 可以要求 `Loaded` 或 `Activated`。前者用于加载准备，后者要求内容加入活动世界；这不保证一个网格必定在屏幕上可见，因为视锥、遮挡、材质和渲染设置仍会影响画面。同一 Cell 被多个源覆盖时，较高的目标状态会胜出，所以某个 `Loaded` 源无法把另一个 `Activated` 源需要的 Cell 压回不可见状态。

`Is Streaming Completed` 有同名但不同 Target 的节点。本文只使用从 **World Partition Streaming Source Component 引用**拖出的版本，含义限定为该源涉及的流送是否完成。它不证明一个指定任务对象已生成，更不证明网络状态、导航、碰撞查询所需条件、自定义异步资源全部准备好。查询时源被禁用、范围不含目标或 Data Layer 不允许目标进入活动世界，也不能把 `true` 理解为“目标 Actor 可交互”。

卸载目标同样不等于当场归还全部内存：其他对象可能共享资产，引用可能延续对象寿命，GC 和资源释放还有自己的时机。业务应在 Actor 离开世界时撤销注册与交互资格，不能拿“某指针仍非空”证明它仍服务于当前世界。只观察画面消失也不能证明 Cell 已卸载。[Actor Lifecycle 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-actor-lifecycle?application_version=5.6)还说明，快速重新加载时可能复用尚未回收的同一 Actor；所以“再次 BeginPlay”也不能证明内存地址改变或变量自动重置。

## 三、完整正常例：一个移动流送源，A/B 两个标记

### 3.1 输入与目标

本例设计为本地单玩家 PIE，无网络，无 HLOD、Data Layer 和跨 Actor 引用。目标是观察“把唯一源从 A 移到 B，再移回 A”如何改变标记的运行生命周期；它不是玩家传送产品功能，也不是性能基准。

所有位置和长度按 UE 默认厘米单位填写。自选参数 `Cell Size = 12800`、`Loading Range = 25600` 仅为让两区域充分分离，不能称为项目推荐值。A 为 `(5000, 5000, 500)`，B 为 `(125000, 5000, 500)`，两者相差 120000 cm。尺寸 500 cm 的小标记远小于 Cell；不预测精确 Cell 编号或切换帧。

准备一个独立的 Blueprint 测试工程与四个 Blueprint 类：

| 资产 | 父类 | 职责 |
| --- | --- | --- |
| `BP_WPController` | PlayerController | 禁用默认玩家流送源，避免第二个源干扰 |
| `BP_WPMode` | GameModeBase | 选用上述 Controller |
| `BP_WPMarker` | Actor | 显示立方体，打印自身 BeginPlay/EndPlay |
| `BP_WPProbe` | Actor | 唯一流送源、固定相机及按键输入 |

下面列出的是应在目标编辑器实施的接线方案，本次没有创建工程、资产或执行蓝图 Compile。

### 3.2 世界和网格

1. 通过 `File > New Level > Open World` 新建测试关卡，保存为 `/Game/WPTest/L_WPTest`。只在这张新建试验图里移除模板 Landscape，保留基础天空与灯光；这样标记不受地形遮挡，也不把地形作为本例的一部分
2. 在 World Settings 中确认 `Enable Streaming` 开启。本文选择 `WorldPartitionRuntimeSpatialHash` 的单网格配置，设置一个名为 `MainGrid` 的网格，Cell Size 为 12800，Loading Range 为 25600；打开 Preview Grids 便于检查。若工程使用 Runtime Hash Set 或界面不提供这些字段，先停止套用本例并核对实际 Hash 类型，不把两种配置强行对应
3. `BP_WPController` 的 Class Defaults 中关闭 `Enable Streaming Source`。在 `BP_WPMode` 中把 Player Controller Class 设为它；World Settings 的 GameMode Override 选择 `BP_WPMode`。本例不使用 Pawn 移动输入，Default Pawn Class 可设为 `None`
4. 打开 `Window > World Partition > World Partition Editor`，框选包含 A、B 的编辑区域，右键 `Load Region from Selection`，以便摆放并保存下面的 Actor。编辑区域加载是作者工作的条件，运行时条件由 Probe 决定；移动编辑器视口不等于移动运行时源

OFPA 会产生外部 Actor 文件；保存时应包含关卡和本例 Actor。不要把只保存 `.umap` 当成已经保存全部测试内容。[官方编辑区域与启用流送说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine?application_version=5.6)也提醒：部分模板虽使用 World Partition，却默认关闭 Enable Streaming。

### 3.3 标记 Blueprint：让运行实例自己留下证据

`BP_WPMarker` 保留 DefaultSceneRoot，添加 Static Mesh Component，选择引擎 BasicShapes 的 Cube，Relative Scale 设为 `(5, 5, 5)`，Collision Presets 为 `NoCollision`。添加 String 变量 `MarkerId`，勾选 Instance Editable。Event Graph 接线如下，所有 Print String 都打开 Print to Log；是否显示到屏幕可自行选择：

```text
Event BeginPlay
  → Format Text: "WP_MARKER {Id} BEGIN", Id = MarkerId
  → Print String

Event EndPlay (End Play Reason)
  → 将 End Play Reason 转成字符串
  → Format Text: "WP_MARKER {Id} END {Reason}"
  → Print String
```

把 Blueprint 的两个实例放到 A、B，分别把 MarkerId 设成 `A`、`B`。它们的 World Partition 设置均为 `Is Spatially Loaded = true`，Runtime Grid 为 `MainGrid`，不属于任何 Data Layer，并关闭 `Include Actor in HLOD`。保持 Actor 未隐藏、组件 Visible 开启。

这里刻意不把两个实例引用存进 Probe、Controller 或 Level Blueprint。打印的是实例自己的生命周期，不需要一个常驻管理器先持有它们。相同 Blueprint 类与网格资产被两实例使用，不等于 A 实例直接引用 B 实例。

### 3.4 Probe Blueprint：源和相机明确属于谁

`BP_WPProbe` 添加 Camera Component 和 World Partition Streaming Source Component，后者命名为 `WPSource`。类与实例设置：

- Probe 实例放在 A，`Is Spatially Loaded = false`，不加入任何 Data Layer，`Auto Receive Input = Player 0`；整个测试只放一个 Probe
- Camera Relative Location 为 `(-2000, 0, 1000)`，Relative Rotation 的 Pitch 为 `-26.565`、Yaw/Roll 为 0，Field of View 为 60。Camera 朝向 Actor 附近的立方体；源的位置取 Probe Actor 的位置，不是 Camera 的偏移位置
- WPSource 设 `Streaming Source Enabled = true`、`Target State = Activated`，Shapes 为空；目标网格筛选保留空，使用本图唯一的普通网格。Shapes 为空时采用网格加载范围，不用调试 Visualizer 的显示半径冒充实际加载范围

该组件是 `UActorComponent`，不能把它当有独立空间 Transform 的 SceneComponent。旧指南的单数 Target Grid/Target HLOD Layer 与可访问的 [5.5 组件 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/Components/UWorldPartitionStreamingSourceCo-?application_version=5.5)中的数组及弃用字段已有差异；本例不填写版本敏感的目标过滤列表。

Probe 的 Event Graph 按下列数据与执行线连接：

```text
Event BeginPlay
  → Get Player Controller (Player Index = 0)
  → Set View Target with Blend
       Target = 上述 PlayerController
       New View Target = Self（BP_WPProbe）
       Blend Time = 0
  → Print String "WP_REQUEST A"

Keyboard 1 / Pressed
  → Set Actor Location (Target = Self, New Location = A, Sweep = false)
  → Print String "WP_REQUEST A"

Keyboard 2 / Pressed
  → Set Actor Location (Target = Self, New Location = B, Sweep = false)
  → Print String "WP_REQUEST B"

Keyboard 3 / Pressed
  → 从 WPSource 引用调用 Is Streaming Completed
  → Format Text: "WP_SOURCE_COMPLETE {Complete}"
  → Print String
```

`A`、`B` 可直接填前述 Vector 常量；本例不需要引用 Marker 实例。`Set View Target with Blend` 的 Target 是 PlayerController，New View Target 是有 Camera 的 Probe，不能反接。Probe 接受 Player 0 输入；开始 PIE 后先点击游戏视口让按键进入游戏，不要在 Output Log 文本框里按数字。

先在蓝图编辑器检查四个类并 Compile/Save，再保存关卡。若编译、网格字段或 GameMode 设置不成立，停止运行步骤并保留错误；本次没有这些实际结果。

### 3.5 从输入到输出的验收路线

从当前关卡启动单玩家 PIE，选择 Play 而非只 Simulate。打开 Output Log 搜索 `WP_`。如需观察 Cell，另输入 `wp.Runtime.ToggleDrawRuntimeHash2D`；该命令是上述 Spatial Hash 路线的辅助观察，不用画面颜色代替 Marker 事件。

| 操作 | 稳定后应观察到的纸面结果 | 机制解释 |
| --- | --- | --- |
| 开始 PIE，Probe 位于 A | A 立方体进入相机视野，出现 `WP_MARKER A BEGIN`；B 没有运行 BEGIN | 唯一源覆盖 A，B 在远处且无其他依赖 |
| 按 2，把 Probe 移到 B | 视点随 Probe 移到 B；随后出现 `WP_MARKER B BEGIN`，A 出现流送移出相关 END | 目标 Cell 集合发生变化，进入和退出分别异步完成 |
| 稳定后按 3 | 在本例的 Activated 目标下，预期打印完成为 true | 这里只检查该源的流送完成；仍与 Marker 事件一起判读 |
| 按 1，回到 A | A 再次 BEGIN，B END；相机重新看到 A | 源再次覆盖 A，世界内容重新进入活动生命周期 |
| 在 A 稳定后再次按 1 | 不应仅因相同请求多出一次 A BEGIN | 位置与需求没有改变，不是每次请求都重建 Actor |
| 停止 PIE | 仍在世界中的实例也会 END | 停止 PIE 的 EndPlay 原因与流送移出必须区分 |

这是**事件约束，不是逐行黄金输出**：A 的 END 与 B 的 BEGIN 没有本文承诺的全局先后顺序，Print 的异步出现也不证明某帧加载耗时。按 2 后相机先移动、B 稍后出现，恰好暴露“请求已发出”与“内容已进入世界”的间隔；产品传送通常要预加载，见下一节。

本例建议每次请求后给自己 10 秒观察窗口。超过窗口未满足条件就记为本次尝试失败，保存配置与日志并排查；10 秒是教学停止条件，不是引擎承诺或性能预算。若起步就有 B BEGIN、A 永不 END，先找第二个源、Is Spatially Loaded、硬引用、运行图与 Enable Streaming，而不是把结果改写成预期。

反例只改变一个变量：关闭 `Enable Streaming`，重启 PIE，再做 A→B。此时空间按需流送不再是本例的前提，不能仍期望两地只激活近处内容。恢复设置后另起一次 PIE，不把不同配置日志混在一起。这项负向对照也尚未运行。

## 四、在正常例上逐项增加能力

### 4.1 Runtime Data Layer：同一地点也可以不出现

回到编辑器，为 B 创建 `/Game/WPTest/DL_B` Data Layer Asset，Type 设为 Runtime。在 Data Layers Outliner 创建并绑定该资产的实例，或者将资产拖入 Outliner；只把 B 加进这个实例，不设父层，不加入其他 Runtime Data Layer，Probe 保持层外。**资产是可复用定义，实例才保存这张 World 的设置**，只在 Content Browser 创建资产还不够。[Data Layers 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---data-layers-in-unreal-engine?application_version=5.6)

用同一 A→B 路线分三次 PIE，每次只改该实例的 Initial Runtime State：

| 初态 | 到 B 后预期 | 不能推出什么 |
| --- | --- | --- |
| Unloaded | B 不进入本例活动世界 | 源完成为 true 不代表 B 存在 |
| Loaded | 允许加载准备，但不把 B 当作可见、可交互 Actor | 看不到 B 不能单靠画面证明内存是否有其资源 |
| Activated | 满足空间条件后，B 应恢复正常例的可见与 BEGIN 结果 | 初态只是目标条件，不承诺同帧达到 |

Editor 的 Loaded/Visible 复选框与 Initial Runtime State 是不同入口。多层成员关系、父子层有效状态会引入额外条件，不能把本例单层表格直接推广为“Actor 的所有层必须 Activated”或“改子层就必定生效”。

运行中切换时，可通过目标版本的 Data Layer 管理入口修改状态，再观察有效状态、流送与目标对象分别收敛。5.6 操作文档仍展示 `UDataLayerSubsystem` 示例；本次可访问的 [5.8 `UDataLayerManager::SetDataLayerRuntimeState`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UDataLayerManager/SetDataLayerRuntimeState)按资产查找匹配实例，并区分 Client-Only、Server-Only、无 Load Filter 的调用侧。本文没有取得可用的 5.6 Manager API 页面，不提供假称在 5.6 编译过的跨版本 C++ 接线。三次初态对照已经能独立验证本节的加载条件，不依赖该 API。

### 4.2 预加载传送：在目的地建立第二个源

正常例主动把相机和源一起移动，所以允许看到空白等待。产品传送应先在 B 维持一个独立源，原玩家源仍在 A；B 源要求 Activated，并在它的组件上检查完成。此时世界可能同时保有 A、B 的内容，峰值内存通常高于仅保留一个区域的方案。

继续传送至少要核对：目的地源仍启用且范围正确、对应 Data Layer 有效状态满足需求、当前 World 的目的地对象重新解析成功、业务自己的准备条件满足。完成查询只是其中一项。然后才把玩家移到 B，确认玩家自己的源接管覆盖，再关闭临时源；失败、取消或超时则关闭临时源并留在 A。

这是接入设计边界，并非本篇已经实现的第二个产品样例。不要用一个固定 Delay 冒充准备信号；也不要在源关闭后仍以先前的完成布尔值作为长期通行证。高优先级能改变调度倾向，不能把未知加载时间变成零。

### 4.3 HLOD：远处可见的代理不等于原物可交互

主例关闭 HLOD 是为了让“Actor 离开世界”与“屏幕仍见远景代理”不混淆。要为远景补外观，需要单独设计并构建 HLOD Layer；创建空 Layer 资产不等于已经生成代理。[HLOD 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine?application_version=5.6)列出 Instancing、Merged Mesh、Simplified Mesh 三类，以及 HLOD 自己的 Cell Size/Loading Range。

一个受控扩展是：保留 Marker 的日志用途，在 B 周围另摆几块 Mobility=Static 的 Static Mesh 建筑；给它们指定 Merged Mesh HLOD Layer 并允许生成 HLOD，然后执行目标版本的 Build HLODs。把源停在“已超出原内容加载范围，但仍落在 HLOD 覆盖内”的中间位置，配合 `wp.Runtime.HLOD 0` 与恢复为 1 的对照检查代理是否负责远景外观。具体中间位置要由该图生成的网格与边界确认，不能把距离常量当作精确切换点。

原始 Actor 已移出而代理仍显示是允许的结果；代理不代表原始 Actor 的能力、交互状态或复制实例仍在。Nanite 是网格渲染机制，HLOD 是聚合与远景表示机制；生成代理是否启用 Nanite是构建选项，不能写成所有 HLOD 必然为 Nanite，或直接承诺固定的 Draw Call 降幅。本扩展资产设置、Build 和 GPU 观察均 NOT_RUN。

### 4.4 引用、重入与运行时生成的对象

给 A 的实例增加指向 B 的强对象引用，会改变此前“无跨区依赖”的前提；官方指南明确提醒关卡 Actor 引用可能被打包组合并一起加载。Level Blueprint 直接引用关卡 Actor 也可能使其常驻。先用 Reference Viewer 和蓝图变量检查依赖，再讨论为什么 B 无法按预期离开；不要把“源已远离”当作唯一卸载证据。

软引用减少“只因引用就立即加载”的绑定，但**软引用不是加载租约**：路径可保存不代表实例此刻存在，解析得到对象也不保证未来不离开世界。运行时交互应按当前 World 和业务身份重新查找有效实例；长期数据放在与流送 Actor 分离的状态存储，Actor 重新进入时再恢复。弱引用主要避免把寿命强行延长，也仍需有效性检查。

运行时 SpawnActor 不能假设会自动加入为编辑期放置内容生成的空间流送 Cell。明确生成位置、Owner/Level 归属、销毁责任和重建数据；不要承诺“加进 Data Layer 就自动获得所有离线装箱行为”。本篇未验证动态 Actor 的某一具体工程方案。

## 五、配置取舍与排查顺序

### 5.1 为什么不存在通用最佳 Cell Size

更小的 Cell 有机会细化空间选择，但可能增加 Cell 数、调度与边界切换；更大的 Cell 则可能把暂时不需要的内容一起加载。加载范围扩大通常覆盖更多内容，却可能减少高速移动时的等待。以上是成本方向，具体结果还依赖包大小、共享资产、对象初始化、依赖组合、源数量与目标平台。

纸面预算可先算：若允许玩家速度为 20 m/s，测得目标内容从请求到业务可用需要 2 s，那么单是行进就需要至少 40 m 的提前量，还要计入测量波动、Cell 边界和转向。本例没有这项测量，数字只是展示 `距离 = 速度 × 时间` 的推导；不能把它写入全项目加载距离。瞬间传送更不能靠速度外推解决。

实际调优应分别记录请求时刻、Cell 流送、Actor 生命周期、业务准备、帧耗时与内存，比较同一路线的冷/热运行。`stat streaming` 不是世界分区加载完成证明；历史稿已把 `stat HLOD` 从其 UE 5.8 指标列表中撤回，本篇保留该纠错，不重新引入这个命令，也不把旧稿对 `stat worldpartition` 字段的描述当作本次实测。

### 5.2 症状与首先检查的条件

| 症状 | 先查什么 | 为什么 |
| --- | --- | --- |
| 一开局 A、B 都 BEGIN | Enable Streaming、Controller 默认源、额外 Probe、常驻标记与引用 | 任一条件都可能破坏单源独立加载前提 |
| 编辑器看见 B，PIE 看不到 | 运行图/GameMode、源位置、空间标记、Runtime Data Layer 初态 | 编辑器加载不等于运行需求 |
| 按 2 没有 REQUEST 日志 | 游戏视口焦点、Auto Receive Input、BP_WPMode/Controller、蓝图错误 | 先证实输入链，别直接怀疑 Cell |
| 有 REQUEST B，但 B 不 BEGIN | Probe 实际位置、网格、B 保存情况、层状态、目标状态 | 请求存在不证明运行实例已就绪 |
| A END 后画面仍有远景 | HLOD 或其他表示、运行实例身份 | 画面与 Actor 生命周期是不同证据 |
| 源完成为 true，交互仍失败 | 查询 Target、源启用状态、目标对象与业务条件 | 流送范围完成不是业务协议完成 |
| 移出后内存没有立即下降 | 共享资产、仍存引用、GC/渲染资源释放 | 可见性变化不等于内存立即回收 |
| PIE 正常，打包失败 | 地图/资产 Cook 范围、生成数据、目标平台与完整构建日志 | PIE 结果不能代替 Cook 或设备验证 |

### 5.3 Level Instance 与多人协作的边界

Level Instance 适合复用建筑或 POI 的编辑组织。[Level Instancing 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/level-instancing-in-unreal-engine?application_version=5.6)区分两种运行路径：可嵌入的 OFPA 内容进入 World Partition 网格；不能嵌入的实例使用关联 Level 的流送。不能一律称为 `InjectExternalStreamingObject` 注入，更不能把“在 WP 中使用 Level Instance”解释成所有旧手工子关卡配置都自动兼容。

OFPA 减少多人同时编辑同一 Level 文件的冲突，但同一 Actor、共享资源、World Settings 和 Data Layer 组织仍可能冲突；它不提供“多人任意修改都互不干扰”的保证。迁移旧地图前保存原图，先检查转换报告、Actor GUID、实例依赖和 Cook，而不是直接替换原工程。

本篇是 Standalone 语义的单玩家教学。服务器是否启用空间流送、服务器/客户端各自持有哪些 Cell、Actor 复制与可见性确认如何配合，都依赖实际版本和设置；不能概括成“服务器把所有加载 Cell 同步给客户端”，也不能无依据建议服务器缩小范围。Data Layer 的网络调用侧按目标 API 与 Load Filter 检查，空间加载结果不构成授权交互判据。多人服务器、旅行和弱网全部 NOT_RUN。

## 六、来源、历史与未验证边界

| 已核对来源 | 本文采用范围 | 没有证明的范围 |
| --- | --- | --- |
| Epic World Partition，页面标为 5.6 | 空间标记、源、网格字段、编辑区域、引用与调试入口 | 精确分格实现、目标工程配置及性能 |
| Epic Data Layers，页面标为 5.6 | Asset/Instance、Runtime 类型、初态与使用流程 | 跨版本 Manager 接线、复杂多层合并规则、网络实测 |
| Epic HLOD / OFPA / Level Instancing，均为 5.6 页面 | 各自工作流和职责边界 | 本例构建产物、代理观感、源码调用链 |
| Epic Streaming Source Component API，页面标为 5.5 | ActorComponent 继承、组件完成查询、目标过滤字段存在差异 | 5.6 或 5.8 编译兼容 |
| Epic SetDataLayerRuntimeState API，页面标为 5.8 | 资产对应实例、调用侧与 Load Filter 限定 | 5.6 API 相同、网络或业务已就绪 |
| Epic UE 5.0 发布说明的 World Partition / Data Layers 段 | 保留 Data Layers 在 5.0 已出现的历史纠错 | 旧稿本机源码核验再次成立 |
| Epic Actor Lifecycle，页面标为 5.6 | BeginPlay/EndPlay、流送移出与 GC 的区别、快速重入可能复用实例 | 本例事件实录、特定 GC 配置结果 |

本次请求若干带 5.6 参数的 API 页面与两个旧 Actor Lifecycle 入口时，工具返回不可访问；后来通过检索找到正确的 `unreal-engine-actor-lifecycle` 入口并成功取得 5.6 页面。原失败保留，API 失败未用 5.5/5.8 页面暗中填成 5.6 成功证据。取得的是公开文档与选段，不是受限引擎源码全文。所有 UI 字段与蓝图节点仍须在将来实际执行时核验，静态教学设计不冒充已经编译或运行。

本文稳定身份 `kb-36ce828463b5`、标题、主责与导航用途保持不变。此前初稿、统计名修正、版本元数据、OKF 迁移以及 Data Layers 时间线纠错已有 Git 历史，不能说本文此前“从未编辑”。[修订前完整正文](https://github.com/fantuan812/learning/blob/72ae2d14c7f032bf495f7b28bc6289d7a07b5588/%E7%9F%A5%E8%AF%86/03-%E5%BC%95%E6%93%8E%E6%9E%B6%E6%9E%84%E4%B8%8E%E8%B5%84%E6%BA%90%E7%B3%BB%E7%BB%9F/%E4%B8%96%E7%95%8C%E7%BB%84%E7%BB%87%E4%B8%8E%E8%B5%84%E6%BA%90%E5%8A%A0%E8%BD%BD/09-WorldPartition%E5%A4%A7%E4%B8%96%E7%95%8C.md)保留普通教学原文；修订准备另保存各版仓外审计副本，不把这些副本称为本仓库公开附件。旧日志、书籍、日期和附件不属于本次修改范围。

本次新增价值是把分区条件接到可搭建的 A/B 正常路线，并撤回无证据的密度自适应、同步就绪、统一实例注入及网络泛化断言。本文未运行 UE、编辑器操作、资产设置、任何蓝图/C++ 编译、HLOD/导航构建、Cook/打包、GPU、设备、网络或性能测试；L2 与 `verified: []` 保持。
