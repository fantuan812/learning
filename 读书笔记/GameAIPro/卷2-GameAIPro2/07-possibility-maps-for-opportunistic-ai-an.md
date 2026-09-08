---
type: Reference
title: "第7章 Possibility Maps for Opportunistic AI and Believable Worlds"
description: "Game AI Pro 工业级精读：Possibility Maps for Opportunistic AI and Believable Worlds。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第7章 Possibility Maps for Opportunistic AI and Believable Worlds

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 7.  
> 原文作者 / 资源：[Possibility Maps for Opportunistic AI and Believable Worlds](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter07_Possibility_Maps_for_Opportunistic_AI_and_Believable_Worlds.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

---

## 1. 概述与核心哲学体系（Introduction & Core Philosophy）

### 1.1 传统全状态模拟的计算瓶颈
在现代复杂游戏体系中，游戏世界状态（Game World State）由所有实体的离散与连续属性共同构成：
* 玩家状态（Player State）：空间坐标 $\mathbf{x}_p \in \mathbb{R}^3$、朝向 $\mathbf{q}_p \in \mathbb{H}$、生命值 $h_p \in \mathbb{R}^+$、物品清单（Inventory）等；
* 非玩家角色状态（NPC State）：空间拓扑位置、内部黑板（Blackboard）变量、感知状态、行为树（Behavior Trees, BT）执行节点、情感状态等；
* 静态与动态环境交互对象状态（Inanimate Object State）：门锁机制、建筑破坏度、电梯层级等。

传统架构倾向于对整个世界施加全局显式模拟（Explicit Simulation），针对未观察区域则采用常规的系统级细节层次（Level-of-Detail, LOD）技术（如降低 Tick 频率、剥离动画求值与物理碰撞）。然而，只要底层仍然维持状态的确定性演化，AI 就必须承担昂贵的远距离路径搜索（Pathfinding）、分层任务网络规划（Hierarchical Task Networks, HTN）以及长周期行为推导。这种机制在面对超视距（Beyond Visual Range, BVR）实体时，造成了严重的 CPU 预算与内存带宽损耗。

### 1.2 延迟决策与机会主义 AI（Opportunistic AI）
可能性地图（Possibility Maps）与概率地图（Probability Maps）打破了“实体必须时刻具备确切物理状态”的传统假设，转而针对玩家未观察到的游戏世界要素（Unobserved Elements of Game State）维护离散或连续的空间/状态分布。

```
                   [ 玩家直接视野内 (Observed) ]
                                |
             +------------------+------------------+
             |                                     |
             v                                     v
   [ 显式模拟引擎 (LOD 0) ]              [ 可能性/概率空间 (Unobserved) ]
   * 行为树 / 分层任务网络 (HTN)         * 延迟决策 (Deferred Decision)
   * 导向行为 (Steering Behaviors)       * 广度波前扩散 (Wavefront Propagation)
   * 精确物理 / 导航网格 (NavMesh)       * 塌缩即实例化 (Observation -> Collapse)
```

其核心架构哲学在于**延迟决策（Deferred Decision-Making）**：
1. **免除先验规划**：对于未观察实体，系统不对其行为进行过早确定（Commitment），而是推迟其具体状态的实例化（Instantiation），直至玩家的感知系统（视野截锥体或听觉范围）即将介入该状态空间。
2. **构建超常智力假象（Illusion of Intelligence）**：由于 AI 拥有在玩家视线外自由收敛状态的权利，它可以在实体被观察到的瞬间，根据当前的宏观战术需求，选择最具进攻性、戏剧性或战术协同度的状态进行实例化（例如：绕后突袭、交叉火力钳形攻势、恰好获取强化道具）。
3. **严格遵从可观察历史（Consistency Enforcement）**：AI 的这种“作弊”被严格约束在玩家的观察历史（Observational History）边界内。只要状态的演化在物理法则与玩家感知盲区内是合法的，玩家就会认为 NPC 制定并执行了一个极其复杂、环环相扣的高级战术规划。

---

## 2. 概率地图与可能性地图的数学基础与状态表征（Foundations & Representations）

### 2.1 状态空间定义
将不可见子系统的状态空间离散化为集合 $S = \{s_1, s_2, \dots, s_n\}$。例如，对于村庄中的 NPC，状态空间可以是其可能身处的离散拓扑区域：
$$S = \{s_{\text{home}}, s_{\text{stables}}, s_{\text{tavern}}, s_{\text{well}}, s_{\text{shop}}, s_{\text{road\_1}}, \dots, s_{\text{road\_}k}\}$$

### 2.2 概率地图（Probability Maps）
概率地图是一个映射函数 $\mathcal{P}: S \to [0, 1]$，表示系统在特定时刻处于状态 $s_i$ 的相对似然度（Likelihood），必须严格满足全概率归一化公理：
$$\sum_{i=1}^{n} \mathcal{P}(s_i) = 1, \quad \forall s_i \in S: \mathcal{P}(s_i) \ge 0$$

系统通过离散马尔可夫转移核（Markov Transition Kernel）进行演化。定义状态转移矩阵 $\mathbf{T} \in \mathbb{R}^{n \times n}$，其中元素 $T_{ij} = P(s_t = j \mid s_{t-1} = i)$ 满足 $\sum_{j=1}^n T_{ij} = 1$。
在第 $t$ 步的概率向量分布为：
$$\mathbf{p}_t = \mathbf{p}_{t-1} \mathbf{T}$$

### 2.3 可能性地图（Possibility Maps）
可能性地图是对概率地图的布尔简化，剥离了具体的浮点标量似然度，仅保留状态在逻辑上的“可达性”或“相容性”：
$$\mathcal{M}: S \to \{0, 1\}$$
其中 $\mathcal{M}(s_i) = 1$ 表示实体在当前时间点在逻辑上“可能”存在于状态 $s_i$；$\mathcal{M}(s_i) = 0$ 则表示绝对不可达或被观测排斥。

### 2.4 两者工程特性的深度对比

| 评估维度 | 可能性地图（Possibility Maps） | 概率地图（Probability Maps） |
| :--- | :--- | :--- |
| **数学域（Domain）** | 二值布尔空间 $\mathbb{B} = \{0, 1\}$ | 连续实数概率区间 $[0, 1] \subset \mathbb{R}$ |
| **核心算法复杂度** | 位运算（Bitsets）、图遍历、元胞自动机 | 矩阵乘法、归一化、浮点累加 |
| **稳态收敛性** | 若系统处于开放连通图，全图迅速置 1 饱和并停止计算 | 转移矩阵随昼夜变换时永不静止，须持续迭代 |
| **运算开销（CPU/Memory）** | 极低（可用位掩码紧凑存储，更新为布尔 OR） | 中等偏高（浮点运算，需要反复归一化） |
| **核心应用定位** | 制造战术智力假象（Illusion of Intelligence）、伏击 | 宏观生态模拟、全域 LOD 逼近、拟真作息 |

---

## 3. 可能性地图的运行机制与延迟实例化（Using Possibility Maps）

### 3.1 传播规则（Propagation Rules）与空间波前扩散
可能性地图的状态演化受制于预设的物理运动学约束规则。若 $S$ 映射为网格化空间（Grid）或导航网格（NavMesh）凸多边形节点图，可能性的扩散本质上是波前扩展（Wavefront Propagation）：

$$\mathcal{M}_{t}(s_i) = \mathcal{M}_{t-1}(s_i) \lor \left( \bigvee_{s_j \in \operatorname{Adj}(s_i)} \left( \mathcal{M}_{t-1}(s_j) \land \text{CanTraverse}(s_j, s_i) \right) \right)$$

其中 $\operatorname{Adj}(s_i)$ 为拓扑相邻状态节点，$\text{CanTraverse}$ 包含实体移动速度、跨越高度障碍能力及物理开销约束。

```
Time 0: 视线内丢失               Time 1: 一阶相邻扩散             Time 2: 二阶扩散(可绕后)
[ . ][ . ][ . ]                [ . ][ ? ][ . ]                [ ? ][ ? ][ ? ]
[ X ][ X ][ X ]                [ X ][ X ][ X ]                [ X ][ X ][ X ]
[ . ][ N ][ ? ]  (向东脱离) ->   [ . ][ . ][ ? ]  (继续东向)  ->   [ . ][ . ][ ? ]
[ X ][ P ][ X ]                [ X ][ P ][ X ]                [ ? ][ P ][ X ]  <-- 致命背刺可能!
```

### 3.2 玩家感知干涉与可能性阻断（Perceptual Invalidation）
可能性的传播并非仅由图的连通度决定，还受玩家感知边界的动态裁剪：
1. **视野截锥体（View Frustum / Line of Sight）**：若射线检测显示从玩家眼睛位置 $\mathbf{x}_p$ 到候选状态节点 $s_k$ 无遮挡且在视野内，而玩家在该处未观察到实体，则强制置零：
   $$\forall s_k \in \operatorname{Frustum}(P), \quad \mathcal{M}(s_k) \leftarrow 0$$
2. **听觉表面阻断（Acoustic / Surface Constraints）**：若从 $s_j \to s_i$ 经过高噪声表面（Noisy Surfaces，如积水、碎石），且该表面处于玩家听觉感知阈值 $R_{\text{hear}}$ 内，则波前阻断：
   $$\text{CanTraverse}(s_j, s_i) = \text{False} \quad \text{if } \operatorname{Dist}(\text{Surface}, \mathbf{x}_p) \le R_{\text{hear}}$$
   除非 AI 决定在此处显式实例化该实体并播放声音脚步，否则二值标记不可穿透该表面。
3. **潜行属性约束（Stealth Capability）**：若实体无静音潜行能力，玩家近身警戒圆（Proximity Circle）以内的格子直接置零，杜绝近身穿透。

### 3.3 观测触发的波函数坍缩机制（Collapse upon Observation）
当玩家移动或旋转视角，导致某些在可能性地图中标记为 `1` 的状态节点落入可被观测区域时，AI 必须立刻作出离散决策：

```
                [ 玩家视线即将扫过状态 s_k 且 M(s_k) == 1 ]
                                    |
                                    v
                       [ AI 战术效用评估 (Utility) ]
                                    |
            +-----------------------+-----------------------+
            |                                               |
            v                                               v
    [ 决策：实例化该状态 ]                        [ 决策：保持隐蔽 ]
            |                                               |
            * 选定 s_k 实例化 NPC 物理实体                   * 强制标记 M(s_k) = 0
            * 所有互斥状态清空: ∀j≠k, M(s_j) = 0             * 可能性波前从其他未观察路径继续扩散
            * 注入具体行为树/战术黑板 (如：发起背刺)        * 玩家视线内呈现“空无一物”
```

* **强制实例化（Forced Instantiation）**：若在经过视野裁剪后，整个地图中仅剩唯一一个可能状态 $s_{\text{last}}$ 满足 $\mathcal{M}(s_{\text{last}}) = 1$，AI 丧失选择权，必须在该节点处实例化实体，以维持世界因果逻辑的连续性。

---

## 4. 战术机会主义实战解析（Tactical Opportunism Cases）

### 4.1 空间背刺与多路径伏击（Spatial Ambush）
参考典型回字形或走廊地图（如图 7.1 拓扑）：
* **状态演化序列**：
  * **(a) 初始对峙**：玩家 $P$ 直面 NPC $N$。NPC 真实坐标确定，可能性仅集中于单个单元格。
  * **(b) 视线丢失**：NPC 向东移动绕入墙体后方。可能性标记沿东侧走廊向北、向南呈波前扩展。
  * **(c) 绕后潜伏**：经过数秒扩散，波前不仅延伸至北侧通道，更通过南部环路回折至玩家背后。此时，由于玩家持续面朝北，背后单元格均为未观察区域，可能性为 1。AI 瞬间获得就地实例化并执行近战背刺（Melee Attack）的机会。
  * **(d)-(e) 动态转角对峙**：玩家向北推进三格并向右转向。转向瞬间，玩家视野即将覆盖东侧走廊。此时 AI 具有决策分水岭：
    * 方案 A：在玩家西侧后方实例化（远程偷袭）；
    * 方案 B：在玩家正南侧实例化（近战偷袭）；
    * 方案 C：在东侧视野边缘实例化（正面接敌）；
    * 方案 D：拒绝在东侧显式出现，东侧单元格标记被动置 0，波前从西侧继续扩散。

### 4.2 多 NPC 隐式协同攻击（Multi-Agent Coordinated Flanking）
若存在两个 NPC $N_1$ 与 $N_2$，传统架构需要规划协同包夹算法（Coordinated Flanking Planner），计算汇合时间点（Time-to-Target）。
使用可能性地图时：
* 系统为 $N_1$ 与 $N_2$ 各自独立维护一个可能性地图 $\mathcal{M}_1$ 与 $\mathcal{M}_2$。
* $N_1$ 向东隐蔽，$N_2$ 向西隐蔽。随着时间推移，$\mathcal{M}_1$ 覆盖东侧及后方，$\mathcal{M}_2$ 覆盖西侧及后方。
* 当玩家转动朝向时，AI **联合评估（Joint Evaluation）**两个地图：
  $$\text{Pose}(N_1, N_2) = \arg\max_{s_a \in \mathcal{M}_1, s_b \in \mathcal{M}_2} \operatorname{Utility}_{\text{ambush}}(s_a, s_b \mid \mathbf{x}_p)$$
* AI 可瞬间在玩家后侧与侧面同时实例化两名实体，在玩家视角中呈现出一种经过精密战术计时（Tactical Timing）的钳形包夹攻击（Coordinated Attack）。

```
        [ 协同伏击方案拓扑 ]
           [ 前方 (North) ]
                  |
    西侧 (N2) <-- 玩家 (P) --> 东侧 (N1)
                  |
           [ 后方 (N1/N2) ]
```

---

## 5. 动态状态空间分裂与分支爆炸（State Space Forking）

### 5.1 交互动作与状态空间分支（Forking Mechanization）
当未观察实体的可能性波前触及可交互环境资源（如医疗包 Health Pack $H$、钥匙 Key、控制开关 Switch）时，实体是否与该物品发生交互构成了离散状态的分野。

若 AI 不愿在波前触碰道具的瞬间强行决定其是否拾取，系统必须执行**地图分支分裂（Forking）**：

```
                    [ 原始可能波前到达道具 H 处 ]
                                  |
               +------------------+------------------+
               |                                     |
               v                                     v
       [ 分支 1: 未拾取道具 ]                [ 分支 2: 已拾取道具 ]
      M_{N}(s, has_item = False)            M_{N}(s, has_item = True)
               |                                     |
    * 初始波前为当前的延续                * 初始状态唯一: 仅在 H 位置为 1
    * 实体具有较低生命值                  * 实体生命值已回满 / 攻击加成
    * 保持更宽广的扩散范围                * 波前从道具点开始重新向外扩散
```

### 5.2 战术延迟价值与组合爆炸控制
分支带来的直接战术收益：在玩家经过若干秒探索后（如图 7.2c 与 7.2d），若玩家此时生命值低、防御弱，AI 可以坍缩至“分支 1”，在近距离实例化低血量近战 NPC；若玩家全副武装，AI 可以坍缩至“分支 2”，在远端走廊实例化已吃药恢复、手持远程武器的强化 NPC。

**组合爆炸问题（Combinatorial Explosion）**：
若环境存在 $K$ 个未观察可交互道具，每个道具触发一次分支，地图数量将呈指数级递增：
$$N_{\text{maps}} = \mathcal{O}(2^K)$$
**工程剪枝策略**：
1. **最大延迟窗口（Max Deferral Timeout）**：设定状态分支的存在寿命，超时后依据启发式效用强制塌缩合并。
2. **容量上限淘汰（Capacity Eviction）**：限制单实体的活跃可能性地图数量（如 $N_{\max} \le 4$），超额时提前将概率/可能性收敛至期望价值最高的分支。

---

## 6. 概率地图的数学推导与马尔可夫链演化（Probability Maps & Markov Models）

### 6.1 离散时间马尔可夫更新
考虑守卫 NPC 的行为状态集合：
$$S = \{s_1: \text{巡逻 } A \to B, \; s_2: \text{巡逻 } B \to A, \; s_3: \text{睡觉}, \; s_4: \text{就餐}, \; s_5: \text{纸牌消遣}\}$$

定义状态转移矩阵 $\mathbf{T}$（对应文献 Table 7.1）：

$$\mathbf{T} = \begin{pmatrix}
0.00 & 1.00 & 0.00 & 0.00 & 0.00 \\
0.95 & 0.00 & 0.00 & 0.05 & 0.00 \\
0.04 & 0.00 & 0.95 & 0.01 & 0.00 \\
0.18 & 0.00 & 0.01 & 0.80 & 0.01 \\
0.50 & 0.00 & 0.50 & 0.00 & 0.00
\end{pmatrix}$$

系统状态分布向量 $\mathbf{p}_t = \begin{bmatrix} p(s_1) & p(s_2) & p(s_3) & p(s_4) & p(s_5) \end{bmatrix}$。

#### 时序演化推导（Table 7.2 还原）
初始时刻 $t=1$，玩家刚刚目击守卫正从 $B$ 走向 $A$，此时确定处于状态 $s_2$：
$$\mathbf{p}_1 = \begin{bmatrix} 0.000 & 1.000 & 0.000 & 0.000 & 0.000 \end{bmatrix}$$

迭代递推：$\mathbf{p}_{t+1} = \mathbf{p}_t \mathbf{T}$

* **$t=2$ 计算**：
  $$\mathbf{p}_2 = \mathbf{p}_1 \mathbf{T} = \begin{bmatrix} 0.950 & 0.000 & 0.000 & 0.050 & 0.000 \end{bmatrix}$$
* **$t=3$ 计算**：
  * $p_3(s_1) = 0.950 \times 0.00 + 0.050 \times 0.18 = 0.009$
  * $p_3(s_2) = 0.950 \times 1.00 + 0.050 \times 0.00 = 0.950$
  * $p_3(s_3) = 0.050 \times 0.01 = 0.0005 \approx 0.001$
  * $p_3(s_4) = 0.050 \times 0.80 = 0.040$
  * $p_3(s_5) = 0.050 \times 0.01 = 0.0005 \approx 0.000$
  $$\mathbf{p}_3 = \begin{bmatrix} 0.009 & 0.950 & 0.001 & 0.040 & 0.000 \end{bmatrix}$$
* **$t=4$ 计算**：
  * $p_4(s_1) = 0.009 \times 0.00 + 0.950 \times 0.95 + 0.001 \times 0.04 + 0.040 \times 0.18 = 0.9025 + 0.0072 = 0.9097 \approx 0.910$
  * $p_4(s_2) = 0.009 \times 1.00 = 0.009$
  * $p_4(s_3) = 0.001 \times 0.95 + 0.040 \times 0.01 = 0.00095 + 0.0004 = 0.00135 \approx 0.001$
  * $p_4(s_4) = 0.950 \times 0.05 + 0.040 \times 0.80 = 0.0475 + 0.032 = 0.0795 \approx 0.080$
  * $p_4(s_5) = 0.040 \times 0.01 = 0.0004 \approx 0.000$
  $$\mathbf{p}_4 = \begin{bmatrix} 0.910 & 0.009 & 0.001 & 0.080 & 0.000 \end{bmatrix}$$
* **$t=5$ 计算**：
  * $p_5(s_1) = 0.009 \times 0.95 + 0.001 \times 0.04 + 0.080 \times 0.18 = 0.00855 + 0.0144 = 0.02299 \approx 0.023$
  * $p_5(s_2) = 0.910 \times 1.00 = 0.910$
  * $p_5(s_3) = 0.001 \times 0.95 + 0.080 \times 0.01 = 0.00095 + 0.0008 = 0.00175 \approx 0.002$
  * $p_5(s_4) = 0.009 \times 0.05 + 0.080 \times 0.80 = 0.00045 + 0.064 = 0.06445 \approx 0.064$
  * $p_5(s_5) = 0.080 \times 0.01 = 0.0008 \approx 0.001$
  $$\mathbf{p}_5 = \begin{bmatrix} 0.023 & 0.910 & 0.002 & 0.064 & 0.001 \end{bmatrix}$$
* **$t=6$ 计算**：
  * $p_6(s_1) = 0.910 \times 0.95 + 0.002 \times 0.04 + 0.064 \times 0.18 + 0.001 \times 0.50 = 0.8645 + 0.01152 + 0.0005 = 0.87652 \approx 0.877$
  * $p_6(s_2) = 0.023 \times 1.00 = 0.023$
  * $p_6(s_3) = 0.002 \times 0.95 + 0.064 \times 0.01 + 0.001 \times 0.50 = 0.0019 + 0.00064 + 0.0005 = 0.00304 \approx 0.003$
  * $p_6(s_4) = 0.910 \times 0.05 + 0.064 \times 0.80 = 0.0455 + 0.0512 = 0.0967 \approx 0.097$
  * $p_6(s_5) = 0.064 \times 0.01 = 0.00064 \approx 0.000$
  $$\mathbf{p}_6 = \begin{bmatrix} 0.877 & 0.023 & 0.003 & 0.097 & 0.000 \end{bmatrix}$$

### 6.2 观测更新中的贝叶斯条件坍缩（Bayesian Observation Collapse）
在 $t=6$ 时，玩家突然踏入餐厅（Dining Hall），对应可观察状态为 $s_4$（就餐）。此时系统进行条件采样：
* **分支 A（发生实例化）**：以当前先验概率 $P = 0.097$ 将守卫直接实例化在餐厅中。
  此时状态强制坍缩为：$\mathbf{p}_{\text{collapse}} = \begin{bmatrix} 0 & 0 & 0 & 1 & 0 \end{bmatrix}$。
* **分支 B（未发生实例化）**：若判定未生成（概率 $1 - 0.097 = 0.903$），则守卫断然不在餐厅中，即已知证据 $\neg s_4$。
  必须剔除 $s_4$ 分量，并利用贝叶斯法则对余下状态执行重归一化（Renormalization）：
  $$\mathcal{P}_{\text{new}}(s_k) = \frac{\mathcal{P}_{\text{prior}}(s_k)}{1 - \mathcal{P}_{\text{prior}}(s_4)}, \quad \forall k \neq 4$$
  
  代入数值：
  * $p_6^*(s_1) = \frac{0.877}{1 - 0.097} = \frac{0.877}{0.903} \approx 0.970$
  * $p_6^*(s_2) = \frac{0.023}{0.903} \approx 0.026$
  * $p_6^*(s_3) = \frac{0.003}{0.903} \approx 0.003$
  * $p_6^*(s_4) = 0.000$
  * $p_6^*(s_5) = \frac{0.00064}{0.903} \approx 0.001$

  $$\mathbf{p}_{6 \mid \neg s_4} = \begin{bmatrix} 0.970 & 0.026 & 0.003 & 0.000 & 0.001 \end{bmatrix}$$
  这正是文献 Table 7.2 最后一行“6 after observation”的严谨数学推导来源。

---

## 7. 静态概率与可能性混合架构（Hybrid Framework）

### 7.1 纯动态概率地图的工程缺陷
1. **昼夜时变转移矩阵导致状态永不收敛**：现实游戏中的转移概率 $\mathbf{T}(\text{time})$ 强依赖游戏时钟（例如夜间睡眠概率飙升）。因此，即便玩家长期不在该区域，系统仍必须对每个离散时间片持续进行矩阵相乘，造成空转消耗。
2. **参数调校困难**：人工设计一个高维且在任意长期迭代下均表现合理、不出现概率病态吸收的转移矩阵 $\mathbf{T}$ 极为繁琐。

### 7.2 混合架构设计模式（The Hybrid Pattern）
为彻底规避上述缺陷， Manslow 提出了静态先验概率（Static Probability Map）与可能性地图（Possibility Map）耦合的混合架构：

```
       [ 静态概率地图 P_static(s) ]           [ 动态可能性地图 M(s) ]
       * 依作息时间查表 (Table 7.3)           * 根据物理移动/障碍动态扩散
       * 代表无先验时的静态似然度             * 维护玩家观测边界 (0 或 1)
                     \                              /
                      \                            /
                       v                          v
                     [ 掩码筛选 (Masking Step) ]
                     P_valid(s) = P_static(s) * M(s)
                                  |
                                  v
                   [ 归一化重加权 (Renormalization) ]
                   P_instantiate(s) = P_valid(s) / ∑ P_valid
```

### 7.3 混合架构数值推演实例
采用文献 Table 7.3 的守卫静态分布：

| 时段（Time） | 巡逻 $A \to B$ | 巡逻 $B \to A$ | 睡觉（Sleep） | 就餐（Eat） | 纸牌（Solitaire） |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **工作日（Working day）** | 0.495 | 0.495 | 0.001 | 0.003 | 0.001 |
| **就餐时间（Mealtimes）** | 0.010 | 0.010 | 0.000 | 0.970 | 0.010 |
| **夜间（Night）** | 0.002 | 0.002 | 0.990 | 0.003 | 0.003 |

**情境**：
当前时间为“就餐时间（Mealtimes）”。
1. 玩家进入并观察了餐厅，发现守卫不在餐厅中（排除了“就餐”状态，该状态可能性在可能性地图中被置 0）。
2. 此时可能性地图中依然为 1 的状态为：$\{\text{巡逻 } A \to B, \; \text{巡逻 } B \to A, \; \text{纸牌消遣}\}$。
3. 玩家紧接着走向游戏室（Games Room，对应纸牌消遣状态）。AI 需要判定守卫是否应该被实例化在游戏室内。
4. **计算过程**：
   * 查表得出所有相容状态的静态概率权重和：
     $$\Omega = P_{\text{static}}(A \to B) + P_{\text{static}}(B \to A) + P_{\text{static}}(\text{Solitaire}) = 0.010 + 0.010 + 0.010 = 0.030$$
   * 亦即：$1 - P_{\text{static}}(\text{Eat}) = 1 - 0.970 = 0.030$。
   * 计算游戏室内实例化的重加权概率：
     $$P(\text{Solitaire} \mid \neg \text{Eat}) = \frac{0.010}{1 - 0.970} = \frac{0.010}{0.030} = 0.333$$
5. AI 将以精确的 $33.3\%$ 几率在玩家踏入游戏室时将守卫就地生成；若未生成，则该状态置 0，余下的两次巡逻状态各自平分剩余概率（各占 $50\%$）。

**计算休眠特性**：当玩家完全离开该区域一段时间后，可能性地图中的所有节点全部变为 `1`（波前完全饱和）。此时，可能性地图退化为全通掩码，**系统无需执行任何更新运算**，直到玩家未来重新返回并产生新的观测输入。

---

## 8. 状态因子化与依赖解耦拓扑（Factorizing Game State）

### 8.1 维度灾难与状态独立性假设（Factorization Principle）
完整的游戏世界状态由高维联合空间构成：
$$\mathbf{S}_{\text{world}} = S_{\text{NPC}_1} \times S_{\text{NPC}_2} \times \dots \times S_{\text{NPC}_M} \times S_{\text{Env}_1} \times \dots \times S_{\text{Env}_K}$$

直接对 $\mathbf{S}_{\text{world}}$ 求解可能性或概率分布在工程上是不可行的。必须执行**因子化（Factorization）**：将其拆解为若干满足条件独立性（Conditional Independence）的低维独立地图子系统：
$$\mathcal{M}(\mathbf{S}_{\text{world}}) = \prod_{m} \mathcal{M}(S_{\text{NPC}_m})$$

### 8.2 强耦合依赖场景的工程解耦方案

#### 场景 1：实体协同交互依赖（例如：警卫打扑克 Poker）
* **依赖矛盾**：纸牌消遣单人即可进行（Solitaire），但打扑克（Poker）必须满足游戏室内的守卫人数 $N_{\text{guards}} \ge 2$。若单独随机生成，极易出现孤身一人在桌前打扑克的穿帮逻辑漏洞。
* **解耦架构解决方案**：
  1. 当玩家推开游戏室大门时，系统对所有激活的守卫可能性地图进行**交叉查询（Cross-Querying）**，计算能够抵达游戏室的守卫集合：
     $$C_{\text{poker}} = \{ g \in \text{Guards} \mid \mathcal{M}_g(s_{\text{poker}}) == 1 \}$$
  2. 若 $|C_{\text{poker}}| \ge 2$，则允许触发扑克事件，并在该集合中挑选 NPC 共同在游戏室内实例化；
  3. 若仅有一名守卫能够抵达（$|C_{\text{poker}}| < 2$），则禁止在该处实例化扑克状态；
  4. **自由度保留保障（Choice Invariance）**：决策调度器在其他区域实例化守卫时，必须施加反向约束，绝不能让某
