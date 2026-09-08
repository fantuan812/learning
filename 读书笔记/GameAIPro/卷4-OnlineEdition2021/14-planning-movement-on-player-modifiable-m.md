---
type: Reference
title: "第14章 Planning Movement on Player-Modifiable Maps"
description: "Game AI Pro 工业级精读：Planning Movement on Player-Modifiable Maps。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
tags:
  - game-ai
  - game-ai-pro
  - automated-testing
  - tactics-ai
  - simulation
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 第14章 Planning Movement on Player-Modifiable Maps

> 来源：*Game AI Pro 4 (Online Edition 2021)*, Chapter 14.  
> 原文作者 / 资源：[Planning Movement on Player-Modifiable Maps](http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter14_Planning_Movement_on_Player-Modifiable_Maps.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷4-OnlineEdition2021](README.md) ｜ [专栏首页](../README.md)

---

## 1. 领域背景与核心工程挑战

在传统回合制策略游戏（Turn-Based Strategy Games, TBS）中，路径规划通常假设环境拓扑结构在单次规划周期内是静态且不变的。智能体（AI Agent）仅需在已知的二维网格或导航网格（NavMesh）上求解单源单汇的最短路径问题。

然而，在一款 $15 \times 15$ 二维网格拓扑的创新回合制独立游戏中，规则赋予了玩家与 AI 动态“建造地形”的特权：
* **动态几何拓扑**：初始棋盘中大部分网格单元为不可通行的“虚空”（Void），仅有极少数单元属于可行走的“地面”（Floor）。
* **板块放置机制**：玩家和 AI 每回合均会获得最多 3 个类似俄罗斯方块的异形板块（Tetris-like Pieces，单块最大边界包围盒为 $5 \times 5$）。这些板块允许自由旋转并放置在棋盘上，唯一的前置约束是**新放置的板块至少有一个非虚空网格与当前棋盘上已存在的地面单元（Floor）正交相邻**。
* **联合规划目标**：移动规划器（Movement Planner）的核心任务是在给定目标位置（Goal Position）的前提下，构建一个同时包含**旋转/放置板块**与**地面单位移动**的复合行动序列。若目标点不可达，规划器必须回退并生成一个终点在空间上尽可能贴近目标的局部最优次优解。

```
+-------------------------------------------------------------+
|                     每回合规划决策管道                      |
|                                                             |
|  [战术候选目标集合]                                         |
|    Goal 1, Goal 2, ... Goal N                               |
|        │                                                    |
|        ▼                                                    |
|  [复合移动规划器 (Movement Planner)] ◄─── (多阶段束搜索)    |
|    - 空间拟合预计算 (PieceFitData)                          |
|    - 状态空间推演 (放置与移动交替)                          |
|        │                                                    |
|        ▼ (输出候选计划集)                                   |
|  [基于效用的决策系统 (Utility-Based System)]                |
|    - 综合评估：路径步长、战术价值、消耗代价                 |
|        │                                                    |
|        ▼                                                    |
|  [下发执行最优行动序列]                                     |
+-------------------------------------------------------------+
```

### 分支因子爆炸与状态复杂度

传统的二维网格 A\* 搜索，每个状态仅由坐标 $(x, y)$ 决定，状态空间节点的分支因子至多为 4（四向正交移动）。引入板块放置后，搜索树呈现极其陡峭的“树冠化”（Bushy Search Tree）：
1. 每个板块具有多达 4 种正交旋转态（3 个板块最多产生 12 个待选实体）。
2. 在 $15 \times 15$ 的图上，满足相邻约束的放置位姿（Placement Poses）极其庞大。
3. 搜索状态不再仅是单纯的位置坐标，还必须绑定当前的棋盘拓扑状态、未使用的板块集合以及剩余移动点数。
4. 在早期的 Python 原型验证中，直接在 A\* 遇到 Void 边界时按需遍历枚举板块放置会导致严重的计算延迟，算法陷入状态与数据结构反复分配的性能泥潭。

因此，工业级落地方案必须解决两大核心诉求：**严格受控的状态空间展开规模**与**几何拟合推演的高度向量化/位运算化（Bitwise Manipulation）**。

---

## 2. 空间拟合预计算系统（Finding Fits via Spatial Bitmasks）

针对“在虚空边界按需探测板块”导致的大量冗余重叠几何判定，架构设计转向**全图离散拟合预计算（Precalculating Every Possible Fit）**。在状态展开前，系统通过自底向上的多层位图管道直接生成全图拟合缓存。

### 2.1 数据流架构与位图分层

