---
type: Reference
title: "第34章 1000 NPCs at 60 FPS"
description: "Game AI Pro 工业级精读：1000 NPCs at 60 FPS。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
tags:
  - game-ai
  - game-ai-pro
  - behavior-trees
  - utility-ai
  - pathfinding
  - spatial-reasoning
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 第34章 1000 NPCs at 60 FPS

> 来源：*Game AI Pro 3: Balanced Cuts of Game AI Professionals*, Chapter 34.  
> 原文作者 / 资源：[1000 NPCs at 60 FPS](http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter34_1000_NPCs_at_60_FPS.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷3-GameAIPro3](README.md) ｜ [专栏首页](../README.md)

---

---

## 1. 工程背景、设计约束与架构解耦

在模拟经营类游戏（Management Simulation Game）《Project Highrise》（SomaSim 开发）中，核心玩法聚焦于高层摩天大楼的建设、租赁及微观经济管理。为了营造真实繁荣的“动态生态建筑”（Living Building），大楼内部必须充斥着数百乃至上千名具备自主日常作息规律的非玩家角色（Non-Player Characters, NPCs）。

### 1.1 核心性能基准与研发约束

1. **高并发计算性能基准（Performance Benchmark）**：系统必须在消费级民用台式机硬件（Commodity Desktop-Grade Hardware）上，同时驱动多达 1000 个具有独立生命周期的 NPC 实时模拟与画面渲染，帧率严格稳定在 60 FPS（即每帧全系统更新预算不得突破 $16.67\,\text{ms}$，分配给全量 AI 计算的单帧预算通常被严格限制在 $2.0 \sim 3.0\,\text{ms}$ 以内）。
2. **经济驱动模拟持久性（Persistent Simulation vs. Instanced Culling）**：NPC 的行为是驱动游戏建筑底层微观经济闭环（Core Economic Loop）的核心源动力（例如白领工人午餐时段前往餐厅消费产生租金收入，大楼管理者以此偿还建设贷款）。NPC 不能采用视野移入生成、视野移出销毁的视锥体剔除机制（Frustum Culling / Instancing），必须在全局空间内以不间断的时序状态机持续演进。
3. **团队资源极限约束（Resource Challenge）**：在核心研发期内，全项目仅有一名全栈工程师负责游戏全量系统的研发（包含图形渲染、UI 系统、建筑网格、物理碰撞、经济模型与底层引擎），AI 架构必须极度轻量、易于构建维护、具备低心智负担。

### 1.2 系统顶层解耦拓扑

系统将传统复杂的智能体控制模型解构为两大相互独立的子模块：**动作决策层（Action Selection）**与**动作执行层（Action Performance）**。

```
+-------------------------------------------------------------+
|               动作决策层 (Action Selection)                  |
|  - 模式 A (淘汰原型): 命题规划器 (Propositional Planner)      |
|  - 模式 B (量产实现): 每日作息调度器 (Daily Routine Scripts) |
|  - 输入: 时间片驱动 (Tick/Hour) + 工作内存 (Working Memory)   |
+-------------------------------------------------------------+
                               |
                        生成确定性动作序列
                        (Sequence of Actions)
                               v
+-------------------------------------------------------------+
|               动作执行层 (Action Performance)                |
|  - 状态执行拓扑: 开环动作队列 (Open-Loop Action Queue)       |
|  - 异常中断: 条件断言触发立即刷新 (Flush Queue on Failure)    |
|  - 空间寻路后端: 楼层板-连接器层次化图空间 (HPA Graph)       |
+-------------------------------------------------------------+
```

---

## 2. 动作决策（Action Selection）体系的演进与选型

动作决策层解决智能体的核心时序命题：“在当前给定的时间截面上，我应该做什么？”在工程演进过程中，研发团队经历了从“基于经典规划（AI Planning）”向“数据驱动的预设脚本调度（Script Scheduler）”的技术范式转移。

### 2.1 初始探索：命题规划器（Propositional Planner）

预制作阶段（Preproduction）为了实现高度灵活的智能涌现，首先尝试了经典规划系统。为规避一阶谓词逻辑在运行时合一与变量绑定带来的巨大算力消耗，开发团队采用了命题规划器（Propositional Planner）。

#### 2.1.1 命题逻辑（Propositional Logic）与一阶谓词逻辑（Predicate Logic）的数学抽象对比

经典 STRIPS（Stanford Research Institute Problem Solver）规划器依赖带有自由变量（Free Variables）的一阶谓词逻辑。定义世界状态为变量谓词的合取式集合：

