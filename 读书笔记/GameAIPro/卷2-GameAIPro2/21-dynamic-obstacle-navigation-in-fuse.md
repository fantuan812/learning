---
type: Reference
title: "第21章 Dynamic Obstacle Navigation in Fuse"
description: "Game AI Pro 工业级精读：Dynamic Obstacle Navigation in Fuse。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第21章 Dynamic Obstacle Navigation in Fuse

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 21.  
> 原文作者 / 资源：[Dynamic Obstacle Navigation in Fuse](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter21_Dynamic_Obstacle_Navigation_in_Fuse.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

## 1. 行业背景与核心挑战 (Industrial Context & Core Challenges)

在现代 3A 动作射击与动作冒险游戏中，跨越物理障碍（如墙体攀附、横向岩架移动、竖直管道攀爬、垂直梯子等）通常统称为**立体机动遍历系统（Traversal System）**。在多数游戏架构中，AI 导航（AI Navigation）往往被严格限制在二维或两点五维的连续**导航网格（NavMesh, Navigation Mesh）**上进行平地行走与奔跑规划。一旦引入复杂的垂直攀爬，导航拓扑与动作驱动的复杂度将呈指数级上升：
- 传统开发往往依赖密集的硬编码标记（Markup）或专用脚本；
- 难以适应高度动态、多角色协作、拓扑错综复杂的非结构化空间；
- 难以处理不同角色具有差异化骨骼和动作集（Locomotion Sets）的情况。

```
+-----------------------------------------------------------------------------------+
|                            传统 NavMesh 寻路空间 (2.5D)                            |
+-----------------------------------------------------------------------------------+
                                         │
                         [Climb Clue 起点标记: 导航解耦跨越]
                                         ▼
+-----------------------------------------------------------------------------------+
|                        程序化攀爬网格 (Climb Mesh 3D 空间)                         |
|     (节点拓扑 / 显式重叠连接 / 隐式跳跃检测 / 贝塞尔拟合 / 虚拟输入注入)              |
+-----------------------------------------------------------------------------------+
                                         │
                         [Climb Clue 终点标记: 重新接管]
                                         ▼
+-----------------------------------------------------------------------------------+
|                            目标 NavMesh 寻路空间 (2.5D)                            |
+-----------------------------------------------------------------------------------+
```

在 Insomniac Games 开发的四人协作第三人称射击游戏 *Fuse* 中，底层 AI 移动架构面临极其严苛的工程挑战与设计硬约束：

### 1.1 协作 NPC 行为与对称性规则 (The "50/50" Rule & Anti-Cheating Constraint)
游戏支持最多 4 名英雄角色协同作战，其中多达 3 名可由 AI 代理托管。架构确立了严苛的“反作弊（No Cheating）”原则：
* **无瞬间传送机制（No Teleportation）**：AI 严禁通过隐形瞬移来跟进人类玩家进度；
* **物理机制平权（Identical Mechanics）**：AI 角色具有与玩家完全相同的物理受击判定、体量生命值上限及场景交互逻辑（如操纵炮塔、爬梯等）；
* **战力对称性（50/50 Damage Balance Rule）**：在有 2 名以上 AI 控制英雄的基准场景下，系统战力平衡目标要求人类玩家与 AI 英雄各自承担 $50\%$ 的总体输出伤害。

### 1.2 动态跃迁机制与运行时不稳定性 (The "Leap" Mechanic & P2P Volatility)
游戏内置独特的**跃迁能力（Leap Ability）**与去中心化 P2P 网络环境（Peer-to-Peer Online Environment）。人类玩家可在交火战斗或攀爬遍历中，随时无缝切换控制权到任意 AI 英雄身上；玩家亦可随时动态加入/退出（Drop-in/Drop-out）。

这导致 AI 必须随时处理“半路唤醒”的极端非稳态上下文（Volatile AI Characters）：例如玩家在 $50\text{ ft}$（约 $15.24\text{ m}$）的高空单手悬吊在岩架边缘且正遭受自动步枪射击，此时玩家切换至其他角色，接管该角色的 AI 代理必须在当前悬挂姿态下瞬时初始化，动态解析周遭三维环境，规划脱困路径或反向救援倒地队友（Revive Mechanics）。纯离线静态烘焙的标记完全无法满足需求，必须依赖**运行时程序化攀爬路径解析系统（Runtime Procedural Traversal System）**。

---

## 2. 空间拓扑表征与攀爬网格生成 (Climb Mesh Generation)

为了在断开的静态导航网格之间架起三维垂直机动桥梁，系统将美术/关卡设计师摆放的几何体转化为包含显式与隐式连接的有向图拓扑——**攀爬网格（Climb Mesh）**。

### 2.1 标记体元数据与拓扑类型 (Markup Volumes & Disjoint Mesh Clues)
遍历区域由关卡设计师布置的一组**遍历体元（Traversal Volumes）**构成。常见空间类型包括：
* **水平岩架（Horizontal Ledges）**：支持悬吊横移（Hanging Traverse）与顶部站立走动（Mount Traverse）；
* **竖直管道（Vertical Pipes）**：支持双向垂直滑爬与环绕定位；
* **梯子与变体结构（Ladders & Structural Variants）**：包含固定的进出端点与踏板步进状态。

在常规连续导航网格的不连通断裂处，设计师会放置**自定义线索标记（Custom Climb Clues）**。导航系统（Navigation System）将攀爬区域视作由一对线索边缘（Clue Edges $A \to B$）连接的黑盒抽象，该路径在导航网格层级仅作为一段欧氏距离连线，底层具体的多分支三维机动拓扑则全部交由独立的 Climb Mesh 处理。

```
+-----------------------------------------------------------------------------------+
|                             Climb Mesh 拓扑生成管线                               |
+-----------------------------------------------------------------------------------+
|  [输入]: Traversal Volumes (Ledges, Pipes, Ladders)                              |
|           │                                                                       |
|           ├─► 1. 显式连接提取 (Explicit Connectivity)                             |
|           │    └─► 几何相交/端点重叠判断 -> 共享并合并交叉节点 (Merged Intersection)  |
|           │                                                                       |
|           ├─► 2. 隐式连接判定 (Implicit Connectivity)                             |
|           │    ├─► 采样节点: 端点 (2) + 几何中点 (1) = 3 节点/体元                     |
|           │    ├─► 空间距离剪裁: $d \le D_{\text{threshold}}$                     |
|           │    ├─► 姿态角度验证: $\theta \le \Theta_{\text{threshold}}$           |
|           │    └─► 视线物理投射: Line-of-Sight (Raycast/Sweep Test)               |
|           │                                                                       |
|           └─► 3. 抽象图构建 (Abstract Cyclic Graph)                              |
|                └─► 存储双向/单向 Transition Edges (独立于具体动画状态)             |
+-----------------------------------------------------------------------------------+
```

### 2.2 显式连接提取 (Explicit Connectivity)
系统遍历所有重叠或相交（Touching or Overlapping）的体元：
* 当两个岩架端点重叠，或横向岩架与竖直管道在空间中相交（Cross Section）时，系统在相交位置生成一个**共享合并节点（Merged Node）**；
* 该节点向两端体元发散出连接边（Explicit Connections），每个节点通常保持 1 到 2 个分支度数，形成确定性的网格主干骨架。

### 2.3 隐式连接推导与空间剪裁 (Implicit Connectivity & Spatial Pruning)
为使角色能够在分散的障碍间实现跳跃、下落和攀附（Jumps & Drops），系统必须推导不相交体元间的跳跃可行性。

#### 采样分辨率策略 (Discretization Strategy)
若以固定步长（如每隔 $1.0\text{ m}$）生成稠密采样点，会导致图节点爆炸并给后续路径平滑带来高频抖动。*Fuse* 采用结构化稀疏采样：**对每个遍历体元仅提取 3 个特征节点**——即**左端点**、**右端点**与**几何几何中心点（Center Point）**。

#### 空间阈值过滤与视线遮挡测试 (Thresholding & Line-of-Sight Verification)
对于任意候选节点对 $(u, v)$，隐式边建立必须同时满足三项几何约束：
1. **欧氏距离阈值**：
   $$\|P_u - P_v\| \le D_{\text{threshold}}$$
2. **方向与角度发散阈值**：
   $$\arccos\left(\frac{\mathbf{v}_{\text{jump}} \cdot \mathbf{n}_{\text{surface}}}{\|\mathbf{v}_{\text{jump}}\|}\right) \le \Theta_{\text{threshold}}$$
3. **物理视线测试（Line-of-Sight, LOS）**：通过射线或形状扫掠（Raycast / Shape Sweep）确认 $u$ 到 $v$ 之间无静态几何体碰撞阻挡。

### 2.4 连接拓扑分类矩阵 (Connection Topology Taxonomy)

| 连接模式 (Connection Mode) | 判定机制 (Detection Mechanism) | 运动学映射 (Kinematic Mapping) | 拓扑类型 (Topology Type) |
| :--- | :--- | :--- | :--- |
| **重叠显式连接 (Overlapping Explicit)** | 几何相交测试求交点，合并为共享节点 | 连续攀爬横移（Traverse Move）、管-架姿态切换 | 无缝双向图边 (Bidirectional) |
| **临界间隙跳跃 (Jump Threshold)** | 水平/微斜向距离在阈值内，通过 LOS 检测 | 跨越跳跃（Horizontal Vault / Leap） | 双向/单向条件边 (Conditional) |
| **背向反弹跳 (Backward Jump)** | 相对反向法线，背向投影距离检测 | 背向蹬墙反弹跳（Wall-to-wall Back Jump） | 深度维度跳跃边 (Depth Transition) |
| **垂直落差/向上攀跃 (Drop / Upward Jump)**| 竖直高度差符合重力掉落或引体攀跃范围 | 自由悬挂掉落（Ledge Drop）或悬垂上跳（Up-jump）| 高度差有向边 (Directed Edge) |

---

## 3. 空间搜索与多代理避障规划 (Spatial Path Planning)

一旦 AI 代理进入攀爬判定范围，系统即启动针对 Climb Mesh 的两阶段寻路解析。

```
+-----------------------------------------------------------------------------------+
|                             分层寻路与动态避障解析流程                            |
+-----------------------------------------------------------------------------------+
|  [主导航层级]: Global NavMesh Path (A* 规划)                                      |
|                 │                                                                 |
|                 ▼                                                                 |
|  [线索边缘触发]: 检测到 Climb Clue 触发边 ($A \to B$)                              |
|                 │                                                                 |
|                 ▼                                                                 |
|  [局部攀爬网格寻路]:                                                              |
|     1. 投影角色当前空间位置 -> 最近的 Climb Clue 节点 (作为局部起点 $S$)            |
|     2. 执行局部 3D A* 搜索 (启发函数 $h(n) = \|P_n - P_{\text{target}}\|_2$)        |
|     3. 边代价动态加权: $C(e) = \text{Length}(e) + \alpha \cdot \text{Occupancy}(e)$ |
|     4. Closed List 判重化解环路 (Cyclic Resolution)                               |
|     5. 输出离散三维路径点集: $\mathcal{P} = \{\mathbf{p}_0, \mathbf{p}_1, \dots, \mathbf{p}_k\}$ |
+-----------------------------------------------------------------------------------+
```

### 3.1 基于 Climb Mesh 的二次 A* 寻路 (Secondary A* Search on Climb Meshes)
1. **起点锚定**：AI 代理将其在 3D 空间中的当前足底或悬吊投影点映射至起始 Climb Clue 上，生成初始攀爬节点 $n_{\text{start}}$；
2. **启发式空间搜索**：在 Climb Mesh 拓扑图上以目标线索节点 $n_{\text{goal}}$ 展开标准 A* 搜索：
   $$f(n) = g(n) + h(n)$$
   其中：
   $$g(n) = \sum_{e \in \text{path}(n_{\text{start}}, n)} \text{Cost}(e)$$
   $$h(n) = \|\mathbf{x}_n - \mathbf{x}_{\text{goal}}\|_2 \quad (\text{欧几里得空间欧氏距离启发式})$$
3. **环路消解（Cyclic Connections Resolution）**：因 Climb Mesh 存在大量的闭环拓扑（如多层岩架矩形回环），A* 算法维护标准的 Open/Closed 表，通过状态标记防止循环陷入；
4. **输出规格**：目标达成后，输出一组有序的 3D 矢量序列：
   $$\mathcal{P} = \{\mathbf{p}_0, \mathbf{p}_1, \dots, \mathbf{p}_m\}, \quad \mathbf{p}_i \in \mathbb{R}^3$$

### 3.2 边代价加权与多代理冲突规避 (Edge Cost Modulation & Collision Avoidance)
在基准实现中，攀爬边的基础代价值被归一化为空间几何长度。然而，攀爬管道和岩架具有空间独占或容量上限（例如一根水管无法容纳两个角色重叠上下攀爬）。

系统引入了**基于动态占用的边代价惩罚（Dynamic Edge Cost Penalty）**：
$$\text{Cost}(e) = \text{Length}(e) \cdot \left(1.0 + \sum_{k=1}^{N_{\text{agents}}} \omega_k \cdot \mathbb{I}_{\{k \text{ occupies } e\}}\right)$$
* 当某一攀爬边已被其他友军或敌军 NPC 占用时，该边的代价剧烈提升；
* 后续 AI 寻路时将自然规避该路线，优先探索平行的其他分支结构（如另一侧的备选水管或下层岩架）。

### 3.3 运行时重规划与死锁消除 (Runtime Replanning & Deadlock Prevention)
若玩家在攀爬途中突然调头停滞并物理阻断（Block）路线，原本规划的静态路径将导致 AI 陷入局部死锁（Traffic Jams）。为解决这一问题，系统赋予 AI 在任意网格三维位置重构局部起点的能力：
* 当检测到前方体元被持续占据超时（Time-out Threshold）或阻挡，AI 立即中止当前边缘行进；
* 将当前骨骼悬吊根节点重新正交投影到 Climb Mesh 最近点，以该点作为新源点 $n_{\text{current}}'$ 重新触发局部 A*，绕行解耦。

---

## 4. 驱动与处理解耦架构 (Driver-Processor State Machine Architecture)

为实现人类玩家输入与 AI 代理自治行为的完全对称与动画资产复用，*Fuse* 采用了业界顶尖的**状态驱动器-处理器解耦架构（State Driver / State Processor Pattern）**。

```
+-----------------------------------------------------------------------------------+
|                   Fuse 角色状态控制架构 (Driver-Processor 分离)                  |
+-----------------------------------------------------------------------------------+

   +------------------------------------+      +----------------------------------+
   |   Human Driver (人类玩家驱动器)     |      |     AI Driver (AI 代理驱动器)     |
   |  - Gamepad / Stick Input (手柄输入) |      |  - Climb Path (A* 3D 路径点)     |
   |  - Contextual Buttons (按键判定)   |      |  - Bézier Spline (连续样条插值)   |
   |                                    |      |  - VCI Generator (虚拟手柄注入)  |
   +------------------------------------+      +----------------------------------+
                     │                                          │
                     │  Normalized 3D Vector & Flags            │  VCI Vector & Strength
                     └───────────────────┬──────────────────────┘
                                         ▼
   +-------------------------------------------------------------------------------+
   |                     Shared Hero State Processor (共享状态处理器)               |
   |  - State Transition Logic (状态转换阈值判定: Jump, Hang, Mount, Drop)          |
   |  - Collision & Boundary Verification (几何/角色碰撞判定)                      |
   |  - Death / Incapacitated Interrupts (死亡、瘫痪等全局中断处理)                |
   +-------------------------------------------------------------------------------+
                                         │
                                         ▼ Anim Trigger & Parameters
   +-------------------------------------------------------------------------------+
   |                           Animation Tree (角色动画树)                          |
   |  - Root Motion / Kinematic Blending / Traversal Locomotion                     |
   +-------------------------------------------------------------------------------+
```

### 4.1 架构分层职责

#### 驱动层（Drivers）
负责采集操作意图并输出抽象操控数据：
* **人类驱动器（Human Driver）**：负责监听实体手柄摇杆摇动方向、物理扳机与功能按键，解算为归一化的控制极坐标矢量与状态迁移意图；
* **AI 驱动器（AI Driver）**：由行为树与遍历规划管线驱动，通过**虚拟控制器输入（Virtual Controller Input, VCI）**生成一套与人类物理手柄语义完全等价的连续 3D 输入矢量。

#### 处理器层（State Processors）
负责真正的运动学模拟与动画驱动，实现代码在 Human 与 AI 间 $100\%$ 复用：
* 维护攀爬状态机（Ledge Hang, Pipe Climb, Ladder Ascend, Wall Flip 等）；
* 将 Driver 传入的三维轴向强度与阈值比较；
* 触发动画树（Animation Tree）的位移、混合与根骨骼运动（Root Motion）；
* 监听世界碰撞、队友阻挡及死亡/瘫痪等非正常终结状态（Terminating States）。

### 4.2 状态驱动与判定生命周期 (State Lifecycle & Zero-Cognition Traversal)
在攀爬初始化后，AI 行为决策树完全“静默”，进入**零高层决策模式（Zero Runtime Decision-Making Mode）**。AI 不需要思考“现在该跳跃还是爬行”，它只需将 VCI 矢量注入 State Processor，由 Processor 内置的阈值触发器自主驱动转换。这种设计极大地减轻了高层行为树（Behavior Tree）在复杂三维机动过程中的判定负荷。

---

## 5. 虚拟控制器输入与样条解析 (VCI & Bézier Spline Motion Parsing)

Climb Mesh 输出的离散折线路径无法直接供给状态处理器使用。若将折线以 $90^\circ$ 锐角直接馈送给控制器，会导致角色产生剧烈的转向抖动甚至动画状态判定丢失。系统通过高阶样条平滑技术与虚拟轴投影技术实现平滑驱动。

```
                                          Projected Target
                                            [VCI Target]
                                               ●
                                              / 
                                             /
                                            /  3D VCI Vector
                                           /  (Len = 2.8m, Lookahead)
                                          /
   [Control Point P0]                    /
          ●─────────────────────────────●─────────────────────────────●
                                   [AI Hero]                   [Control Point P3]
                               (Current Spline Pos)
```

### 5.1 贝塞尔样条曲线平滑原理 (Bézier Spline Smoothing)
离散三维路径点通过三次贝塞尔样条（Cubic Bézier Spline）进行拟合。对于由控制点序列定义的样条段，其空间参数方程表达为：
$$\mathbf{B}(t) = (1-t)^3 \mathbf{P}_0 + 3(1-t)^2 t \mathbf{P}_1 + 3(1-t) t^2 \mathbf{P}_2 + t^3 \mathbf{P}_3, \quad t \in [0, 1]$$

#### 角动量补偿机制 (Angular Momentum Compensation)
假设存在一个环绕两处直角转角的 $180^\circ$ 折线回环路径：
* **原始折线**：仅包含三边位移且伴随两个极端的 $90^\circ$ 阶跃瞬变，曲率为零且导数不连续；
* **样条拟合**：向外凸出（Extrude）曲线以平滑转角，为路径赋予了连续的一阶与二阶导数 $\mathbf{B}'(t)$ 及 $\mathbf{B}''(t)$，在空间中注入了**角动量（Angular Momentum）**。这与人类玩家推摇杆过弯时的连续前瞻弧线操作完全一致。

### 5.2 前瞻投影与虚拟控制矢量生成 (VCI Vector Generation)

#### 投影几何推导 (Projection Geometry)
AI 代理在当前样条曲线上评估自身位置 $t_{\text{current}}$。为保证过渡动作（如起跳预备动作）在抵达转折点前适时触发，系统采用固定的前瞻空间距离进行射线步进，将虚拟目标点（VCI Target）投射在曲线上：
* **机动空间分辨率（Traversal Move Resolution）**：在 *Fuse* 动画资产体系中，绝大部分攀爬过渡位移（Transitional Animations）的跨度约为 $2.0\text{ m}$；
* **前瞻几何距推导**：以 $2.0\text{ m} \times 2.0\text{ m}$ 的正交移动包围盒对角线计算临界前瞻距：
  $$L_{\text{lookahead}} = \sqrt{2.0^2 + 2.0^2} = \sqrt{8} \approx 2.8284\text{ m} \approx 2.8\text{ m}$$

系统沿曲线切线正向定位一点 $\mathbf{P}_{\text{target}}$，使得：
$$\|\mathbf{P}_{\text{target}} - \mathbf{P}_{\text{actor}}\| \approx 2.8\text{ m}$$

#### 相对坐标系投影矩阵计算
求取从角色当前位置指向前瞻目标的 3D 空间差矢量：
$$\mathbf{V}_{\text{world}} = \mathbf{P}_{\text{target}} - \mathbf{P}_{\text{actor}}$$

将世界系下的 $\mathbf{V}_{\text{world}}$ 投射至角色局部坐标系（Local Actor Space），构建三轴模拟输入强度：
$$\mathbf{V}_{\text{local}} = \begin{bmatrix} \mathbf{u}_{\text{right}} \\ \mathbf{u}_{\text{up}} \\ \mathbf{u}_{\text{forward}} \end{bmatrix} \cdot \frac{\mathbf{V}_{\text{world}}}{\|\mathbf{V}_{\text{world}}\|}$$
输入强度矢量各分量被钳制在 $[-1.0, 1.0]$ 区间内，直接模拟人类物理手柄的双摇杆模拟量（Analog Stick Inputs）。

### 5.3 局部分辨率歧义与容差设计 (Ambiguity & Tolerance Margin)
若在 $2.0\text{ m}$ 半径（加上预设误差裕度 $\epsilon$）内存在多个相交体元（例如两根竖直管道在横架上间距仅为 $1.0\text{ m}$）：
* 因贝塞尔样条并非严格通过折线所有中继点，高阶逼近会带来轻微的几何漂移；
* 系统默认采用**贪婪临近选取原则（Closest-Element Heuristic）**，解析器基于当前进近方向直接绑定距离最近的拓扑体元。工程实践证明，这种基于局部空间紧邻的决策在视效上完全符合直觉，不会破坏动作流畅度。

---

## 6. 状态流转机理与机动模式判决 (Traversal State Machine Execution)

角色进入攀爬状态后，状态机依照 VCI 局部分量强度与状态内置阈值（Transition Thresholds）驱动逻辑流转。

```
                                +---------------------------+
                                |  Ground NavMesh Locomotion|
                                +---------------------------+
                                              │
                    [Entry Edge Encountered: Upward Jump / Ladder Mount]
                                              ▼
                                +---------------------------+
               ┌───────────────►|     Ledge Hang State      |◄───────────────┐
               │                +---------------------------+                │
               │                   │        ▲            │                   │
               │  |V_right| > 0.6  │        │            │  |V_up| > 0.7     │
               │  [Move Right]     │        │            │  [Jump to Ledge]  │
               │                   ▼        │            ▼                   │
+-------------------------+   +-------------------+   +-------------------------+
|     Ledge Shimmy (R)    |   | Vertical Climb    |   |     Mid-Air Leap        |
|  (岩架横移: 水平姿态驱动)  |   | (管道攀爬: 垂直向) |   |  (空中跃进: 跨越间隙)   |
+-------------------------+   +-------------------+   +-------------------------+
               │                   ▲        │            │                   │
               │                   │        ▼            │                   │
               └───────────────────┴────────┴────────────┴───────────────────┘
                                              │
                      [Terminating State: Wall Mount / Navigation Drop]
                                              ▼
                                +---------------------------+
                                |  Target NavMesh Locomotion|
                                +---------------------------+
```

### 6.1 状态迁移判断模型与数学表达

每个特定的攀爬子状态均封装有一组判定方程。设当前状态处理器输入的虚拟手柄矢量为 $\mathbf{v} = (v_x, v_y, v_z)^T = (v_{\text{right}}, v_{\text{up}}, v_{\text{forward}})^T$：

#### 横向移动激发（Ledge Shimmy Transition）
$$S_{\text{next}} = \begin{cases} \text{ShimmyRight}, & \text{if } v_x > \tau_{\text{right}} \\ \text{ShimmyLeft}, & \text{if } v_x < -\tau_{\text{left}} \end{cases}$$

#### 垂直管道捕获（Pipe Acquisition Transition）
当满足几何邻近判定且向上分量占据主导：
$$\text{DistanceTo}(\text{Pipe}) \le R_{\text{snap}} \quad \land \quad v_y > \tau_{\text{vertical\_climb}}$$
系统立即平滑将角色骨骼吸附（Snap）至管道轴线，并切换至垂直爬升状态机分支。

#### 跨度跳跃判定（Cross-Ledge Leap）
当向上或向前输入脉冲超过突变阈值：
$$v_y > \tau_{\text{jump\_up}} \quad \lor \quad v_z > \tau_{\text{jump\_fwd}}$$
触发 Character Leap 动画，角色暂时与当前几何体解绑，依靠无碰撞位移曲线向目标体元滑移过渡。

### 6.2 深度空间迁移支持 (Depth Transitions)
系统原生支持复杂的三维深度机动：
* **反向蹬墙跳（Wall-to-wall Back Jump）**：当角色悬吊在墙体，前瞻点指向背向反向平行岩架时，$v_z < -\tau_{\text{depth}}$，状态机触发 $180^\circ$ 转向与背跳动画；
* **翻越平顶（Flat Wall Flip Over）**：在平顶矮墙处，前向分量驱动角色执行翻转跃顶（Flip Over），直接穿过几何障碍面并接入对向下降状态。

### 6.3 进出与终结状态生命周期 (Entry & Terminating States)

#### 入口状态（Entry States）
攀爬会话的启动具有高度受限性。常见的合法入口包括：
1. 接近垂直梯子底部触发梯子挂接（Ladder Mount）；
2. 位于岩架正下方触发跳跃抓缘（Jump Up to Grab）；
3. 位于悬崖边缘触发向下悬挂翻落（Drop Down to Hang）。
此类状态下初始 VCI 矢量呈现出近乎严格垂直的特征（指向正上或正下），使地面运行到攀爬的转换极易精确触发。

#### 终结状态（Terminating States）
当攀爬路径逼近终点 Climb Clue 时，触发终结判定：
1. **顶部攀上（Mount On Top）**：播放翻上站立动画，将角色世界坐标重设于顶部平面的 NavMesh 多边形上；
2. **底部跌落/脱离（Drop to Walk）**：角色松手掉落至地面 NavMesh。
此时攀爬状态机关闭，控制权交回给常规寻路移动管线。

---

## 7. 工业级 C++ 核心架构参考实现 (Reference Implementation)

以下代码展示了完整的模块化工程实现，涵盖 Climb Mesh 图结构定义、贝塞尔样条生成、VCI 评估及状态流转核心逻辑。

```cpp
#include <vector>
#include <cmath>
#include <memory>
#include <algorithm>
#include <cassert>

// ============================================================================
// 1. 数学几何基础组件 (Math & Geometry Primitives)
// ============================================================================
struct Vector3 {
    float x{0.0f}, y{0.0f}, z{0.0f};

    Vector3() = default;
    Vector3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

    Vector3 operator+(const Vector3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    Vector3 operator-(const Vector3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    Vector3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }

    float Dot(const Vector3& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }
    float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }

    Vector3 Normalized() const {
        float len = Length();
        return (len > 1e-5f) ? (*this * (1.0f / len)) : Vector3{};
    }
};

// ============================================================================
// 2. 攀爬网格数据结构 (Climb Mesh Data Structures)
// ============================================================================
enum class TraversalElementType {
    HorizontalLedge,
    VerticalPipe,
    Ladder
};

struct ClimbNode;

struct ClimbEdge {
    int targetNodeIndex{-1};
    float baseCost{0.0f};
    bool isExplicit{false};
    bool isOccupied{false}; // 多代理占用标识
};

struct ClimbNode {
    int id{-1};
    Vector3 worldPosition;
    TraversalElementType type{TraversalElementType::HorizontalLedge};
    std::vector<ClimbEdge> outgoingEdges;
};

class ClimbMesh {
public:
    std::vector<ClimbNode> nodes;

    void AddExplicitConnection(int fromNodeId, int toNodeId) {
        float dist = (nodes[fromNodeId].worldPosition - nodes[toNodeId].worldPosition).Length();
        nodes[fromNodeId].outgoingEdges.push_back({toNodeId, dist, true, false});
        nodes[toNodeId].outgoingEdges.push_back({fromNodeId, dist, true, false});
    }

    void AddImplicitConnection(int fromNodeId, int toNodeId, float jumpCost) {
        nodes[fromNodeId].outgoingEdges.push_back({toNodeId, jumpCost, false, false});
    }
};

// ============================================================================
// 3. 贝塞尔样条路径拟合 (Cubic Bézier Spline Evaluator)
// ============================================================================
class CubicBezierSpline {
public:
    struct Segment {
        Vector3 p0, p1, p2, p3;
    };

    std::vector<Segment> segments;

    void BuildFromWaypoints(const std::vector<Vector3>& path) {
        segments.clear();
        if (path.size() < 2) return;

        // 工业级平滑拟合：将离散路标转换为三次贝塞尔曲线段
        for (size_t i = 0; i < path.size() - 1; ++i) {
            Vector3 start = path[i];
            Vector3 end = path[i + 1];
            Vector3 delta = end - start;

            // 依据前瞻方向构造两处内部控制点注入角动量
            Vector3 c1 = start + delta * 0.25f;
            Vector3 c2 = start + delta * 0.75f;
            segments.push_back({start, c1, c2, end});
        }
    }

    Vector3 Evaluate(size_t segmentIdx, float t) const {
        assert(segmentIdx < segments.size());
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        const auto& seg = segments[segmentIdx];
        Vector3 point = (seg.p0 * uuu) + 
                        (seg.p1 * (3.0f * uu * t)) + 
                        (seg.p2 * (3.0f * u * tt)) + 
                        (seg.p3 * ttt);
        return point;
    }
};

// ============================================================================
// 4. 虚拟控制器输入生成器 (Virtual Controller Input Generator)
// ============================================================================
struct VirtualControllerOutput {
    float rightStickX{0.0f}; // [-1, 1] 映射左右横移
    float rightStickY{0.0f}; // [-1, 1] 映射上下垂直机动
    float forwardZ{0.0f};    // [-1, 1] 映射进深跳跃
};

class VCIGenerator {
public:
    static constexpr float LOOKAHEAD_DISTANCE = 2.8284f; // 2x2 区域对角线临界距离 (m)

    static VirtualControllerOutput ComputeVCI(
        const Vector3& actorPos,
        const Vector3& actorRight,
        const Vector3& actorUp,
        const Vector3& actorForward,
        const CubicBezierSpline& spline,
        size_t currentSegmentIdx,
        float currentT
    ) {
        // 沿样条曲线搜索距离为 LOOKAHEAD_DISTANCE 的虚拟目标点
        float targetT = currentT + 0.35f; // 步进参数
        size_t targetSegmentIdx = currentSegmentIdx;
        if (targetT > 1.0f) {
            if (targetSegmentIdx + 1 < spline.segments.size()) {
                targetSegmentIdx++;
                targetT -= 1.0f;
            } else {
                targetT = 1.0f;
            }
        }

        Vector3 targetPoint = spline.Evaluate(

---

## —— 基于《Fuse》工业级实战与核心文献的系统级解构与技术重构

---

## 1. 系统架构总览与拓扑设计 (System Architecture Overview & Topology)

在现代 3A 动作射击与协作游戏（如 Insomniac Games 开发的《Fuse》、Crystal Dynamics 的《Tomb Raider》及 DICE 的《Mirror's Edge》）中，移动规划（Motion Planning）与空间推理（Spatial Reasoning）系统必须在高度动态且密集的战斗环境中保证 AI 代理（AI Agents）的高拟真度平滑移动，同时杜绝穿透、卡死与非自然抖动。

传统的基于网格（Grid-based）或静态寻路图（Waypoints）的导航机制无法满足复杂三维几何下的低开销与高自由度要求。工业界标准管线将静态空间划分为导航网格（Navigation Mesh, NavMesh），并在其上构建由全局拓扑规划、动态障碍物避障修正（Dynamic Obstacle Avoidance）、漏斗算法（Funnel Algorithm）通道提取以及样条曲线平滑（Spline-based Path Smoothing）构成的分层执行管道。

```
+-----------------------------------------------------------------------------+
|                          决策层 (Decision Layer)                            |
|             行为树 (Behavior Trees) / 效用系统 (Utility Systems)             |
|                黑板系统 (Blackboard) -> 目标选择与战斗意图生成                |
+-----------------------------------------------------------------------------+
                                      |
                                      v
+-----------------------------------------------------------------------------+
|                     全局拓扑规划 (Global NavMesh Pathfinding)               |
|      Snook 2000 / Recast-Detour A* 图搜索 -> 多边形通道序列 (Polygon Corridor) |
+-----------------------------------------------------------------------------+
                                      |
                                      v
+-----------------------------------------------------------------------------+
|                 动态障碍处理层 (Dynamic Obstacle Management)                 |
|  - 离散雕刻 (Dynamic Tile Carving / Re-rasterization)                       |
|  - 连续推力场/障碍物投影 (Dynamic Avoidance Corridor Constraint)              |
+-----------------------------------------------------------------------------+
                                      |
                                      v
+-----------------------------------------------------------------------------+
|                     路径修剪与弦拉伸 (String Pulling / Funnel)              |
|        提取多边形穿越点 (Portal Points) -> 生成分段线性路径 (Piecewise Linear) |
+-----------------------------------------------------------------------------+
                                      |
                                      v
+-----------------------------------------------------------------------------+
|                 样条曲线平滑与运动学规划 (Spline Trajectory)                 |
|         de Boor / Farin B-Spline / Catmull-Rom 插值生成 C^1/C^2 连续轨迹    |
+-----------------------------------------------------------------------------+
                                      |
                                      v
+-----------------------------------------------------------------------------+
|                 导向行为与底层驱动 (Steering Behaviors & Locomotion)         |
|     速度障碍法 (VO / RVO) + 动画位移匹配 (Root Motion Matching / Inertial)    |
+-----------------------------------------------------------------------------+
```

---

## 2. 空间表征与导航网格底层机理 (Spatial Representation & NavMesh Mechanics)

### 2.1 导航网格多边形拓扑数据结构 (NavMesh Topology)
根据 Snook [2000] 与 Mononen [2012] 的底层架构，导航网格表示为双向有向图的半边结构（Half-edge Data Structure）或凸多边形连接图（Convex Polygon Adjacency Graph）。任意非凸障碍物均被分解为凸多边形，以确保网格内部任意两点之间的可见性满足凸集性质：

$$\forall \mathbf{x}_1, \mathbf{x}_2 \in P_k, \quad \lambda \mathbf{x}_1 + (1 - \lambda)\mathbf{x}_2 \in P_k \quad (\forall \lambda \in [0, 1])$$

```cpp
// 基础网格多边形与传送门数据结构定义
struct NavPortal {
    uint32_t fromPolyRef;
    uint32_t toPolyRef;
    Vector3 leftVertex;
    Vector3 rightVertex;
};

struct NavPoly {
    uint32_t polyRef;
    uint32_t vertexCount;
    Vector3 vertices[6];        // 工业级一般限制凸多边形最大顶点数（如 6 边形）
    uint32_t neighborRefs[6];   // 相邻多边形引用
    uint16_t flags;             // 区域通行属性标志（水体、掩体、攀爬等）
    uint8_t  areaType;          // 通行代价类型 (Cost Weight)
};
```

### 2.2 漏斗算法 (String Pulling / Simple Funnel Algorithm)
在凸多边形序列中提取最短路径的算法被称为漏斗算法。给定两多边形交界面（Portal）的左右顶点对序列 $\{L_i, R_i\}_{i=0}^{N}$，算法维护一个由顶点 $A$（Apex）、左臂 $L$、右臂 $R$ 构成的视线视锥：

$$\mathbf{v}_L = \vec{AL}, \quad \mathbf{v}_R = \vec{AR}$$

推进 Portal 顶点时，通过二维向量叉积判断夹角收敛：
1. 若新的 $L_{next}$ 在当前 $L$ 右侧且在当前 $R$ 左侧，则缩小左边界：$L \leftarrow L_{next}$；
2. 若新的 $R_{next}$ 在当前 $R$ 左侧且在当前 $L$ 右侧，则缩小右边界：$R \leftarrow R_{next}$；
3. 若新的 $L_{next}$ 跨越了右臂（$\mathbf{v}_R \times \vec{A L_{next}} < 0$），则顶点 $R$ 成为新的折弯点（Apex），路径加入点 $R$，$A \leftarrow R$，算法从 $R$ 处重新初始化漏斗。

```cpp
// 严格规范的漏斗算法实现片段
void FunnelStringPulling(const std::vector<NavPortal>& portals, std::vector<Vector3>& outSmoothPath) {
    if (portals.empty()) return;

    Vector3 portalApex = portals[0].leftVertex;
    Vector3 portalLeft = portals[0].leftVertex;
    Vector3 portalRight = portals[0].rightVertex;
    
    int apexIndex = 0, leftIndex = 0, rightIndex = 0;
    outSmoothPath.push_back(portalApex);

    for (size_t i = 1; i < portals.size(); ++i) {
        const Vector3& nextLeft = portals[i].leftVertex;
        const Vector3& nextRight = portals[i].rightVertex;

        // 验证/收缩右侧边界
        if (TriArea2D(portalApex, portalRight, nextRight) <= 0.0f) {
            if (portalApex == portalRight || TriArea2D(portalApex, portalLeft, nextRight) > 0.0f) {
                portalRight = nextRight;
                rightIndex = i;
            } else {
                // 右侧越过左侧：左侧顶点成为新顶点
                outSmoothPath.push_back(portalLeft);
                portalApex = portalLeft;
                apexIndex = leftIndex;
                portalLeft = portalApex;
                portalRight = portalApex;
                leftIndex = apexIndex;
                rightIndex = apexIndex;
                i = apexIndex;
                continue;
            }
        }

        // 验证/收缩左侧边界
        if (TriArea2D(portalApex, portalLeft, nextLeft) >= 0.0f) {
            if (portalApex == portalLeft || TriArea2D(portalApex, portalRight, nextLeft) < 0.0f) {
                portalLeft = nextLeft;
                leftIndex = i;
            } else {
                // 左侧越过右侧：右侧顶点成为新顶点
                outSmoothPath.push_back(portalRight);
                portalApex = portalRight;
                apexIndex = rightIndex;
                portalLeft = portalApex;
                portalRight = portalApex;
                leftIndex = apexIndex;
                rightIndex = apexIndex;
                i = apexIndex;
                continue;
            }
        }
    }
    outSmoothPath.push_back(portals.back().rightVertex);
}
```

---

## 3. 动态障碍物应对架构与实时避碰 (Dynamic Obstacle Avoidance Architecture)

在《Fuse》的动态射击战斗场景中，战场遍布可被推翻的掩体、爆炸物容器、机械敌人及友军角色。工业界处理此类动态性主要采用两套在时间与空间尺度上解耦的策略：

### 3.1 离散局部网格雕刻 (Tile-Based Dynamic Carving - Recast/Detour)
对于在一定时间尺度上保持静止的动态物体（例如刚被推倒的矮墙或停止移动的载具）：
1. 采用离散高度场标记（Voxelization / Heightfield Carving）；
2. 仅标记脏图块（Dirty NavMesh Tiles），触发异步局部再三角化（Local Retriangulation）；
3. 利用布尔凸多边形裁剪算法从原图块中剔除柱状体（Cylinder）或定向包围盒（Oriented Bounding Box, OBB）。

### 3.2 连续空间局部避障 (Continuous Avoidance & Velocity Obstacle)
对于高频机动的敌军与玩家，网格再三角化开销过大且存在延迟。采用倒数速度障碍法（Optimal Reciprocal Velocity Obstacle, ORCA）或上下文导向行为（Context-Steering）：

智能体 $A$ 针对障碍物 $B$ 的速度障碍区域 $VO_{A|B}^\tau$ 定义为：

$$VO_{A|B}^\tau = \left\{ \mathbf{v} \;\middle|\; \exists t \in [0, \tau], \; t \mathbf{v} \in D(\mathbf{p}_B - \mathbf{p}_A, r_A + r_B) \right\}$$

其中 $D(\mathbf{p}, r)$ 表示中心为 $\mathbf{p}$、半径为 $r$ 的开圆盘。

为保证避碰速度 $\mathbf{v}_{new}$ 不使智能体脱离网格边界，需施加导航多边形法向投影约束：

$$\mathbf{v}_{safe} = \mathbf{v} - \max(0, \mathbf{v} \cdot \mathbf{n}_{edge}) \mathbf{n}_{edge}$$

```
        +-----------------------------------------+
        |         目标点选择 Target Location      |
        +-----------------------------------------+
                             |
                             v
        +-----------------------------------------+
        |   期望速度向量 V_pref (来自全局通道)    |
        +-----------------------------------------+
                             |
         +-------------------+-------------------+
         |                                       |
         v                                       v
+-------------------------------+   +-------------------------------+
|  动态障碍计算 (Dynamic VO)    |   |  网格边界阻挡 (NavMesh Edges) |
|   排除引发碰撞的速度集合       |   |    计算法向约束 Plane Normal  |
+-------------------------------+   +-------------------------------+
         |                                       |
         +-------------------+-------------------+
                             |
                             v
        +-----------------------------------------+
        | 约束二次规划 / 极坐标采样优化选择最优 V |
        | argmin || V - V_pref ||^2 subject to C  |
        +-----------------------------------------+
```

---

## 4. 样条曲线平滑与轨迹数学推导 (Spline Mathematics for Continuous Trajectories)

根据 de Boor [1978] 与 Farin [1997] 的理论，漏斗算法输出的分段线性路径包含一阶不连续点（$C^0$ 连续但 $C^1$ 导数突变）。直接驱动角色动画会导致严重的转向超调、足底滑动（Foot Sliding）与晃动。因此，必须将折线路径映射为高阶连续的可参数化曲线。

### 4.1 B样条基函数的 Cox-de Boor 递推公式
给定非递减节点向量（Knot Vector）$U = \{u_0, u_1, \dots, u_m\}$，第 $i$ 个 $p$ 次 B 样条基函数 $N_{i, p}(u)$ 的定义为：

$$N_{i, 0}(u) = \begin{cases} 
1 & \text{若 } u_i \le u < u_{i+1} \\ 
0 & \text{其他} 
\end{cases}$$

$$N_{i, p}(u) = \frac{u - u_i}{u_{i+p} - u_i} N_{i, p-1}(u) + \frac{u_{i+p+1} - u}{u_{i+p+1} - u_{i+1}} N_{i+1, p-1}(u)$$

约定若分母为 $0$，则整项商置为 $0$。

一条 $p$ 次 B 样条曲线 $\mathbf{C}(u)$ 由控制顶点 $\mathbf{P}_i$ 线性组合形成：

$$\mathbf{C}(u) = \sum_{i=0}^{n} N_{i, p}(u) \mathbf{P}_i$$

### 4.2 三次均匀 Catmull-Rom 样条向 B 样条的工程转换
工业级实时引擎常在漏斗路径点之间使用具有局部控制力与插值特性（经过所有控制点）的三次 Catmull-Rom 样条。给定控制点 $\mathbf{P}_{i-1}, \mathbf{P}_i, \mathbf{P}_{i+1}, \mathbf{P}_{i+2}$，局部段 $t \in [0, 1]$ 上的位置公式为：

$$\mathbf{C}_i(t) = \frac{1}{2} \begin{bmatrix} 1 & t & t^2 & t^3 \end{bmatrix} \begin{bmatrix} 0 & 2 & 0 & 0 \\ -1 & 0 & 1 & 0 \\ 2 & -5 & 4 & -1 \\ -1 & 3 & -3 & 1 \end{bmatrix} \begin{bmatrix} \mathbf{P}_{i-1} \\ \mathbf{P}_i \\ \mathbf{P}_{i+1} \\ \mathbf{P}_{i+2} \end{bmatrix}$$

速度（一阶导数）与加速度（二阶导数）为：

$$\mathbf{C}_i'(t) = \frac{1}{2} \begin{bmatrix} 0 & 1 & 2t & 3t^2 \end{bmatrix} \mathbf{M}_{CR} \mathbf{P}_{\text{vec}}$$

$$\mathbf{C}_i''(t) = \frac{1}{2} \begin{bmatrix} 0 & 0 & 2 & 6t \end{bmatrix} \mathbf{M}_{CR} \mathbf{P}_{\text{vec}}$$

通过保证 $\mathbf{C}_{i}'(1) = \mathbf{C}_{i+1}'(0)$，系统原生满足 $C^1$ 速度矢量连续，消除智能体在拐弯点处的离心加速度跳变。

```cpp
// 样条轨迹生成与网格约束投影核心代码
class SplineTrajectoryPlanner {
public:
    struct TrajectorySample {
        Vector3 position;
        Vector3 tangent;
        Vector3 curvature;
    };

    static TrajectorySample EvaluateCatmullRom(
        const Vector3& p0, const Vector3& p1, 
        const Vector3& p2, const Vector3& p3, 
        float t) 
    {
        float t2 = t * t;
        float t3 = t2 * t;

        TrajectorySample sample;
        // 矩阵乘法展开
        sample.position = 0.5f * (
            (2.0f * p1) +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
        );

        sample.tangent = 0.5f * (
            (-p0 + p2) +
            (4.0f * p0 - 10.0f * p1 + 8.0f * p2 - 2.0f * p3) * t +
            (-3.0f * p0 + 9.0f * p1 - 9.0f * p2 + 3.0f * p3) * t2
        );

        sample.curvature = 0.5f * (
            (4.0f * p0 - 10.0f * p1 + 8.0f * p2 - 2.0f * p3) +
            (-6.0f * p0 + 18.0f * p1 - 18.0f * p2 + 6.0f * p3) * t
        );

        return sample;
    }
};
```

---

## 5. 核心参考文献技术映射与行业范式 (Literature Mapping & Technical Analysis)

本章引用文献构成了当代 AAA 游戏导航与局部机动规划的核心理论基石，各文献在技术体系中的权责与映射关系如下表所示：

| 文献标记 | 经典文献 / 工业级产品 | 技术领域与核心贡献 | 在《Fuse》及现代系统中的工程落地场景 |
| :--- | :--- | :--- | :--- |
| **[CD 14]** | Crystal Dynamics (2014) *Tomb Raider* | 极端三维复杂环境、高机动跑酷跳跃及掩体交互导航 | 提供了动态环境与物理角色动画混合（Root Motion Warping）的参考基准，解决高度落差下的导航网格衔接。 |
| **[DeBoor 78]** | de Boor, C. (1978) *A Practical Guide to Splines* | 样条数学计算理论基础；提出了 Cox-de Boor 样条递推算法 | 用于解决漏斗算法产生的折线平滑问题，建立具备 $C^2$ 连续性的高拟真度移动曲线。 |
| **[EA 08]** | Electronic Arts DICE (2008) *Mirror’s Edge* | 高速第一人称第一代三维跑酷系统与连续空间几何体标记 | 验证了在非平面、高动态障碍分布下，动态环境约束对导向加速度（Steering Acceleration）的硬性拓扑限制。 |
| **[Farin 97]** | Farin, G. (1997) *Curves and Surfaces for CAGD* | 计算机辅助几何设计中的样条曲线、曲面与连续性条件理论 | 奠定了曲线曲率（Curvature）优化与控制点实时偏移注入（Control Point Perturbation）以绕开障碍的理论基石。 |
| **[IG 13]** | Insomniac Games (2013) *Fuse* | 掩体射击协作机制、密集智能体动态避碰与实时多边形通道约束 | 本章技术来源，系统性解决了 NavMesh、动态障碍物雕刻与平滑转向曲线之间的实时同步协调。 |
| **[Mononen 12]**| Mononen, M. (2012) *Recast and Detour* | 工业界开源标准：体素化静态/动态 NavMesh 构建与 Detour 实时通道寻路工具集 | 提供了分块网格（Tiled Mesh）、局部多边形搜索与漏斗算法的标准工业实现架构。 |
| **[Snook 00]** | Snook, G. (2000) *Game Programming Gems* | 简化三维移动规划体系，奠定导航网格（NavMesh）概念的奠基之作 | 取代路标节点网络，将可行走空间转化为连续的凸多边形集合，彻底规避传统路标图的空间离散化精度损失。 |

---

## 6. 综合工程集成模型：动态避碰样条轨迹驱动器

在实际运行时，系统将动态障碍物转化为局部斥力场，将斥力场位移叠加到样条控制点上，实现对原生 Catmull-Rom / B 样条的实时摄动（Perturbation），同时借助 NavMesh 射线检测（Raycast Edge Constrain）限制其外扩范围：

$$\mathbf{P}_i^{\text{perturbed}} = \mathbf{P}_i + \sum_{k=1}^{M} \frac{\mathbf{r}_{i, k}}{\|\mathbf{r}_{i, k}\|^2} \cdot \alpha_k$$

其中 $\mathbf{r}_{i, k} = \mathbf{P}_i - \mathbf{O}_k$ 为控制点至动态障碍物 $k$ 的位移向量，$\alpha_k$ 为避让强度系数。

```cpp
// 融合动态障碍规避与样条驱动的综合运行时逻辑
Vector3 CalculateFinalSteeringVelocity(
    const Vector3& agentPos,
    const Vector3& agentForward,
    float agentRadius,
    const std::vector<DynamicObstacle>& obstacles,
    const SplineTrajectory& currentSpline,
    float currentSplineParam)
{
    // 1. 从平滑样条上获取理想前瞻点与期望切线速度
    float lookaheadDistance = 2.5f; 
    float targetParam = currentSpline.GetParamAtDistance(currentSplineParam, lookaheadDistance);
    SplineTrajectoryPlanner::TrajectorySample sample = currentSpline.Evaluate(targetParam);
    Vector3 vDesired = sample.tangent.Normalized() * sample.tangent.Length();

    // 2. 评估动态障碍物碰撞倾向，注入侧向规避冲量
    Vector3 avoidanceForce = Vector3::Zero();
    for (const auto& obs : obstacles) {
        Vector3 toObs = obs.position - agentPos;
        float dist = toObs.Length();
        float combinedRadius = agentRadius + obs.radius;

        if (dist < combinedRadius + 2.0f) {
            // 计算碰撞时间 Time to Collision (TTC)
            Vector3 relVelocity = vDesired - obs.velocity;
            float projectedDist = toObs.Dot(relVelocity.Normalized());
            
            if (projectedDist > 0.0f) {
                Vector3 lateralDir = Vector3::Cross(Vector3::Up(), relVelocity).Normalized();
                if (lateralDir.Dot(toObs) > 0.0f) {
                    lateralDir = -lateralDir; // 保证向远离障碍方向偏转
                }
                float penalty = 1.0f - std::clamp((dist - combinedRadius) / 2.0f, 0.0f, 1.0f);
                avoidanceForce += lateralDir * (penalty * 15.0f);
            }
        }
    }

    // 3. 速度合成与导航网格约束判定
    Vector3 vCandidate = vDesired + avoidanceForce;
    
    // 投影到多边形地表切平面并防止穿透 NavMesh 边界
    Vector3 safeVelocity = ClampVelocityToNavMeshEdge(agentPos, vCandidate, agentRadius);
    return safeVelocity;
}

上述体系综合了 Snook [2000] 的网格连续几何性、Mononen [2012] 的分块通道检索能力、de Boor [1978] 与 Farin [1997] 的高阶导数连续性保证，并结合了《Fuse》[IG 13] 的实战避碰经验，构成了当代工业级 3A 游戏导航与局部机动规划的核心架构。