```
                   [ 游戏世界状态 (Game State) ]
                               │
                               ▼
        ┌─────────────────────────────────────────────┐
        │       可通行性地图 (Pathability Map)         │
        │   每单元对应单比特枚举 (Floor, Void, etc.)   │
        └──────────────────────┬──────────────────────┘
                               │
                               ▼
        ┌─────────────────────────────────────────────┐
        │          邻接图 (Adjacency Map)             │
        │   Void && Adjacent(Floor) -> 标记为有效边缘  │
        └──────────────────────┬──────────────────────┘
                               │
                               ▼
        ┌─────────────────────────────────────────────┐
        │            拟合图 (Fit Map)                 │
        │   横向 5-bit 窗口缓存 (Pathability & Adj)    │
        └──────────────────────┬──────────────────────┘
                               │  结合预旋转板块库 (Max 12 Pieces)
                               ▼  按行进行快速位与 (Bitwise AND)
        ┌─────────────────────────────────────────────┐
        │       潜在放置列表 (Potential Placements)   │
        │       所有合法摆放位姿的密集索引列表        │
        └──────────────────────┬──────────────────────┘
                               │
                               ▼
        ┌─────────────────────────────────────────────┐
        │          空间潜力图 (Potential Map)         │
        │   Grid Cell (x,y) -> Set<PlacementIndex>    │
        └─────────────────────────────────────────────┘
```

### 2.2 各层级结构定义与数学约束

#### 1. 预旋转板块集合（Pre-rotated Pieces）
AI 拥有的至多 3 个板块在输入规划器前全部解构，展开为至多 12 个固定的绝对姿态板块 $P = \{p_1, p_2, \dots, p_k\}$（$k \le 12$）。搜索过程中无需再进行三角函数或旋转矩阵变换。

#### 2. 可通行性地图（Pathability Map）
使用正交位掩码（Bitmask Flags）记录地图信息：
$$\text{CellType}(x, y) \in \{ \text{Floor}, \text{Void}, \dots \}$$
每个类型仅占据 1 个独立的二进制位，支持使用单周期位运算执行多类型联合筛选查询。

#### 3. 邻接图（Adjacency Map）
定义二维布尔映射 $Adj(x, y)$，判定规则为：
$$Adj(x, y) = \left( \text{Type}(x, y) == \text{Void} \right) \land \left( \exists (nx, ny) \in \mathcal{N}_4(x, y) \text{ s.t. } \text{Type}(nx, ny) == \text{Floor} \right)$$
其中 $\mathcal{N}_4(x, y)$ 表示与 $(x, y)$ 正交相邻的 4 个邻域单元。

#### 4. 拟合图（Fit Map）
由于板块的最大尺寸限定为 $5 \times 5$，为避免频繁读取离散内存，系统将每一行连续 5 个单元的状态打包为一个轻量位域（Bitfield）。拟合图按单元缓存当前位置向右延伸 5 个格子的可通行性与邻接有效性。

#### 5. 潜在放置合法性检验（Potential Placement Feasibility）
设某个板块在相对于坐标 $(x, y)$ 处的某一行掩码为 $RowBitmask$。该行有效当且仅当满足两个几何正交条件：
* **无重叠约束（Non-overlap Constraint）**：板块的所有实体网格必须落在 Void 单元上，不能与已存在的非 Void 单元重叠：
  $$\text{RowBitmask} \ \& \ \text{FitMap.OccupiedBits}(x, y) == 0$$
* **邻接相交约束（Adjacency Overlap Constraint）**：整个板块的所有行中，必须至少有一个实体网格与邻接图中标记为 True 的单元重叠：
  $$\bigvee_{row=0}^{h-1} \left( \text{RowBitmask}_{row} \ \& \ \text{FitMap.AdjacencyBits}(x + \text{offset}_x, y + row) \right) \neq 0$$

#### 6. `PieceFitData` 聚合门面类
计算完成后，系统将潜在放置列表与空间潜力图封装进 `PieceFitData`。该类提供常数时间复杂度的空间检索接口：
$$\text{GetPlacementsForCell}(x, y) \rightarrow \{ \text{Index}_0, \text{Index}_1, \dots \}$$
直接返回能使目标不可行单元 $(x, y)$ 变为可通行的所有合法板块放置操作索引。

---

## 3. 分层规划器架构与双前沿束搜索（Dual-Frontier Beam Search）

### 3.1 复合搜索状态定义

在动态改版地图中，搜索图的节点不能仅仅退化为空间位置 $(x, y)$。类 `SearchState` 包含了规划生命周期中的所有自由度：

