---
type: Reference
title: "第24章 Being Where It Counts: Telling Paragon Bots Where to Go"
description: "Game AI Pro 工业级精读：Being Where It Counts: Telling Paragon Bots Where to Go。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第24章 Being Where It Counts: Telling Paragon Bots Where to Go

> 来源：*Game AI Pro 3: Balanced Cuts of Game AI Professionals*, Chapter 24.  
> 原文作者 / 资源：[Being Where It Counts: Telling Paragon Bots Where to Go](http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter24_Being_Where_It_Counts_Telling_Paragon_Bots_Where_to_Go.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷3-GameAIPro3](README.md) ｜ [专栏首页](../README.md)

---

---

## 1. 架构总览与分层决策模型

在多人在线战术竞技（Multiplayer Online Battle Arena, MOBA）游戏体系中，高阶智能体（AI Bots）的系统架构被严格解耦为两套具有不同时空尺度的决策与执行体系：**战术层（Tactical Layer）** 与 **战略层（Strategic Layer）**。

```
+-------------------------------------------------------------------------+
|                        战略层 (Strategic Layer)                          |
|  - 全局态势感知、目标图构建 (Objective Graph)、战力影响扩散 (Influence)    |
|  - AI 指挥官 (AI Commander)：多智能体资源配额匹配与全局目标分发          |
+-------------------------------------------------------------------------+
                                    |
                           [目标分配 / 战略行为修饰]
                                    v
+-------------------------------------------------------------------------+
|                        战术层 (Tactical Layer)                          |
|  - 微观移动控制：分段寻路 (NavMesh A*)、局部导向行为 (Steering Behaviors)  |
|  - 实时微操战斗：技能释放策略、血量监控与回血消耗品使用、仇恨目标动态切换 |
+-------------------------------------------------------------------------+
```

### 1.1 战术层（Tactical Layer）
战术层聚焦于**瞬时微操与近距离交战**（Moment-to-moment combat）。
* **核心职责**：管理单一英雄的战斗表现，确保其行为动作不打破玩家的“浸入感/怀疑消除”（Suspension of disbelief）。具体包含基础技能判定、生命值危险阈值下的消耗品（如治疗药水）使用、合乎逻辑的攻击目标选择策略（Target Selection Policies），以及遭遇致命危机时的撤退脱战行为。
* **局限性**：尽管微观表现优秀，但战术层仅决定单次碰撞的视觉合理性，其对整场比赛胜负走向的影响非常有限。如果缺乏全局协作，各个 Bot 陷入“各自为战”的单兵逻辑，极易被人类玩家逐个击破。

### 1.2 战略层（Strategic Layer）
战略层主导**中远期战术目标的推演、协同编队与全局决策**（Broader, team-level, strategic decision-making）。
* **核心职责**：全局态势感知（战场敌友单位分布、对线推塔进度）、预判敌方战略动向、评估防御设施受损危险度、敲定推进或回防目标，并执行资源最优调度（即派遣特定角色与数量的英雄前往目标地点）。
* **设计哲学**：MOBA 的本质是**在正确的时间将兵力集结在正确的地点**（Being where it counts）。在双方操作水平处于同一基准线的前提下，局部的兵力优势（Numerical Advantage）或突袭的战术突然性（Element of Surprise）是获胜的充要条件。战略系统的目标就是实现兵力集结。哪怕最原始的协同机制（即两名 Bot 选择同一条路径推进），也能在客观上分摊受击风险并将局域火力翻倍，从而构建出极具说服力的“团队协同假象”（Illusion of Cooperation）。

---

## 2. 空间推理与目标图架构 (Objective Graph Architecture)

为了使 AI 能够像人类选手查看小地图（Mini-map）一样对战场全局进行宏观理解，系统构建了一套覆盖全图的高层次抽象拓扑结构——**目标图（Objective Graph）**。该图充当分层导航图（Hierarchical Navigation Graph）的最高层，脱离底层密集网格，专注于语义化战略点。

```
               [防御塔 Node 1] <====================> [防御塔 Node 2]
                /     ^                                  ^     \
  Jungle Path  /      | (Direct Path)      (Direct Path) |      \ Jungle Path
              v       v                                  v       v
       [野怪营地 Node 3] <===========================> [经验井 Node 4]
                                 Cross Jungle
```

### 2.1 目标图节点（Nodes）
节点将游戏地图中的实体设施与战术锚点进行高维抽象，紧凑存储于连续内存数组中。
* **数据成员**：
  * 节点唯一索引（`Index`）
  * 关联地图实体指针（`Pointer to Map Structure`，如防御塔、经验井）
  * 世界空间坐标（`Location`，从关联实体拉取）
  * 缓存属性集（`Cached Properties`）：结构体类型（Type）、阵营归属（Team Ownership）、所属分路（Lane ID，如上路/中路/下路/野区）
* **语义覆盖范畴**：
  * **结构体设施**：防御塔（Towers）、基地核心（Base Core）、经验井（XP Wells，用于部署经验收割机 Harvester 并定期提取 XP）。
  * **战术兴趣点**：野怪营地（Jungle Creeps' Camps，提供经验与增益 Buff）、视野高台制高点（Vantage Points）、重生点（Spawn Locations）、战术伏击点（Ambush Locations）。

### 2.2 目标图边（Edges）
边代表节点之间在物理游戏世界中的 AI 可达性与通行代价。

#### 生成阶段：全连通有向图（Complete Digraph）构建
1. 在地图离线烘焙期（或编辑器阶段），遍历每一对节点组合 $(u, v)$。
2. 触发底层导航网格寻路查询（NavMesh Pathfinding Query）。
3. 若可达，则将其实际测得的折线路径长度赋值给边成本：
   $$Cost(u \to v) = \text{Length}(\text{PathfindingPath}(u, v))$$
4. **非对称性验证**：必须分别双向评估 $u \to v$ 与 $v \to u$。由于地图存在单向掉落平台、高低地跳跃点或动态障碍，非平凡地图上 $Cost(u \to v)$ 与 $Cost(v \to u)$ 通常不相等。
5. 最终生成包含所有可能连通度的全连通有向图。在《Paragon》规模的地图中，该全离线烘焙计算耗时不超过 3 秒。

#### 优化阶段：容差修剪算法（Edge Pruning）
全连通有向图存在海量几何冗余（如直接连接基地与敌方二塔的边）。算法引入松弛容差，修剪掉“能够被中间节点有效替代”的长边，保留高价值拓扑连接。