$$\mathcal{S}_{\text{predicate}} = \bigwedge_{i} P_i(x_1, x_2, \dots, x_k)$$

其典型的移动规则定义如下：

```lisp
Rule: 
  Preconditions:  (at-location X) & (desired-location Y) & ~(equal X Y)
  Action:         (go-to Y)
  Postconditions: (at-location Y) & ~(at-location X)
```

在谓词规划中，状态空间搜索必须遍历全图域对象以寻找变量替换集 $\sigma = \{X \mapsto u, Y \mapsto v\}$，其搜索复杂度随环境实体数量呈指数级组合爆炸。

相对地，命题规划器直接剥离了自由变量体系，所有前置条件（Preconditions）与后置效应（Postconditions）均严格来自于一个有限且编译期固化的已实例化命题全集（Finite Set of Grounded Propositions）：

$$\mathcal{P} = \{p_1, p_2, \dots, p_N\}$$

对应的命题规则定义为：

```lisp
Rule: 
  Preconditions:  at-home & is-hungry
  Action:         go-to-restaurant
  Postconditions: ~at-home & at-restaurant
```

> **注**：尽管谓词规则可通过所有变量可能取值的笛卡尔积（Cartesian Product）全展开为命题规则，但若对象域庞大，会导致命题基数 $|\mathcal{P}|$ 发生组合爆炸。在《Project Highrise》有限的室内活动拓扑中，该基数保持在极低水位。

#### 2.1.2 空间复杂度压缩与位向量（Bit Vector）计算拓扑

由于命题全集 $\mathcal{P}$ 在编译期预先确定，全局世界状态 $\mathbf{s}$、规则 $r$ 的前置条件掩码 $\mathbf{m}_{\text{prec}}$、预期位模式 $\mathbf{v}_{\text{prec}}$ 以及后置删除/添加效果掩码可以直接映射到固定位宽（如 64 位或 128 位）的高性能位向量（Bit Vector）中：

1. **前置条件验证（Precondition Check）**：
   通过常数时间 $O(1)$ 的位运算指令（Bitwise Operations），判定当前世界状态向量 $\mathbf{s} \in \{0, 1\}^N$ 是否完全满足动作前置条件：
   $$(\mathbf{s} \ \& \ \mathbf{m}_{\text{prec}}) == \mathbf{v}_{\text{prec}}$$
2. **后置效应应用（Postcondition Transition）**：
   在前置条件达成后，通过按位与非（AND-NOT）清零以及按位或（OR）置位，实现状态单周期跃迁：
   $$\mathbf{s}' = (\mathbf{s} \ \& \ \sim \mathbf{mask}_{\text{del}}) \mid \mathbf{mask}_{\text{add}}$$

#### 2.1.3 计划缓存机制（Plan Caching）

得益于位向量表示，每个搜索生成的规划路径 $\pi = \langle a_1, a_2, \dots, a_k \rangle$ 均附带一个适用上下文掩码 $\mathbf{C}_{\pi} = (\mathbf{m}_{\pi}, \mathbf{v}_{\pi})$。若某 NPC 产生决策需求时的输入状态 $\mathbf{s}$ 满足：

$$(\mathbf{s} \ \& \ \mathbf{m}_{\pi}) == \mathbf{v}_{\pi}$$

系统可直接跳过图搜索（如 $A^*$ 或前向状态空间搜索），以 $O(1)$ 复杂度直接复用该规划指令序列。在只有少数 NPC 类别的大楼模型中，全局仅生成了极少数的基础计划模板并被全量共享命中，搜索分支因子（Search Space Fan-Out）趋近于零。

### 2.2 范式转移：每日作息脚本系统（Daily Routine Scripts）

尽管命题规划器展现了极致的计算性能，但在工程推进至内容生产阶段时被全量移除。其根本原因并非运行性能瓶颈，而是**抽象层级的严重错位（Wrong Level of Abstraction）**。

#### 2.2.1 抽象层级错位与涌现性缺陷

在模拟经营游戏中，策划需求的核心是**“强设计控制权”（Authorial Control）**：设计者需要精准把控不同社会阶层（Socio-Economic Status）的住户或职员何时起床、何时通勤、何时前往特定等级的餐厅消费，并注入具有叙事特征的趣味偏差（Quirks & Flavor）。

* **规划系统的失效边界**：规划器聚焦于微观局部反应（即“在此情境下我该如何反应以达到目标”），若通过调整局部规则驱动群体在宏观时间线上产生确定性的社会阶层生活节奏，会导致设计者陷入难以调和的参数试错深渊。
* **作息范式（Scripts/Routines）的认知学基础**：认知科学文献（Schank and Abelson 1977; Suchman 1987）表明，人类在日常生活交互中高度依赖程式化的固定脚本，而非每次均从第一性原理（First Principles）出发推导全量行动计划。