```csharp
public sealed class SearchState : IComparable<SearchState>
{
    public int X;
    public int Y;
    public PathabilityMap Map;          // 当前状态演化出的独立/共享地形拓扑
    public uint AvailablePiecesMask;    // 未使用板块的位掩码（每板块对应1位）
    public int RemainingMoves;          // 本回合剩余的移动预算
    public float CostG;                 // 起点到当前状态的实际移动代价
    public float HeuristicH;            // 到达最终目标启发式预估代价值

    public float Score => CostG + HeuristicH;

    public int CompareTo(SearchState other)
    {
        return this.Score.CompareTo(other.Score);
    }
}
```

### 3.2 双前沿调度机制（Dual-Frontier Architecture）

若将板块放置操作与四向步行移动统一加入传统 A\* 的单一优先队列（Open Set / Frontier），极度膨胀的分支因子将使优先队列迅速被“放置板块”的操作淹没。

为此，规划器设计了**双前沿调度架构**：
1. **主前沿队列（Main Frontier）**：仅负责处理在当前已知确定地图拓扑下的确定性地面四向移动（Normal Movement）。
2. **次级前沿队列（Second Frontier / Next Frontier）**：在搜索展开遇到相邻 Void 单元时，调用 `PieceFitData` 找到所有合法板块放置。由此衍生的新状态**不推入主队列**，而是被重定向放入次级优先队列中隔离。
3. **束搜索剪枝（Beam Search Pruning）**：当主队列的所有纯地面移动完全耗尽后，次级队列通过启发式评分进行截断剪枝，仅保留 **Top-K（系统实证取 $K=16$）** 个最具潜力的放置状态。这些保留的状态将作为下一阶段规划的初始种子。

```
 [ 当前阶段: 主前沿队列 (Main Frontier) ]
               │
      Pop 最优状态 (X, Y)
         ├──> [正交移动: Floor] ──> 更新 G 值 ──> 推回 [Main Frontier]
         │
         └──> [遭遇边界: Void]
                 │
                 ▼ 查询 PieceFitData
              生成“放置板块”新状态 (生成新地图, 减少可用板块)
                 │
                 ▼
 [ 收集进入: 次级前沿队列 (Second Frontier) ]
               │
               │ (直到 Main Frontier 完全耗尽)
               ▼
 [ 束搜索剪枝 (Beam Pruning): 保留 Top 16 ]
               │
               ▼
 [ 将这 16 个状态作为 Main Frontier 进入下一阶段 Pass ]
```

### 3.3 多阶段推进主循环（Listing 1 C# 架构还原）

规划轮次的总 Pass 数由 AI 持有的待放置板块数 $N$ 严格约束。系统总共执行 $N + 1$ 次规划 Pass（多出的一次用于在放置完最后一块板块后，单位继续在其上移动）：

```csharp
public class CompositeMovementPlanner
{
    private PriorityQueue<SearchState> frontier;
    private PriorityQueue<SearchState> nextFrontier;
    private int nrPlaceablePieces;
    public bool FoundAPlan { get; private set; }

    public void Plan()
    {
        // 若无可放置板块，初始化次级前沿容器
        if (nrPlaceablePieces == 0)
        {
            nextFrontier = new PriorityQueue<SearchState>();
        }

        // 阶段推进循环：处理放置 1 到 N 个板块的级联演化
        for (int pass = 1; pass <= nrPlaceablePieces; pass++)
        {
            nextFrontier = new PriorityQueue<SearchState>();
            
            // 执行单阶段核心 A*：耗尽当前地图下的所有可能地面移动
            ExecutePlanningPass();
            
            if (FoundAPlan) 
                return;
            
            // 若在当前阶段没有任何合法的板块放置分支产生，搜索提前收敛失败
            if (nextFrontier.Count == 0) 
                return;

            // 束搜索核心：将次级前沿赋给主前沿，并强行截断至前 16 个最优状态
            frontier = nextFrontier;
            frontier.Prune(16);
        }

        // 最终通行 Pass：在放置完至多第 N 个板块后，尝试最终移动到终点
        ExecutePlanningPass();
    }

    private void ExecutePlanningPass()
    {
        // 核心执行逻辑：
        // 1. 经典 A* 循环弹出 frontier 中的最优节点。
        // 2. 若抵达 Goal 则标记 FoundAPlan = true 并退出。
        // 3. 针对邻接 Floor，更新 Cost 并推入 frontier。
        // 4. 针对邻接 Void，计算合法放置，生成新 SearchState 并存入 nextFrontier。
        // 5. 持续追踪全生命周期中距离 Goal 欧氏/曼哈顿距离最小的局部最优状态。
    }
}
```