**修剪准则**：对于边 $E(u \to v)$，若存在任意第三方节点 $w \in V \setminus \{u, v\}$，使得绕行成本满足：
$$Cost(u \to v) \ge Cost(u \to w) + Cost(w \to v) + \text{EdgeCostOffset}$$
则标记 $E(u \to v)$ 为可修剪边（Pruned Edge）。其中 $\text{EdgeCostOffset}$ 为关卡设计师配置的容差偏置参数。

**修剪实现算法伪代码**：
```cpp
// Listing 24.1: Pruning of graph edges
Graph::PruneEdges(InNodes, InOutEdges)
{
    // 按路径成本从大到小对边集进行降序排列，优先检测长跨度冗余边
    SortEdgesByCostDescending(InOutEdges);

    for Edge in InOutEdges:
        for Node in (InNodes - {Edge.StartNode, Edge.EndNode}):
            // 三角不等式松弛判定，引入设计者容差
            if Edge.Cost >= InOutEdges[Edge.StartNode][Node].Cost 
                          + InOutEdges[Node][Edge.EndNode].Cost 
                          + EdgeCostOffset:
                // 标记为已修剪，严禁从拓扑结构中物理剔除！
                Edge.IsPruned = true;
                break;
}
```

> **工业设计模式精粹**：**逻辑修剪（Mark as Pruned）而非物理移除（Delete）**。被修剪的边保留了精确的底层 NavMesh 几何通行代价，在运行期的**影响图战力扩散**与**跨节点欧几里得测距替代**中充当高精度的距离缓存池，规避了运行时昂贵的 NavMesh 查询。

### 2.3 最近节点空间查询网格 (Closest Graph Node Lookup Grid)
在运行时频繁调用底层寻路以确认 Bot 或小兵所处的目标图节点会产生极高的计算开销。系统为此设计了空间哈希降维网格：
* **几何结构**：覆盖整张地图的粗粒度 2D 网格（Coarse Grid）。
* **离线计算机制**：在编辑器预构建期，对每个网格单元的几何中心点 $C_{\text{cell}}$，执行到底层所有图节点的 NavMesh 真实路径测试，记录在真实路径成本意义下的最近节点：
  $$\text{TargetNode} = \arg\min_{N \in V} \left(\text{PathLength}(C_{\text{cell}}, \text{Location}(N))\right)$$
  可选缓存精确测定的路径代价值。
* **权衡考量（Trade-off）**：
  * 分辨率越高：空间语义映射精度越高，但内存占用（Memory Footprint）激增；
  * 分辨率越低：占用极少内存，但边界处可能产生拓扑映射偏差。

---

## 3. 动态态势感知：影响图与战力扩散算法

战略层不关注小兵个体的微观朝向或特定技能冷却，只关注宏观的**战力潜能（Combat Potential）** 与 **区域威胁度（Danger Level）**。

### 3.1 影响度数学模型与扩散机制
系统引入影响图（Influence Map）理论，在目标图上进行离线烘焙边引导的周期性战力扩散（Propagation）。

```
  [英雄/兵线]
      | (LookupClosestGraphNode)
      v
 [局部主节点 N] (注入 100% 原始影响度)
      |
      +---> 沿全量边 (包含 Pruned Edges) 衰减广播
      |
      +---> [邻接节点 A] (衰减影响度 = f(EdgeCost))
      +---> [邻接节点 B] (衰减影响度 = f(EdgeCost))
```

1. **影响衰减函数**：设基础战力为 $I_0$，边代价为 $d = \text{Edge.Cost}$，影响度衰减函数一般随距离递减：
   $$I(d) = I_0 \cdot \phi(d)$$
   式中 $\phi(d)$ 为距离衰减因子（在代码中表现为 `CalculateHeroInfluence(Hero, Edge.Cost)`）。
2. **小兵行军约束（Lane Locking）**：小兵仅沿固定分路推进，严禁穿透野区扩散至其他分路。因此，兵线影响仅在满足 $\text{Edge.EndNode.LaneID} == \text{Wave.LaneID}$ 的边上传导。

### 3.2 影响度计算执行伪代码
```cpp
// Listing 24.2: Influence calculations
Graph::UpdateInfluence(InAllHeroes, InAllMinionWaves)
{
    ResetInfluenceInformation();

    // 1. 英雄战斗力与威胁度扩散
    for Hero in InAllHeroes:
        Node = LookupClosestGraphNode(Hero.Location);
        Influence = CalculateHeroInfluence(Hero, 0);
        Node.ApplyInfluence(Influence);

        // 利用图的完整连接性（利用包含已修剪边在内的所有边）向外传播
        for Edge in Node.Edges:
            Influence = CalculateHeroInfluence(Hero, Edge.Cost);
            Edge.End.ApplyInfluence(Hero.Team, Influence);

    // 2. 兵线态势扩散（严格限制分路一致性）
    for Wave in InMinionWaves:
        Node = LookupClosestGraphNode(Wave.CenterLocation);
        Influence = CalculateWaveInfluence(Wave, 0);
        Node.ApplyInfluence(Influence);

        for Edge in Node.Edges:
            // 阻断野区泄露，防止战略误判
            if Edge.EndNode.LaneID != Wave.LaneID:
                continue;

            Influence = CalculateWaveInfluence(Wave, Edge.Cost);
            Edge.End.ApplyInfluence(Wave.Team, Influence);
}
```

---

## 4. 分层寻路与移动规划 (Hierarchical Planning)

### 4.1 拓扑 A* 与启发式修饰
在战略规划阶段，寻路算法采用标准的 $A^*$ 算法在目标图的**非修剪边（Unpruned Edges）** 上运行：
* **修剪边的作用**：促使 Bot 倾向于沿着包含“战略兴趣点”的合理通道移动。
* **威胁规避启发式（Threat-Aware Heuristic）**：在节点评价函数 $f(n) = g(n) + h(n)$ 中加入敌方影响度惩罚项 $P_{\text{danger}}(n)$：
  $$f'(n) = g(n) + h(n) + \omega_{\text{threat}} \cdot \text{EnemyInfluence}(n)$$
  此项能驱使 AI 绕开高危伏击区域或敌方重兵把守的分路。由于目标图节点数量极度精简，复杂的启发式评估完全不会引入性能瓶颈。

### 4.2 延迟分段导航机制 (Deferred Step-by-Step Path Generation)
为了解决长距离移动规划在动态 MOBA 战场中的脆弱性，底层引擎抛弃了“一次性生成整条 NavMesh 折线路径”的传统方案，采用**反应式局部推进机制**：