#### 2.2.2 每日作息脚本数据结构与语法规范

研发团队转而构建了面向时间片的分层作息描述语言。以下为高工时办公室白领实装脚本规范：

```javascript
name "schedule-office.7"
blocks [
    { from  8 to 20 tasks [ go-work-at-workstation ] }
    { from 20 to  8 tasks [ go-stay-offsite ] }
]
oneshots [
    { at  8.0 prob 1.00 tasks [ go-get-coffee ] }
    { at 12.0 prob 1.00 tasks [ go-get-lunch ] }
    { at 15.0 prob 0.50 tasks [ go-get-coffee ] }
    { at 17.5 prob 0.25 tasks [ go-visit-retail ] }
    { at 20.0 prob 0.50 tasks [ go-get-dinner ] }
    { at 20.0 prob 0.25 tasks [ go-get-drink ] }
]
```

##### 语义结构解析

1. **块级持续脚本（Continuous Blocks）**：划分全天宏观时间区间，定义主状态基底（Base State）。例如 $08:00 \sim 20:00$ 强制循环执行工位工作任务；其余区间处于离场/居家状态。内部以低开销循环轮询（Looping Activity）承载可调微变种动画（Tunable Variations）。
2. **瞬时事件脚本（Oneshot Tasks）**：注入确定性与概率性脉冲事件。参数包含触发绝对时间（$t \in [0.0, 24.0]$）、评估通过率 $\text{prob} \in [0.0, 1.0]$ 及目标动作集。
3. **工作内存（Working Memory, 黑板模型）交互**：任务节点与 NPC 个体的轻量工作内存绑定。内存仅记录核心索引：如工位坐标 `workstation_id`、住址 `home_id`、当前转移目标 `target_destination`。

#### 2.2.3 智能体表示复杂度的负缩放律

在预制作早期，团队曾尝试为 NPC 引入饥饿值（Hunger）、疲劳度（Tiredness）等生理状态驱动的个性模型（Personality Models）。该设计随后被彻底剔除，提炼出管理类游戏 AI 核心工业定律：

$$\text{Utility}(\text{Detailed NPC State}) \propto \frac{1}{\text{Count}(\text{Simulated NPCs})}$$

```
玩家认知效用 / 设计可控性
  ^
  |  * (单个/少量 NPC: 极高偏好内部复杂性与微观生理反馈)
  |   \
  |    \
  |     \
  |      \
  |       * (海量 NPC 模拟: 内部隐状态蜕变为“信息黑箱”，产生不可控的宏观非预期行为)
  |        \
  |         +--------------------------------------------->
  0          10                              1000        NPC 数量规模
```

在超大规模 NPC 场景下，复杂的内部生理隐状态（Hidden Information）对玩家而言不具备可读性，当上千个智能体产生非线性群体涌现时，极易引发不可控的经济崩溃，使玩家和策划陷入调试灾难。因此，必须将个体内部状态完全抽象为确定性的外部时间线脚本。

---

## 3. 动作执行（Action Performance）架构：极简开环控制与模型降维

动作决策层产出的脚本最终需要落地为角色在场景物理空间中的连续位移与状态跃迁。传统 AI 体系在此处往往伴随着高额的每帧感知计算开销，而《Project Highrise》在此采用了两大底层重构策略：**开环执行拓扑**与**空间表示降维**。

### 3.1 闭环反馈系统的计算瓶颈与开环动作队列（Open-Loop Action Queues）

以包容式架构（Subsumption Architecture, Brooks 1986）或自远动树（Teleoreactive Trees, Nilsson 1994）为代表的经典智能体系统，以及大部分标准行为树（Behavior Trees, BT），通常设计为闭环反馈系统（Closed-Loop Feedback Systems）：

```
[闭环系统] 传感器采样 (Sensory Poll) ---> 状态重校验 (Revalidate) ---> 条件重评估 (Re-evaluate) [每帧高 CPU 开销]
```

为了在 $60\,\text{FPS}$ 下支持 1000 个 NPC，本作将动作执行推向极端的**开环控制范式（Open-Loop Execution）**：系统默认世界是良性且相对静态的，智能体在执行动作期间彻底关闭主动环境感知（No Continuous Perception），将计算开销完全摊平。

