---
type: Reference
title: "第13章 Choosing Effective Utility-Based Considerations"
description: "Game AI Pro 工业级精读：Choosing Effective Utility-Based Considerations。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第13章 Choosing Effective Utility-Based Considerations

> 来源：*Game AI Pro 3: Balanced Cuts of Game AI Professionals*, Chapter 13.  
> 原文作者 / 资源：[Choosing Effective Utility-Based Considerations](http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter13_Choosing_Effective_Utility-Based_Considerations.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷3-GameAIPro3](README.md) ｜ [专栏首页](../README.md)

---

在现代 AAA 级动作角色扮演游戏（ARPG）与大型多人在线游戏（MMO）的系统级人工智能设计中，驱动非玩家角色（Non-Player Character, NPC）在动态战场环境中做出符合角色特质、战术合理且兼顾可读性的决策，是 AI 架构的核心命题。本工程文档基于《激战2：决战迈古玛》（*Guild Wars 2: Heart of Thorns*）的工业界实战沉淀，深入解构基于无限轴效用系统（Infinite Axis Utility System, IAUS）的高扩展性、全数据驱动的决策架构，并系统化剖析战术位移规划（Tactical Movement）、技能裁决（Skill Selection）与效用考量因子（Considerations）的构建哲学与工程优化。

---

## 1. 系统架构与数学评估拓扑（Architecture & Mathematical Topology）

### 1.1 IAUS 决策架构全景

本系统完全采用**数据驱动（Data-Driven）**的架构范式构建。AI 原型被划分为不同的生物原型或职阶（Archetypes/Species），每个原型挂载一组可选的候选决策集合（Decisions）。系统底层的评估核心为**决策分值评估器（Decision Score Evaluator, DSE）**。

在每一个思考周期（Think Cycle）中，AI 代理（Agent）遍历其所属原型分配的 DSE 候选池，实时执行数学评估与几何推理，获取最高分值的决策并指派为当前周期的执行意图。

```
+-----------------------------------------------------------------------------------+
|                            AI Agent (Think Cycle)                                 |
+-----------------------------------------------------------------------------------+
                                          |
                   +----------------------+----------------------+
                   |                      |                      |
             +-----------+          +-----------+          +-----------+
             |   DSE 1   |          |   DSE 2   |          |   DSE N   |
             +-----------+          +-----------+          +-----------+
                   |
     +-------------+-------------+
     |             |             |
+----------+  +----------+  +----------+
| Consider |  | Consider |  | Consider |
|  ation 1 |  |  ation 2 |  |  ation M |
+----------+  +----------+  +----------+
     |             |             |
 [Raw In]      [Raw In]      [Raw In]
     |             |             |
[Normalize]   [Normalize]   [Normalize]
  x in [0,1]    x in [0,1]    x in [0,1]
     |             |             |
[Curve f(x)]  [Curve f(x)]  [Curve f(x)]
  y in [0,1]    y in [0,1]    y in [0,1]
     |             |             |
     +-------------+-------------+
                   |
                   v
    DSE Score = Product of Considerations
    (If any c_i == 0.0 -> Early-Out Fast Fail)
                   |
                   v
             Argmax(Scores) -> Execute Best Decision
```

### 1.2 数学求值管道与归一化（Evaluation Pipeline & Normalization）

任意一个 DSE 的总效用分值，由其管辖的所有考量因子（Considerations）的分值累乘（Multiplicative Formulation）而成：

$$S_{\text{DSE}} = \prod_{i=1}^{M} C_i$$

其中 $M$ 为该决策下挂载的考量因子总数，$C_i \in [0, 1]$ 为单个考量因子的最终输出分值。

单个考量因子 $C_i$ 的求值管道分为两级：

1. **输入参数提取与区间归一化（Normalization via Bookends）**：
   从黑板（Blackboard）、游戏世界状态或空间查询系统中读取原始物理量 $I_{\text{raw}}$，依据预设的上下边界（Bookends） $[I_{\min}, I_{\max}]$ 进行钳位映射：
   $$x = \text{clamp}\left(\frac{I_{\text{raw}} - I_{\min}}{I_{\max} - I_{\min}}, 0.0, 1.0\right)$$
   *注：若输入量超过最大边界 $I_{\max}$，归一化值直接饱和输出为 $1.0$；低于 $I_{\min}$ 则饱和为 $0.0$。*

2. **响应曲线重映射（Response Curve Remapping）**：
   归一化输入 $x \in [0, 1]$ 经过一条预设或配置的非线性响应曲线函数 $f(x)$，产生最终分值：
   $$C_i = f(x), \quad f: [0, 1] \to [0, 1]$$
   此机制允许策划在零代码修改（Zero-Code）的前提下，实现对各维度权重与偏好形态的精细化塑造。

### 1.3 短路截断与管线性能优化（Early-Out Optimization）

乘法聚合模型的代数特性赋予了系统天然的**短路评估（Early-Out）**能力：

$$\exists k \in [1, M], \quad C_k = 0 \implies S_{\text{DSE}} = 0$$

一旦某个前置考量因子的评估结果为零，整个 DSE 立即被判定为无效，评估管线瞬间终止对该决策后续考量因子的计算并跳向下一个 DSE。

#### 工程排列优化规则
在构建 DSE 考量因子执行队列时，必须严格按照**计算开销升序**排列：

$$\text{Cost}(C_1) \le \text{Cost}(C_2) \le \dots \le \text{Cost}(C_M)$$

- **低开销/布尔状态（Lowest Cost）**：执行状态位检查（如 `IsRunning`）、定身状态检查（如 `IsRooted`）、可见性状态查询（如 `IsInvisible`），时间复杂度为 $O(1)$。
- **中开销/局部几何运算（Medium Cost）**：距离平方计算、向量点积（$\mathbf{u} \cdot \mathbf{v}$）、影响图（Influence Map）插值采样。
- **高开销/全局空间推理（Highest Cost）**：视线遮挡物理射线投射（Line-of-Sight Raycast）、导航网格（NavMesh）可达性检测与全局寻路计算。

通过将 $O(1)$ 的布尔截断置于顶端，昂贵的空间射线投射置于末端，系统能够在实际评估中剪除绝大多数计算分支，确保数十乃至数百个 AI 在密集同屏对抗下的毫秒级性能稳定。

---

## 2. 战术位移规划决策集（Tactical Movement DSEs）

### 2.1 逃离危险区域（Evade Dangerous Areas）