```
[战略目标判定] 
      │
      ▼
生成目标图拓扑序列：[Node 1] ---> [Node 2] ---> [Node 3] ---> [Node 4]
      │
      ▼
计算 NavMesh 路径 (Node 1 -> Node 2)
      │
      ▼ (抵达 Node 2 时)
触发下一次 NavMesh 寻路 (Node 2 -> Node 3)
      │
      └─> [若中途遭遇战斗 / 阵亡 / 目标取消] ──> 立即打断，无需销毁未来路径
```

* **性能节约**：生成一系列局部短距离路径的平摊计算成本显著低于单次全图超长路径搜索。
* **容错性**：在充满高度不确定性的战局中，Bot 大概率会在行进中途遭遇突发状况（如遭遇敌方打野、遭遇伏击或阵亡）。若预先生成全量底层路径，中途被打断会导致未使用的规划计算被直接浪费。

---

## 5. 目标驱动体系 (Bot Objectives)

目标（Objective）在架构上充当**动态行为修饰器（Behavior Modifier）**。当 AI 被赋予某个特定目标时，该目标不仅规定了目的地与到达后的行为逻辑，还负责裁定自身的人员分配规则与适应度评估。

### 5.1 目标属性与优先级动态评价
每个目标绑定特定的图节点，其优先级分值 $P_{\text{obj}}$ 受多维环境因子联合调制：

$$P_{\text{obj}} = \mathcal{F}\left(\text{Type}, \text{Location}, \text{Health}, \text{EnemyPresence}, \text{CustomFactors}\right)$$

| 评价维度 (Factors) | 战略推演影响机制 |
| :--- | :--- |
| **目标固有类型 (Type)** | 防御性任务的基准优先级天然高于进攻性任务（如保卫防御塔优先于推塔）。 |
| **空间拓扑战略级 (Location)** | 核心基地防御目标（Base Defense）优先级远高于外塔防御。 |
| **设施物理状态 (Health)** | 防御塔残血度越高，紧急程度权重呈非线性上升。 |
| **敌方威胁度 (Enemy Presence)** | 直接读取目标图节点上的敌方影响度分值；防守任务随敌方兵力增加急剧提权。 |
| **目标专属特征参数 (Specifics)** | **经验井 (XP Well)** 目标具有独特的双向动态函数：收割机满载滞留时间越长，战术溢出浪费越严重，采集优先级越高；然而己方英雄平均等级越高，对经验的需求阈值下降，优先级随之衰减。 |

### 5.2 目标的生命周期与注册系统
* **预实例化机制**：所有类型的目标（推进、防守、采矿、伏击等）均拥有对应的生成器（Generator）。在比赛初始化阶段，预先实例化所有潜在目标实例。
* **系统监听解耦**：目标实例在初始化后，向其关联的目标图节点以及底层世界系统注册事件监听回调。
  * *示例*：防御目标（Defend Objective）注册防御塔被摧毁事件；经验收割目标注册收割机储量饱满事件。
* **状态机管理**：目标在运行时受控于两种状态：
  * **休眠态（Dormant）**：条件不满足（如建筑安然无恙、野怪未刷新），AI 指挥官完全跳过该目标的仲裁。
  * **可用态（Available）**：被激活并进入指挥官的排序评估队列。

---

## 6. AI 指挥官战略资源仲裁系统 (The AI Commander)

AI 指挥官（AI Commander）是战略调度的中枢神经系统，其本质是一个**基于贪心拍卖与效用衰减的多智能体资源分配引擎**。

```
                         [战局事件触发]
                               |
                               v
               +-------------------------------+
               | 步骤 1: 目标更新与状态排序     |
               | - 计算可用目标优先级 P_obj    |
               | - 按 P_obj 降序排列           |
               +-------------------------------+
                               |
                               v
               +-------------------------------+
               | 步骤 2: 智能体过滤与打分       |
               | - 排除不合格 Bot (技能/工具)   |
               | - 计算角色与距离适应度 Score   |
               +-------------------------------+
                               |
                               v
               +-------------------------------+
               | 步骤 3: 贪心分配与反馈循环     |
               | ┌──> 选取最高优先级目标       |
               | │    分发最小资源子集         |
               | │    动态衰减目标优先级       |
               | └── 尚有未分配 Bot? ──(YES)──┘|
               +-------------------------------+
                               | (NO)
                               v
                         [执行目标分发]
```

### 6.1 仲裁与分配流水线
指挥官按阵营严格独立运算（避免共享作弊信息），其分配流水线分为三步：

#### 步骤 1：目标自省与优先级排序
遍历己方阵营所有预分配的目标实例，触发其自省函数以决定其处于 `Dormant` 还是 `Available`。
* *动态语义转换示例*：经验井目标在己方控制或中立时处于“常规采矿（Regular）”模式；一旦被敌方安放收割机，则其内部逻辑自动切换为“敌对夺取（Offensive）”模式。
* 对所有处于 `Available` 状态的目标计算 $P_{\text{obj}}$ 并实施降序快排。

#### 步骤 2：针对英雄候选集的过滤与评分
目标对己方所有待分配英雄实施筛选与能力打分（详见第 7 节），排除不满足硬性前置条件的单位（如未装备收割机钥匙能力的英雄无法领取建井任务）。

#### 步骤 3：多轮贪心分配与边际效用衰减循环
算法执行迭代循环，直至己方阵营全部可用单位分配完毕：
1. 取出当前可用目标中优先级最高的目标 $O_{\text{top}}$。
2. 允许 $O_{\text{top}}$ 从其候选池中遴选**最小必要资源子集（Minimum Set of Resources）**，例如单个近战（Brawler）或者“辅助（Support）+ 法师（Caster）”的双人组合。
3. **优先级阻尼衰减（Priority Penalty）**：目标获取资源后，其实际需求被局部满足，优先级必须对应下调。扣除公式严格依赖于全局上下文：
   $$\Delta P = \frac{\text{MaxPriority}}{\text{TeamSize}} \times N_{\text{assigned}}$$
   $$P_{\text{obj}} \leftarrow P_{\text{obj}} - \Delta P$$
   其中 $\text{MaxPriority}$ 为所有活跃目标的最大优先级，$\text{TeamSize}$ 为队伍总人数，$N_{\text{assigned}}$ 为本次占用的单位数量。该机制确保单个目标不会无限虹吸全局资源，使多路防守与野区控制得以并发推进。