```
[开环系统] 脚本调度器 (Script Scheduler)
                 |
                 v 实例化展开
        +-----------------------------------------------------+
        | 动作线性队列 (Linear Action Queue)                   |
        | [ Action 0 ] -> [ Action 1 ] -> ... -> [ Action N ] |
        +-----------------------------------------------------+
                 |
                 v 顺序执行 (No Sensing / No Polling)
         执行直至队列耗尽 (Queue Empty) 
                 |
                 +--------------------------------------------+
                 |                                            |
           (成功完成)                                     (触发断言失败)
                 v                                            v
         触发下一轮动作决策                             立即刷新队列 (Flush Queue)
         (Re-run Action Selection)                            |
                                                              v
                                                       压入兜底响应动作
                                                       (Queue Fallback Script)
```

#### 3.1.1 开环执行生命周期流水线

1. **序列下发**：动作决策层将脚本与 NPC 的工作内存复合，编译为扁平的原子动作队列：
   $$\text{Queue} = \langle \text{EnterLobby}, \text{WaitForElevator}, \text{TakeElevator}, \text{WalkToDesk}, \text{PlaySitAnimation} \rangle$$
2. **线性无感知消耗**：NPC 严格沿队列单向轮巡出队并执行，过程不执行任何针对外部环境的重采样（Resensing）与重验证逻辑。一个涵盖数十个子动作的午餐序列仅需消耗不到 1 分钟真实时间。
3. **延迟异常断言（Lazy Failure Assertions）**：动作本身不主动探测世界变化，仅在其底层硬性前置失败（例如空间寻路不可达）时触发中断信号。
4. **失效回退协议（Failure Recovery Pipeline）**：一旦断言失败，整个队列瞬间执行原子清除（Flush），清空后续所有管线指令，并可选择向队列前端压入轻量级兜底动作（Fallback Script，如：面向摄像机播放抱怨气泡与动画），随后直接触发决策层重新下发初始任务。由于单次决策的计算开销极小，直接废弃并重建执行链的代价远低于维持复杂的运行时动态修复机制。

---

## 4. 领域特化空间搜索：楼层板-连接器层次化图模型

高层建筑的典型空间特性是：纵向横切面（Cut-away Side View）由大量横向走廊（2D 空间网格）与少数纵向交通核（楼梯、扶梯、电梯井）构成。对于 $100$ 层高、横向 $150$ 网格宽度、包含 $4$ 个大型垂直电梯井的极高密度超大建筑，底层原始网格节点总规模达：

$$|\mathcal{V}_{\text{grid}}| = 100 \times 150 = 15{,}000\,\text{Nodes}$$

在此类网格上对数百名并发寻路智能体直接执行经典 $A^*$ 搜索，单帧 CPU 周期将被迅速耗尽。

### 4.1 传统算法方案的局限性对比

在确立最终空间拓扑前，工程团队对业界前沿的高性能网格搜索算法进行了可行性技术论证：

| 算法方案 | 核心技术原理 | 局限性与《Project Highrise》场景冲突点 |
| :--- | :--- | :--- |
| **标准网格 $A^*$** | 基于原始 2D Grid 展开邻接搜索，通过曼哈顿/欧几里得启发函数引导。 | 搜索空间极大（$\sim 15{,}000$ 节点），高并发时内存访问发散，分支膨胀。 |
| **HPA\* (Botea et al. 2004)** | 通用分层聚类算法，将网格静态划分为规整的 Local Clusters。 | 抽象边界未对齐建筑语义；玩家动态施工时，边界互联点重建成本高。 |
| **JPS (Harabor & Grastien 2012)** | 跳点搜索（Jump Point Search），利用对称性剪枝寻找跳点，消除冗余展开。 | 动态构建跳点拓扑。玩家高频建造、拆除墙体导致结构极不稳定，开销剧烈波动。 |
| **JPS+ (Rabin 2015)** | 离线全预处理跳点距离与查找表，在线运行时可达到极快查询速度。 | 预处理计算复杂度高昂（Heavy Preprocessing），完全无法适应玩家实时扩建。 |

### 4.2 核心突破：领域驱动模型降维（Domain-Specific Model Reduction）

针对通用算法在动态建造场景下的缺陷，团队跳出“优化图搜索算法”的思维惯性，转向**“利用领域知识彻底优化搜索空间（Optimize the Search Space instead of Optimizing the Algorithm）”**。