#### 战术目标
针对地面魔法范围技能（AoE）、物理机关陷阱（Mechanical Traps）及敌对势力生成的局部危险区域，驱使 NPC 在危急时刻主动向安全区域翻滚规避（Dodge），同时播放特定闪避动画获取短暂无敌帧，避免承受毁灭性伤害。

#### 考量因子流水线与数学响应建模

```
+----------------------------------------------------------------------------------------------------+
|                                Evade Dangerous Areas DSE Pipeline                                  |
+----------------------------------------------------------------------------------------------------+
| 1. [Early-Out Switch] Interrupt Gate    : Is the character already executing an uninterruptible action?
| 2. [Early-Out Switch] Mobility Gate     : Is the character rooted, frozen, or immobilized?
| 3. [Influence Map]    Danger Evaluation : Infinite-Resolution Influence Map query -> y = 1 - (x - 1)^4
| 4. [Cycle-Breaking]   Runtime Limiter   : Continuous execution time dampening     -> y = 1 - x^6
| 5. [Cycle-Breaking]   Cooldown Timer    : Prevention of rapid restrobing           -> y = x^5
| 6. [Personality Curve] Self-Health Check: High HP enables damage soaking, low HP demands immediate retreat
+----------------------------------------------------------------------------------------------------+
```

1. **动作中断检测（Is-Current-Action-Interruptible）**：布尔开关。正在执行无法打断的技能或位移时直接置 0。
2. **移动能力锁定（Is-Mobility-Unconstrained）**：布尔开关。角色被施加定身（Root）、冰冻（Frozen）状态时返回 0。
3. **环境危险度考量（Influence Map Danger Level）**：
   通过无限制分辨率影响图（Infinite-Resolution Influence Map）每隔固定帧率刷新聚合威胁数据。
   - **输入映射**：将当前 NPC 空间坐标投影至危险影响图，获得威胁强度 $x \in [0, 1]$。
   - **响应函数**：
     $$f_{\text{danger}}(x) = 1 - (x - 1)^4, \quad x \in [0, 1]$$
   - **函数特性解析**：该曲线为凹函数，一阶导数在 $x \to 0$ 处极大，随 $x$ 增大而迅速饱和至 $1.0$。该设计有效滤除了微小扰动（Squelching Unimportant Hazards），但在环境威胁刚进入边缘时即大幅放大被感知危险度（Amplify Moderate Danger），驱使处于危险边缘的角色坚决避险。

```
  f(x) ^
   1.0 |                   ********************
       |            ******
       |        ***
       |      **
       |     *
       |    *
   0.0 +----------------------------------------->
      0.0                                      1.0  x (Hazard/Danger)
```

4. **运行时间压制（Runtime Consideration）**：
   - **战术意图**：打破循环行为（Break Cycles），防止角色连续触发翻滚成为不可命中的无敌目标。
   - **输入量**：该决策连续激活的标准化时间 $x = \frac{t_{\text{run}}}{t_{\text{run\_max}}}$。
   - **响应函数**：
     $$f_{\text{runtime}}(x) = 1 - x^6, \quad x \in [0, 1]$$
   - **函数特性解析**：在大部分运行周期内输出几乎恒等于 $1.0$，直到逼近上限阈值时迅速暴跌归零，以硬截断强迫切换到其他决策。

5. **冷却延迟惩罚（Cooldown Consideration）**：
   - **战术意图**：防止在两个分值接近的决策间高频震荡（Anti-Strobing）。
   - **输入量**：上次执行完毕后的流逝时间 $x = \frac{t_{\text{elapsed}}}{t_{\text{cooldown}}}$。
   - **响应函数**：
     $$f_{\text{cooldown}}(x) = x^5, \quad x \in [0, 1]$$
   - **函数特性解析**：高阶多项式压制使得冷却期前中期分值趋近于 0，仅在冷却将近完成的尾端急剧抬升（Spikes back），随后恢复其全额效用。

```
     Runtime Curve: y = 1 - x^6                 Cooldown Curve: y = x^5
  y ^                                        y ^
1.0 |***********************               1.0 |                                *
    |                       **                 |                               **
    |                         *                |                              *
    |                          *               |                             *
    |                           *              |                           **
0.0 +---------------------------->         0.0 +---------------------------->
   0.0                         1.0 x          0.0                         1.0 x
```

6. **自身生命值反馈（Self Health Status）**：
   - **战术意图**：赋予生物战斗“性格”（Personality）。满血角色的规避意愿被适当调低，容忍承受部分伤害以换取对玩家的攻击窗口；生命值低下时则大幅提升避险敏感度。

---

### 2.2 潜伏突进（Close to Melee when Invisible）

#### 战术目标
针对拥有隐形（Stealth/Invisibility）能力的刺客、掠食者（Predator）或盗贼原型，在潜行期间切断常规徘徊逻辑，实施向目标后背的盲区隐蔽奔袭，并在破隐瞬间释放爆发打击。

```
[Target: Player] <----------------- (Moving Closer) ----------------- [AI: In Stealth]
       |                                                                     |
       +------------------- Relative Distance < Speed * Time ---------------+
```

#### 考量因子组合拓扑

1. **非中断布尔开关（Not-Interrupted Gate）**：保证 AI 处于动作自由状态。
2. **可位移布尔开关（Can-Move Gate）**：排除被硬控状态。
3. **隐形状态校验（Is-Target-Invisible）**：查询角色战斗标签集（Buffs/Status Flags）。若当前非隐形状态，直接短路置 0。
4. **隐形时效可达性（Reachability within Cloak Duration）**：
   - **空间几何考量**：综合目标与自身的直线距离 $d = \|\mathbf{p}_{\text{target}} - \mathbf{p}_{\text{AI}}\|$ 以及潜行剩余持续时间 $t_{\text{stealth}}$。
   - **数学重映射**：在近距离至中距离区间保持满分；当距离超过角色以最大移动速度 $v_{\max}$ 无法在潜行结束前抵达的物理极值时，曲线向极远处平缓衰减归零（Tapering Off）。
5. **自身健康考量（Self Health Value）**：若生命值过低不足以支撑近战刺杀风险，压制该决策效用。
6. **隐身位移时限（Runtime Limitation）**：硬性限制几秒内的跑动，防止隐身超长距离奔袭给玩家造成丢失目标或断节奏的负面挫败体验。

---

## 3. 技能选择裁决机制（Skill Selection Decisions）