---

## 7. 智能体效用评分机制与人类协同处理

### 7.1 智能体效用评分模型 (Agent Scoring Model)
每个目标对候选智能体进行独立评分，以保障决策权力的局部化（Localization），防止单个目标的算法缺陷污染全局指挥系统。评分由**角色契合度**与**移动时间成本**复合构成：

#### 角色偏好加权（Role Preferences）
系统支持自定义复杂评估与标准偏好乘数法。英雄可同时具备多重标签（如兼具坦克 `Tank` 与近战 `Brawler` 属性）。目标定义各职业的偏好权重向量 $\mathbf{W}_{\text{role}}$，智能体的角色得分由其最高项决定：
$$S_{\text{role}}(A) = \max_{r \in \text{Roles}(A)} (W_{\text{role}}(r))$$
*示例*：若目标设定 $W(\text{Tank}) = 0.1$，$W(\text{Brawler}) = 0.3$，某英雄同时具有这两个标签，则其最终角色得分为 $0.3$。

#### 距离与行进时间成本（Travel Time Cost）
战略层利用目标图中预先烘焙的边成本，在 $O(1)$ 复杂度内提取出当前英雄所在图节点到目标节点的精确通行路径距离 $D(A, O)$：
$$S_{\text{distance}}(A) = \mathcal{G}\left(\frac{D(A, O)}{V(A)}\right)$$
将中等评分但近距离的英雄派往目标，其效用往往远高于派遣地图另一端的高评分英雄。

#### 阵亡状态时空折叠技巧（Dead Heroes Virtualization）
针对处于死亡冷却状态的英雄，系统引入时间等效性投影：
1. 将阵亡英雄虚拟投影在己方泉水基地（Base Spawn）。
2. 计算其从基地抵达目标的物理行军耗时 $T_{\text{travel}}$。
3. 将剩余复活倒计时 $T_{\text{respawn}}$ 直接注入通行时间中，构建综合抵达时间：
   $$T_{\text{effective}} = T_{\text{travel}} + T_{\text{respawn}}$$
4. **战术收益**：在基地遭受致命围攻时，一名尚需 10 秒复活的守家核心英雄，其综合响应耗时（10s 复活 + 0s 行程 = 10s）将显著优于一名存活但在地图边缘需要 20 秒才能赶回的高战力单位。

### 7.2 人机混合编队容错拓扑 (Human-Bot Mixed Teams)

```
        +-----------------------------------------+
        |          AI 指挥官资源分配决策           |
        +-----------------------------------------+
                 |                       |
      (分配至 Bot 智能体)        (分配至人类玩家)
                 |                       |
                 v                       v
      [驱动战术层执行对应行为]   [触发轮盘 UI 提示: "Defend Left!"]
                 |                       |
                 |                       | (人类自由行动：接受/违抗)
                 v                       v
        +-----------------------------------------+
        | 下一轮决策帧：AI 重新评估态势并动态修正补偿 |
        +-----------------------------------------+
```

AI 指挥官在数据契约层面**将人类玩家与 Bot 完全同等对待**（No Exceptions for Human Agents）：
* **一致性分配**：指挥官在执行分配流水线时，将人类玩家视作普通的可用代理（Agent）并计算其适配目标。
* **引导而非越权（Guidance over Control）**：AI 无法控制人类操作，系统将目标转化为游戏屏幕交互命令（Team Communication System，如“防守左路”、“集合团战”）。
* **抗扰动自愈机制**：战略流水线具有周期反应性。若人类玩家未遵从战术引导而自由行动，指挥官在下一次调度帧中将读取到真实的人类物理坐标与态势分布。分配给人类的目标由于未被实际响应，其高优先级依然存在，系统会自动选派下一个最佳 Bot 补足缺口，防止战局系统崩溃。
* **反向语义接口扩展**：人类玩家发出的战术轮盘标记可直接作为战略层的先验输入（Hints），瞬时拉高对应关联图节点的优先级基准分，形成双向协作闭环。

---

## 8. 核心数据结构与工业设计模式实现

以下为基于现代 C++ 规范重构的战略层核心数据结构与调度控制体系，涵盖目标图、影响传播网格以及指挥官资源拍卖逻辑。