```
原始网格表示 (Raw Grid Representation)
[Floor N]   [ ][ ][ ][ ][ ][ ][ ][ ][E1][ ][ ][ ][ ][ ][E2][ ][ ]  ... 150 列
               |                                            |
               v 拓扑压缩 (Semantic Clustering)               v
抽象图模型 (High-Level Graph Model)
[Node: FloorPlate_k] <======== Edge: Connector (Elevator/Stairs) ========> [Node: FloorPlate_m]
  |                                                                           |
  +-- 单一楼板内移动: 纯一维横向平移 (Direct Linear Approach: dx = x_target - x_curr)
```

#### 4.2.1 拓扑抽象图结构定义

1. **楼层板节点（Floor Plate Node, $\mathcal{N}$）**：在同一物理楼层内连续无阻隔连通的横向单元格集合。若某楼层中途被机房或不可通行区域截断，则自然切分为互不相连的独立楼板节点。每个楼板内部为完全连通凸集，内部移动退化为平凡的**一维一阶直线逼近（Straight-Line Approach）**，完全无需寻路。
2. **连接器有向边（Connector Edge, $\mathcal{E}$）**：纵向连通不同楼层板的交通工具（楼梯 Stairs、自动扶梯 Escalators、电梯井 Elevators）。

#### 4.2.2 降维数学证明与空间复杂度跃变

对于一个规格为 $H = 100$ 层、$W = 150$ 宽、包含 $K = 4$ 个通贯垂直电梯的基准摩天楼环境：

* **原始空间节点数**：
  $$|\mathcal{V}_{\text{raw}}| = H \times W = 100 \times 150 = 15{,}000$$
* **降维后抽象图节点数**（假定单层无物理截断）：
  $$|\mathcal{V}_{\text{graph}}| = H = 100$$
* **抽象图边数统计**（每个电梯轴在对应楼层提供垂直跨越能力）：
  $$|\mathcal{E}_{\text{graph}}| = H \times K = 100 \times 4 = 400$$

**空间复杂度压缩率**达到惊人的两个数量级：

$$\text{Reduction Ratio} = \frac{|\mathcal{V}_{\text{raw}}|}{|\mathcal{V}_{\text{graph}}|} = \frac{15{,}000}{100} = 150 \times$$

在仅有 100 个节点、400 条边的微型拓扑图上，图遍历的常数开销完全可以被底层 CPU L1/L2 缓存吸收。在此拓扑上直接运行标准 $A^*$ 算法，配合定制的启发式权重因子：

$$h(u, v) = \Delta \text{Floor}(u, v) \times C_{\text{vertical\_cost}} + \Delta X_{\text{connector}}(u, v) \times C_{\text{horizontal\_cost}}$$

搜索过程中的 Open Set 节点展开数量被牢牢锁定在极低范围，不再产生任何算力发散。当玩家建造新房间、扩建或拆卸楼梯时，系统仅需在常数复杂度 $O(1)$ 时间内分裂、合并对应的局部 `FloorPlate` 节点或更新关联边权重，完美兼顾了高并发寻路效率与极高的运行时动态图可变性。

---

## 5. 架构总结与工业界实践准则

《Project Highrise》以单程序员资源在 60 FPS 的严格时间预算内达成 1000 个全自主 NPC 模拟，为复杂模拟系统的工业化落地确立了一组核心范式：

```
+------------------------------------------------------------------------------------+
|                       高并发智能体模拟的工业设计法则                                   |
+------------------------------------------------------------------------------------+
| 1. 复杂度法则 (Scale vs. Complexity):                                               |
|    智能体内部状态的表示精度必须与系统并发规模呈严格反比。在超大规模 NPC 场景下，坚决     |
|    剔除无法被玩家直接感知的微观生理隐状态。                                           |
+------------------------------------------------------------------------------------+
| 2. 调度法则 (Abstraction Alignment):                                               |
|    模拟经营系统的核心是设计控制力与宏观生活节奏。采用基于时间表的作息脚本替代局部自发推导的  |
|    经典规划器，实现抽象层级与业务需求的精准对齐。                                     |
+------------------------------------------------------------------------------------+
| 3. 执行法则 (Open-Loop Simplification):                                            |
|    在低对抗、良性场景中，以极简开环执行队列替代持续感知闭环；遭遇执行异常时直接回退重建，   |
|    消除感知轮询与动态修复带来的海量微分配开销。                                       |
+------------------------------------------------------------------------------------+
| 4. 空间法则 (Data Model Reduction over Algorithmic Tweaks):                         |
|    优先利用游戏业务边界进行空间模型降维。当问题规模经由领域知识被压缩两个数量级时，基础     |
|    算法的表现往往远胜于针对原始规模打补丁的高复杂度定制算法。                         |
+------------------------------------------------------------------------------------+