---

## 4. 状态拓扑哈希与增量式缓存（Zobrist Hashing）

由于构建 `PieceFitData` 涉及全图的位图扫描与相交检验，若不同搜索路径演化出相同或等价的地形与板块状态，重复构建将造成毁灭性的性能开销。

### 4.1 复合缓存键设计

判断一个 `PieceFitData` 实例能否被复用，完全且仅取决于两个要素：
1. **当前棋盘的拓扑结构**（`PathabilityMap`）。
2. **当前可用的板块集合**（`AvailablePiecesMask`）。

因此，`SearchState` 提供如下计算复合哈希标识的方法：
```csharp
public ulong HashForFitData()
{
    return this.Map.GetZobristHash() ^ (ulong)this.AvailablePiecesMask;
}
```
系统维护一个全局的哈希字典 `Dictionary<ulong, PieceFitData>`，在为搜索状态准备拟合数据时，优先查表，实现计算结果的高效跨阶段复用。

### 4.2 地图的 Zobrist 空间哈希推导

对于静态 $15 \times 15$ 网格，为支持棋盘拓扑的高性能动态异或更新，系统采用了博弈 AI（如国际象棋、围棋程序）中成熟的 **Zobrist Hashing** 机制。

* **预分配伪随机矩阵**：
  为棋盘上的每个坐标单元 $(x, y) \in [0, 14] \times [0, 14]$ 以及每一种可能的地形枚举状态 $t \in \text{CellTypes}$ 预生成一个均匀分布的 64 位无符号伪随机整数：
  $$R(x, y, t) \in [0, 2^{64}-1]$$

* **全局拓扑哈希合成**：
  整张地图的初始哈希值由所有单元状态的随机数异或（XOR）叠加而成：
  $$H_{\text{Map}} = \bigoplus_{x=0}^{14} \bigoplus_{y=0}^{14} R(x, y, \text{Type}(x, y))$$

* **增量式 O(K) 变更加速**：
  当某个板块覆盖了 $K$ 个网格，将这 $K$ 个网格从 $\text{Void}$ 修改为 $\text{Floor}$ 时，无需遍历整张地图重算哈希，仅需利用异或的可逆代数性质进行原位置反转与新状态叠加：
  $$H_{\text{NewMap}} = H_{\text{OldMap}} \oplus \left( \bigoplus_{i=1}^K R(x_i, y_i, \text{Void}) \right) \oplus \left( \bigoplus_{i=1}^K R(x_i, y_i, \text{Floor}) \right)$$
  此特性将地图变动的哈希时间复杂度由原本的 $O(W \times H)$ 降低至与修改格子数量成正比的 $O(K)$。

---

## 5. 进阶优化路线与工业架构权衡（Advanced Optimizations & Trade-Offs）

作者在工业落地中采取了分阶段的优化方法论：
1. **忽略微观底层优化**：前期避免陷入汇编、寄存器级的低级调优，保留后期 Profiling 分析驱动的空间。
2. **聚焦算法复杂度控制**：通过 Beam Search 剪枝和双前沿设计限制昂贵操作的数量。
3. **定位高度局部的优化点**：先行落地逻辑简单完备的基线方案，同时预留替换为更复杂、极限性能算法的架构接口。

在此方法论指导下，作者提出了两项极具深度但根据项目生命周期权衡后未完全并入主干的进阶优化：

### 5.1 64位拟合图极限位压（Faster Fit Maps via 64-bit Packaging）

#### 原理分析
在基线实现中，拟合图对于每个位置缓存向后延伸的 5 个网格状态，采用按行循环读取，每次判定一个 $5 \times 5$ 的板块需要执行 10 次二元位运算（每行分别比对 Pathability 与 Adjacency）。

#### 优化方案
将整个 $5 \times 5$ 的子空间拓扑完全打包在一个单一的 64 位整型（UInt64）中：
* 每个格子仅需 1 个比特记录通行性，共需 25 比特；
* 每个格子仅需 1 个比特记录邻接性，共需 25 比特；
* 二者合并仅需 50 比特，完全可以容纳在单个 64 位寄存器中（低 32 位为通行性位域，高 32 位为邻接性位域）。

```
0                                 24 25                           49 50       63
+-----------------------------------+-------------------------------+-----------+
|    25-bit Pathability Footprint   |   25-bit Adjacency Footprint  |  Padding  |
+-----------------------------------+-------------------------------+-----------+
```
通过该内存排布，判定任意复杂异形 $5 \times 5$ 板块的有效性，可将计算压缩为**仅需 2 次 64 位宽的位逻辑操作（Bitwise AND）**，吞吐量相较于逐行扫描提升数倍。