```cpp
#include <vector>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <limits>

// ============================================================================
// 1. 目标图核心数据结构 (Objective Graph Architecture)
// ============================================================================

enum class EStructureType : uint8_t {
    Tower,
    Inhibitor,
    CoreBase,
    XPWell,
    JungleCamp,
    TacticalPoint
};

enum class EHeroRole : uint8_t {
    Tank,
    Brawler,
    Caster,
    Support,
    Ranger
};

struct FVector3 {
    float X{0.0f}, Y{0.0f}, Z{0.0f};
};

struct FGraphEdge {
    int32_t StartNodeIndex{-1};
    int32_t EndNodeIndex{-1};
    float Cost{0.0f};              // NavMesh 寻路所得真实几何路径长度
    bool bIsPruned{false};         // 仅做逻辑修剪，内存中保留用于空间测距与战力扩散
};

struct FGraphNode {
    int32_t NodeIndex{-1};
    void* BoundStructurePtr{nullptr}; // 绑定的实体设施引用
    FVector3 WorldLocation;
    EStructureType StructureType;
    int32_t OwningTeamID{-1};
    int32_t LaneID{-1};            // 0: 野区, 1: 边路A, 2: 中路, 3: 边路B

    std::vector<FGraphEdge> OutgoingEdges;
    float AccumulatedInfluence[2]{0.0f, 0.0f}; // 索引代表队伍 ID

    void ResetInfluence() {
        AccumulatedInfluence[0] = 0.0f;
        AccumulatedInfluence[1] = 0.0f;
    }

    void ApplyInfluence(int32_t TeamID, float Value) {
        if (TeamID >= 0 && TeamID < 2) {
            AccumulatedInfluence[TeamID] += Value;
        }
    }
};

// ============================================================================
// 2. 目标与智能体抽象 (Bot Objectives & Agents)
// ============================================================================

class AAgentCharacter {
public:
    int32_t EntityID{-1};
    int32_t TeamID{-1};
    FVector3 CurrentLocation;
    bool bIsDead{false};
    float RespawnTimer{0.0f};
    float MoveSpeed{500.0f};
    std::vector<EHeroRole> Roles;
    bool bHasHarvesterKeyAbility{false};

    bool HasRole(EHeroRole InRole) const {
        return std::find(Roles.begin(), Roles.end(), InRole) != Roles.end();
    }
};

enum class EObjectiveState : uint8_t {
    Dormant,
    Available
};

class FBotObjective {
public:
    virtual ~FBotObjective() = default;

    int32_t AssociatedNodeIndex{-1};
    int32_t OwningTeamID{-1};
    float CurrentPriority{0.0f};
    EObjectiveState State{EObjectiveState::Dormant};

    virtual void UpdateStateAndPriority(const std::vector<FGraphNode>& GraphNodes) = 0;
    virtual bool CanAssignAgent(const AAgentCharacter& Agent) const = 0;
    virtual float ScoreAgent(const AAgentCharacter& Agent, const FGraphNode& Node, float PathCost) const = 0;
    virtual size_t GetMinimumRequiredResources() const = 0;
};

// 典型实现：经验井收割目标
class FXPHarvesterObjective : public FBotObjective {
public:
    bool bIsEnemyOccupied{false};
    float HarvesterFillRatio{0.0f};
    float TeamAverageHeroLevel{1.0f};

    void UpdateStateAndPriority(const std::vector<FGraphNode>& GraphNodes) override {
        // 若经验井未建造且非己方状态，转入激进夺取模式或休眠
        if (HarvesterFillRatio <= 0.05f && !bIsEnemyOccupied) {
            State = EObjectiveState::Dormant;
            CurrentPriority = 0.0f;
            return;
        }

        State = EObjectiveState::Available;
        // 优先级受收割机满载度正向驱动，受团队平均等级负向衰减
        float Urgency = HarvesterFillRatio * 100.0f;
        float LevelScale = std::max(0.1f, 1.0f - (TeamAverageHeroLevel / 20.0f));
        CurrentPriority = Urgency * LevelScale * (bIsEnemyOccupied ? 1.5f : 1.0f);
    }

    bool CanAssignAgent(const AAgentCharacter& Agent) const override {
        // 强行剔除无插旗能力（收割钥匙）的角色
        return Agent.bHasHarvesterKeyAbility;
    }

    float ScoreAgent(const AAgentCharacter& Agent, const FGraphNode& Node, float PathCost) const override {
        float Score = 0.5f; // 基础效用
        // 角色偏好加权
        if (Agent.HasRole(EHeroRole::Support)) Score += 0.4f;
        if (Agent.HasRole(EHeroRole::Brawler)) Score += 0.2f;

        // 距离衰减：利用预计算路径距离换算为通行时间
        float EffectiveTime = PathCost / std::max(1.0f, Agent.MoveSpeed);
        if (Agent.bIsDead) {
            EffectiveTime += Agent.RespawnTimer; // 融入阵亡惩罚
        }
        float DistanceScore = std::max(0.0f, 1.0f - (EffectiveTime / 60.0f));
        return Score * DistanceScore;
    }

    size_t GetMinimumRequiredResources() const override {
        return 1; // 仅需一名单位前往开启或收取
    }
};

// ============================================================================
// 3. AI 指挥官战略资源分配引擎 (The AI Commander Execution Engine)
// ============================================================================

class FAICommander {
public:
    struct FAgentScoringRecord {
        AAgentCharacter* Agent{nullptr};
        float Score{0.0f};
    };

    void ExecuteObjectiveAssignment(
        int32_t TeamID,
        std::vector<FGraphNode>& GraphNodes,
        std::vector<std::shared_ptr<FBotObjective>>& TeamObjectives,
        std::vector<AAgentCharacter*>& TeamAgents)
    {
        // 阶段 1: 状态自省与可用性判定
        std::vector<std::shared_ptr<FBotObjective>> AvailableObjectives;
        float MaxGlobalPriority = 0.0f;

        for (auto& Obj : TeamObjectives) {
            Obj->UpdateStateAndPriority(GraphNodes);
            if (Obj->State == EObjectiveState::Available) {
                AvailableObjectives.push_back(Obj);
                if (Obj->CurrentPriority > MaxGlobalPriority) {
                    MaxGlobalPriority = Obj->CurrentPriority;
                }
            }
        }

        if (AvailableObjectives.empty() || TeamAgents.empty()) {
            return;
        }

        // 按优先级降序排序可用目标
        std::sort(AvailableObjectives.begin(), AvailableObjectives.end(),
            [](const std::shared_ptr<FBotObjective>& A, const std::shared_ptr<FBotObjective>& B) {
                return A->CurrentPriority > B->CurrentPriority;
            });

        // 阶段 2 与阶段 3: 贪心资源竞标与优先级阻尼衰减循环
        std::vector<AAgentCharacter*> UnassignedAgents = TeamAgents;
        const float PriorityPenaltyPerAgent = MaxGlobalPriority / static_cast<float>(TeamAgents.size());

        while (!UnassignedAgents.empty()) {
            // 每次提取当前优先级最高的目标
            auto HighestObjIt = std::max_element(AvailableObjectives.begin(), AvailableObjectives.end(),
                [](const std::shared_ptr<FBotObjective>& A, const std::shared_ptr<FBotObjective>& B) {
                    return A->CurrentPriority < B->CurrentPriority;
                });

            if (HighestObjIt == AvailableObjectives.end() || (*HighestObjIt)->CurrentPriority <= 0.0f) {
                // 剩余所有目标均失去激励，终止本次匹配
                break;
            }

            auto CurrentObj = *HighestObjIt;
            const FGraphNode& TargetNode = GraphNodes[CurrentObj->AssociatedNodeIndex];

            // 过滤并评分所有可用候选人
            std::vector<FAgentScoringRecord> ScoredCandidates;
            for (auto* Agent : UnassignedAgents) {
                if (CurrentObj->CanAssignAgent(*Agent)) {
                    // 获取空间图拓扑测距
                    float PathCost = 0.0f;
                    if (Agent->bIsDead) {
                        // 虚拟基地节点索引通常为 0
                        PathCost = GraphNodes[0].OutgoingEdges[TargetNode.NodeIndex].Cost;
                    } else {
                        // 实际开发中通过 Closest Graph Node Lookup Grid 查表获取
                        PathCost = GraphNodes[TargetNode.NodeIndex].OutgoingEdges[TargetNode.NodeIndex].Cost;
                    }

                    float FinalScore = CurrentObj->ScoreAgent(*Agent, TargetNode, PathCost);
                    ScoredCandidates.push_back({Agent, FinalScore});
                }
            }

            if (ScoredCandidates.empty()) {
                // 目标虽然处于最高优先级但无符合硬性要求的可用人员，施加惩罚使其降级，允许其他目标竞争
                CurrentObj->CurrentPriority = 0.0f;
                continue;
            }

---

在大型多人在线战术竞技游戏（MOBA，如 Epic Games 的 *Paragon*）中，顶层战略控制层（AI Commander Layer）与底层微观行为树（Individual Behavior Tree Layer）的解耦协同是游戏 AI 架构设计的核心命题。本文基于《Game AI Pro 3》第 24 章技术文献，深入剖析依托**目标图（Objective Graph）**实现的战机目标动态分配（Opportunistic Objectives）、基于概率占用网络（Occupancy Maps）的不完全信息态势推断（Probabilistic Presence Propagation）、基于神经网络的全局胜率/态势价值评估（World State Map Evaluation），以及由此引发的群体涌现战术行为（Emergent Tactical Behaviors）。

---

## 1. 战机目标决策模型（Opportunistic Objectives）

### 1.1 核心设计原理与时空约束
常规的宏观决策管道中，智能体（Bot）通常由 AI 指挥官分配单一定向的高优先级宏观任务（如防御外塔或推进主路）。然而，长距离图上拓扑寻路（Topological Graph Pathfinding）耗时过长，若忽略路径沿途分布的低成本边际收益（如野区资源获取、收集经验水井），智能体的行为模式会显得机械且效率低下。

**战机目标机制（Opportunistic Objectives）** 允许智能体在沿目标图拓扑路径推进时，实时拦截、评估并动态插拔轻量级的子任务：
* **寻路偏好注入（Biased Pathfinding）**：在宏观路径规划阶段调整启发式加权，促使寻路算法倾向于途径存在“未分配且可抢占”战机目标的图节点。
* **节点触发式抢占（Node Progression Check）**：智能体沿图路径物理移动，每抵达一个目标图拓扑节点 $v \in \mathcal{V}$，即触发即时可用目标查询。
* **双向握手协议（Bidirectional Contract）**：目标本身保留仲裁权（Arbitration Right），仅当目标特性满足“低延时、快周转”特征时，才允许被中途认领。

```
[全局AI指挥官] ── 派发主目标 ──> [智能体宏观寻路器]
                                         │ 沿着目标图推进
                                         ▼
                             ┌─ [遍历当前图节点 v] ──┐
                             │                     │
                             ▼                     ▼
                     (检测未分配目标)        (继续执行主目标)
                             │
                             ▼
                     (战机目标资格判定)
                             │
                    [目标允许中途认领?] ──── 否 ────> 忽略并继续推进
                             │ 是
                             ▼
                    [压栈战机目标栈]
                             │
                             ▼
                    [就地快速执行 (如打野/采矿)]
                             │
                             ▼
                    [出栈并恢复主目标路径]
