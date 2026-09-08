---
type: Reference
title: "第33章 Infected AI in The Last of Us"
description: "Game AI Pro 工业级精读：Infected AI in The Last of Us。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
tags:
  - game-ai
  - game-ai-pro
  - tactical-movement
  - combat-ai
  - steering
  - navigation
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 第33章 Infected AI in The Last of Us

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 33.  
> 原文作者 / 资源：[Infected AI in The Last of Us](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter33_Infected_AI_in_The_Last_of_Us.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

---

## 1. 核心设计哲学与系统目标

在《最后生还者》（*The Last of Us*）的系统架构中，被真菌（*Cordyceps*）寄生感染的人群（Infected）与普通人类幸存掠夺者（Hunters）在底层共享同一套 AI 运行时系统。架构设计的核心挑战在于：**如何在统一的底层代码底座之上，构建出两套行为模式、感知范式与战术体验截然不同的智能体。**

* **人类敌人（Hunters）**：展现出高度协同、言语沟通、手势掩护、寻求掩体以及冷酷自保的理性战术行为。
* **感染者（Infected）**：展现出混乱、异化、非理性、感官极端分化以及完全丧失自我保护本能的侵略机制。

```
       +-------------------------------------------------------------+
       |                  游戏统一底层 AI 运行时架构                    |
       |                (Unified AI Engine / Runtime)                |
       +-------------------------------------------------------------+
                                      |
            +-------------------------+-------------------------+
            |                                                   |
            v                                                   v
+-----------------------+                           +-----------------------+
|  掠夺者 (Hunters)     |                           |  感染者 (Infected)    |
+-----------------------+                           +-----------------------+
| - 协同搜索网络        |                           | - 混乱、异化本能      |
| - 掩体战术利用        |                           | - 极度敏锐/极端盲视   |
| - 语言与手势沟通      |                           | - 忽略自我保护与防守  |
| - 自保撤退机制        |                           | - 声音刺激强驱动响应  |
+-----------------------+                           +-----------------------+
```

### 1.1 负向智能抑制原则（"Not Stupid Before Smart"）
工业级 AI 研发的核心准则强调：**在赋予智能体高级智能假象之前，必须首先彻底消除愚蠢的行为痕迹。** 
智能体的拟真度并非取决于其决策树有多么深邃，而取决于其对环境与玩家输入的即时反馈、自然的物理动画过渡、高品质的音频表征，以及空间移动的稳定性。一旦 AI 出现卡入几何体、朝障碍物无休止奔跑或在关键威胁面前完全停滞，玩家对“智能体”的拟真感知就会瞬间瓦解。

### 1.2 模块化与快速迭代诉求
为应对游戏设计在后期制作中的高频变更（例如 Stalker 变种的立项到实现仅在发售前数月完成），AI 系统必须满足：
1. **决策与执行完全解耦**：上层意图与底层动作执行隔离。
2. **零残留实验隔离**：原型功能添加或移除时，不会对稳定系统的代码造成任何级联污染。

---

## 2. 感染者实体谱系与特征矩阵

根据真菌感染阶段的推移，感染者被解构为四类具备不同物理与感官特征的变体。系统通过对这些特征的量化分配，在同一实体类中演化出迥异的对抗策略：

| 感染类型 (*Type*) | 移动速度 (*Speed*) | 视觉能力 (*Vision*) | 出场稀有度 (*Rarity*) | 战斗模式与弱点表征 (*Combat & Weakness*) |
| :--- | :--- | :--- | :--- | :--- |
| **Runner（跑者）** | 极快 (*Fast*) | 有限视觉 (*Limited*) | 极高 (*Common*) | 缺乏协调的群体蜂拥冲锋；受声音刺激直接引导。 |
| **Stalker（潜行者）** | 极快 (*Fast*) | 有限视觉 (*Limited*) | 稀少 (*Uncommon*) | 避开开阔区域，潜伏于视线死角阴影中进行近距离伏击。 |
| **Clicker（循声者）** | 中等 (*Medium*) | 完全盲视 (*Blind*) | 稀少 (*Uncommon*) | 依靠异化听觉定位；致命近战狂暴攻击；免疫非武器徒手近战。 |
| **Bloater（巨无霸）** | 缓慢 (*Slow*) | 完全盲视 (*Blind*) | 极罕见 (*Rare*) | 厚重真菌装甲；近身绝对秒杀（免疫近战）；掷出产生毒雾的孢子囊体。 |

---

## 3. 感官系统：逻辑声音驱动模型与空间遮挡计算

感染者感知世界的核心通道是**听觉**。系统彻底摒弃了直接依赖游戏音频播放层反推感知的传统方案，建立了完全独立的**逻辑感知事件系统（Logical Sensory Event System）**。

```
[玩家/环境行为] (例如: 奔跑/投掷)
       |
       +---> 音频渲染管线 (Audio Rendering) ----> 驱动声卡 (玩家耳机听到声音)
       |
       +---> 逻辑感知系统 (Sensory System)   ----> 广播 LogicalSoundEvent (仅 AI 处理)
```

该架构将声效表现与决策判定解耦：音效设计师可随意调整混音资产与声像衰减曲线，而完全不破坏由关卡策划标定的 AI 听觉判定边界。

### 3.1 逻辑声音传播与感知范围修正公式

关卡中发生的物理或角色动作会向空间广播一个逻辑声音包。AI 实体能否接收并响应该逻辑声音，由基础事件广播半径、实体类型系数、行为状态缩放因子以及空间物理遮挡率共同决定。

#### 基础广播半径与玩家移动速度的关联
为避免传统潜行动作游戏中“只要蹲伏即可绝对隐蔽靠近敌人”的刻板套路，游戏将玩家的移动速度引入广播半径的动态计算中。玩家在感染者近身范围内移动越快，声波传播半径呈正比例甚至超线性放大：

$$R_{\text{broadcast}} = f(v_{\text{player}})$$

* 在距离感染者较远时，玩家可保持较快蹲伏移动；
* 在逼近感染者近战判定范围时，微小的速度增加都会使逻辑声音广播边界瞬间触达感染者。

#### 实体接收范围修正模型
对于特定角色类型 $T$ 处于特定行为状态 $B$ 时，其对声源事件 $E$ 的有效感知半球半径 $R_{\text{effective}}$ 计算如下：

$$R_{\text{effective}}(T, B) = R_{\text{broadcast}} \times S_{\text{type}}(T) \times M_{\text{behavior}}(B)$$

其中：
* $R_{\text{broadcast}}$ 为逻辑声音的基础扩散半径（米）；
* $S_{\text{type}}(T)$ 为实体类型灵敏度系数。设计指标中，感染者的基准听觉灵敏度为人类掠夺者的 6 倍：
  
  $$\frac{S_{\text{type}}(\text{Infected})}{S_{\text{type}}(\text{Hunter})} \approx 6.0$$

* $M_{\text{behavior}}(B)$ 为当前行为状态下的动态敏感度调节因子。当感染者处于未警觉（*Unaware/Wander*）状态时，降低其感知范围：
  
  $$M_{\text{behavior}}(\text{Unaware}) < 1.0$$
  
  该机制延长了玩家潜行观察与战术准备的时间窗口；一旦感染者进入警觉或骚动状态（*Agitated*），该系数提升至标准甚至更高水平。

#### 逻辑呼吸事件（Logical Breathing）
为解决“完全静止的玩家在感染者近距离贴脸时无法被发现”的不自然情况，系统引入了无声音频资源绑定的**隐式逻辑呼吸事件（Logical Breathing Sound）**。该事件以极小半径 $R_{\text{breathing}}$ 循环向外广播，使得完全盲视的 Clicker 能够在极近物理距离下仅凭呼吸扰动发现玩家。

### 3.2 射线投射遮挡评估模型（Raycasting Occlusion Model）

空间障碍物必须对声音的逻辑扩散施加阻断。系统采用空间物理射线检测计算声音遮挡率：

```
+----------------+                [掩体墙体]                +----------------+
|  逻辑声源点    |                  |墙|                    |    感染者 AI   |
| (Sound Source) *==================|体|-- - - - - - - - - >| (Infected AI)  |
+----------------+     物理射线检测 (Raycast)               +----------------+
                 | <------------- 阻断 / 衰减 ------------> |
```

1. 当逻辑事件在空间点 $\mathbf{P}_{\text{sound}}$ 触发时，引擎在空间中筛选出距离小于 $R_{\text{effective}}$ 的候选 AI 集合。
2. 对集合内的每个 AI 实体，从其头部空间点 $\mathbf{P}_{\text{head}}$ 向声源点 $\mathbf{P}_{\text{sound}}$ 投射多条物理探测射线（Raycasts）。
3. 判定声线是否与碰撞几何体（Collision Meshes）产生交点。若光线被完全遮蔽且无回折传播路径，则判定该逻辑声音被物理隔离（Fully Occluded），事件被静默抛弃；若部分遮挡，则基于穿透介质属性施加二次衰减。

### 3.3 意图传达（Intent Communication）与被废弃的回声定位方案

AI 系统的行为判定必须清晰地传达给玩家。如果玩家无法推测 AI 为何做出反应，再精确的感知算法也会被视为系统缺陷。

#### 激惹过渡态（Agitated State）
当感染者开始察觉微弱声音扰动时，不会立即触发致命冲锋，而是首先切换至“激惹状态”，通过颤动、头部偏转及发出威胁性低吼等动画和音效向玩家示警。这为玩家提供了调整走位或停止移动的交互窗口。

#### 回声定位原型设计及其退役溯源
开发团队曾为盲视的 Clicker 研发基于蝙蝠声呐机制的局部空间认知模型：
* **运作逻辑**：Clicker 周期性发出犬吠状高频鸣叫（Barking），在叫声发出的瞬间瞬时开启全向瞬态视觉，获取当前锥形空间范围内的障碍物与威胁快照，用以构建局部环境心智模型。
* **废弃动因**：该系统造成了不可调和的心理模型冲突——在玩家认知中，“Clicker 是完全盲视的怪物”，但系统底层的瞬时快照使得玩家感觉自己“被一个瞎子看见了”。由于无法在没有穿帮感的前提下（如使用破坏真实沉浸感的全屏环境形变扭曲特效）向玩家传达这种声呐感知机制，该方案最终被移出代码库，仅保留叫声作为装饰性氛围音频。

### 3.4 声东击西机制与道具平衡性控制

感染者由于大脑高度真菌化，丧失了基于上下文的高级因果归纳推理能力：
* **人类掠夺者（Hunters）**：观察到手电筒光束或被砖块击中时，会立刻根据光束矢量或投掷轨迹反向推导潜伏者的物理方位。
* **感染者（Infected）**：感知系统完全被刺激本身捕获。投掷砖块或瓶子落地的撞击点被判定为唯一的初级刺激源，感染者会盲目扑向撞击声发生点。

#### 道具交互的边界控制
* **燃烧瓶（Molotov Cocktails）**：由于声音吸引机制，投掷燃烧瓶产生的破裂声容易将整片区域内的感染者一次性引向火焰自焚，导致战斗难度急剧崩塌。AI 系统与战斗系统联合增加了**单次燃烧实体吞噬上限阈值**，超额敌人会对危险区域产生被动回避，以维持资源调配的战术张力。
* **烟雾弹（Smoke Bombs）**：烟雾弹本应仅阻断视线，但对盲视且极度依赖听觉的 Clicker 而言理应无效。为了游戏机制的连贯性与趣味性，系统在烟雾区域内植入了**声学阻断介质属性**，感染者进入烟雾内部会同时陷入致盲与致聋状态，为玩家近身潜行暗杀提供环境掩护。

---

## 4. 架构拓扑：技能（Skills）与行为（Behaviors）的解耦系统

系统摒弃了单一单体状态机架构，采用了严格分层的 **技能-行为执行拓扑（Skills and Behaviors Architecture）**。

```
+-------------------------------------------------------------------------+
| 高层决策域：技能系统 (Skills Hierarchy)                                 |
| - 职责：解决 "What to do" (我要干什么: 追杀? 搜寻? 伏击?)               |
| - 特性：特定实体特异性高度绑定，各实体间互不复用                         |
+-------------------------------------------------------------------------+
                                    |
                           调用 / 轮询 / 状态中断
                                    |
                                    v
+-------------------------------------------------------------------------+
| 低层执行域：行为系统 (Behaviors Hierarchy)                               |
| - 职责：解决 "How to do it" (如何实现: 避障寻路? 空间网格离散搜索?)     |
| - 特性：与具体角色类型完全解耦，全 AI 实体高度泛化复用                   |
+-------------------------------------------------------------------------+
```

### 4.1 职责边界划分

* **技能层（Skills）**：
  * 基于智能体动机、能力集、黑板数据（Blackboard Data）及外界感知信息进行宏观判定。
  * 核心职能：裁决当前战术目标（“我应该进入追杀状态、潜伏状态，还是探索最后已知位置？”）以及目标空间选点（“当前战术位置何处最优？”）。
  * 一旦裁决完成，技能激活下层特定的行为状态机并挂起等待行为执行回执（`Success` / `Failure`）。

* **行为层（Behaviors）**：
  * 封装跨实体共享的基础执行能力，屏蔽底层物理、动画及寻路细节。
  * 经典代表为 `move-to` 行为：负责针对目标坐标解算导航网格（NavMesh）走廊、驱动避障算法（Steering / Local Obstacle Avoidance）、混合根骨骼位移动画（Root Motion Extraction），并向父级技能报告到达或阻塞状态。同一 `move-to` 模块可根据输入参数自适应表现为掠夺者的屈身潜行走位，亦可表现为感染者的无规则狂暴冲锋。

### 4.2 纯数据驱动实体设计（Data-Driven Character System）

为了消除工程实现中的硬编码条件分支，所有感染者变体在底层**共享唯一的 C++ 实体类**。

```
[不可维护的硬编码设计 (Anti-pattern)]
if (Character->GetType() == CharacterType::Clicker) {
    SetHearingScale(6.0f);
    SetBlind(true);
}

[规范的数据驱动设计 (Data-Driven Pattern)]
if (Character->GetVisionType() == VisionType::Blind) { ... }
float effectiveRadius = baseRadius * Character->GetPerceptionParams().HearingMultiplier;
```

* **严格禁止类型判定**：C++ 代码库严禁出现针对 `Runner`、`Clicker` 等具体实体枚举的硬编码分支（如 `switch(type)`）。所有判定必须下沉为原子属性查询（例如：查询 `VisionType`、`MeleeVulnerabilityProfile` 或 `HearingSensitivityMultiplier`）。
* **动态配置与难度扩展**：不同难易度设定（Easy/Normal/Hard/Survivor）通过数据资产表中不同的刺激阈值矩阵调节，避免通过增减同屏怪物数量破坏内存与性能预算。

---

## 5. 优先级技能状态机调度与变体装配

AI 实体的技能调度器在主循环中维护一个按优先级从高到低排序的技能列表。

```
                    [ 技能调度器评估周期开始 ]
                                |
                                v
                   [ 读取优先级技能列表 (Table 33.2) ]
                                |
             +----------------->+
             |                  |
             |                  v
             |        [ 评估技能条件是否有效? ]
             |             /          \
             |          (否)          (是)
             |           /              \
             |          /                v
             +-- [ 迭代至下一技能 ]    [ 抢占/启动该技能 ]
                                         |
                                         v
                               [ 终止后续低优先级检测 ]
```

* **评估与抢占原则**：Tick 循环从最高优先级向后轮询。一旦某个技能的准入前置条件（Preconditions）满足，立刻打断当前运行的低优先级技能（Interrupt），接管实体控制权，并截断本次轮询。
* **兜底保底约束（Fallback Guarantee）**：列表末尾必须存在一个无条件触发的保底技能（通常为 `Wander`）。AI 绝不允许落入“无活动技能”的真空状态，否则会导致智能体逻辑失控、原地冻结或撞墙。

### 5.1 感染者变体技能优先级矩阵

根据文献实录，四类感染者的优先级技能管线映射如下：

| 优先级次序 | Runner（跑者） | Stalker（潜行者） | Clicker（循声者） | Bloater（巨无霸） |
| :---: | :--- | :--- | :--- | :--- |
| **Top 1** | On-fire（着火失控） | On-fire（着火失控） | On-fire（着火失控） | On-fire（着火失控） |
| **Top 2** | Chase（追杀冲锋） | Ambush（阴影伏击） | Chase（追杀冲锋） | Throw（抛掷孢子） |
| **Top 3** | Search（搜寻排查） | Sleep（休眠潜伏） | Search（搜寻排查） | Chase（追杀冲锋） |
| **Top 4** | Follow（跟随协同） | Wander（巡逻游荡） | Follow（跟随协同） | Search（搜寻排查） |
| **Top 5** | Sleep（休眠伪装） | *(末端已保底)* | Sleep（休眠伪装） | Follow（跟随协同） |
| **Top 6** | Wander（漫游保底） | | Wander（漫游保底） | Sleep（休眠伪装） |
| **Top 7** | *(末端已保底)* | | *(末端已保底)* | Wander（漫游保底） |

*注：`Ambush` 为 Stalker 独占特化技能；`Throw` 为 Bloater 独占特化技能。在整个底层行为库中，仅有 `infected-canvass` 与 `infected-listen` 属于感染者专用行为，其余下层行为全量与人类掠夺者复用。*

### 5.2 架构调试界面（Debugging Architecture）
为降低复合状态机对黑盒测试带来的不确定性，框架支持在运行时以游戏手柄替代上层技能层直接驱动底层行为。
调试菜单允许测试人员绕过复杂的技能准入逻辑：
* 强制赋予角色特定精神仪态（Demeanors，如极度放松、极度狂躁）；
* 屏幕游标直接下发坐标，强制执行底层行为（如 `move-to` 或特定排查动作）；
* 提供单帧状态回滚、空间瞬移（Teleport）与历史指令重放机制，用于逐帧调试骨骼动画与转向曲线的过渡瑕疵。

---

## 6. 空间搜索（Search Skill）与排查行为（Infected-Canvass Behavior）

当感染者在追逐过程中丢失目标视线时，`Search Skill` 激活。与人类敌人有条不紊、交替掩护的系统性排查不同，感染者的搜索需要呈现出**狂躁、非理智、充满压迫感但又缺乏严谨战术规划**的生物本能。

### 6.1 基于导航网格视线阻断的搜索图拓扑构建（Search Graph Generation）

系统利用导航多边形（NavMesh Polygons）之间的可见性差异提取潜在藏匿点：
* **藏匿点定义（Hiding Places）**：在导航网格中，无法被其他特定多边形直接通过视线投射穿透的多边形区域，被标记为藏匿拓扑节点。
* **广度优先搜索图生算法（BFS Visibility Graph Extraction）**：
  1. 提取目标最后已知位置（Last Known Location）所在的多边形中心点，将其作为根搜索点 $S_0$ 压入图谱。
  2. 沿连通的邻接导航多边形向外进行广度优先遍历（Breadth-First Traversal）。
  3. 针对每个遍历的多边形，自当前测试中心向父级搜索点进行视线追踪。当检测到与上一搜索点之间的视线发生物理阻断（Line of Sight is broken）时，取阻断前最后一个保持通视的多边形中心点，生成新的搜索节点 $S_n$。
  4. 重复此拓扑扩散，直至达到搜索半球硬性边界或生成的搜索点数量达到系统预设上限 $N_{\max}$。

最终生成的搜索图保障了**图内节点之间具备局部视线连通性，且对周遭所有死角多边形形成几何视野覆盖**。系统引入玩家位置预测模型（Player Location Prediction），计算随时间扩散的目标分布概率波，使感染者优先向高概率扩散节点聚集，对保守型蹲伏玩家形成持续挤压。

### 6.2 感染者局部勘测行为：Infected-Canvass 算法

到达宏观搜索点后，为驱动感染者在局部执行无序、狂躁但又高效的环境覆盖，系统设计了 `infected-canvass` 行为。该算法完全避免了显式的航向角计算（Steering Vector Calculation），而是**基于动画动作集的后向预测评估机制**。

```
+-----------------------------------------------------------------------------------+
|                        Infected-Canvass 算法流水线流程                             |
+-----------------------------------------------------------------------------------+
| 步骤 1: 投影逻辑网格                                                              |
|        以感染者当前坐标为中心，铺设半径为 R_canvass 的二维离散网格                  |
| 步骤 2: 空间掩模剔除                                                              |
|        计算几何障碍物与圆周外单元格，直接标记为 Seen(已探索)，留出待检单元格集合 U    |
| 步骤 3: 候选动画集姿态预测                                                        |
|        上层技能注入候选动画集合 A = {a_1, a_2, ...}，解算每段动画位移后的 (P_end, Theta) |
| 步骤 4: 感知楔形区采样统计                                                        |
|        在预测位姿投射张角为 Theta_wedge 的扇形探测区，统计覆盖的未探索网格数量 Count(U)     |
| 步骤 5: 效用评分与动画决选                                                        |
|        基于未探索格数与近期使用惩罚计算效用值，最高分动画直接进入驱动播放          |
| 步骤 6: 状态写入与拓扑平移                                                        |
|        动画播放结束，将楔形区单元格标记为 Seen，以新坐标为中心迭代至步骤 3          |
+-----------------------------------------------------------------------------------+
```

#### 算法数学与几何步骤详析

1. **局部离散网格投影（Local Logical Grid Projection）**：
   以智能体当前空间坐标 $\mathbf{P}_0 = (x_0, y_0)$ 为原点，在其足底平面铺设一个由边长为 $d$ 的离散单元格组成的二维逻辑网格矩阵 $\mathcal{M}$，其覆盖范围受勘测半径 $R_{\text{canvass}}$ 约束：
   
   $$\mathcal{M} = \{ C_{i,j} \mid \text{dist}(\mathbf{P}_0, C_{i,j}) \le R_{\text{canvass}} \}$$

2. **障碍物与视锥剔除掩模（Spatial Masking）**：
   检测静态几何阻挡与超出 $R_{\text{canvass}}$ 的单元格，将其状态初始化标记为 $\text{Seen}$（已检查），其余网格初始化为 $\text{Unseen}$（未检查）。待检集合表示为：
   
   $$\mathcal{U} = \{ C_{i,j} \in \mathcal{M} \mid \text{State}(C_{i,j}) = \text{Unseen} \}$$

3. **候选动画集终止姿态预测（End-State Prediction）**：
   上层技能向行为注入一组各向异性的搜寻根骨骼动画切片 $\mathcal{A} = \{ a_1, a_2, \dots, a_k \}$。针对每一个候选动画 $a_m$，根据其根骨骼位移轨迹解算角色在播放结束时的末端空间坐标与朝向角：
   
   $$\mathbf{X}_{\text{end}}(a_m) = (\mathbf{P}_{\text{end}}^m, \theta_{\text{end}}^m)$$

4. **感知楔形区（Sensory Wedge）空间积分**：
   在每一个候选末端位姿 $\mathbf{X}_{\text{end}}(a_m)$ 前方构建一个虚拟感知楔形区域 $\mathcal{W}(\mathbf{X}_{\text{end}}^m)$。该楔形由预设张角 $\Phi$ 和前向距离 $L$ 界定。统计该楔形几何范围内部落入的未检查单元格数目：
   
   $$N_{\text{unseen}}(a_m) = \sum_{C_{i,j} \in \mathcal{W}} \mathbb{I}\left( \text{State}(C_{i,j}) == \text{Unseen} \right)$$
   
   *参数调优意义*：感知楔形的大小与物理视线无需一致。增大楔形张角会导致角色产生宏观大跨度的快速扫描动作；缩小楔形张角则迫使角色在局部细碎移动，呈现高密度的彻底搜刮。

5. **效用评分公式与决策决选（Utility Evaluation & Selection）**：
   为避免角色在两个高覆盖率动画之间陷入来回抖动的死循环（Oscillation），算法引入了近期使用抑制惩罚项（Recency Penalty）：
   
   $$U(a_m) = N_{\text{unseen}}(a_m) - \lambda \cdot \text{UsageHistory}(a_m)$$
   
   其中 $\text{UsageHistory}(a_m)$ 为动画 $a_m$ 在前 $H$ 帧内被执行的频次衰减积分，$\lambda$ 为惩罚权重因子。
   
   最终选定的动作由最大效用函数确定：
   
   $$a^* = \arg\max_{a_m \in \mathcal{A}} U(a_m)$$

6. **状态迭代与闭环推进**：
   驱动角色播放动画 $a^*$ 并产生实际位移。动画执行结束时，当前物理楔形所覆盖的单元格全部被覆写标记为 $\text{Seen}$：
   
   $$\forall C \in \mathcal{W}(\mathbf{X}_{\text{end}}^*), \quad \text{State}(C) \leftarrow \text{Seen}$$
   
   系统随后以当前最新的世界坐标为锚点，重构或平移网格局部空间，循环执行上述选点评估过程。

#### 方案工程优势评估
该算法不包含求解角色“应该往哪个矢量方向旋转多少度”的高等微积分方程，**角色的所有运动轨迹天然受限于动画师提供的骨骼动画资源库本身**。
1. **彻底规避足底滑动与物理穿帮**：运动矢量完全来自根骨骼动画偏移，消除了物理推力与腿部动画不匹配导致的滑步现象。
2. **极高的动画表现张力**：动画师可以制作扭曲、跌撞、趔趄甚至躯体抽搐的搜寻动作，而无需担心转向控制器无法对齐航向角。
3. **低算力消耗与自适应几何适应**：通过离散位图遮蔽碰撞体，算法在任意不规则多边形拓扑空间（散落瓦砾、倾覆家具的室内）中均能稳健运行，展现出混乱但符合直觉的高压感知特性。

---

## 7. 架构全景总结与设计启示

《最后生还者》感染者 AI 系统在工业落地层面的核心启示可归纳为：

1. **认知分离优于物理模拟**：听觉系统通过逻辑事件剥离真实声音，在保证音效设计自由度的同时，实现了精准的感知边界控制。
2. **负向抑制优于正向堆砌**：消灭角色的愚蠢行为（穿墙、卡死、无逻辑自杀）比赋予其深邃的战术思维更能建立智能假象。
3. **数据抽象优于继承发散**：统一代码底座与纯数据驱动的属性矩阵，能够支撑极晚期的设计变更，赋予系统高度的扩展弹性。
4. **动画驱动推理优于运动学反求**：在非理性生物的近距离行为解算中，以动画终态效用引导空间搜寻，兼顾了顶级的美术视觉质感与底层的逻辑确定性。

---

本技术文档基于顽皮狗（Naughty Dog）在《Game AI Pro 2: More Collected Wisdom of Game AI Professionals》第 33 章中的工程公开资料，深度解构《最后生还者》中感染者（Infected）系统的顶层决策逻辑（High-Level Decision Logic）、底层行为构建块（Low-Level Behavioral Building Blocks）、空间搜索与感知机制（Spatial Search and Sensory Mechanisms）以及工程模式落地。

---

## 1. 架构总览：分层技能与模块化行为系统

顽皮狗为感染者构建了一套两层解耦的 AI 运行时架构：
1. **技能层（Skill Layer）**：定义宏观态势感知与战术目标（如追踪、包抄、跟随、漫游、休眠）。技能层维护高层状态转移，屏蔽物理交互与底层导航细节。
2. **行为层（Behavior Layer）**：由轻量级、模块化且可复用的执行单元组成（如 `move-to`、`surprise`、`infected-canvass`）。行为层直连导航系统（Navigation System）、动画系统（Animation System）与物理碰撞查询。

```
+-------------------------------------------------------------------------+
|                              技能层 (Skill Layer)                         |
|   +--------------+   +--------------+   +--------------+   +--------+   |
|   | Chase Skill  |   | Follow Skill |   | Ambush Skill |   |  ...   |   |
|   +-------+------+   +-------+------+   +-------+------+   +---+----+   |
+-----------|------------------|------------------|--------------|--------+
            |                  |                  |              |
            +-------------+    |    +-------------+              |
                          v    v    v                            v
+-------------------------------------------------------------------------+
|                            行为层 (Behavior Layer)                       |
|   +-------------------+  +-------------------+  +-------------------+   |
|   | surprise behavior |  |  move-to behavior |  |  infected-canvass |   |
|   +---------+---------+  +---------+---------+  +---------+---------+   |
+-------------|----------------------|----------------------|-------------+
              v                      v                      v
+-----------------------+  +-------------------+  +-----------------------+
| 骨骼动画控制器 (Root)  |  | 导航系统(NavMesh) |  | 空间感知 / 触觉避障   |
+-----------------------+  +-------------------+  +-----------------------+
```

该解耦设计的核心工程收益在于：
- **逻辑收敛性**：高层技能仅需向行为层下发上下文参数（如目标点、搜索半径、动画组集合），并侦听行为执行返回的生命周期状态（`Success`、`Failure`、`Running`），消除了技能代码内的分支膨胀。
- **资产重用最大化**：相同的行为构件（例如 `infected-canvass`）在不同参数配置与动画集驱动下，能够兼顾“小范围警戒排查”、“追逐间隙的重新定向（Reorient）”以及“着火受击挣扎（On-Fire）”等多重差异化表现。

---

## 2. 核心技能拓扑与状态决策流（Skill Topologies）

### 2.1 追逐技能（Chase Skill）
追踪技能赋予感染者最纯粹的侵略本能：一经感知目标，立即以最高速度寻路接近并实施近战压制。其执行管线划分为三个严格解耦的时序阶段：

```
[感知刺激 (Stimulus Detected)]
             │
             ▼
  阶段一：触发惊觉 (Surprise Behavior)
  ├── 挂起位移计算
  ├── 播放惊觉动画 (向目标朝向对其转向)
  └── 触发音频警报 (Scream Alert)，通知玩家暴露
             │
             ▼
  阶段二：空间机动 (Move-To Behavior)
  ├── 绑定目标 Entity / 位置
  ├── 连续动态重规划路径 (Continuous Path Refinement)
  └── 技能层仅轮询判定 [到达/丢焦/路径阻塞]
             │
             ▼ 追逐过程中周期性评估
  阶段三：瞬态重定向 (Reorient / Canvass Interruption)
  ├── 短时间切入 infected-canvass 行为
  ├── 施加专属局部抖动动画 (Frantic Search Animations)
  └── 战术意图：打破机械感，赋予玩家潜行脱困窗口
```

#### 决策伪代码实现（Chase Skill Logic）
```cpp
enum class SkillStatus { Running, Success, Failure };

class ChaseSkill : public Skill {
private:
    EntityID targetEntity;
    SurpriseBehavior surpriseBehavior;
    MoveToBehavior moveToBehavior;
    InfectedCanvassBehavior canvassBehavior;
    
    float timeSinceLastReorient = 0.0f;
    const float REORIENT_INTERVAL = 4.5f;
    const float REORIENT_DURATION = 0.8f;
    bool isReorienting = false;

public:
    virtual void OnEnter(Blackboard& bb) override {
        targetEntity = bb.Get<EntityID>("TargetStimulusSource");
        surpriseBehavior.SetOrientationTarget(targetEntity);
        surpriseBehavior.Execute();
    }

    virtual SkillStatus Update(float dt, Blackboard& bb) override {
        // 1. 惊觉动画播放阶段（前摇）
        if (surpriseBehavior.IsRunning()) {
            return SkillStatus::Running;
        }

        // 2. 战术停顿/再定向阶段 (Reorient)
        if (isReorienting) {
            if (canvassBehavior.GetElapsedTime() >= REORIENT_DURATION) {
                canvassBehavior.Terminate();
                isReorienting = false;
                timeSinceLastReorient = 0.0f;
                moveToBehavior.Resume();
            }
            return SkillStatus::Running;
        }

        // 3. 驱动寻路机动阶段
        timeSinceLastReorient += dt;
        if (timeSinceLastReorient >= REORIENT_INTERVAL && CanSafelyPauseChase()) {
            moveToBehavior.Pause();
            canvassBehavior.SetRadius(1.5f);
            canvassBehavior.SetAnimationSet(AnimSetType::FranticReacquire);
            canvassBehavior.Execute();
            isReorienting = true;
            return SkillStatus::Running;
        }

        // 4. 执行常规追踪行为
        return moveToBehavior.UpdateMove(targetEntity);
    }
};
```

---

### 2.2 跟随技能（Follow Skill）：去中心化涌现协调（Emergent Flocking）
在传统群体 AI 中，多智能体协同常依托黑板系统的显式消息广播（Message Board Broadcast）或指挥官系统（Squad Coordinator）。顽皮狗针对感染者的无心智生物设定，设计了无直接信息共享的分布式级联响应协议：

*   **零知识共享（Zero Knowledge Sharing）**：跟随者**不知道**领头感染者因何被激怒，也**未获取**玩家的任何 Transform、速率向量或遮挡状态。
*   **本能冲动（Compulsion to Follow）**：当感染者 $A$ 启动 `Chase Skill` 时，$A$ 进入奔跑冲刺态。周围未直接探测到玩家的感染者 $B_1, B_2$ 的感知系统捕捉到 $A$ 的激化运动，触发 `Follow Skill`。
*   **级联式猎捕收敛**：从世界表现看，形成集群冲锋；在数学上，追踪前沿的到达时延保证了原发告警感染者必然作为先锋最先抵达：
    $$t_{\text{arrival}}(A) < t_{\text{arrival}}(B_i) = t_{\text{detect}}(B_i \to A) + t_{\text{traverse}}(B_i \to \text{Player})$$

```
+---------------+     Sensory Event (Noise/Sight)     +---------------+
| Player Entity | ──────────────────────────────────> |  Infected A   | (Chase)
+---------------+                                     +-------+-------+
                                                              │ Physical Velocity
                                                              │ & Visual Agitation
                                                              ▼
                                                      +---------------+
                                                      |  Infected B   | (Follow)
                                                      +-------+-------+
                                                              │
                                                              ▼
                                                      +---------------+
                                                      |  Infected C   | (Follow)
                                                      +---------------+
```

---

### 2.3 潜伏潜伏技能（Ambush Skill）：基于隐蔽度的空间推理
潜伏者（Stalker）特化了这一技能，直接替换掉常规的 `Chase Skill`。

#### 战术位置评估系统（Dynamic Tactical Position Evaluation）
依据 Straatman et al. [2006] 的战术位置评分模型，潜伏技能依赖对周围环境离散掩体点（Cover Points）的高频多目标效用函数评估：

$$U(c) = w_d \cdot f_d(c, p) + w_v \cdot f_v(c, p) + w_a \cdot f_a(c, p)$$

*   $c \in \mathcal{C}$：场景内掩体候选点集合。
*   $p$：玩家估算空间坐标。
*   $f_d(c, p)$：距离适应度函数，使用高斯衰减保持在最佳伏击距离环内：
    $$f_d(c, p) = \exp\left( -\frac{(\|c - p\| - d_{\text{ideal}})^2}{2\sigma_d^2} \right)$$
*   $f_v(c, p)$：玩家视线遮挡度量（Visibility Penalty）：
    $$f_v(c, p) = 1.0 - \text{RaycastClear}(c + h_{\text{eye}}, p + h_{\text{eye}})$$
    强惩罚项，确保潜伏者隐蔽在玩家视野盲区或实体几何体后。
*   $f_a(c, p)$：突袭路径可达性（Approach Viability），度量掩体点沿导航网格发起突袭的几何拐角复杂度与时间成本。

```
              [ 掩体候选评估 (Tactical Cover Evaluation) ]
                                   │
                                   ▼
          +──────────────────────────────────────────────────+
          │  状态 1: 伏击等待 (Lay in Wait)                  │
          │  - 潜伏于掩体后，隐匿自身，扰乱玩家计数          │
          +────────────────────────+─────────────────────────+
                                   │
                玩家进入突袭距离阈值 (Trigger Radius)
                                   │
                                   ▼
          +──────────────────────────────────────────────────+
          │  状态 2: 爆发冲锋 (Hit-and-Run Attack)            │
          │  - 脱离掩体，以最短时间窗口贴脸近战              │
          +────────────────────────+─────────────────────────+
                                   │
                   攻击完成 OR 受到高额伤害反击
                                   │
                                   ▼
          +──────────────────────────────────────────────────+
          │  状态 3: 掩体撤退 (Retreat to Cover)             │
          │  - 评估背向掩体点，迅速遁入黑暗，打破视线交互   │
          +──────────────────────────────────────────────────+
```

---

### 2.4 投掷技能（Throw Skill）：巨无霸（Bloater）的弹道投射与退化控制
巨无霸通过剥落机体装甲上的孢子囊（Mycelium Growths）向玩家投掷。

#### 前置拦截预判点求解（Lead Target Prediction）
系统计算一阶预判落点 $P_{\text{impact}}$：
$$P_{\text{pred}} = P_{\text{player}} + \vec{V}_{\text{player}} \cdot t_{\text{flight}}$$
考虑重力加速度 $\vec{g} = (0, -9.8, 0)^T$ 与初始抛射仰角，飞行耗时 $t_{\text{flight}}$ 通过解三次方程或牛顿迭代法求得。投掷在着陆点生成孢子气雾，施加局部阻尼力场，强制削减玩家线性速率。

#### 护甲绑定机制与耐久度退化设计
*   **装甲板槽位拓扑**：孢子囊绑定在多块物理护甲片（Armor Plates $A_1, A_2, \dots, A_n$）上。
*   **动作源动态退化**：当板块 $A_i$ 被破坏，动画提取管线从可用存活板块映射集中进行多路复用：
    $$\mathcal{A}_{\text{valid}} = \{ A_k \in \mathcal{A} \mid \text{Health}(A_k) > 0 \}$$
*   **挑战下限保障（Combat Pressure Invariant）**：只要巨无霸存活（即至少存活 1 处攻击点或核心生命大于零），其投掷弹药量设为 $\infty$，从而避免后期战斗因机制耗尽导致难度骤降。

---

### 2.5 着火技能（On-Fire Skill）：涌现式受击扰动反应
不同于传统状态机直接锁定入固定死亡位移，着火状态被建模为一个受控的自主机动反应：
*   **行为重用**：以高刷新率调用 `infected-canvass` 行为，但限定在微小搜索半径 $R_{\text{flail}} \in [0.8\text{m}, 1.5\text{m}]$。
*   **碰撞容错**：依托 Canvass 行为内置的几何避障能力，即使角色陷入极度疯狂的随机晃荡与肢体抽搐（Flailing Animations），也能确保完全不会发生穿模或嵌入静态碰撞体（Wall Interpenetration）。
*   **微动作混同（Stochastic Micro-Actions）**：将短步长闪避、肢体乱拍、高频朝向切换结合，呈现出极度不可控却在物理与环境逻辑上自洽的濒死状态。

---

### 2.6 漫游技能（Wander Skill）：空间探索与覆写机制

```
                      +-----------------------------+
                      |     Wander Skill (保底优先级) |
                      +--------------+--------------+
                                     │
           ┌─────────────────────────┴─────────────────────────┐
           ▼                                                   ▼
+---------------------+                             +---------------------+
| 固定样条漫游        |                             | 导航网格随机漫游    |
| (Fixed Route Spline)|                             | (NavMesh Poly Walk) |
+----------+----------+                             +----------+----------+
           │                                                   │
           ├─ 编辑器摆放 Hermite 样条                          ├─ 维护已访问历史环形缓冲
           ├─ 绑定空间交互锚点 (Smart Points)                  │  V = {PolyID_1, ..., PolyID_k}
           │  (如驻足倾听、搜寻废墟残骸)                       ├─ 随机拓扑搜寻候选多边形
           └─ 用于遭遇战前初始潜行巡逻                         │  Poly_next ∉ V
                                                               └─ 用于脱战后大范围动态覆写
```

*   **样条巡逻（Fixed Spline Patrol）**：关卡设计师使用三次 Hermite 样条曲线预设轨迹，结合上下文动作标记点（Action Markers），构造具备高度可预测性的循环步态，供玩家研判潜行时机。
*   **非重复随机多边形漫步（Non-Repeating Poly Random Walk）**：
    维护访问历史队列 $\mathcal{H}$（大小容量为 $M$）：
    $$\text{TargetPoly} \in \mathcal{N}(\text{CurrentPoly}) \setminus \mathcal{H}$$
    如果局部连通分量均处于已访问状态，则重置历史并执行长距离随机投影，强迫感染者覆盖整个开阔战术区域。

---

### 2.7 休眠技能（Sleep Skill）：潜行哨兵与阶段唤醒阶梯
休眠技能专用于关卡阻塞点（Choke Points）的“哨兵型”配置，提供精细分级的环境威胁感知：

```
[无干扰] ───────────────────────────────────────────> 极低感知敏锐度 (Low Sensitivity)
  │
  ├─ 微扰动 (玩家近距疾走 / 视域近边缘掠过)
  │   └── 触发肢体微动 (Stir Animation)，给予玩家警示回退窗口
  │
  ├─ 中远距离高分贝瞬时噪声 (Impact Noise)
  │   └── 唤醒 -> 挂载 infected-canvass 在狭窄半径就地排查 -> 目标丢失 -> 复归休眠
  │
  └─ 持续高频扰动 OR 超限高分贝近距离爆发
      └── 暴怒唤醒 (Enrage) -> 无缝升格至 Chase Skill
          └── 追逐结束退出机制：进入 Wander 巡逻，永不回到初始 Sleep 状态
```

---

## 3. 底层空间搜索机制：Canvas 行为深度剖析（`infected-canvass`）

`infected-canvass` 是感染者体系中执行密集空间搜索（Dense Spatial Search）的核心算法单元。它摒弃了算力开销庞大的全局网格全覆盖规划（如完全遍历路径生成），采用一种**基于局部扰动与动画驱动的有机搜索启发式模型**。

```
                  +-----------------------------------+
                  |   Infected-Canvass 执行启动       |
                  +-----------------+-----------------+
                                    │
                                    ▼
                  +-----------------------------------+
                  |   空间尺度判定 (Spatial Sizing)   |
                  +-----------------+-----------------+
                                    │
                  ├─────────────────┴─────────────────┐
                  ▼                                   ▼
        [小范围区域 (Small Area)]           [开阔大范围 (Large Area)]
                  │                                   │
                  ▼                                   ▼
        +-------------------+               +-------------------+
        | Near Set 动画栈   |               | Far Set 动画栈    |
        +---------+---------+               +---------+---------+
                  │                                   │
                  ├─ 高频原地转向 (Random Yaw)        ├─ 混合中长距离冲刺 (Bursts)
                  └─ 近身快速头部扫描视觉呈现         └─ 大步幅折线穿越与短促悬停
                  │                                   │
                  └─────────────────┬─────────────────┘
                                    │
                                    ▼
                  +-----------------------------------+
                  | 导航网格安全巡回 (NavMesh Steer)  |
                  | 随机非穷竭性多边形推演            |
                  +-----------------------------------+
```

### 3.1 动力学与动画切分双轨制
*   **近程集合（Near Set Animations）**：
    约束在角位移极高、线位移极低的状态。采样高频伪随机序列驱动角色执行多向偏航（Yaw Turning），通过头部注视骨骼（Look-At IK）的随机角速度偏移，营造视觉上的有机神经质感。
*   **远程集合（Far Set Animations）**：
    引入冲刺爆发（Sprint Bursts）。算法在搜索区域凸包内部动态生成短期瞬态航路点（Waypoints），交替执行长突进与短悬停，使得大空间搜索具备非线性加速度变化。

### 3.2 空间状态机与数学抽象
Canvass 行为在局部搜索平面 $\mathbb{R}^2$ 上定义为随机漂移-扩散过程（Drift-Diffusion Process）。在任意时刻 $t$，下一代航路点 $X_{k+1}$ 生成规则如下：

$$X_{k+1} = \Pi_{\text{NavMesh}}\left( X_k + R(\theta) \cdot \begin{pmatrix} d \\ 0 \end{pmatrix} \right)$$

其中：
- $R(\theta)$ 为随机偏航旋转矩阵，$\theta \sim \mathcal{U}(-\theta_{\max}, \theta_{\max})$。
- 步长 $d$ 依据动画组类型从截断分布中抽取：
  $$d \sim \begin{cases} 
  \text{Rayleigh}(\sigma_{\text{near}}), & \text{Near Set} \\ 
  \text{Gamma}(\alpha_{\text{far}}, \beta_{\text{far}}), & \text{Far Set} 
  \end{cases}$$
- $\Pi_{\text{NavMesh}}(x)$ 为将位置点 $x$ 正交投影回合法导航网格多边形的映射算子，保证无碰撞越界。

---

## 4. 感染者技能与底层行为依赖矩阵

下表展示了高层技能与底层行为之间的调用映射关系与执行约束：

| 技能名称 (Skill) | 依赖的核心底层行为 (Behaviors Used) | 空间范围 / 约束参数 | 核心动画集合 (Anim Sets) | 战术意图与状态回退机制 |
| :--- | :--- | :--- | :--- | :--- |
| **Chase Skill** | `surprise`, `move-to`, `infected-canvass` | 动态全图寻路；局部 Canvass 半径 $\le 2.0\text{m}$ | 惊觉转向、极速冲刺、Frantic Reacquire 抽搐重定向 | 追杀玩家；通过瞬态局部搜索给予玩家脱战窗口。 |
| **Follow Skill** | `move-to` | 跟随目标前方向量与位置衰减 | 标准移动/疾跑集 | 群体协同涌现；无需全局指挥官调度，形成围剿态势。 |
| **Ambush Skill** | `move-to`, 掩体评估系统 | 掩体集合 $\mathcal{C}$，保持特定潜伏视距 | 隐蔽移动、低姿态伏击姿势、突击连招 | 仅用于 Stalker；基于掩体与盲区的非对称心理恐怖打击。 |
| **Throw Skill** | 特化射击循环 (Projectile Behavior) | 距离玩家保持中远射程；落点提前量求解 | 剥离菌包动画（按存活甲片路由） | 仅用于 Bloater；范围减速粒子覆盖，限制玩家走位。 |
| **On-Fire Skill** | `infected-canvass` | 微小极小半径 ($0.8\text{m} \sim 1.5\text{m}$) | 受击挣扎抽搐、失衡无序扑打动画 | 被火焰引燃后的无序肢体本能，完全规避实体穿模。 |
| **Wander Skill** | `move-to` | 全局场景；固定 Spline 或已访问多边形去重 | 迟钝行进、探索搜寻、环境动作锚点 | 潜伏关卡低威胁游荡；脱战后大范围全图随机扫荡。 |
| **Sleep Skill** | `infected-canvass`, 肢体微动 | 极小半径（近乎静止，触发后扩展至扰动点） | 沉睡、惊醒警戒微动、苏醒咆哮 | 阻塞点哨兵；阶梯式扰动响应，完全惊醒后永久转为 Wander。 |

---

## 5. 架构经验与工程反思（Architectural Takeaways）

顽皮狗在《最后生还者》感染者系统实现中总结的工程哲学，为工业级游戏 AI 架构提供了标准化范式：

### 5.1 模块化解耦驱动的高层极简化
*   **高层精简，底层自治**：技能仅负责“策略意图”（Intent），无需感知“如何转向”、“如何绕过小障碍物”。例如，`Chase Skill` 无需在代码中编写射线检测去修正每帧的切向速度，所有动态拐弯与寻路全部下沉至 `move-to`，这极大压缩了状态机的嵌套圈复杂度。
*   **排查与除错收敛**：AI 出现“弱智行为”（Stupid Looking Glitches）往往不是高层状态机逻辑错误，而是底层导航切角、动画根位移（Root Motion）与物理碰撞冲突所致。模块化行为系统能够将表现 Bug 精准定位到单一行为构件内部，而不会污染高层业务逻辑。

### 5.2 表现力驱动的行为重用（Behavior Reuse through Context Parametrization）
`infected-canvass` 行为的高复用率证明了：**核心逻辑一致性 + 资产可插拔性 = 极高工程杠杆率**。
*   在搜索技能中，它是“战术扫荡”；
*   在追逐技能中，它是“神经质式的停顿再定向”；
*   在着火状态下，它是“痛苦挣扎避障”。
逻辑核心始终是“在指定导航多边形区域内根据步长执行非碰撞随机漫游”，但通过注入不同的动画资源集合（Near/Far/Flail Sets）与参数（Radius/Duration），实现了视觉体验上的高度差异化。

### 5.3 数据配置与实体解耦（Parameter Tuning Independence）
所有感染者变种（Runner、Stalker、Clicker、Bloater）均共享完全同质化的底层技能与行为代码库。不同感染者类型的特异化表现，百分之百通过独立的数据配置表（Tuning Properties: 感知阈值、掩体倾向性、Canvass 采样频率、动画槽位）完成。这种设计消除了类派生爆炸，确保了整个 AI 代码底座的高度稳定与易维护性。