系统采用**考量因子预设集（Premade Consideration Sets）**机制，将通用的技能裁决逻辑抽象为高度可复用的元模型。

### 3.1 冲锋突进技能（Charge Attacks）

#### 战术目标
针对重甲卫士、猛兽等角色的冲锋突进技能，评估中距离发起冲撞的时机，规避极近距离的资源浪费与超远距离的无效冲锋，同时受战场局部友军拥挤度及角色视野几何约束。

#### 核心考量因子配置

```
         Target Direction
                ^
                |
                |  d in [d_min, d_max]
                |
           [AI Agent]
           Facing: u
```

1. **不可中断开关（Can-Act Switch）**：基础动作门禁。
2. **移动受限开关（Can-Move Switch）**：定身检测门禁。
3. **中距离窗口考量（Distance to Target Window）**：
   - **输入量**：目标欧氏距离 $d$。
   - **响应特性**：采用**凸形钟形/梯形响应曲线**。在极近距离（近战普攻范围内）分值为 0（不消耗突进冷却）；在冲锋有效杀伤区间 $[d_{\text{min}}, d_{\text{max}}]$ 内达到峰值 1.0；在超过冲锋最大距离后彻底衰减至 0。
4. **友军局部拥挤度检测（Ally Density via Influence Map）**：
   - **战术意图**：控制战局节奏与压力（Pacing/Pressure Management）。当目标周围已有 2 名及以上同盟 NPC 围攻时，强制压制第三名敌人的冲锋打入，避免玩家遭受不可抗拒的连续多重击倒（Piling On）。
5. **自身健康状态（Self Health Preference）**：
   - 常规战斗原型偏好在健康状态良好时发动进攻；自杀式/狂暴原型（Suicidal Archetype）可通过反转该响应曲线，实现“濒死疯狂自爆冲锋”的战斗特质。
6. **相对方位角筛选（Relative Direction Dot Product）**：
   - **空间数学公式**：
     设 AI 朝向向量为 $\mathbf{u}$，AI 指向目标的位移向量为 $\mathbf{d} = \mathbf{p}_{\text{target}} - \mathbf{p}_{\text{AI}}$：
     $$\cos \theta = \mathbf{u} \cdot \frac{\mathbf{d}}{\|\mathbf{d}\|}$$
   - **几何约束**：仅对 $\cos \theta > 0$（目标在角色前半球）产生高分，避免由于选择身后目标而引发突兀的模型瞬间回旋切向（Animation Snapping）与视觉跳帧。
7. **直接视线遮挡检测（Line-of-Sight, LoS）**：
   - 作为整个 DSE 队列的**末端考量因子**。发起物理射线检测：
     $$\text{Raycast}(\mathbf{p}_{\text{AI\_Eye}}, \mathbf{p}_{\text{Target\_Center}})$$
   - 若被几何静态阻挡物阻断，直接截断置 0。由于开销极大，此考量因子依赖前序所有因子的短路过滤。

---

### 3.2 侧翼与背刺攻击技能（Side and Rear Attacks）

#### 战术目标与工程设计亮点
侧翼与背刺技能（Flanking/Backstab Attack）专用于评估偷袭动作。值得注意的是，该技能**主动移除了“定身移动受限开关（Can-Move Switch）”**。

> **工程设计准则**：在通用基类中硬编码业务规则将导致脆弱性（Fragility）。当定身状态下的刺客面对一个正好背对自己的敌人时，进行原地背刺攻击是完全合理的游戏机制。采用纯数据驱动的 DSE，策划只需在该决策中**省去移动检测考量因子**，即可自然允许定身状态触发，无需为硬编码逻辑修补额外的例外旁路（Bypass Hack）。

#### 目标背向判定几何空间推导

```
                      Target Facing: v
                             ^
                             |
                   +---------+---------+
                   |      Target       |
                   +---------+---------+
                             ^
                              \
                               \  Offset Vector: d_t2a
                                \
                                 \
                             +-------+
                             |  AI   |
                             +-------+
```

1. **向量定义**：
   - 目标标准化朝向单位向量：$\mathbf{v}_{\text{target}}$
   - 从目标指向 AI 的相对空间偏移向量：
     $$\mathbf{d}_{t \to a} = \mathbf{p}_{\text{AI}} - \mathbf{p}_{\text{target}}$$
   - 相对单位向量：
     $$\hat{\mathbf{d}}_{t \to a} = \frac{\mathbf{d}_{t \to a}}{\|\mathbf{d}_{t \to a}\|}$$

2. **几何点积判别量**：
   $$k = \mathbf{v}_{\text{target}} \cdot \hat{\mathbf{d}}_{t \to a}$$
   - 若 $k \to 1$：目标朝向直接指向 AI，说明 AI 处于目标的正前方正视范围内。
   - 若 $k = 0$：AI 位于目标侧翼（Perpendicular）。
   - 若 $k \to -1$：目标完全背向 AI，AI 处于目标的绝对正后方。

3. **响应曲线设计**：
   输入量 $x$ 映射点积标量 $k \in [-1, 1] \to [0, 1]$。
   响应曲线在 $k \to 1$ 时完全衰减至 0（正面对峙拒绝发动）；在 $k \le 0$（侧翼与后背）区间输出高分值。

#### 多目标优先级仲裁机制（Target Prioritization Metrics）

当场上存在多个潜在攻击目标时，DSE 借助后置考量因子进行多目标效用仲裁：
- **距离亲和度**：$\|\mathbf{d}\|$ 越小，分值线性抬升。
- **残血收割偏好（Execute Wounded）**：针对敌对目标的当前生命百分比采用平缓递减曲线，优先裁决打击残血敌人，但不将满血目标置零。
- **视野锥体匹配（Cone of Vision）**：基于 AI 朝向 $\mathbf{u}_{\text{AI}}$ 与指向目标向量 $\hat{\mathbf{d}}_{a \to t}$ 的点积，偏好视野正前方的敌对目标。
- **终局视线检测（Final LoS Raycast）**：阻断无物理路径的穿墙目标。

---

## 4. 效用考量因子的工程设计分类学（Considerations Taxonomy）

构建高鲁棒性的 IAUS，本质在于对考量因子的精准分类与正交解耦。工业界实战中，考量因子严格划分为三大体系：