```

### 1.2 工业级接口与算法实现（C++17 工业级规格）

战机目标机制依赖于智能体与目标对象之间的契约。以下为目标图寻路与战机目标判定的工业级拓扑核心结构：

```cpp
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <cstdint>

// 目标类型定义
enum class EObjectiveType : uint8_t {
    PushLane,         // 主干道推进（不可作战机目标）
    DefendTower,      // 防御防御塔（不可作战机目标）
    HarvestJungleXP,  // 采集野区经验井（典型战机目标）
    NeutralCreepCamp  // 击杀轻量野怪营地（典型战机目标）
};

// 目标图节点基础信息
struct ObjectiveNode;

struct IObjective {
    virtual ~IObjective() = default;
    virtual EObjectiveType GetType() const = 0;
    virtual float GetEstimatedExecutionTime() const = 0;
    virtual bool AllowsOpportunisticAssignment() const = 0;
    virtual bool EvaluateSuitability(uint32_t botId, const ObjectiveNode& currentNode) = 0;
    virtual void ExecuteOpportunistic(uint32_t botId) = 0;
};

// 目标图节点拓扑
struct ObjectiveNode {
    uint32_t NodeId;
    std::vector<uint32_t> NeighborNodeIds;
    std::vector<std::shared_ptr<IObjective>> AssociatedObjectives;
    float DangerInfluenceWeight{ 0.0f }; // 敌方危险影响度
};

