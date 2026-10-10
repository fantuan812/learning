---
type: Concept
title: "02 Niagara 高级技巧"
description: "Niagara高级模块、数据接口、事件、CPU/GPU边界、Ribbon与组件生命周期的静态教学。"
status: stable
verified: []
maturity: L2
updated: 2026-10-10
---
# 02 Niagara 高级技巧

> 知识成熟度：L2。公开一手文档/API静态核对；所有例子均为 PAPER_EXPECTED / NOT_RUN，不包含引擎运行、编译、GPU测试或性能实测。
> 知识基线：Epic UE 5.6 版本化概念文档；补充API页面在2026-10-10实际返回的版本标识为UE 5.8。逐项来源见第9节，不能据此声称一个UE 5.8工程已经兼容、编译或运行。
> 版本基准：按上述5.6文档与实际返回5.8 API分别核对；没有目标引擎构建号，不能视为跨版本二进制/API兼容承诺。
> 适用范围：UE客户端视觉特效，重点是常规有状态Niagara发射器；轻量发射器、不同RHI、移动设备及项目自定义模块需另核实。
> 最后更新：2026-10-10，修正执行位置、事件语义、数据交换、空间类型、池化和示例；不沿用旧文未给出证据的“已核对5.8源码”声明。
> 前置要求：已读[01-Niagara粒子系统基础](./01-Niagara粒子系统基础.md)，能独立搭建Sprite特效。版本入口保留为[Unreal Engine官方文档总页](https://dev.epicgames.com/documentation/en-us/unreal-engine)，具体结论以本文逐项来源为准。

## 1. 概述

基础篇解释系统、发射器、属性与生命周期。本篇进一步解决六类问题：怎样从世界获取数据，怎样让发射器响应另一个发射器，怎样选择CPU/GPU，怎样构造Ribbon/Beam，怎样让玩法代码管理参数和实例，以及怎样将粒子数据送入材质。

高级特效最容易错在接口两端：图里读的是哪个对象、哪一帧、哪种空间；结果由谁接收、能否延迟、实例何时失效。比如雨滴看起来撞到了地面，不等于CPU已经拿到同帧命中；火焰粒子死亡，不等于技能结算；组件对象仍有效，也不等于它仍属于上一轮播放。

本文保留火焰→烟火、碰撞→涟漪、音乐可视化、网格采样、闪电、池化技能、Data Channel和材质动画这些用途。需要权威命中、伤害、网络同步时，由玩法系统先决定事实，再把表现数据交给Niagara。这样的分工也允许特效被降级、剔除或省略。

## 2. 核心概念

| 概念 | 作用 | 必须同时问的问题 |
| --- | --- | --- |
| Module / Scratch Pad | 参数图中的一次计算或修改 | 执行组、读写属性、次数、先后顺序是什么？ |
| Data Interface（DI） | 暴露外部数据或操作的函数集合 | 当前函数支持哪个模拟目标、依赖哪个资源？ |
| Collision | 用选定查询路径近似或检测接触 | CPU场景查询、深度、距离场或硬件追踪哪一种？ |
| Audio Spectrum | 从音频分析源采样近期频谱 | Submix是否收到声音、频段与分辨率如何设置？ |
| Mesh / Spline DI | 采样网格或曲线 | 对象、LOD、蒙皮时刻与输出空间匹配吗？ |
| Particle Event / Event Handler | 源事件触发接收发射器行为 | CPU限制、Persistent ID、消费上限和载荷是什么？ |
| Niagara Data Channel（NDC） | 按结构交换数据流，聚合表现请求 | 读写可见域、时序、空间分区和监听者寿命是什么？ |
| Niagara Parameter Collection（NPC） | 多资产共享参数 | 是共享状态还是逐次事件？两者不能混用 |
| GPU Simulation | 把粒子脚本放到GPU执行 | System/Emitter仍有CPU开销，读回另计 |
| Simulation Stage | 以粒子或DI为迭代源执行额外阶段 | 迭代次数、读写依赖、缓冲和规模如何限制？ |
| Ribbon / Beam | 将有序粒子连成带状几何，构成拖尾或光束 | 分带ID、连接顺序、宽度和朝向是否独立设置？ |
| Pooling | 复用分配过的组件 | 谁持有播放实例、何时归还、归还后谁还在访问？ |

## 3. 原理详解

### 3.0 高级模块：先写清计算合同，再选择执行阶段

模块从Parameter Map读取输入，再把输出写回Map；后面的模块看到前面的结果。`Particles.*`可保存粒子跨帧状态，模块局部临时值不能充当下一帧缓存。Scratch Pad作用域受所在System/Emitter资产限制；跨资产复用时可导出独立Module Script。不要把“在图中可选”当作所有阶段、CPU和GPU均支持。依据：[Scratch Pad文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-scratch-pad-modules-in-unreal-engine?application_version=5.6)。

以自定义“沿指定方向匀速偏移”为例，输入必须注明：速度Vector，单位cm/s，输入空间World或Local；DeltaTime单位s；输出为Simulation空间的Position。该教学模块是直接位置积分，只用于不再由其他模块重复积分同一速度的最小案例。计算过程是“将速度方向转换到Simulation空间 → 乘本次DeltaTime → 加到当前Position”。转换向量时不叠加平移；转换点时才处理原点。若资产已用速度/力求解模块更新Position，应把作用量接入其已有速度或力路径，避免双重更新。

| 放置位置 | 正确用途 | 误放的可观察后果 |
| --- | --- | --- |
| Particle Spawn | 只初始化一次颜色、寿命、初始位置或存储初始值 | 持续运动模块放这里，只偏移一次 |
| Particle Update | 每次模拟更新随时间变化的状态 | 每帧固定加“10cm”而非速度×时间，运动受帧率影响 |
| System / Emitter | 计算该作用域共享且可在此读取的数据 | 每粒子不同的数据被过早合并，失去差异 |
| Simulation Stage | GPU多阶段或多轮处理，如网格演化 | 多一轮不是免费精度；错误读写依赖会改变结果 |

System、Emitter、Particle的可读写命名空间不同，不能从粒子模块随意修改共享System状态。常规有状态发射器的阶段与依赖可参考[Key Concepts](https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine?application_version=5.6)。

Simulation Stage需明确迭代源是粒子还是DI、一次迭代处理多少元素、读的是哪份输入、写的是哪个输出，以及是否允许本阶段同缓冲更新。[Generic Simulation Stage API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraSimulationStageGeneric)列出IterationSource、NumIterations和部分更新控制，不能从“有这些开关”推导任意邻域算法没有竞态。网格分辨率每轴加倍时，3D单轮元素数变成8倍；增加迭代数还会进一步增加计算与数据传输。这是规模推导，不是帧耗时测量。流体具体求解留给[Niagara流体模拟](./04-Niagara流体模拟.md)。

### 3.1 数据接口详解

#### 3.1.1 CPU函数、GPU资源与实例状态

DI通常是`UNiagaraDataInterface`派生对象。CPU脚本通过VM及外部函数绑定访问相应实现；GPU路径需要对应的Shader函数、资源和参数，并非把任意C++或UObject调用自动翻译成HLSL。API中的`CanExecuteOnTarget`、`GetVMExternalFunction`、`GetFunctionHLSL`、实例数据初始化/销毁及向渲染线程传递数据等入口，表明两条路径有各自合同。依据：[DI API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterface)。

选DI时检查对象是否仍存在、函数输入类型、单位、Simulation/Local/World空间、取样帧、支持的目标、读写权限与空数据行为。CPU端可调用不代表能安全地在任意线程触摸UObject；GPU资源存在也不代表已包含本帧更新。自定义DI的线程同步和资源寿命超出本文实现范围。

#### 3.1.2 碰撞：不同表示有不同盲区

| 路径 | 数据依据与用途 | 不能忽略的边界 |
| --- | --- | --- |
| CPU碰撞 | 世界场景查询，适合小规模、需要场景碰撞过滤的表现 | 碰撞通道/响应、简单或复杂碰撞和查询次数影响结果；不是任意渲染三角形的“绝对精确”查询 |
| GPU Scene Depth | 根据当前视图深度近似接触，适合可见表面的廉价视觉反应 | 屏外和被遮挡表面没有该视图所需信息；只看实际写入深度的表面 |
| GPU距离场 | 使用场景距离场近似几何，可避免只依赖当前视图深度 | 距离场是否生成、表示范围、分辨率、几何类型和平台支持影响结果 |
| GPU硬件光追碰撞 | 以支持的追踪场景处理接触 | 5.6文档标为Experimental，有硬件/RHI依赖，异步计算落后一帧 |

不能把所有GPU碰撞写成“只依赖Scene Depth”，也不能把所有模式都套上硬件光追的一帧时序。[GPU Raytracing Collisions](https://dev.epicgames.com/documentation/en-us/unreal-engine/gpu-raytracing-collisions-in-niagara-for-unreal-engine?application_version=5.6)给出这些模式间的差别及追踪回退；其项目设置只是该功能的前提，本文未修改或测试任何设置。

Collision模块的反弹、摩擦和事件载荷以所选模块输入为准，不保证自动继承命中物体Physics Material的全部规则。雨滴穿墙时，先确认模拟目标、碰撞模式与表面表示，再查速度/步长和Bounds。不要仅增大采样数就宣称修好了屏外深度缺失。生成CPU Collision Event还需要第3.2节的事件模块与接收器。

#### 3.1.3 音频频谱与粒子发声

[Audio Spectrum API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterfaceAudioSpectr-)列出Submix、MinimumFrequency、MaximumFrequency、Resolution、NoiseFloorDb及GetSpectrumValue。典型音乐可视化流程为“声音路由到目标Submix → DI采样频谱 → 选择频段与幅度映射 → 驱动柱高、颜色或生成速率”。例如将幅度限制到约定区间后映射到0～100cm柱高；是否平滑、怎样响应静音由效果设计规定。频谱更新与游戏帧不应被假设为无延迟同步。

Loop Region是播放/循环相关概念，不能作为该DI必须设置FFT分析范围的通用步骤。无数据时依次查播放状态、Submix路由、DI引用、频段范围与噪声阈值，再检查模拟目标兼容性。

[Audio Player API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterfaceAudioPlayer)仍提供单次和持续发声入口、每Tick播放限制及Concurrency等设置；没有依据把它整体写成“UE5已弃用”。大量粒子触发声音要限制每Tick数量和并发；需独立于粒子管理的重要音频，仍适合交给普通Audio Component。采样声音与播放声音是不同用途。

#### 3.1.4 网格体与样条采样

| 数据源 | 保留的用途 | 接入前检查 |
| --- | --- | --- |
| Skeletal Mesh | 伤口火花、骨骼/表面附着、剑气采样 | 实际组件、源模式、骨骼/Socket或三角形采样、LOD、蒙皮模式与数据时刻 |
| Static Mesh | 建筑灰尘、碎裂表面分布 | 静态资产与组件区别、表面采样所需数据、局部到世界变换 |
| Landscape | 地形高度、法线相关表现 | 所用DI函数及目标平台可提供的数据；材质信息不等于任意纹理自动可读 |
| Spline | 路径粒子、闪电、轨迹引导 | 采样位置/切线所在空间、曲线更新时刻、长度或归一化参数约定 |

Skeletal Mesh的骨骼采样与CPU三角形/蒙皮表面采样需要的数据不同。不能概括成“所有蒙皮数据只在渲染线程，CPU必须靠Physics Asset”。公开[DI API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterfaceSkeletalMes-)提供SkinningMode、SamplingRegions、WholeMeshLOD、SourceMode及MeshUserParameter；[实例数据API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/Niagara/FNDISkeletalMesh_InstanceData)还明确区分CPU网格数据可访问性。目标资产是否保留CPU采样所需数据须按所用函数检查，不能发明统一“Readable”开关。

绑定`Actor`让DI再寻找组件，与直接指定`SkeletalMeshComponent`不是同一件事。角色有多个网格时，前者可能取错对象；第4.5节使用专门的组件覆盖函数。关闭Require Current Frame Data可允许采用上一帧骨骼数据并更早调度，但高速运动可能有视觉偏差；这是时效与调度的交换，不是无代价优化。

#### 3.1.5 其他常用能力

| 能力 | 典型用途 | 代价或边界 |
| --- | --- | --- |
| Render Target 2D / Volume | 粒子与材质交换图像/体数据 | 资源格式、尺寸、读写时刻和带宽；不是CPU同步数组 |
| Grid 2D / 3D Collection | 网格化场、Simulation Stage | 分辨率、迭代次数、清空与边界条件 |
| Neighbor Grid 3D | 局部邻域查询、群体效果 | 每格容量、溢出和搜索半径；不能默认保留所有邻居 |
| Niagara Array DI | 外部批量数值、位置/向量数组 | 元素类型、数组长度、更新频率、CPU/GPU同步方向 |
| Camera | 相机相关行为 | 使用的是哪个视图，分屏/立体视图需单独设计 |
| Noise / Curl Noise / Voronoi | 有机运动、纹理化扰动 | 这些名称可能是模块/函数/DI能力，不一概当作独立DI类 |
| Physics Field | 场驱动表现、Chaos关联 | 场类型、采样时刻与可用数据；不等同权威物理模拟 |
| Simple Counter | 限定作用域中的计数用途 | 原子性不等于跨系统全局一致，也不保证GPU遍历次序 |
| Data Channel | 多次表现请求或跨系统数据流 | 见第3.5节的可见性和时序合同 |

本表是用途索引，不是逐函数兼容性证明。具体DI函数在目标编辑器的签名、可用目标和资源前提仍需核验。

### 3.2 事件与粒子通信

#### 3.2.1 内置事件链的实际接口

[UE5.6事件文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/events-and-event-handlers-in-niagara-effects-for-unreal-engine?application_version=5.6)说明常规Events/Event Handlers只支持CPU模拟，并列出Location、Death、Collision三个生成模块。Location Event随源粒子生命周期生成位置数据，不表示自动检测“进入区域”；若要进入区域触发，应另外计算区域条件。不要把未经核对的`Send to Other Emitter`或Spawn Event当作这些内置模块的统一替代名称。

完整连接关系为：

```text
源发射器（CPU，Requires Persistent IDs）
  Particle Update：Generate Death / Location / Collision Event
                    Collision事件之前需有Collision模块
        ↓ 事件数据集，由接收方选择源发射器和事件
接收发射器（CPU）
  Event Handler Properties：Source、Execution Mode、数量限制
  Receive Death / Location / Collision Event：匹配生成类型
        ↓ 按需要接收位置、速度等属性，参与新粒子的初始化
  Particle Update / Renderer：后续运动和外观
```

死亡火焰生成烟和火星、碰撞雨滴生成涟漪、烟花火箭生成轨迹，都可沿这条CPU链设计。自定义事件要给出真实的生成与接收脚本、载荷定义及目标支持，不能只填一个任意事件名。

#### 3.2.2 上限、顺序与代价

[Event Handler参考](https://dev.epicgames.com/documentation/en-us/unreal-engine/add-event-handler-group-reference-for-niagara-effects-in-unreal-engine)的Max Events Per Frame限制消费量，超出的事件被忽略；它不承诺留到下帧，也不自动提供Cooldown或可靠队列。Spawned Particles只作用于本次事件生成的粒子；Every Particle让事件脚本作用于已有粒子，负载会随事件量与粒子量一起增长。

引擎概念文档说在条件允许时事件处理发生在源事件之后的同帧，不能升级为“任何线程、GPU、游戏回调都同帧可见”。源粒子ID、接收粒子ID与数组索引也不能互换。需要冷却、选择优先级或必达重试时由项目显式定义，不应假设内置事件队列代劳。

如果只是跟随或读取其他粒子的属性，可以评估Particle Attribute Reader；它与事件触发不同，还需核对发射器依赖、目标支持和ID/索引有效性。NDC用于数据流交换，Export DI用于数据回传；这些都不是把Event Handler切到GPU就能获得的同一种机制。

### 3.3 CPU、GPU、读回与预算

#### 3.3.1 选型不是粒子数分界线

| 问题 | CPU候选 | GPU候选 |
| --- | --- | --- |
| 传统Event Handler链是否必需？ | 本文核对的CPU路径 | 不支持该事件链，先重设计数据流 |
| DI函数是否有对应实现？ | 看CPU函数与线程前提 | 看GPU函数、资源、平台及调度阶段 |
| 粒子操作适合并行吗？ | 小负载可避免GPU派发粒度浪费 | 大量相似粒子更新可能获益 |
| 瓶颈在哪？ | 占用CPU预算，仍有渲染成本 | 占用GPU计算、显存和带宽，仍有CPU管理成本 |
| 要把结果交给玩法吗？ | 仍需明确导出接口和时刻 | 延迟与容量必须可接受，不能按即时查询理解 |
| 平台是否满足？ | 按所用查询/DI核验 | 按RHI、设备能力与具体碰撞路径核验 |

常规Niagara CPU脚本由VM执行；选择GPU只改变粒子Spawn/Update及Simulation Stage的目标，System/Emitter脚本仍有CPU开销。不存在通用的“5000以下一律CPU”或固定“数万就安全”。[Scalability and Best Practices](https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-and-best-practices-for-niagara?application_version=5.6)明确要求结合硬件与瓶颈选择。移动端同样要看具体功能与渲染路径，不能以旧ES3.1印象概括所有GPU粒子支持。

建议先列功能约束，再比较同一视觉目标的CPU/GPU候选；记录System实例数、Emitter数、粒子峰值、碰撞次数、材质屏幕覆盖和读回量。减少实例可能降低调度成本，却使剔除粒度变粗；改成GPU也无法自动减少半透明像素重叠。没有目标设备测量时，只能提出候选方案。

#### 3.3.2 Bounds、确定性与空间类型

Bounds用于可见性与相关调度判断，不是粒子容器容量。过小会使可见效果提前消失；为了“保险”设成整张地图又会让许多本可剔除的实例继续相关。GPU发射器应按具体版本与资产配置设置可靠边界，不能把“必须打开某个统一GPU Culling开关否则性能雪崩”写成通则。对长距离移动的效果，固定大边界也有代价。

[Emitter Settings](https://dev.epicgames.com/documentation/en-us/unreal-engine/emitter-settings-reference-for-niagara-effects-in-unreal-engine?application_version=5.6)的Determinism主要约束随机数：相同配置、种子、非变化DeltaTime等前提才有意义；修改脚本也会改变结果。这不保证跨平台浮点逐位相同，不保证外部碰撞/骨骼/音频数据相同，也不构成服务器权威回放。核验时应固定随机调用路径、输入时间线、质量档、步长和重置时点。

UE通常以cm、s作为该类位置与时间计算约定；本文例子明确使用cm、cm/s、s。位置使用Niagara `Position`，速度/方向/位移使用相应Vector语义。UE5的LWC使Position与普通Vector不能任意互换；把世界目标点用Vec3无条件塞给Position绑定，可能只在原点附近“看起来正确”。[LWC文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-in-niagara-for-unreal-engine?application_version=5.6)特别指出隐式类型复制可能丢失原点分块信息；组件位置参数使用类型匹配的SetVariablePosition，并在图里明确World与Simulation空间转换。

#### 3.3.3 数据导出：异步结果不是当前状态

[Export DI](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterfaceExport)仍有回调接收对象与GPU分配容量设置，不是“UE5已弃用”；GPU数据并非绝对不能读回。与此同时，[同步GPU数据读取API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/FScopedNiagaraDataSetGPUReadback)明确会让CPU等待GPU，定位于工具/调试，并指向异步捕获入口。本文不把调试捕获接口推荐成每帧玩法查询。

项目若选择异步导出，应将数据视为带延迟的观察，约定容量、丢失处理和接收对象寿命；不能凭一个回调保证完整粒子集或固定延迟。对于LWC导出，公开文档要求按回调的Simulation Position Offset还原适用的世界位置；Local Space还要遵循相应坐标合同，不能盲目把任意Vector都加同一偏移。

本篇的防错设计是：每轮表现请求有项目侧请求号/代次；只有能够关联到该代次的结果才能更新它，已取消代次的迟到结果丢弃。该关联元数据需要项目自己携带或隔离接收对象，Export DI不会自动给出业务请求号。不能关联的回读只用于调试或无权威表现。第4.6节提供静态迟到结果反例。

### 3.4 Ribbon / Beam 特效

#### 3.4.1 分带、连接顺序和朝向是三个问题

Ribbon Renderer将同一分带的粒子按连接顺序组成带状几何。[Ribbon属性API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraRibbonRendererProperties)给出RibbonIdBinding、RibbonLinkOrderBinding、Width、Facing和Tessellation等独立绑定；不要靠粒子数组索引或固定运动方向推断连接身份。

| 属性 | 合同 | 典型错误 |
| --- | --- | --- |
| RibbonID | 一条带的稳定身份，多条轨迹用不同ID | 两次无关爆炸使用同一带，出现跨场景长线 |
| RibbonLinkOrder | 带内明确的连续顺序 | 重复或重置顺序导致交叉连接 |
| RibbonWidth | 约定空间单位的带宽，可随年龄变化 | 以为Width能修正错序 |
| RibbonFacing / Facing Mode | 屏幕朝向或自定义方向 | 把几何翻转问题与连接错序混为一谈 |

运动方向改变可以让带子折返，但不会自动改变显式连接序；单向运动也无法修复错误ID。需要断带时变更分带身份或生命周期。细分更密可改善曲线外形，也会增加几何工作。面向相机用[Facing Mode中的Screen](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/ENiagaraRibbonFacingMode)；本文不再从未读取的5.8源码推断所谓“2D Ribbon”特性的存在与否。

#### 3.4.2 Beam与闪电

Niagara已有Static Beam模板与Beam相关模块，底层使用Ribbon Renderer；“没有专用Beam渲染器”不能说成“没有Beam发射器模板”。[官方Beam教程](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-beam-effect-in-niagara-for-unreal-engine?application_version=5.6)使用Beam Emitter Setup、Spawn Beam、Beam Width，并将Update Beam放在Jitter Position之前。

直线激光可以用起终点和中间采样点形成Ribbon；弯曲闪电可采样Spline，再以受控噪声扰动中间点。起终点是否绝对世界坐标、跟随哪个组件、是否每帧更新应明确。用于连接的采样序号可作为LinkOrder；噪声改变位置而不重新随机打乱连接序。这样既保留光束轮廓，也能控制闪烁；每帧完全无关联的随机位置可能产生明显跳变。

### 3.5 Niagara与蓝图 / C++交互

#### 3.5.1 参数类型与激活顺序

下列是本文需要的[NiagaraComponent API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraComponent)索引，不声称覆盖全部API：

| 接口类别 | 选用要点 |
| --- | --- |
| SetVariableFloat / Int / Bool | 使用完整`User.Name`及对应标量类型 |
| SetVariableVec2 / Vec3 / Vec4、Quat | 向量/旋转；世界位置另用Position类型 |
| SetVariablePosition、SetVariableLinearColor | 分别匹配Position与LinearColor；旧文SetVariableColor不是此处核对的名称 |
| SetVariableActor / Object、Texture / StaticMesh | 接收对象类型各异；DI可能还需专门覆盖函数 |
| SetNiagaraVariableFloat | 公开签名使用FString名称；不是旧文所写FNiagaraVariable键 |
| Activate / Deactivate、SetPaused | 激活、停止生成/完成流程和暂停需区别 |
| ReinitializeSystem、SetAsset | 重新初始化或换资产，可能改变实例数据与覆盖参数 |
| OnSystemFinished | 表现系统完成通知，不承担命中或伤害权威 |
| SetLODDistance、SetGpuComputeDebug | 调试/管理入口，不等于自动完成平台预算配置 |

参数在Particle Spawn读取时，应先写参数再Activate。自动激活后才设置值，不能保证首批粒子看到新值。蓝图也采用“Spawn时Auto Activate=false → 设置全部User参数与对象 → 绑定所需回调 → Activate”的顺序。所有Spawn都处理空返回；视觉系统生成失败不得回滚已经成立的玩法命中。

#### 3.5.2 池化、实例寿命和归还

[ENCPoolMethod](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/ENCPoolMethod)的公开选择包括None、AutoRelease、ManualRelease：None每次新建；AutoRelease由池管理归还；ManualRelease要求调用者负责释放。文档还列出隐藏的内部状态，不能把FreeInPool当成普通业务Spawn策略。

池里保留的是已分配、当前不用的组件，不是把已经销毁的UObject“复活”。池化降低反复分配和GC压力，仍有激活、脚本、渲染和保留内存成本；预热池也可能带来加载或初始化峰值。一个无限循环、一直活跃的系统不会只因选了AutoRelease就自动完成并腾出容量。

| 生命周期用途 | 推荐的拥有关系 | 释放边界 |
| --- | --- | --- |
| 一次性受击/脚步 | AutoRelease，初始化后调用者不再持有业务控制引用 | 资产须能完成；回池后不要用旧引用修改下一次播放 |
| 需要主动停止的持续技能 | ManualRelease，明确的唯一拥有者 | 结束/取消/拥有者销毁均走归还路径，归还后清除引用 |
| 长期挂在角色上的循环表现 | 可复用已持有组件，按资产管理激活 | 不能把“隐藏”“暂停”“Inactive”统称已经归池 |

[ReleaseToPool的公开合同](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UFXSystemComponent/ReleaseToPool)说明释放过程会停用并在完成时归还，调用后继续使用该引用不安全。业务侧应先解除自身回调/记录，再归还，随后不再解引用。`IsValid`只检查对象有效性，不证明它仍代表旧请求；`OnSystemFinished`也不适合进行伤害结算。

同一资产可按不同用途选择拥有策略，重点是每个调用点职责明确。不要依赖上一轮User参数残留；每次初始化明确写全需要的值。当前API描述了池机制用于清理覆盖参数的入口，但其他项目侧缓存、回调、外部接收对象仍需自己管理；频繁SetAsset还会触及DI实例状态，不能把池化当作任意状态清理器。

#### 3.5.3 NPC、Array、Data Channel与Export的选择

| 需求 | 机制 | 关键理由 |
| --- | --- | --- |
| 一个组件当前的颜色/目标 | User参数 | 拥有关系直接，按需更新 |
| 多个资产共享风速等状态 | NPC | 共享参数集合，不是一次性事件队列 |
| 一组同类型采样值 | Array DI | 明确数组长度、类型和更新批次 |
| 连续的碰撞/爆发请求、跨系统数据流 | NDC | 结构化载荷，可让已有监听系统处理多次请求 |
| 粒子观察返回C++/蓝图 | Export DI或明确支持的通道路径 | 需独立处理时序、容量与接收者寿命 |

[NPC API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraParameterCollection)仍说明其共享参数用途，因此不能声称“UE5已弃用，由NDC统一替代”。[NDC概述](https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-data-channels-overview?application_version=5.6)强调数据流与共享模拟，支持高频爆发请求聚合，并非只适合“低频整块数据”。本文没有逐版本核对首次引入与稳定性演进，不能把“UE5.0+统一稳定可用”作为迁移结论。

NDC的监听System、载荷结构、空间分区、可见域及读写时机必须共同配置。5.8公开[单元素写入API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataChannelLibrary/WriteToNiagaraDa-_1)分别提供对蓝图、Niagara CPU、Niagara GPU的可见性，并说明最早在后续Tick Group读取；不是写完立即可见。该节点为BlueprintInternalUseOnly，本文不把它直接当成稳定C++业务调用模板。NDC也不自动完成网络复制或GPU结果即时回传。

#### 3.5.4 每帧驱动参数

变化时更新小参数可减少不必要调用，但真实移动目标需要逐帧或按允许误差更新；“为了省Set而不更新”会让表现滞后。数组适合批量提交，NDC适合聚合请求，二者仍有数据准备、复制和消费开销。是否合批、多久更新取决于信息时效要求与测量，不能按“几百个Set一定掉帧”给无环境结论。

### 3.6 Niagara与材质配合

#### 3.6.1 混合方式由画面目标决定

| 目标 | 候选 | 视觉与成本取舍 |
| --- | --- | --- |
| 火焰、辉光、能量 | Additive | 叠亮背景，不会遮暗，亮背景可能不明显 |
| 烟、灰尘、半透明液体 | Translucent或符合材质设计的混合方式 | 需要透明度层次，注意排序、覆盖面积与着色成本 |
| 调制/暗化类效果 | Modulate | 乘法变暗，不是通用冲击波或折射开关 |
| 与几何相交的粒子 | 合适的透明混合＋DepthFade | 降低交界硬缝，不改变实际碰撞 |

依据：[Material Blend Modes](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-blend-modes-in-unreal-engine?application_version=5.6)。混合模式与Material Domain、Shading Model是不同设置。Additive也要着色和混合被覆盖的像素，改成Additive不会自动降低overdraw。

#### 3.6.2 属性绑定、SubUV与软粒子

`Particles.Color`通过Renderer绑定供材质使用；位置、尺寸、速度等也必须与对应Renderer和材质节点的语义一致，不能把Niagara属性名当成任意材质输入自动可用。`Particles.DynamicMaterialParameter`通常承载一个四分量向量，不是单个float；逐粒子的颜色/溶解值本就可走粒子数据，不会仅因“每粒子不同”自动失去实例化。更换独立材质/纹理资源或新增Renderer的成本是另一问题。

[Sprite Renderer API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraSpriteRendererProperties)列出DynamicMaterial绑定、SubImageIndexBinding和SubImageSize。4×4图集约定16格，离散索引0～15；可令索引由NormalizedAge映射并限制到此范围。Renderer设置列数/行数，图集材质采用匹配UV；帧间混合需对应设置和材质支持，不能只靠不存在的通用“Sub UVs”勾选框。项目若用连续索引，还需确认混合和末帧处理。

软粒子效果通常通过材质深度相关计算实现。`DepthFade`接收原Opacity与FadeDistance，用场景深度差淡化透明对象和不透明表面的交界；FadeDistance按世界距离理解。它与相机近距离淡出是不同效果，不是一个通用“Soft Particles”材质开关。依据：[Depth Material Expressions](https://dev.epicgames.com/documentation/unreal-engine/depth-material-expressions-in-unreal-engine)。透明表面不提供所需深度时，不能期待同样的相交淡出；移动端检查目标渲染路径的数据支持。

#### 3.6.3 材质实例与共享值

MPC适合跨对象共享状态；Renderer材质参数绑定可按发射器实例生成动态材质实例；粒子动态参数适合每粒子差异。Custom Primitive Data属于组件/图元层级，不能代替任意逐粒子属性。修改共享材质实例会影响共享它的使用者，应先弄清资源与数据的作用域。必要时使用图集或纹理数组表达逐粒子纹理选择，并评估采样与资源成本。

## 4. 代码 / 蓝图示例与可复现的纸面追踪

所有示例均为PAPER_EXPECTED / NOT_RUN。C++是本文创作的接口用法，未编译，不是引擎源码；图表是完整约束下的静态预期，不能冒充PIE截图、GPU结果或性能记录。

### 4.1 技能光束：先写参数、再激活、按拥有关系收尾

先制作能自行完成的一次性Beam System：User.TargetPosition为Position，User.BeamColor为LinearColor，User.Power为Float；目标点按世界坐标输入，在模块里正确转换到Simulation空间。Power只控制外观，不参与伤害判定。粒子生命周期必须有限，System/Emitter不能无限发射。以下函数是完整的“一次性无持有”调用，接口依据[Function Library](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraFunctionLibrary)。

```cpp
// 静态接口示例；调用前项目需正确依赖Niagara模块，所有输入均由调用方提供。
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

void SpawnSkillBeamVisual(const UObject* WorldContext, UNiagaraSystem* System,
    const FVector& StartWorld, const FVector& TargetWorld,
    const FLinearColor& Color, float Power)
{
    if (!WorldContext || !System || StartWorld.ContainsNaN() ||
        TargetWorld.ContainsNaN() || !FMath::IsFinite(Power)) return;
    const FVector Direction = TargetWorld - StartWorld;
    if (Direction.IsNearlyZero()) return; // 本例约定零长度不显示光束
    UNiagaraComponent* FX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        WorldContext, System, StartWorld, Direction.Rotation(), FVector::OneVector,
        true, false, ENCPoolMethod::AutoRelease, true);
    if (!FX) return; // 预剔除/不可生成等情形允许视觉缺席
    FX->SetVariablePosition(FName(TEXT("User.TargetPosition")), TargetWorld);
    FX->SetVariableLinearColor(FName(TEXT("User.BeamColor")), Color);
    FX->SetVariableFloat(FName(TEXT("User.Power")), FMath::Clamp(Power, 0.0f, 1.0f));
    FX->Activate(true);
    // AutoRelease：此处不返回或缓存FX，之后不再修改这个播放实例。
}
```

需要播放结束通知或主动取消的技能则使用下面完整的蓝图拥有流程，不能直接给无持有示例随意加一个长期裸指针：

```text
状态：Idle / Active；拥有者只保留一个ActiveComponent，初值空。
Start：
  先执行Stop旧请求；Spawn选ManualRelease、AutoActivate=false。
  返回空 → 保持Idle，玩法结果照常成立。
  返回组件 → 写全参数，存ActiveComponent，绑定OnSystemFinished，Activate。
OnSystemFinished(Finished)：
  Finished != ActiveComponent → 忽略不属于当前播放的通知。
  相同 → 解除自身该回调，清ActiveComponent并设Idle，调用Finished.ReleaseToPool。
         此后不再访问Finished；这里只更新表现/UI记录。
Stop或拥有者EndPlay：
  ActiveComponent为空 → 无操作。
  非空 → 暂存局部引用，解除自身回调，清ActiveComponent并设Idle，ReleaseToPool。
          此后不再访问局部引用；释放完成不依赖拥有者再接收通知。
```

这两个方案各自有完整的释放责任。回调流程假设单个拥有者只控制一个当前实例，重入时先清状态；更多并行技能需要分别管理其拥有关系，本文不扩展成通用对象池框架。无限循环资源、暂停/剔除和强制停用都应在目标项目验收中检查，不承诺回调代表真实播放时长。

### 4.2 CPU死亡事件链：火焰 → 烟和火星

构造同一System中的A火焰、B烟、C火星，三个均用CPU。A启用Persistent IDs；在更新中生成Death Event。B、C各添加对应Source和Receive Death Event，选择Spawned Particles；B每事件生成2粒，C每事件生成3粒，二者Max Events Per Frame均设4，不启用随机生成数量。B/C只由事件生成，关闭其他Spawn来源；位置来自事件，寿命分别1s与0.5s，颜色在各自初始化中显式指定。不要假设Death载荷自动包含自定义Color/Scale；若需继承，先核对载荷并实现匹配脚本。

纸面输入是一轮更新产生3个死亡事件，三者位置分别为(0,0,0)、(100,0,0)、(200,0,0)cm。在事件被正常消费且没有其他剔除的前提下，预期B生成6粒、C生成9粒，分别按源位置分组。若同轮产生6个死亡事件，则每个Handler最多消费4个，预期B为8粒、C为12粒，超出的2个不承诺留到下帧；未指定优先规则时，不把哪两个被忽略写成固定选择。

代价不止新粒子数，还包括两个Handler以及新粒子的完整生命周期。连锁爆炸可沿相同机制设计，但必须限制分支数量和链深度；“最多两级”只可作为项目预算选择，不能充当引擎限制。

### 4.3 碰撞事件生成涟漪

小规模展示可用CPU雨滴：Collision → Generate Collision Event；CPU涟漪接收Receive Collision Event，按碰撞位置生成粒子，朝向由命中法线确定，寿命0.5s，尺寸从0增长到40cm。源与接收器启用所需ID，明确每帧事件与生成数上限；持续接触是否反复产生事件要在源脚本中控制，不能默认只有一次。

若雨滴必须用GPU，原先“GPU雨滴直接触发CPU Event Handler”的连线不成立。可选择由玩法/环境查询先提供稀疏落点，再用User参数、Array或NDC生成表现；或设计有明确延迟和容量的GPU导出，仅用于允许延迟的视觉涟漪。离屏降雨如果依赖Scene Depth，不能把看不到的地面当作已得到碰撞。此处只描述替代数据路线，不声称已验证任何GPU导出实现。

### 4.4 Data Channel：聚合两次命中表现

这是蓝图数据合同，不使用旧文猜测的C++ Handler/Writer签名。建立NDC_Impact载荷：ImpactPosition（Position，世界坐标cm）、ImpactNormal（Vector，单位方向）、Intensity（Float，约定0～1）。选择Islands，下面两落点约定在同一Island内，写入Search Parameters使用实际命中位置。每个Island仅有一个CPU监听System；其Emitter寿命由System管理，System持续监听，并用Complete if Unused在无请求后完成。具体创建界面可沿[官方5.6碰撞聚合教程](https://dev.epicgames.com/documentation/unreal-engine/combining-collision-effects-in-niagara-data-channels?application_version=5.6)。

给出两次不同命中：位置(0,0,0)与(100,0,0)，法线均(0,0,1)，Intensity分别0.5与1。玩法验证输入后在帧N通过目标版本的Write To Niagara Data Channel蓝图节点逐条写入，开启CPU可见性。Channel启用Keep Previous Frame Data，监听器Read Current Frame关闭；用生成向导设置每条记录固定生成1粒、无筛选条件、自动转换Position到Simulation空间，关闭其他生成来源。Init Particles From NDC放在Initialize Particle之前，用读出的Position初始化位置，并用Intensity驱动外观。假定监听器正常Tick且未被剔除，帧N+1预期新增2粒，帧N+2无新请求时新增0粒；不是每帧重复消费旧命中。两条记录不应因两个监听器重复消费而变成四次表现。

反例：写入后立刻同调用栈读取，不应期待本例的上一帧读取器立即看到新记录。若改成GPU粒子脚本直接读NDC且不走NDC生成路径，就要显式保证GPU可见；但不能把“GPU发射器”一概等同“必须打开GPU可见”。官方5.6教程的Spawn Conditional / Spawn Direct在CPU端读取后向GPU生成数据，该路径仍需CPU可见，相关数据会自动供给GPU。数据保留和读写顺序由具体Channel规定，本文不把它当可靠持久队列。此案例未测量聚合收益，也没有承诺NDC自动去重或复制到服务器。

### 4.5 指定骨骼组件，避免误绑定Actor

前提：一个尚未激活的有效NiagaraComponent，资产暴露名为User.SourceMesh的Skeletal Mesh DI User参数；调用者提供确切USkeletalMeshComponent。下面用公开函数绑定该组件；它与“Object参数直接装组件”属于不同资产合同，不能混写。

```cpp
// 静态接口节选；FX和Mesh由调用方验证非空，绑定后再Activate。
UNiagaraFunctionLibrary::OverrideSystemUserVariableSkeletalMeshComponent(
    FX, TEXT("User.SourceMesh"), Mesh);
```

[Function Library](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraFunctionLibrary)的该函数参数是USkeletalMeshComponent，不是GetOwner()返回的Actor。若资产采用MeshUserParameter引用Object，则改按该DI预期配置对象类型并用匹配入口，不把两种路线叠加成必需步骤。验证时用角色身上的两个不同骨骼组件作为正反控制，观察粒子跟随哪一个；还需验证组件销毁、LOD变化和上一帧数据造成的偏移。

### 4.6 模块、读回与空间的纸面检查

| 编号 | 给定输入与操作 | PAPER_EXPECTED；判定理由 |
| --- | --- | --- |
| A | 第3.0节匀速模块，p=0，v=100cm/s，分别用10次0.1s或20次0.05s更新，无其他力/积分/转换 | 两者总位移均100cm；说明单位与DeltaTime因果，未声称一般数值积分跨步长相同 |
| B | 错误模块每次固定加10cm，同样两组更新 | 分别100cm和200cm，暴露帧次数依赖 |
| C | 发射器局部原点在世界(1000,0,0)，无旋转/缩放，世界目标(1100,0,0) | 对局部点应得到(100,0,0)；若直接把1100作局部值，将出现世界2100的错误目标 |
| D | 请求代次7发起允许延迟的读回，取消7并开始8，随后收到标明7的结果 | 丢弃旧代次，不能更新8或归还8的组件；代次信息由项目设计提供 |
| E | 启用Determinism但改变DeltaTime序列或外部骨骼输入 | 不据种子相同断言整段轨迹一致 |
| F | 同一Ribbon两个不相干轨迹误用同一ID；修正为不同ID且各自顺序连续 | 修正后分成两条带；单纯改Facing Mode不能解决误连接 |
| G | AutoRelease组件已回池，旧逻辑还保留一个IsValid的引用 | 该引用不能被当作旧播放继续设置参数；对象有效性不证明业务拥有权 |

## 5. 最佳实践：把收益和代价放在一起

1. 把共享计算放到合适作用域，减少重复采样；只有不需要逐粒子差异且时序可接受的数据才能上移。
2. 限制事件量、连锁分支和新粒子数，同时考虑完整寿命内的峰值；Handler的丢弃策略不提供可靠消息服务。
3. CPU/GPU按功能和瓶颈选择；实例调度、粒子模拟、碰撞、材质、排序与读回分别记预算，不用单一粒子数阈值替代。
4. Bounds覆盖真实运动范围并兼顾剔除，移动长轨迹与静止局部效果应分别设计。
5. User参数、NPC、Array、NDC和Export根据状态/数据流/回传需求选择；高频或批量本身不足以决定接口。
6. 池化策略与拥有者责任配套，所有退出路径释放；池预热、保留内存和重新激活都有成本。
7. Ribbon保持稳定身份与顺序；噪声影响外形，朝向影响面对方向，各自调试。
8. 半透明先控制屏幕覆盖与层数，再检查材质复杂度；Additive是视觉选择，不是性能豁免。
9. 版本敏感功能先核对目标版本和平台；Experimental标签、公开API签名与本地实现验证分别记录。
10. 使用Niagara Debugger及项目可用的CPU/GPU性能工具检查实际瓶颈；本文没有运行stat、profilegpu或任何调试CVar，不沿用旧文未证实的5.8命令承诺。

## 6. 常见问题 FAQ

**Q1：GPU粒子收不到事件？**
先区分传统Event Handler与NDC、Attribute Reader、Export。本文核对的传统事件路径只支持CPU；不是把接收方也改成GPU就能支持Spawn/Death事件。

**Q2：粒子穿过地面？**
先查碰撞模式和该模式是否表示这块地面，再查过滤、厚度/分辨率、速度/步长与Bounds。Depth无法提供屏外几何，距离场和硬件追踪各有不同资源前提。

**Q3：每帧SetVariable会不会卡？**
有调用与数据处理成本，但是否成为瓶颈取决于实例量、类型、更新频率与目标平台。去掉未变化的写入；移动目标仍需满足允许延迟，批量路径也要测量。

**Q4：Ribbon扭曲？**
分开查ID、LinkOrder、粒子位置、宽度、朝向和细分；Screen朝向不能修复错序，固定运动方向不能修复ID冲突。

**Q5：NPC还能用吗？**
公开API仍描述其共享参数功能，没有依据要求所有UE5项目迁移到NDC。共享风速可以是NPC；连续命中请求更适合数据流，按实际用途选。

**Q6：Beam怎么做？**
可从Static Beam模板开始，使用Beam模块与Ribbon Renderer；需要曲线可加样条，使用稳定顺序的采样点，明确端点空间和更新时机。

**Q7：骨骼采样没反应？**
先确认真实组件与DI源绑定，再查采样函数、所需CPU数据、LOD、Sampling Region和蒙皮时刻。不要仅把Actor塞进期待组件的入口，也不要把Physics Asset当作所有采样的必需项。

**Q8：音乐可视化没数据？**
检查声音是否进入DI指定Submix、频率区间、分辨率和噪声阈值。Loop Region不是频谱DI通用前提；Audio Player与Audio Spectrum作用不同。

## 7. 关联阅读

- [01-Niagara粒子系统基础.md](./01-Niagara粒子系统基础.md)：架构、属性与生命周期基础；本篇承接，不重复认领基础篇整改贡献。
- [03-VFX性能优化.md](./03-VFX性能优化.md)：GPU、Bounds、半透明及平台预算的进一步问题；邻篇的历史具体结论不自动成为本文已验证事实。
- [04-Niagara流体模拟.md](./04-Niagara流体模拟.md)：Simulation Stage与网格化场的深入用途。
- 渲染与图形相关主题：材质域、混合、Scene Depth与资源生命周期；音频系统相关主题：组件、Submix和频谱分析。

## 8. 验证建议与未运行边界

本轮实际完成公开资料与正文的静态核对、单位/数量的纸面推导。未运行UE/PIE、GPU、C++编译、算法/模型、目标测试、网络、配置、性能基准或PowerShell；上述示例没有运行通过状态，`verified: []`保持为空。

| 后续验证入口 | 正常样例 | 负向控制或失败路径 | 应记录的证据 |
| --- | --- | --- | --- |
| 模块阶段与单位 | 第4.6 A、C | B及双重积分 | 版本、模块顺序、属性轨迹、步长、空间 |
| CPU事件链 | 第4.2三事件、六事件 | 切成GPU、缺Persistent ID、错误Source | 各阶段数量与事件消费，不能只看画面 |
| CPU/GPU碰撞 | 可见/屏外、薄厚表面 | 数据源缺失、不可支持的平台 | RHI、碰撞模式、输入轨迹、命中结果 |
| 池化与生命周期 | 正常完成、重复请求、主动取消 | 无限循环、拥有者EndPlay、迟到通知 | 请求与组件身份、拥有关系、释放次数 |
| 异步导出 | 明确关联的结果可用 | 第4.6 D、容量不足、接收者已销毁 | 发送/接收时刻、代次、实际缺失处理 |
| 数据绑定与渲染 | 双网格、4×4图集、多条Ribbon | 错组件、越界索引、ID冲突 | 参数类型、绑定、资源版本、属性与画面 |
| 预算比较 | 相同视觉目标的两种实现 | 冷池/热池、长轨迹、屏幕大覆盖 | 目标设备、构建、实例/粒子/像素规模及测量口径 |

只完成静态L2不证明这些运行项通过；性能收益和兼容性必须由匹配范围的实测补充。普通历史正文原字节在原Git及仓外审计材料保留，不能声称正文与manifest两路径中包含全部历史归档；工作日志、书籍、附件和旧运行raw不因本篇修订而改变。

## 9. 一手来源与实际核对范围

核对日期均为2026-10-10。网页版本标签只是公开页面的版本身份，未访问受限引擎源码或读取本机Build.version。版本化5.6页面与默认返回5.8的API分开列出；API清单证明接口存在，不证明所有平台的实现细节。固定5.6 API链接有无法读取情形，故不把5.8返回结果伪记成5.6核对成功。

| 来源 | 实际阅读范围与版本 | 支持内容 / 未覆盖 |
| --- | --- | --- |
| [Key Concepts](https://dev.epicgames.com/documentation/en-us/unreal-engine/key-concepts-in-niagara-effects-for-unreal-engine?application_version=5.6) | 5.6，Selection Stack、Namespaces | 阶段/作用域；非本地执行时序跟踪 |
| [Scratch Pad](https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-scratch-pad-modules-in-unreal-engine?application_version=5.6) | 5.6，Scope、Data Flow、Local Parameters、空间转换选段 | 模块复用与数据合同；未逐节复现完整教程 |
| [Events](https://dev.epicgames.com/documentation/en-us/unreal-engine/events-and-event-handlers-in-niagara-effects-for-unreal-engine?application_version=5.6) / [Handler参考](https://dev.epicgames.com/documentation/en-us/unreal-engine/add-event-handler-group-reference-for-niagara-effects-in-unreal-engine) | 分别5.6 / 返回5.8，生成/接收及属性表 | CPU限制、ID、消费上限；非自定义GPU事件支持证明 |
| [Scalability](https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-and-best-practices-for-niagara?application_version=5.6) / [Emitter Settings](https://dev.epicgames.com/documentation/en-us/unreal-engine/emitter-settings-reference-for-niagara-effects-in-unreal-engine?application_version=5.6) | 5.6，实例、VM、GPU/CPU、池化、Bounds、Determinism | 选型机制；无项目性能数字 |
| [DI API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterface) / [Simulation Stage](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraSimulationStageGeneric) | 返回5.8，目标、VM/HLSL、实例数据、迭代配置 | 接口职责；未阅读实现体和同步细节 |
| [GPU碰撞](https://dev.epicgames.com/documentation/en-us/unreal-engine/gpu-raytracing-collisions-in-niagara-for-unreal-engine?application_version=5.6) | 5.6，模式、Experimental、一帧延迟、回退 | 该硬件追踪路径；不代表全部GPU碰撞相同时序 |
| [Audio Spectrum](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterfaceAudioSpectr-) / [Audio Player](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterfaceAudioPlayer) | 返回5.8，Submix/频段/分辨率、播放限制 | 接口用途；未录音或运行音频 |
| [Skeletal Mesh DI](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterfaceSkeletalMes-) / [实例数据](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/Niagara/FNDISkeletalMesh_InstanceData) | 返回5.8，源、LOD、蒙皮、当前帧及CPU数据标志 | 采样前提；未核对具体项目资产 |
| [LWC](https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-in-niagara-for-unreal-engine?application_version=5.6) | 5.6，Position、转换、Export Offset | 空间类型；未远原点运行验证 |
| [Component](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraComponent) / [Function Library](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraFunctionLibrary) | 返回5.8，参数/生命周期/Spawn/骨骼组件覆盖签名 | C++接口核对；无编译与UHT验证 |
| [Pool枚举](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/ENCPoolMethod) / [ReleaseToPool](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UFXSystemComponent/ReleaseToPool) | 返回5.8，公开选项与释放后的引用限制 | 拥有关系；未验证所有取消/剔除状态实现 |
| [NPC](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraParameterCollection) / [NDC概述](https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-data-channels-overview?application_version=5.6) | 分别返回5.8 / 5.6，共享状态与请求聚合 | 不支持“全面替代NPC”推论 |
| [NDC碰撞聚合](https://dev.epicgames.com/documentation/unreal-engine/combining-collision-effects-in-niagara-data-channels?application_version=5.6) / [写入API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataChannelLibrary/WriteToNiagaraDa-_1) | 分别5.6 / 返回5.8，Islands、监听、可见性与后续Tick Group | 第4.4数据合同；无跨版本可编译C++承诺 |
| [Export](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraDataInterfaceExport) / [同步读取](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/FScopedNiagaraDataSetGPUReadback) | 返回5.8，回调/GPU容量、阻塞与调试用途 | 有读取路径；无实时性或固定延迟保证 |
| [Beam](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-beam-effect-in-niagara-for-unreal-engine?application_version=5.6) / [Ribbon属性](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraRibbonRendererProperties) / [Facing](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/ENiagaraRibbonFacingMode) | 5.6教程及返回5.8 API，模板/模块、ID/顺序/朝向选段 | Ribbon与Beam关系；非“2D Ribbon”全版本考证 |
| [Blend Modes](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-blend-modes-in-unreal-engine?application_version=5.6) / [Sprite属性](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/UNiagaraSpriteRendererProperties) / [Depth](https://dev.epicgames.com/documentation/unreal-engine/depth-material-expressions-in-unreal-engine) | 分别5.6 / 返回5.8 / 返回5.8，混合、动态参数/SubUV、DepthFade | 材质通路；非全平台材质能力矩阵 |