| 考量因子类别 (Category) | 核心功能定位 (Functional Objective) | 典型案例 (Representative Examples) | 工程实现范式 (Implementation Pattern) |
| :--- | :--- | :--- | :--- |
| **强制门禁型<br>(Mandatory Considerations)** | 逻辑前置校验，提供绝对可行性硬门槛；支持快速短路（Early-out）。 | 动作未在中断保护中、未被硬控锁定、拥有技能消耗资源。 | 布尔评估函数 $[0 \text{ or } 1]$；作为预设模板（Templates）在新建 DSE 时自动填充，支持按需剔除。 |
| **形态区分型<br>(Distinguishing Considerations)** | 提取系统世界状态与角色状态标签，区分特异化战术情境。 | 自身隐形 Buff、目标燃烧状态、目标被眩晕、背向目标。 | 桥接层读取 Gameplay 标签/状态机标志，结合阈值或阶跃曲线进行过滤。 |
| **平衡体验型<br>(Balance & "Feel" Considerations)** | 塑造角色战斗节奏、拟真“心智”、难度平滑度与行为性格。 | 连续执行运行时间（Runtime）、冷却计时（Cooldown）、局部拥挤度、残血避险倾向。 | 标准化预设非线性曲线（如高阶多项式 $x^n$、$1 - (x-1)^n$），杜绝行为高频抖动（Strobing）。 |

---

## 5. 空间推理与影响图系统集成（Spatial Reasoning & Influence Maps）

### 5.1 无限分辨率影响图的数学查询

战术决策严重依赖**空间推理（Spatial Reasoning）**。传统基于固定网格（Grid-Based）的影响图在复杂地形和宏大场景下面临内存膨胀与分辨率阶梯失真问题。《激战2：决战迈古玛》使用无限制分辨率影响图系统：

- **空间表示**：每个危险源（法术圈、火海、地雷）抽象为一个包含空间中心坐标 $\mathbf{c}_j$、有效半径 $R_j$ 及基础强度 $A_j$ 的连续空间衰减场（Continuous Spatial Attenuation Field）。
- **空间点累加模型**：
  对任意空间采样点 $\mathbf{p}$，该点的危险度标量场 $D(\mathbf{p})$ 为各威胁核函数的线性叠加：
  $$D(\mathbf{p}) = \sum_{j} A_j \cdot K(\|\mathbf{p} - \mathbf{c}_j\|, R_j)$$
  其中核函数 $K(r, R) = \max\left(0, 1 - \frac{r^2}{R^2}\right)$ 提供平滑衰减。
- **IAUS 接口**：DSE 输入层仅需向影响图系统提交 NPC 当前世界坐标 $\mathbf{p}_{\text{AI}}$，单次查询即可获得环境危险度、局部敌我冲突烈度（Conflict Density）与友军聚集度度量。

### 5.2 导航网格（NavMesh）寻路开销的工程权衡

在空间验证层中，**视线遮挡（LoS）**与**导航网格可达性（NavMesh Reachability）**存在计算成本与真实性之间的强冲突：

- **完全可达性验证的代价**：
  为每个潜在目标在每个思考周期计算完整的 $A^*$ 寻路或走廊漏斗平滑（String Pulling），在大型战场多对多博弈中将直接导致主线程 CPU 缓存击穿与算力超载。
- **工程近似方案（Engineering Approximation）**：
  在冲锋等技能决策中，直接**舍弃全量 NavMesh 路径可行性计算，退化为单纯的单次物理射线 LoS 检查**。
  - **经验事实**：在实际战斗中，“AI 能够直接看到目标，但在导航网格拓扑上完全无法通行”的边界情况（Corner Cases）极其罕见（仅存在于单向悬崖边缘或特定阻隔网）。
  - **收益**：以忽略极少数罕见边界异常的微小代价，将决策开销降低 2 到 3 个数量级，极大提升了同屏战斗集群的承载吞吐量。

---

## 6. C++ 工程级代码实现（Production C++ Implementation）

以下为工业级、高性能 IAUS 的核心框架代码实现，包含归一化、曲线多项式映射、DSE 求值器、以及短路优化管道。