// 战机目标拦截与裁决器
class OpportunisticArbitrator {
public:
    static std::shared_ptr<IObjective> TryAcquireOpportunisticObjective(
        uint32_t botId,
        const ObjectiveNode& node,
        float maxAllowedDetourDuration) 
    {
        for (const auto& objective : node.AssociatedObjectives) {
            if (!objective) continue;

            // 1. 目标自身必须允许“顺路执行”
            if (!objective->AllowsOpportunisticAssignment()) {
                continue;
            }

            // 2. 耗时硬性约束：必须快进快出
            if (objective->GetEstimatedExecutionTime() > maxAllowedDetourDuration) {
                continue;
            }

            // 3. 智能体现有上下文适应性判定
            if (objective->EvaluateSuitability(botId, node)) {
                return objective;
            }
        }
        return nullptr;
    }
};
```

---

## 2. 概率“存在感”传播模型（Probabilistic "Presence" Propagation）

在 MOBA 类游戏中，战争迷雾（Fog of War, FoW）构成了严格的非完全信息博弈（Imperfect Information Game）。为消除底层“全图透视（Omniscience）”所破坏的游戏拟真感，高阶架构引入了构建于目标图之上的概率存在感传播网格，属于 **占用栅格地图（Occupancy Maps, Isla 2006）** 在连续非规则拓扑图上的延伸。

### 2.1 状态转移与空间扩散数学推导

设游戏宏观地图的目标图为拓扑无向图 $G = (\mathcal{V}, \mathcal{E})$，其中 $\mathcal{V}$ 为拓扑节点集，$\mathcal{E}$ 为连通边集。设第 $k$ 个敌方英雄在时间戳 $t$ 处于节点 $v \in \mathcal{V}$ 的概率分布为 $P(X_k(t) = v)$，满足：
$$\sum_{v \in \mathcal{V}} P(X_k(t) = v) = 1, \quad \forall t \ge 0$$

#### 1. 确定性观测吸收（Observation Collapsing）
当敌方英雄 $k$ 进入己方友军单位（英雄、防御塔、小兵网格）的可视视线（Line of Sight, LoS）内时，概率波函数坍缩。设最邻近拓扑节点为 $v_{\text{observed}}$：
$$P(X_k(t_{\text{seen}}) = v) = 
\begin{cases} 
1.0, & \text{if } v = v_{\text{observed}} \\
0.0, & \forall v \ne v_{\text{observed}}
\end{cases}$$

#### 2. 时空马尔可夫演化与信息扩散（Temporal Diffusion Step）
当敌方英雄脱离视线（$t > t_{\text{last\_seen}}$），概率分布在图上随时间推进进行马尔可夫扩散。定义离散转移算子：
$$P(X_k(t + \Delta t) = v) = (1 - \lambda) P(X_k(t) = v) + \sum_{u \in \mathcal{N}(v)} \frac{\lambda}{\text{deg}(u)} P(X_k(t) = u)$$
其中 $\mathcal{N}(v)$ 为节点 $v$ 的邻接拓扑集，$\text{deg}(u)$ 为邻居度数，$\lambda \in (0, 1)$ 为基于该英雄最大移动速度 $v_{\max}$ 与时间步长 $\Delta t$ 映射的扩散系数：
$$\lambda \approx \min\left(1.0, \, \frac{v_{\max} \cdot \Delta t}{\bar{d}_{\text{edge}}}\right)$$
$\bar{d}_{\text{edge}}$ 为拓扑图中相邻节点的平均空间距离。

#### 3. 意图驱动的热点偏置校准（Heuristic Bias Weighting）
在实际对抗中，敌方移动并非完全随机游走（Random Walk），而是受到战术利益驱使（如残血防御塔、野区 Buff 资源刷新）。引入意图吸引子权值矩阵 $w(v)$，归一化后验概率：
$$\tilde{P}(X_k(t) = v) = \frac{P(X_k(t) = v) \cdot w(v) \cdot \exp(-\alpha \cdot D(v, v_{\text{last}}))}{\sum_{u \in \mathcal{V}} \left[ P(X_k(t) = u) \cdot w(u) \cdot \exp(-\alpha \cdot D(u, v_{\text{last}})) \right]}$$
其中：
* $D(u, v_{\text{last}})$ 为图上沿拓扑边的最短测地距离（Geodesic Distance）；
* $\alpha$ 为基于衰减速率的时间衰减常数；
* $w(v) \in [1.0, W_{\max}]$ 为目标节点 $v$ 的战术热度系数（Hot Location Value）。

### 2.2 概率存在感拓扑计算流程

```
[敌方实体进入视野] ────── 概率坍缩 ──────> P(v_observed) = 1.0, 其余节点 = 0.0
       │
   (失去视野)
       │
       ▼
[随时间步更新扩散] ── 拓扑邻域迭代 ──> P(v, t+Δt) = (1-λ)P(v,t) + Σ [λ/deg(u) * P(u,t)]
       │
       ▼
[战术热点偏置修正] ── 乘以空间意图权重 ──> 结合距离阻尼与地图价值分布
       │
       ▼
[全局概率归一化] ──── 生成后验占用地图 ──> 提供给战略指挥官进行伏击/防守决策
```

---

## 3. 基于数据驱动的地图胜率与态势评估（Map Evaluation）

### 3.1 抽象图拓扑的特征向量化（Feature Vectorization）
传统网格或连续 3D 坐标系包含巨量空间噪声，直接输入神经网络会导致维数灾难。目标图作为离散空间抽象，能够自然将“全图局势（World State）”压入一个低维特征张量中。

每个图节点 $v_i \in \mathcal{V}$（$i = 1, \dots, N$）提取固定的语义状态元组：
$$\mathbf{f}(v_i) = \Big[ p_{\text{presence}}(v_i), \, I_{\text{enemy}}(v_i), \, I_{\text{ally}}(v_i), \, \mathcal{S}_{\text{tower}}(v_i), \, \mathcal{R}_{\text{resource}}(v_i) \Big]^T$$

| 特征维度（Feature Dimension） | 物理与战术含义 | 数值归一化域 |
| :--- | :--- | :--- |
| $p_{\text{presence}}(v_i)$ | 概率存在感网络输出的敌方英雄综合分布概率 | $[0.0, 1.0]$ |
| $I_{\text{enemy}}(v_i)$ | 节点处敌方战斗力影响图强度（Influence Mapping） | $[0.0, \infty) \to [0.0, 1.0]$ |
| $I_{\text{ally}}(v_i)$ | 节点处己方战斗力影响图强度 | $[0.0, \infty) \to [0.0, 1.0]$ |
| $\mathcal{S}_{\text{tower}}(v_i)$ | 防御塔阵营与剩余血量百分比（+1.0己方全血，-1.0敌方全血）| $[-1.0, 1.0]$ |
| $\mathcal{R}_{\text{resource}}(v_i)$ | 野区资源/经济水井存活与就绪状态 | $\{0.0, 1.0\}$ |

将全图 $N$ 个节点的特征连接，并补充队伍全局标量（经济差 $\Delta \text{Gold}$、经验差 $\Delta \text{XP}$、击杀比率等），得到全图状态张量：
$$\mathbf{\Psi}_{\text{world}} = \Big[ \mathbf{f}(v_1)^T, \mathbf{f}(v_2)^T, \dots, \mathbf{f}(v_N)^T, \, \Delta \text{Gold}, \, \Delta \text{XP} \Big]^T \in \mathbb{R}^{5N + 2}$$

### 3.2 神经网络评估拓扑与战略姿态裁决（Stance Arbitration）
通过人类高水平对抗对局（Match Telemetry Data）进行时间切片采样，将切片时的 $\mathbf{\Psi}_{\text{world}}$ 与最终比赛胜负（$y \in \{0, 1\}$）进行监督训练，构建胜率评估器与宏观姿态生成器：

$$V(\mathbf{\Psi}_{\text{world}}) = \sigma\left( \mathbf{W}_3 \cdot \text{ReLU}\left( \mathbf{W}_2 \cdot \text{ReLU}\left( \mathbf{W}_1 \mathbf{\Psi}_{\text{world}} + \mathbf{b}_1 \right) + \mathbf{b}_2 \right) + \mathbf{b}_3 \right)$$

评估输出直接指导 AI 指挥官调整队伍宏观战术姿态（Tactical Stance）：
* **$V(\mathbf{\Psi}_{\text{world}}) < \tau_{\text{defend}}$（高危劣势期）**：强制切换防御姿态（Defensive Stance）。宏观指挥官收缩防线，优先分配守塔与回撤保命目标，禁止深入敌方野区。
* **$\tau_{\text{defend}} \le V(\mathbf{\Psi}_{\text{world}}) \le \tau_{\text{attack}}$（均势拉锯期）**：维持平衡姿态（Balanced Stance）。注重资源获取、兵线均衡推导及战机目标夺取。
* **$V(\mathbf{\Psi}_{\text{world}}) > \tau_{\text{attack}}$（战略优势期）**：进入压制姿态（Offensive Stance）。战略调度智能体集结强行推进、包抄围剿并掠夺敌方半场资源。

---

## 4. 涌现式群体智能与分层架构协同（Emergent Collective Behaviors）

在 *Paragon* 的工业实践中，底层智能体并未编写显式的“伏击（Ganking）”或“补位（Lane Rotation）”微观脚本，这些高阶行为全部由战略层向目标图派发指令后**自然涌现（Emergent）**形成。

### 4.1 核心分层拓扑架构（Structural Topology）

```
 +-----------------------------------------------------------------------+
 |                 AI Commander Layer (宏观指挥官战略层)                 |
 |                                                                       |
 |   +----------------------+    +-----------------------------------+   |
 |   | 态势评估神经网络     |    | 概率存在感传播网格                |   |
 |   | (NN World Evaluator) |    | (Probabilistic Occupancy Net)     |   |
 |   +----------+-----------+    +-----------------+-----------------+   |
 |              │ (输出全局进攻/防御姿态)          │ (输出节点危险与可视度)   |
 |              ▼                                  ▼                     |
 |   +---------------------------------------------------------------+   |
 |   | 目标图黑板中心 (Objective Graph Blackboard)                   |   |
 |   |   - 节点拓扑连通矩阵                                          |   |
 |   |   - 动态影响力覆盖 (Dynamic Influence Map)                    |   |
 |   |   - 宏观目标集合与战机目标池 (Objective Priority Queue)      |   |
 |   +-----------------------------------+---------------------------+   |
 +---------------------------------------┼-------------------------------+
                                         │ 战略指令下发 (派发节点与目标)
                                         ▼
 +-----------------------------------------------------------------------+
 |                 Agent Operational Layer (智能体行动层)                 |
 |                                                                       |
 |   +---------------------------------------------------------------+   |
 |   | 图路径导航执行器 (Graph Path Follower & Detour Arbitrator)    |   |
 |   |   - 拓扑寻路与战机目标拦截检测 (Opportunistic Interception)   |   |
 |   +-----------------------------------+---------------------------+   |
 |                                       │ 状态约束驱动
 |                                       ▼
 |   +---------------------------------------------------------------+   |
 |   | 智能体底层行为树 (Bot Individual Behavior Tree)              |   |
 |   |   - 战斗微操 (Micro Combat)                                   |   |
 |   |   - 局部避障与移动导向 (Steering Behaviors & NavMesh)         |   |
 |   |   - 技能释放与冷却管理 (Ability Execution Pipeline)           |   |
 +---+---------------------------------------------------------------+---+