---

### 5.2 量子搜索范式（Quantum Search via Bitwise Set Traversal）

#### 原理与数学推导
“量子搜索”（Quantum Search）是作者提出的一种消除搜索状态实体分配的激进探索技术。其核心思想源于物理学中的“态叠加”：**在没有真正生成具体新地图拓扑的情况下，同时探索所有可能放置板块产生的影响**。

1. **超位置叠加图（Quantum Map）**：
   在次级前沿中筛选出 Top 32 个最具价值的候选板块放置位姿。分配一个 32 位无符号整型 $Q(x, y)$。第 $i$ 个比特为 1，表示第 $i$ 个候选放置方案能够将格子 $(x, y)$ 转化为可通行的地面（Floor）：
   $$Q(x, y) = \sum_{i=0}^{31} \mathbb{I}(\text{Placement } i \text{ covers } (x, y)) \cdot 2^i$$

2. **隐式图遍历（Implicit Graph Traversal）**：
   运行专门定制的“量子 A\*”，搜索智能体并不在真正的物理地图上漫游，而是在叠加图上扩散：
   * 起始点位于与当前 Floor 邻接的 Void 单元。
   * 状态转移至相邻单元 $(nx, ny)$ 时，计算两点集合的位与交集：
     $$\text{Mask}_{\text{Transition}} = Q(x, y) \ \& \ Q(nx, ny)$$
   * **断路剪枝（Circuit Breaker）**：
     若 $\text{Mask}_{\text{Transition}} == 0$，表明**没有任何单一板块能够同时覆盖这两个连续单元**，此路径在几何上不可行，直接剪枝抛弃。

```
[ 单元 A 位掩码: 0b0011 ] (Placement 0, 1 可达)
           │
           │  Bitwise AND
           ▼
[ 单元 B 位掩码: 0b0010 ] (Placement 1 可达)
           │
           ├─> 结果: 0b0010 != 0 (合法！存在共享方案 Placement 1，可继续扩散)
           │
[ 单元 C 位掩码: 0b0100 ] (Placement 2 可达)
           │
           ▼  Bitwise AND
[ 结果: 0b0000 == 0 ] (非法！无公共板块能同时连通 B 和 C，立即剪枝)
```

#### 工程权衡与落地决策（Trade-Offs）
* **优势**：该算法原型在单 Pass 内展现了极高的能效比，使得 AI 可以在不构建任何新 `PathabilityMap`、不实例化任何全新 `SearchState` 的情况下，完成整层状态空间的超前剪枝。
* **放弃原因**：当搜索推进到多 Pass（即需连续放置第 2、第 3 个板块）时，板块与板块之间在时间维度上的拓扑依赖、消耗约束以及游戏的特殊战术逻辑会导致“叠加态”发生维度灾难，系统复杂度与维护成本过高。在工业实践的 ROI 权衡下，作者最终保留了更为稳定、调试友好的双前沿束搜索架构。

---

## 6. 核心架构总结

在面对动态几何拓扑修改的复杂路径规划问题时，本工程实践给出了标准的工业级解题范式：

| 模块 / 挑战 | 传统规划（Classic Pathfinding） | 本方案工业设计（Author's Architecture） | 核心优势 |
| :--- | :--- | :--- | :--- |
| **状态表示** | 仅二维空间坐标 $(x, y)$ | 坐标 + 地图拓扑哈希 + 板块掩码 + 步数 | 完备表征时空与拓扑演变 |
| **几何可行性判断** | 运行时碰撞体/网格探测 | 离散位图分层预计算 (`PieceFitData`) | 避免分支暴增时的重复计算 |
| **分支因子控制** | 无（依赖启发式全局搜索） | 确定性移动与放置分离 + 双前沿束搜索 (Top 16) | 严格锁定每回合最大时间复杂度 |
| **多图缓存优化** | 重建网格或深拷贝完整图结构 | 全局哈希字典 + 增量式 Zobrist Hash | 常数级查找，极小化 GC 内存开销 |

---

## 7. 参考文献

* **[Bisiani 87]** Bisiani, R. 1987. *Beam search*. In Encyclopedia of Artificial Intelligence, ed. S. Shapiro, 56–58. New York: Wiley & Sons.
* **[Zobrist 69]** Zobrist, A. L. 1969. *A New Hashing Method with Application for Game Playing*, Technical Report 88. Madison: University of Wisconsin.