```cpp
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// ============================================================================
// 1. 响应曲线系统 (Response Curves)
// ============================================================================
enum class ResponseCurveType : uint8_t {
    Linear,
    Polynomial,
    InversePolynomial,
    CustomPreset
};

struct ResponseCurve {
    ResponseCurveType type = ResponseCurveType::Linear;
    float exponent = 1.0f;
    float slope = 1.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;

    // 核心重映射计算: 将 [0, 1] 的输入值 re-map 至 [0, 1]
    inline float Evaluate(float x) const noexcept {
        x = std::clamp(x, 0.0f, 1.0f);
        float y = 0.0f;

        switch (type) {
            case ResponseCurveType::Linear:
                y = slope * (x - xOffset) + yOffset;
                break;
            case ResponseCurveType::Polynomial:
                // 对应 Cooldown: y = (x - xOffset)^n + yOffset
                y = std::pow(std::max(0.0f, x - xOffset), exponent) + yOffset;
                break;
            case ResponseCurveType::InversePolynomial:
                // 对应 Danger: y = 1 - (x - 1)^4 或 Runtime: y = 1 - x^6
                y = 1.0f - std::pow(std::abs(x - xOffset), exponent) + yOffset;
                break;
            case ResponseCurveType::CustomPreset:
            default:
                y = x;
                break;
        }
        return std::clamp(y, 0.0f, 1.0f);
    }
};

// ============================================================================
// 2. 考量因子核心抽象 (Consideration Interface)
// ============================================================================
class AIBlackboard; // 前置声明黑板数据上下文

class IConsideration {
public:
    virtual ~IConsideration() = default;

    // 执行考量计算与曲线评估
    virtual float Score(const AIBlackboard& bb) const = 0;
    
    // 获取相对预估开销，用于 DSE 排序优化
    virtual uint32_t GetExecutionCost() const noexcept = 0;
};

// 基础数值型考量因子模板
class BaseNumericConsideration : public IConsideration {
protected:
    float minValueBookend = 0.0f;
    float maxValueBookend = 1.0f;
    ResponseCurve responseCurve;

    inline float NormalizeInput(float raw) const noexcept {
        if (maxValueBookend <= minValueBookend) return 0.0f;
        return std::clamp((raw - minValueBookend) / (maxValueBookend - minValueBookend), 0.0f, 1.0f);
    }

public:
    BaseNumericConsideration(float minVal, float maxVal, ResponseCurve curve)
        : minValueBookend(minVal), maxValueBookend(maxVal), responseCurve(curve) {}
};

// ============================================================================
// 3. 典型考量因子工程实现 (Concrete Considerations)
// ============================================================================

// 3.1 移动门禁 (O(1) 强制门禁，极低开销)
class MobilityConstraintConsideration final : public IConsideration {
public:
    float Score(const AIBlackboard& bb) const override;
    uint32_t GetExecutionCost() const noexcept override { return 1; }
};

// 3.2 危险区域影响图考量因子 (中等开销)
class EvadeDangerInfluenceConsideration final : public BaseNumericConsideration {
public:
    EvadeDangerInfluenceConsideration()
        : BaseNumericConsideration(0.0f, 1.0f, ResponseCurve{ResponseCurveType::InversePolynomial, 4.0f, 1.0f, 1.0f, 0.0f}) {}
        // y = 1 - (x - 1)^4

    float Score(const AIBlackboard& bb) const override;
    uint32_t GetExecutionCost() const noexcept override { return 20; }
};

// 3.3 视线遮挡检测考量因子 (高昂物理开销，必须位于流水线末端)
class LineOfSightConsideration final : public IConsideration {
public:
    float Score(const AIBlackboard& bb) const override;
    uint32_t GetExecutionCost() const noexcept override { return 1000; }
};

// ============================================================================
// 4. 决策分值评估器 (Decision Score Evaluator, DSE)
// ============================================================================
class DecisionScoreEvaluator {
private:
    std::string decisionName;
    std::vector<std::unique_ptr<IConsideration>> considerations;
    bool isSorted = false;

public:
    explicit DecisionScoreEvaluator(std::string name) : decisionName(std::move(name)) {}

    void AddConsideration(std::unique_ptr<IConsideration> consideration) {
        considerations.push_back(std::move(consideration));
        isSorted = false;
    }

    // 按开销升序排列考量因子，最大化 Early-Out 效率
    void OptimizeEvaluationPipeline() {
        std::sort(considerations.begin(), considerations.end(),
            [](const std::unique_ptr<IConsideration>& a, const std::unique_ptr<IConsideration>& b) {
                return a->GetExecutionCost() < b->GetExecutionCost();
            });
        isSorted = true;
    }

    // 核心求值管道: 包含短路评估
    float Evaluate(const AIBlackboard& bb) {
        if (!isSorted) {
            OptimizeEvaluationPipeline();
        }

        float totalScore = 1.0f;

        for (const auto& consideration : considerations) {
            const float score = consideration->Score(bb);

            // 浮点数极小值快速短路截断
            if (score <= 0.0001f) {
                return 0.0f; // Early-Out: 规避后续高开销考量因子的计算
            }

            totalScore *= score;
        }

        return totalScore;
    }

    const std::string& GetName() const noexcept { return decisionName; }
};

// ============================================================================
// 5. 思考周期决策仲裁驱动 (Decision Arbiter)
// ============================================================================
class UtilityAgent {
private:
    std::vector<DecisionScoreEvaluator> availableDSEs;

public:
    void RegisterDSE(DecisionScoreEvaluator dse) {
        availableDSEs.push_back(std::move(dse));
    }

    // 每 Think Cycle 执行一次最高效用仲裁
    std::string Think(const AIBlackboard& blackboard) {
        float bestScore = -1.0f;
        std::string bestDecision = "Idle";

        for (auto& dse : availableDSEs) {
            const float score = dse.Evaluate(blackboard);
            if (score > bestScore) {
                bestScore = score;
                bestDecision = dse.GetName();
            }
        }

        // 仅当分值高于底线阈值时执行，否则退化为 Idle
        return (bestScore > 0.05f) ? bestDecision : "Idle";
    }
};
```

---

## 7. 工业级落地实施要义（Production Implementation Guidelines）

### 7.1 响应曲线预设调色盘（Preset Palette Paradigm）

在商业项目的大规模设计流中，允许策划自由绘制任意数学曲线虽然灵活性极强，但极易造成心理认知负担、难以调优以及全队设计规范的割裂。

- **预设曲线调色盘（Preset Curve Palette）**：
  为策划团队提供预设的 5 至 8 条标准曲线资产，覆盖常见行为形态：
  1. *线性增长/衰减（Linear Growth/Decay）*；
  2. *阶跃开关（Step Function）*；
  3. *快速启动/晚期饱和（Concave Quadratic / Quartic - 类似危险逃离）*；
  4. *晚期陡增/早期压制（Convex High-Order Polynomial - 类似冷却恢复）*；
  5. *钟形窗口区间（Bell Curve - 类似冲锋技能距离筛选）*。
- **高级模式隐藏（Advanced Mode Encapsulation）**：
  将高阶多项式系数、任意幂次配置隐藏在“高级开发者模式（Advanced Mode）”折叠面板之后，普通角色配置仅允许在预设调色盘中下拉选择。

### 7.2 角色扮演法推导考量指标（Applied Role-Play Methodology）

当为一个全新的怪物或战术角色构建 DSE 时，系统设计应采用**推演式角色扮演法（Applied Role-Play）**：

1. **同理心置换与动机拆解**：
   例如构建“潜伏神偷（Thief）靠近目标实施偷窃”决策：
   - 设想处于角色的真实处境：“为什么我现在要选这个目标？”（目标携带高价值财物）；
   - “为什么我现在绝不能上去？”（目标正转过身看着我，或者周围有守卫，或者距离太远我跑不过去）。
2. **具象化物理输入抽取（Concrete Metric Distillation）**：
   将上述心理动机映射为底层可量化的客观物理量：
   - 目标

---

在现代 AAA 游戏的人工智能架构中，效用系统（Utility Systems）以其有机、连续且高鲁棒性的决策流转特性，广泛应用于复杂动态环境下的非玩家角色（Non-Player Character, NPC）行为决策。本文基于《激战2：决战迈古玛》（*Guild Wars 2: Heart of Thorns*）的核心决策建模实践，系统解构效用考量项（Considerations）、归一化响应曲线（Response Curves）的设计几何特征、多决策交织振荡消除算法，以及代理变量（Proxy Variables）在空间推理与状态评估中的工程落地。

---

## 1. 响应曲线构建的核心维度与数学抽象

在效用理论中，每一个决策考量项（Consideration）本质上是一个将游戏世界原始输入数据映射为归一化效用得分的传递函数。效用系统要求所有响应曲线均在单位正方形区间内进行定义：

$$\text{Domain: } x \in [0, 1], \quad \text{Range: } y \in [0, 1]$$