```

### 4.2 涌现战术机制解构

```
                 [战术行为的涌现机制 (Emergent Behaviors)]
               ┌─────────────────────────┴─────────────────────────┐
               ▼                                                   ▼
      [兵线轮换与牺牲补位]                                  [战术伏击与多抓一]
        (Lane Rotation)                                         (Ganking)
               │                                                   │
  1. 某一路友方英雄阵亡                               1. 概率传播层计算出单点落单目标
  2. 该路节点出现防御真空                             2. 该节点防守价值被瞬时拉高
  3. AI指挥官评估防御塔易损性增加                     3. 附近多名游走型英雄评估为最优介入者
  4. 邻近英雄在宏观评估下被重定向                     4. 多智能体沿图拓扑并发前往该节点汇聚
  5. 形成自动补线掩护塔机制                           5. 在物理底层呈现出有预谋的协同包抄

1. **兵线轮换与队友补位（Lane Rotation & Filling In）**：
   * **现象**：当某一路友军英雄阵亡，邻近路线或野区打野的智能体能够迅速切入该防御塔并清理兵线。
   * **涌现本质**：英雄阵亡导致局部兵线失去己方战斗力影响输入（$I_{\text{ally}}$ 归零），防御塔节点承压剧增，目标图调度系统动态提高了该节点防御目标（`DefendTower`）的效用权值。空闲智能体在周期性寻优匹配中重新绑定了该目标。

2. **多路协同伏击（Emergent Ganking）**：
   * **现象**：多名漫游（Roaming）智能体在敌方完全意想不到的角度切入，形成局部人数优势将其瞬秒。
   * **涌现本质**：概率存在感传播模块标注出敌方核心输出位在某一孤立拓扑节点暴露且周围无支援；目标图快速构建打击任务，多个在不同路径上的巡逻智能体计算发现该节点的战机打击效用达到峰值，同时沿着拓扑测地线向该节点进发，在底层运动层面表现为“有预谋的高阶多包一伏击”。

---

## 5. 关键技术总结与工程演进比对

| 架构维度 | 传统纯规则/行为树架构 (Rule-Based BTs) | 目标图宏观协同架构 (Objective Graph Architecture) |
| :--- | :--- | :--- |
| **空间推理粒度** | 依赖连续局部空间寻路（NavMesh），宏观认知完全割裂 | **离散图拓扑语义抽象**，兼具微观 NavMesh 与宏观拓扑推理 |
| **目标感知模式** | 静态轮询或硬编码规则，智能体容易“视线狭隘” | **战机目标即时抢占机制**，有效利用时空碎片边际收益 |
| **迷雾信息处理** | 采用底层“作弊透视”或简单的最后视线记录，缺乏演化 | **概率占用网络（Occupancy Maps）扩散**，逼真拟合人类直觉 |
| **全局战局认知** | 难以对整体战场态势提供量化评价指标 | **神经网络向量化评估**，快速完成从战局特征到攻防姿态映射 |
| **战术协同深度** | 需硬编码复杂的队伍协同树（Group BTs），易产生逻辑死锁 | 依赖宏观目标分发，**协同与伏击行为由空间物理状态自然涌现** |

目标图（Objective Graph）通过对复杂 3D 游戏关卡进行离散拓扑降维，成功为原本扁平抽象的任务目标赋予了连续的空间上下文（Spatial Context）。该系统不仅将长距离寻路开销降至最低，同时为环境危险度评估、未完全信息下的概率滤波以及基于机器学习的局势预测提供了高内聚的数学底座，为工业级对抗类 AI 的架构研发确立了标准范式。