其中 $x$ 为经过截断与重映射后的归一化环境变量（Normalized Context Input），$y$ 为该考量项输出的归一化效用分值（Utility Score）。构建或选取响应曲线需严格围绕三个核心几何与拓扑特征展开。

```
Score y
  1.0 +-----------------------+ (1, 1) [饱和最大值]
      |          . * * *      |
      |      . *              |  单调递增曲线 (Monotonic Increasing)
      |   .*                  |  f'(x) >= 0
      | .*                    |
  0.0 +-----------------------+
     (0,0) [底限输入]         1.0 Normalized Input x
```

### 1.1 曲线趋势方向（Directionality）与权重倾斜

曲线的整体走势定义了决策相关性随输入量变化的梯度方向：
* **递增曲线（Increasing Curves）**：导数 $\frac{\mathrm{d}y}{\mathrm{d}x} \ge 0$。当输入向 $x \to 1.0$ 趋近时，决策相关性逐渐增加。例如针对盗贼（Thief）NPC，目标的预估财富（Perceived Wealth）越高，发起窃取决策的效用越大。
* **递减曲线（Decreasing Curves）**：导数 $\frac{\mathrm{d}y}{\mathrm{d}x} \le 0$。输入向上限延伸时，决策效用衰退。例如盗贼与目标的欧式距离（Distance），目标距离越远，穿过战场接近目标所面临的潜在威胁几何倍增，发起接近行为的效用显著衰减。

### 1.2 单调性（Monotonicity）与非单调特定行为塑形

单调性约束保证函数在有效定义域 $[0, 1]$ 内一阶导数恒不改变符号：

$$\frac{\mathrm{d}y}{\mathrm{d}x} \ge 0 \quad \text{或} \quad \frac{\mathrm{d}y}{\mathrm{d}x} \le 0, \quad \forall x \in [0, 1]$$

* **单调性的工程价值**：大幅简化设计人员对考量项得分逻辑的理解与心理建模（Mental Model），避免多考量项非线性乘积时出现难以预料的局部驻点（Stationary Points）。在《激战2：决战迈古玛》的大规模工程实践中，绝大多数响应曲线均严格保持单调性。
* **非单调曲线的特化应用**：用于特定空间距离保持或状态极值匹配。例如“保持在攻击臂长距离（Arm's Length）”，AI 必须在“太近”（需要拉开）与“太远”（需要接近）之间维持平衡。此时使用倒 U 型曲线（Inverted U-shaped Curve，如高斯分布曲线或反向二次多项式）：

$$y(x) = \max\left(0, 1 - 4(x - 0.5)^2\right)$$

该函数在 $x = 0.5$（最佳近战交战距离）取得峰值 $1.0$，而在太近（$x \to 0$）或太远（$x \to 1$）时效用迅速滑落至 $0$。

### 1.3 端点行为（Endpoints）与绝对失效机制

端点值 $y(0) = f(0)$ 与 $y(1) = f(1)$ 决定了在输入处于极值时系统的决策硬约束：
* **单调递增系统端点**：$f(0) = \min(y)$，$f(1) = \max(y)$。
* **单调递减系统端点**：$f(0) = \max(y)$，$f(1) = \min(y)$。
* **硬失效（Invalidation）与软降级（Deprioritization）**：
  * 若输入达到极值时端点值跌落至 $0$（即 $y = 0$），在相乘聚合模型中将导致复合效用归零，即对该决策施加了“绝对否决（Absolute Veto）”。
  * 若端点值设计为有限小量（例如 $y = 0.15$），则仅降低决策优先级（Soft Deprioritize），允许在无其他更优方案时依然可以被选取执行。

---

## 2. 响应曲线构建决策流程与状态映射

为了规范化响应曲线设计，工业级 AI 设计需遵循以下四步检查链条（Checklist Pipeline）：

```
[原始输入采集] Raw World Input (Distance, Health, etc.)
       │
       ▼
[输入边界截断映射] Normalize via Bookends: x = Clamp((Raw - InMin) / (InMax - InMin), 0, 1)
       │
       ▼
[斜率与单调性判定] Monotonic? (Yes -> Inc/Dec Slope; No -> Peak/Valley Injection)
       │
       ▼
[端点与否决条件设定] Endpoint Assignment: (Soft Deprioritize vs. Hard Zero Invalidation)
       │
       ▼
[效用输出计算] Final Utility Score: y ∈ [0.0, 1.0]
```

### 响应曲线配置规范表

| 考量项物理属性 | 输入夹取区间 (Bookends) | 响应几何拓扑 | 端点行为定义 $[f(0), f(1)]$ | 预期游戏性行为 |
| :--- | :--- | :--- | :--- | :--- |
| **近战攻击距离** | $[0\text{m}, 25\text{m}]$ | 倒 U 型非单调二次曲线 | $[0.0, 0.0]$，峰值 $f(0.5)=1.0$ | 仅在中等间距执行接近，过远或贴身均不选 |
| **自身生命百分比** | $[0.1, 0.8]$ | 单调递减三次曲线 | $[1.0, 0.0]$ | 血量危急时极速拔高“喝治疗药水”决策 |
| **窃取目标财富** | $[0\text{g}, 500\text{g}]$ | 单调递增 Logit 曲线 | $[0.05, 1.0]$ | 极度贫穷软降级；目标越富裕，窃取冲动越强 |

---

## 3. 多决策交织分数振荡（Decision Oscillation）及其工业解法

效用系统具有随世界状态动态升降的有机流转能力。然而，当两个或多个候选决策的效用响应得分在特定状态区间高度接近时，系统可能在帧间发生严重的“乒乓振荡（Ping-Pong / Oscillation）”，导致 AI 在两种行为（如“射击”与“寻找掩体”）之间高频抽搐。

### 3.1 基础序列化与防振荡机制的局限性

传统对抗评分冲突与生成行为序列的方法包括：
1. **冷却时间与降权链（Cooldowns and Weighted Step-Down）**：为决策配置冷却时间（Cooldown），并将序列中后续决策的基础权重逐级递减。当前动作触发后立即进入冷却，AI 顺理成章跌落至次优决策，形成天然的行为流（Sequence）。
2. **承诺增益（Commitment Bonus）**：为上一帧被选中的决策注入微小的效用增益因子 $\beta$：

   $$U_{\text{effective}}(D_i) = U(D_i) + \delta_{i, \text{last}} \cdot \beta$$

3. **全局决策加权（Decision Weight Multipliers）**：扩大多决策的基准得分范围（例如由 $[0, 1]$ 映射至 $[0, 4]$）。
4. **运行期保护考量（Runtime Considerations）**：锁定最小执行周期。

**底层缺陷分析**：上述方案并不能从拓扑上消除两个决策超曲面（Decision Hypersurfaces）相交产生的振荡带，而仅仅是在参数空间内**平移了振荡点的位置**。若状态变量穿过新的交叉点，振荡将依然存在，甚至由于引入了历史相关变量而演变成更隐蔽、更难调试的时序抖动。

### 3.2 振荡消除的双重核心解决范式

```
方案 A: 维度扩增 (Adding a Consideration)
-------------------------------------------------------------------------
Decision A Score:  ───► [ C1 ] ──x── [ C2 ] ──────────────► Score A
                                                              ▲ (消除重叠)
Decision B Score:  ───► [ C1 ] ──x── [ C2 ] ──x── [ C3 ] ──► Score B
                                                    ▲
                                            新增考量破坏退化流形

方案 B: 响应曲线形状微调 (Shape Tuning via Visual Editor)
-------------------------------------------------------------------------
Oscillation Zone:  f_A(x) ≈ f_B(x) 在区间 [x_low, x_high] 内重合
Curve Adjustment:  通过设计工具拖动曲线断点，收窄梯度重合的定义域区间，
                   压缩驻留概率。
```

1. **升维解耦法（Adding a Consideration）**：
   * **机理**：向处于振荡态的某一决策追加全新的考量项 $C_{\text{new}}$。从数学几何角度来看，这为决策评分空间引入了一个全新的正交维度。原先在一维/低维投影上重合的效用流形，在高维空间中被完全分离，显著打破对称性，强制某一决策产生压倒性竞争优势，从而根除振荡。
2. **响应曲线收窄微调（Response Curve Retuning via Visual Sliders）**：
   * 在关卡剧本特定角色（Set Pieces）等极端设计约束下，可能无法引入逻辑自洽的新考量项。此时必须调校原有曲线的斜率与端点，最小化 $\left| U(D_A) - U(D_B) \right| < \epsilon$ 的状态驻留窗口。
   * **工具链需求**：尽管振荡问题在数学上存在闭式解（Closed-form Analytical Solutions），但在商业引擎中推导方程解析解成本过高。工业界标准做法是研发带有**可视化滑动条（Sliders）与实时曲线图表**的编辑调试器，允许 AI 架构师实时模拟多决策输入交互，肉眼排查并压制振荡区域。

---

## 4. 考量项选取与代理变量（Proxy Variables）工程化

在设计效用考量项时，首要准则是探寻 AI 角色内部的核心心理动机：“*我为什么要执行（或不执行）这个动作？*”
然而，真实物理模拟或完全精确的推导往往计算高昂，且逻辑过于错综复杂。

### 4.1 隐形伏击案例：显式建模 vs. 代理变量

以“隐形刺客试图在隐形失效前冲刺进入近战范围攻击目标”为例，存在两种架构实现路线：

```
[显式复杂动力学模型]
  ├── 采集隐形 Buff 剩余时间 t_remaining
  ├── 计算自身即时速度向量 V_self 与目标速度向量 V_target
  ├── 射线碰撞检测与路径动态重构耗时推演
  └── 联合求解非线性拦截偏微分方程 ──────────────► [计算开销巨大 / 脆弱]

[代理变量替代模型]
  └── 直接采样固定时间常数 (Time Constant Proxy) ───► [极低开销 / 高度稳健]
```

* **显式建模（Explicit Model）**：严苛追踪 Buff 剩余倒计时、双方速度向量、加速度极限、转向半径与动态避障路径，联合求解截击方程。系统极其脆弱，极易受环境突发阻挡崩溃。
* **代理变量（Proxy Variable）**：采用简单的**时间常数（Time Constant）**替代整个复杂的动力学系统。
* **人类玩家的叙事心理补完（Narrative Apophenia）**：工程实践证明，显式模型与代理时间常数在表现力上的微小差异，玩家几乎无法察觉。即使代理变量由于过于简化在某些边界情况下表现欠妥（如隐形提前 0.5 秒解除了），玩家也会在脑海中脑补叙事理由（例如“刺客因剧烈跑动露出了破绽”或“隐形法术能量不稳定”）。**玩家的叙事脑补是游戏 AI 架构中最廉价且最高效的容错机制。**

### 4.2 空间推理代理：模块化战术影响图（Modular Influence Maps）

为了将复杂的空间态势感知注入效用考量项中，现代架构使用无限分辨率影响图（Infinite-Resolution Influence Mapping）与模块化战术影响图（Modular Tactical Influence Maps）的采样点作为空间代理变量：

```
+--------------------------------------------------------------------+
|                Modular Tactical Influence Maps                     |
|                                                                    |
|   [ Threat Map ]           [ Cover Map ]        [ Friendly Density ]
|     (威胁分布)               (掩体有效性)             (友军协同网)     |
|          \                      |                      /           |
|           \                     |                     /            |
|            ▼                    ▼                    ▼             |
|       +-----------------------------------------------+            |
|       |  Composite Layer Linear/Multiplicative Fusion |            |
|       |      I_combined(x) = ∑ w_i * M_i(x)           |            |
|       +-----------------------------------------------+            |
|                                 │                                  |
|                                 ▼                                  |
|               [ Spatial Consideration Utility ]                    |
+--------------------------------------------------------------------+
```

通过将“多敌人交叉威胁”、“掩体遮蔽角”与“友军火力网重叠”离散烘焙为单向影响值并进行线性或乘算复合，AI 架构师只需在考量项中提取目标位置的复合影响图浮点值作为输入，便可一次性代表极度复杂的立体空间推理。

---

## 5. 模块化效用决策引擎 C++ 核心实现

以下代码演示了工业级效用系统的底层核心构建：包含单调/非单调响应曲线的数学定义、考虑承诺增益的多决策评估器，以及通过代理变量消除乒乓振荡的评估管线。

```cpp
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>
#include <string>

// ============================================================================
// 1. 响应曲线拓扑定义 (Response Curve Primitives)
// ============================================================================
enum class CurveType {
    Linear,
    Polynomial,
    Logistic,
    InvertedU_Shape
};

struct ResponseCurve {
    CurveType type = CurveType::Linear;
    float slope = 1.0f;
    float exponent = 1.0f;
    float xShift = 0.0f;
    float yShift = 0.0f;

    // 核心计算函数：在 [0, 1] 空间执行求值并截断
    float Evaluate(float x) const {
        // 保证输入位于单位区间 [0, 1]
        x = std::clamp(x, 0.0f, 1.0f);
        float y = 0.0f;

        switch (type) {
            case CurveType::Linear:
                y = slope * (x - xShift) + yShift;
                break;
            case CurveType::Polynomial:
                y = slope * std::pow(std::max(0.0f, x - xShift), exponent) + yShift;
                break;
            case CurveType::Logistic:
                // 经典 S 型曲线变体
                y = 1.0f / (1.0f + std::exp(-slope * (x - xShift))) + yShift;
                break;
            case CurveType::InvertedU_Shape:
                // 非单调特征曲线：用于保持距离等特化状态
                // y = max(0, 1 - 4 * (x - 0.5)^2)
                y = 1.0f - 4.0f * (x - 0.5f) * (x - 0.5f);
                break;
        }

        return std::clamp(y, 0.0f, 1.0f);
    }
};

// ============================================================================
// 2. 考量项抽象基类与代理变量实现
// ============================================================================
class AIContext {
public:
    float distanceToTarget = 0.0f;      // 原始物理量：距离
    float targetPerceivedWealth = 0.0f; // 抽象属性：目标财富
    float selfHealthPct = 1.0f;         // 自身血量状态 [0, 1]
    float influenceMapSafety = 0.5f;    // 空间推理代理变量：战术影响图安全系数
    float proxyTimeConstant = 1.0f;     // 状态代理变量：隐形持续推演常量
};

class IConsideration {
public:
    virtual ~IConsideration() = default;
    virtual float Score(const AIContext& context) const = 0;
};

// 距离考量项：单调递减曲线实现
class DistanceConsideration : public IConsideration {
private:
    float minRange;
    float maxRange;
    ResponseCurve curve;

public:
    DistanceConsideration(float inMin, float inMax, ResponseCurve inCurve)
        : minRange(inMin), maxRange(inMax), curve(inCurve) {}

    float Score(const AIContext& context) const override {
        // 归一化输入 (Bookending)
        float normalized = (context.distanceToTarget - minRange) / (maxRange - minRange);
        normalized = std::clamp(normalized, 0.0f, 1.0f);
        return curve.Evaluate(normalized);
    }
};

// 代理变量考量项：通过战术影响图采样消除振荡
class InfluenceSafetyProxyConsideration : public IConsideration {
private:
    ResponseCurve curve;

public:
    explicit InfluenceSafetyProxyConsideration(ResponseCurve inCurve) : curve(inCurve) {}

    float Score(const AIContext& context) const override {
        // 直接使用空间推理聚合代理值
        return curve.Evaluate(context.influenceMapSafety);
    }
};

// ============================================================================
// 3. 决策类与多考量项乘算聚合 (Multiplicative Aggregation)
// ============================================================================
class Decision {
public:
    std::string name;
    float weight = 1.0f;
    std::vector<std::shared_ptr<IConsideration>> considerations;

    Decision(std::string inName, float inWeight) 
        : name(std::move(inName)), weight(inWeight) {}

    void AddConsideration(std::shared_ptr<IConsideration> cons) {
        considerations.push_back(cons);
    }

    // 计算决策复合效用得分
    float EvaluateScore(const AIContext& context) const {
        if (considerations.empty()) return 0.0f;

        float finalScore = weight;
        
        // 考量项相乘，任一考量项返回 0 将直接否决决策
        for (const auto& c : considerations) {
            float s = c->Score(context);
            finalScore *= s;
            if (finalScore <= 0.0f) {
                return 0.0f; // 提前短路截断
            }
        }
        return finalScore;
    }
};

// ============================================================================
// 4. 效用决策选择器 (带承诺增益以抑制高频振荡)
// ============================================================================
class UtilityBrain {
private:
    std::vector<std::shared_ptr<Decision>> decisions;
    std::string lastExecutedDecision = "";
    const float commitmentBonus = 0.05f; // 承诺增益因子 β

public:
    void RegisterDecision(std::shared_ptr<Decision> d) {
        decisions.push_back(d);
    }

    std::shared_ptr<Decision> SelectBestDecision(const AIContext& context) {
        std::shared_ptr<Decision> bestDecision = nullptr;
        float highestScore = -1.0f;

        for (const auto& d : decisions) {
            float baseScore = d->EvaluateScore(context);

            // 应用承诺增益 (Commitment Bonus)，防止临界点帧间乒乓振荡
            if (d->name == lastExecutedDecision) {
                baseScore += commitmentBonus;
            }

            if (baseScore > highestScore) {
                highestScore = baseScore;
                bestDecision = d;
            }
        }

        if (bestDecision) {
            lastExecutedDecision = bestDecision->name;
        }

        return bestDecision;
    }
};
```

---

## 6. 权威文献引证与技术图谱延伸

本章所构筑的技术体系与现代游戏 AI 理论紧密互锁，其底层支撑性研究与文献序列包括：

1. **双效用推理体系（Dual-Utility Reasoning）**  
   *Dill, K. 2015. "Dual-utility reasoning." In Game AI Pro Vol. 2.*  
   *技术映射*：解耦短期行为动机效用与长线战术战略意图效用，阐明如何在多层级目标下并行评估效用。
2. **效用系统理论基石（Introduction to Utility Theory）**  
   *Graham, R. 2014. "An introduction to utility theory." In Game AI Pro Vol. 1.*  
   *技术映射*：确立归一化响应空间、幂函数/Logit 曲线对偶变换，以及乘法与补偿加权聚合的数学一致性法则。
3. **空间与时间行为架构调度（Architecture Tricks: Managing Behaviors in Time, Space, and Depth）**  
   *Mark, D. 2013. Lecture, Game Developers Conference 2013.*  
   *技术映射*：论述了时间常数、空间截断等代理变量的有效性，为复杂动力学系统的离散化简化奠定理论根基。
4. **战术影响图与无限分辨率空间推理（Infinite-Resolution & Modular Influence Mapping）**  
   *Lewis, M. 2015 / Mark, D. 2015. In Game AI Pro Vol. 2.*  
   *技术映射*：提出了将空间场论映射为标量采样点的方法，使得效用系统能够以 $O(1)$ 的时间复杂度完成深度空间态势考量。
