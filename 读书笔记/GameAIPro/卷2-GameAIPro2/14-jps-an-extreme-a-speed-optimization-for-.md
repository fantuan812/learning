---
type: Reference
title: "第14章 JPS+: An Extreme A* Speed Optimization for Static Uniform Cost Grids"
description: "Game AI Pro 工业级精读：JPS+: An Extreme A* Speed Optimization for Static Uniform Cost Grids。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第14章 JPS+: An Extreme A* Speed Optimization for Static Uniform Cost Grids

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 14.  
> 原文作者 / 资源：[JPS+: An Extreme A* Speed Optimization for Static Uniform Cost Grids](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter14_JPS_Plus_An_Extreme_A_Star_Speed_Optimization_for_Static_Uniform_Cost_Grids.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

## 1. 算法背景与核心命题 (Introduction & Problem Formulation)

在游戏工业界与实时仿真系统中，基于栅格地图（Grid Maps）的路径规划一直面临着计算吞吐量与内存占用的严峻挑战。传统的 $A^*$ 寻路算法（$A^*$ Pathfinding）在均匀代价静态网格（Static Uniform Cost Grids）上进行图搜索时，存在严重的对称性冗余计算。

跳点搜索算法（Jump Point Search, 简称 JPS，Harabor 2012）通过状态空间在线剪枝技术（State-Space Pruning），在保证最终路径达到欧几里得/切比雪夫度量完全最优（Perfectly Optimal）的前提下，将传统 $A^*$ 的性能提升了一个数量级。

而 **JPS+**（Harabor 与 Rabin 于 2014 年分别独立提出）更进一步，通过**离线静态地图预分析（Static Map Preprocessing）**，将沿各行进方向直达障碍物墙体（Walls）及关键跳点（Jump Points）的距离直接“固化”（Burn In）至离线数据表中。在运行时，寻路器无需进行逐格射线检测（Raymarching/Raycasting），而是实现 $O(1)$ 复杂度的直接跳跃。

在 $40 \times 40$ 规模的基准静态测试地图中，传统 $A^*$ 寻路耗时为 $180.05\,\text{ns}$，JPS 耗时为 $15.04\,\text{ns}$，而 JPS+ 仅需 $1.55\,\text{ns}$。JPS+ 相较传统 $A^*$ 达成了 **116 倍**的加速比，且始终维持绝对的最优解输出。

---

## 2. 状态空间剪枝策略 (Pruning Strategy)

### 2.1 对称路径冗余与剪枝机理
在开阔的无障碍网格空间中，从起点 $S$ 到目标点 $G$ 往往存在大量代价等价的最优路径（Equivalent and Optimal Paths）。

```
路径 1:           路径 2:           路径 3:           路径 4:
[S] -> ·          [S]               [S]               [S]
       |             \                 \                 |
       v              v                 v                v
       ·              · -> ·            ·                ·
       |                   |            | \              |
       v                   v            v  v             v
       [G]                 [G]          ·  [G]           · -> [G]
```

传统 $A^*$ 会将这些等价节点重复压入开启列表（Open List），并在邻域扩展时进行大量冗余的松弛操作（Relaxation）。JPS/JPS+ 的核心策略是通过确立统一的拓扑遍历优先序，打破状态图的局部对称性，使得每个节点在开阔区域内至多被访问一次。

JPS/JPS+ 的加速收益分解如下：
1. **拓扑剪枝（Pruning Rules）**：过滤不必要的后继邻居，贡献约 50% 的性能提升；
2. **跳点展开（Open List Bottleneck Elimination）**：跳过非关键的线性走廊节点，仅将核心决策节点压入开启列表，消除优先队列的入堆/调整瓶颈，贡献剩余 50% 的性能提升。

### 2.2 邻居过滤规则推导
设当前节点为 $x$，其父节点为 $p$。从 $p$ 到 $x$ 的归一化前进方向为 $\vec{d} = \text{norm}(x - p)$。

#### 2.2.1 直行方向规则（Straight/Cardinal Movement）
当智能体沿正向（北、南、西、东）移动至 $x$ 时，后继节点集被严密剪枝，仅保留沿该方向继续直行的单一节点：
$$\text{PrunedNeighbors}(x, \vec{d}) = \{ x + \vec{d} \}$$
其余 7 个方向的邻域节点均被剔除。

#### 2.2.2 对角方向规则（Diagonal Movement）
当智能体沿对角线（东北、西北、东南、西南）移动至 $x$ 时，设分量方向为 $\vec{d}_x$ 与 $\vec{d}_y$（满足 $\vec{d} = \vec{d}_x + \vec{d}_y$）。此时仅保留对角前进方向及其构成的两个正交基方向，其余 5 个方向均被剪枝：
$$\text{PrunedNeighbors}(x, \vec{d}) = \{ x + \vec{d},\; x + \vec{d}_x,\; x + \vec{d}_y \}$$

```
对角线行进（以朝东北方向前进为例，p -> x）：
[NW 剪枝]   [ N 保留 ]   [NE 保留 ]
       \        ^        ^
        \       |       /
   [W 剪枝] -- [x] --> [ E 保留 ]
                ^
               /
             [p]
[SW 剪枝]   [ S 剪枝 ]   [SE 剪枝]
```

---

## 3. 强迫邻居与跳点分类学 (Forced Neighbors & Jump Points Taxonomy)

### 3.1 强迫邻居判定准则 (Forced Neighbors)
当空间中存在障碍物（Walls）阻断时，上述剪枝规则可能导致漏检全局最优路径。若存在某个非对角线直达路径无法覆盖的邻居节点 $n$，使得智能体必须经由当前节点 $x$ 拐弯转向才能获得最优距离，则 $n$ 被定义为 $x$ 的**强迫邻居（Forced Neighbor）**。

强迫邻居只在沿正交基方向（Cardinal Directions）行进时触发，共有 8 种拓扑结构（4 个方向 $\times$ 两侧对称）：

```
[东向行进]             [西向行进]             [北向行进]             [南向行进]
+---+---+---+         +---+---+---+         +---+---+---+         +---+---+---+
| W | F |   |         |   | F | W |         | F |   |   |         |   | P |   |
+---+---+---+         +---+---+---+         +---+---+---+         +---+---+---+
| P | N |   |         |   | N | P |         | W | N |   |         | W | N |   |
+---+---+---+         +---+---+---+         +---+---+---+         +---+---+---+
|   |   |   |         |   |   |   |         |   | P |   |         | F |   |   |
+---+---+---+         +---+---+---+         +---+---+---+         +---+---+---+
(左侧遇墙强迫)        (右侧遇墙强迫)        (左侧遇墙强迫)        (左侧遇墙强迫)

+---+---+---+         +---+---+---+         +---+---+---+         +---+---+---+
|   |   |   |         |   |   |   |         |   |   | F |         |   |   |   |
+---+---+---+         +---+---+---+         +---+---+---+         +---+---+---+
| P | N |   |         |   | N | P |         |   | N | W |         |   | N | W |
+---+---+---+         +---+---+---+         +---+---+---+         +---+---+---+
| W | F |   |         |   | F | W |         |   | P |   |         |   |   | F |
+---+---+---+         +---+---+---+         +---+---+---+         +---+---+---+
(右侧遇墙强迫)        (左侧遇墙强迫)        (右侧遇墙强迫)        (右侧遇墙强迫)

图例：P: 父节点 (Parent), N: 当前节点 (Current Node), W: 阻挡墙体 (Wall), F: 强迫邻居 (Forced Neighbor, 虚线标识)
```

### 3.2 跳点分类学 (Taxonomy of Jump Points)
JPS+ 将所有在路径规划中承担枢纽拓扑职能的节点划分为四类：

| 跳点类别 (Flavor) | 核心判定规则 | 空间拓扑意义 |
| :--- | :--- | :--- |
| **主跳点 (Primary Jump Point)** | 沿特定正交方向行进时，拥有至少一个强迫邻居的节点 | 障碍物拐角处的关键拓扑节点，标志着对称性路径发生分化 |
| **正向跳点 (Straight Jump Point)** | 沿正交方向射线投射，能在撞墙前线性直达主跳点的中间节点 | 构成指向主跳点的引导矢量通道，记录沿轴线到达主跳点的步长 |
| **对角跳点 (Diagonal Jump Point)** | 沿对角线方向行进时，能直达主跳点或相关正向跳点的节点 | 承载正交分量扩散，对角移动若其正交投影存在跳点则该节点即为跳点 |
| **目标跳点 (Target Jump Point)** | 搜索过程中的目标节点 $G$ 或其正交对齐投影节点 | 寻路过程的终点判定节点 |

> **关键拓扑特性**：
> 节点的主跳点属性具有**方向依赖性（Direction-Dependent）**。同一节点当从西向东行进时可能是主跳点，而从南向北行进时则可能退化为普通空闲节点。因此，每个栅格节点必须存储 4 个独立的正交跳点状态标志（Flags）。

---

## 4. 空间跳跃与墙体距离编码 (Spatial Leap & Wall Distances)

### 4.1 符号距离场编码设计
为在紧凑的内存布局下同时表示跳点距离和碰撞信息，JPS+ 采用**正负符号空间度量压缩方案**：
* **正整数值 ($d > 0$)**：表示沿该搜索方向前进 $d$ 个单位步长后，将**直接命中一个有效跳点**；
* **零或负整数值 ($d \le 0$)**：表示沿该搜索方向无有效跳点，其绝对值 $|d|$ 表示**沿该方向行进到阻挡墙体的几何步长**。当数值为 $0$ 时，表示当前相邻栅格即为障碍物。

### 4.2 八方向距离表结构
对于地图上的任意自由栅格 $C(r, c)$，预处理模块为其计算一个 8 维有符号短整型数组：
$$\mathbf{Dist}[r][c] = [d_{\text{N}}, d_{\text{NE}}, d_{\text{E}}, d_{\text{SE}}, d_{\text{S}}, d_{\text{SW}}, d_{\text{W}}, d_{\text{NW}}]^T \in \mathbb{Z}^8$$

```
方向编码拓扑索引：
       [NW: 7]   [ N: 0]   [NE: 1]
             \      |      /
      [ W: 6] -- [r, c] -- [ E: 2]
             /      |      \
       [SW: 5]   [ S: 4]   [SE: 3]
```

预处理完成后，主跳点标记被彻底剔除，运行期寻路引擎仅依赖该 8 维距离向量表即可实现任意方向的直接跳跃，将射线探针的时间复杂度压至严格常数时间 $\mathcal{O}(1)$。

---

## 5. 地图离线预处理算法实现 (Map Preprocess Implementation)

### 5.1 扫描线算法流程 (Sweep Strategy)
为了高效生成全图八方向距离查找表，算法采用多通道正交/对角扫描线流水线（Multi-pass Scanline Pipeline）：

1. **Pass 1: 主跳点提取**：遍历整图自由空间节点，检查 8 种强迫邻居模板，为节点标记 4 个正交方向的主跳点布尔位掩码（`jumpPoints[r][c][dir]`）；
2. **Pass 2: 西向距离扫描（Westward Sweep）**：由左向右（自西向东）横向扫描，维护上一次遇到的墙体及跳点计数；
3. **Pass 3: 东向距离扫描（Eastward Sweep）**：由右向左（自东向西）横向扫描；
4. **Pass 4: 北向距离扫描（Northward Sweep）**：自顶向下（从北向南）纵向扫描；
5. **Pass 5: 南向距离扫描（Southward Sweep）**：自底向上（从南向北）纵向扫描；
6. **Pass 6: 西南/东南对角扫描（SW/SE Sweep）**：自底向上沿对角波前推导；
7. **Pass 7: 西北/东北对角扫描（NW/NE Sweep）**：自顶向下沿对角波前推导。

### 5.2 正向扫描核心代码解析 (Listing 14.1)

以下为西向扫描实现代码。该算法自左向右扫描每行，当遇到墙体时重置计数器；扫描过程中若捕获到主跳点，则其右侧的后续栅格均可以该跳点作为目标，记录正距离；若中途未捕获跳点，则记录至西侧墙体的负距离。

```cpp
// Listing 14.1: Left to right sweep to mark all westward straight jump points and westward walls.
for (int r = 0; r < mapHeight; ++r)
{
    int count = -1;
    bool jumpPointLastSeen = false;
    
    for (int c = 0; c < mapWidth; ++c)
    {
        // 遇到不可通行的墙体单元格，重置局部扫描状态
        if (m_terrain[r][c] == TILE_WALL)
        {
            count = -1;
            jumpPointLastSeen = false;
            distance[r][c][West] = 0; // 墙体自身距离为 0
            continue;
        }
        
        count++; // 递增步进距离计数器
        
        if (jumpPointLastSeen)
        {
            // 沿西向回溯能触达已记录的主跳点，赋正整数
            distance[r][c][West] = count;
        }
        else // 沿西向回溯仅能触达墙体，赋负整数表示墙体距离
        {
            distance[r][c][West] = -count;
        }
        
        // 若当前节点自身在西向上为判定出的主跳点，刷新扫描参考原点
        if (jumpPoints[r][c][West])
        {
            count = 0;
            jumpPointLastSeen = true;
        }
    }
}
```

### 5.3 预处理流水线拓扑图

```
+-------------------------------------------------------------+
| 原始静态网格几何拓扑 (Static Terrain Grid: TILE_WALL/FREE)     |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
| Pass 1: 强迫邻居模式匹配 (Primary Jump Points Detection)     |
| 生成 4 轴向布尔位图: jumpPoints[r][c][CardinalDir]           |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
| Pass 2~5: 正交向扫描线流水线 (Cardinal Sweeps)               |
| • Westward (L -> R)           • Eastward (R -> L)           |
| • Northward (Top -> Down)     • Southward (Down -> Top)     |
| 输出: distance[r][c][N/E/S/W] (包含跳点正距与墙体负距)       |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
| Pass 6~7: 对角向扫描线流水线 (Diagonal Sweeps)               |
| • 依据正交分量跳点存在性传递对角跳步                          |
| • 阻挡计算与负距离回填                                       |
| 输出: distance[r][c][NE/SE/SW/NW]                           |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
| 紧凑 8 轴有符号距离场构建完成 (Baked JPS+ Preprocessed Map)   |
| 运行时寻路引擎装载: 内存连续分配 int8_t/int16_t 数组         |
+-------------------------------------------------------------+
```

---

## 6. JPS+ 运行期寻路拓扑与工程规范 (Runtime Engine Design)

### 6.1 运行期跳步解析机制
在运行期，节点后继扩展不再通过遍历单个像素栅格完成，而是基于离线烘焙的 $\mathbf{Dist}$ 数组与目标点 $G(r_g, c_g)$ 进行常数时间解析：

设当前展开节点为 $u$，行进方向为 $\vec{d}$，烘焙距离为 $D = \mathbf{Dist}[u.r][u.c][\vec{d}]$：
1. **跳点生成判定**：
   * 若 $D > 0$，表明沿方向 $\vec{d}$ 前进 $D$ 步即达一个中间跳点 $v = u + D \cdot \vec{d}$，直接将 $v$ 作为后继压入 Open List；
2. **目标节点投影拦截（Target Jump Point）**：
   * 计算当前行进光线上是否存在目标节点 $G$ 的投影：
     * 若 $\vec{d}$ 为正向且 $G$ 恰在同一射线轴线上；
     * 若 $\vec{d}$ 为对角向且 $G$ 位于该对角移动所覆盖的正交扇区内；
   * 设智能体沿 $\vec{d}$ 到达与 $G$ 碰撞或投影拦截点的距离为 $k$。若 $k \le |D|$（或在 $D > 0$ 时 $k < D$），则产生目标跳点，无需跨越完整 $D$ 步，直接将目标节点 $G$ 或其转向对焦点作为最优后继压入；
3. **剪枝终止**：
   * 若 $D \le 0$ 且行进光线上无目标拦截点，表明沿该方向仅存在死胡同墙体，该方向的邻域分支在当前帧被直接丢弃，无需触发任何动态开销。

### 6.2 工业级实现要点与系统级优化
* **内存布局与 Cache 局部性（Data Locality）**：
  八方向距离数据应按连续内存平面分配（SoA 或紧凑 AoS），建议使用有符号 `int8_t`（支持 $\pm 127$ 栅格范围，超出范围做区间钳位截断）以压缩 L1/L2 缓存占用，降低寻路过程中的 Cache Miss。
* **分层路径回溯平滑（Path Smoothing）**：
  JPS+ 输出的最优路径由若干稀疏跳点线段组成。在游戏引擎运动控制栈中，该数据直接交付导向行为（Steering Behaviors）或走廊生成器使用，天然省去了传统 $A^*$ 路径后处理中繁杂的漏斗算法（Funnel Algorithm）或射线拉直（String Pulling）计算开销。

---

## 1. 离线预处理机理与对角线扫描算法（Offline Preprocessing & Diagonal Sweeps）

在跳点寻路增强算法（JPS+, Jump Point Search Plus）架构中，算法将传统跳点搜索（JPS）在运行时所执行的高频、重复性递归射线探测完全剥离，转而通过离线阶段（Offline Preprocessing）计算并固化在静态网格图结构中。

### 1.1 距离场符号语义规范与内存编码

网格中每个非障碍物节点均维护一个包含 8 个离散方向的有符号整型距离查找表 $\text{distance}[r][c][d]$，其中 $d \in \{\text{North}, \text{Northeast}, \text{East}, \text{Southeast}, \text{South}, \text{Southwest}, \text{West}, \text{Northwest}\}$。该字段承担距离度量与目标类型编码的双重语义：

*   **正整数（$\text{distance} > 0$）**：表明沿方向 $d$ 前进指定步数可直接命中一个**主跳点**（Primary Jump Point，即强制邻居触发点）或**中间跳点**（Intermediate Jump Point）。例如 $+k$ 代表沿该方向移动 $k$ 个网格步长到达跳点。
*   **负整数或零（$\text{distance} \le 0$）**：表明沿方向 $d$ 在遭遇任何跳点前已先行撞击障碍物（Wall）或地图边界。其绝对值 $|k|$（当值为 $0$ 时表示距离为 $1$ 步撞墙）精确表征到达碰撞边界的可通行步长。

### 1.2 自底向上西南对角线扫描实现（Listing 14.2）

在预处理流程中，必须先完成基数方向（Cardinal Directions：东、西、南、北）的直线扫描，随后才能进行对角线方向（Diagonal Directions）的合成扫描。以下为从底至顶扫描标记西南方向（Southwest）跳点与墙体碰撞距离的核心实现：

```cpp
// 扫描全图：自底向上遍历行索引 (Down to up sweep)
for (int r = 0; r < mapHeight; ++r)
{
    for (int c = 0; c < mapWidth; ++c)
    {
        if (!IsWall(r, c))
        {
            // 边界检查或邻接单元存在阻挡（包括拐角切角阻挡约束）
            if (r == 0 || c == 0 || IsWall(r - 1, c) ||
                IsWall(r, c - 1) || IsWall(r - 1, c - 1))
            {
                // 碰撞体位于西南对角线一步之内 (Wall one away)
                distance[r][c][Southwest] = 0;
            }
            else if (!IsWall(r - 1, c) && !IsWall(r, c - 1) &&
                     (distance[r - 1][c - 1][South] > 0 ||
                      distance[r - 1][c - 1][West] > 0))
            {
                // 沿西南对角线移动一步的后继节点在其正南或正西分支上存在直线跳点
                // 当前节点距该对角跳点步长为 1 (Straight jump point one away)
                distance[r][c][Southwest] = 1;
            }
            else
            {
                // 继承并递增前序西南对角线节点的距离标量
                int jumpDistance = distance[r - 1][c - 1][Southwest];
                if (jumpDistance > 0)
                {
                    // 传播有效跳点距离
                    distance[r][c][Southwest] = 1 + jumpDistance;
                }
                else
                {
                    // 传播障碍物距离（负向累加）
                    distance[r][c][Southwest] = -1 + jumpDistance;
                }
            }
        }
    }
}
```

---

## 2. JPS+ 运行时执行拓扑与剪枝引擎（Runtime Topology & Pruning Engine）

JPS+ 运行时的核心优势在于：完全消除了传统 JPS 在运行时沿光线行进的逐格递归判定循环（Recursive Raycast Elimination）。搜索空间被严格限制在由离线导出的拓扑关键节点（Key Points）之间。

### 2.1 依赖父节点移动矢量的方向剪枝查找表（ValidDirLookUpTable）

基于自然邻居（Natural Neighbors）与强制邻居（Forced Neighbors）的空间拓扑关系，算法构建静态查表器 `ValidDirLookUpTable`。输入上一跳的移动方向，即刻输出本轮搜索允许展开的候选方向集合，完成一阶剪枝（First-pass Pruning）：

| 进入当前节点的移动方向（Traveling Direction） | 允许探索的合法后继方向集合（Valid Directions） | 包含的拓扑结构说明 |
| :--- | :--- | :--- |
| **South (正南)** | `West, Southwest, South, Southeast, East` | 直行分量、自然对角分量及潜在拐角切向分量 |
| **Southeast (东南)** | `South, Southeast, East` | 对角自然扩展及其两个正交分量 |
| **East (正东)** | `South, Southeast, East, Northeast, North` | 直行分量、自然对角分量及潜在拐角切向分量 |
| **Northeast (东北)** | `East, Northeast, North` | 对角自然扩展及其两个正交分量 |
| **North (正北)** | `East, Northeast, North, Northwest, West` | 直行分量、自然对角分量及潜在拐角切向分量 |
| **Northwest (西北)** | `North, Northwest, West` | 对角自然扩展及其两个正交分量 |
| **West (正西)** | `North, Northwest, West, Southwest, South` | 直行分量、自然对角分量及潜在拐角切向分量 |
| **Southwest (西南)** | `West, Southwest, South` | 对角自然扩展及其两个正交分量 |

```
              North
          NW    ↑    NE
            \   |   /
      West ←─── ┼ ───→ East
            /   |   \
          SW    ↓    SE
              South

[Traveling South]     ==> Valid: { West, SW, South, SE, East }
[Traveling Southeast] ==> Valid: { South, SE, East }
```

### 2.2 目标跳点动态合成（Target Jump Point Synthesis）数学推导

在朝向目标点（Goal Node, 记为 $G$）搜索时，预处理表中记录的跳点是静态的，地图中并不预存由动态目标位置所引发的跳变。当搜索射线接近目标时，需要动态合成第四类跳点——**目标跳点（Target Jump Point）**。

#### 触发条件判据
设当前节点为 $C(r_c, c_c)$，目标节点为 $G(r_g, c_g)$。定义行跨度与列跨度为：
$$\Delta r = |r_g - r_c|, \quad \Delta c = |c_g - c_c|$$
定义基数方向网格差值为 $\text{DiffNodes}(C, G) = \max(\Delta r, \Delta c)$。

1. **基数方向命中（Cardinal Direction Hit）**：
   目标与当前节点严格共线（Exact Direction），且在碰撞距离之内：
   $$\text{DiffNodes}(C, G) \le |\text{distance}[r_c][c_c][d]|$$
   此时直接将 $G$ 生成为后继后向传播节点。

2. **对角线方向合成（Diagonal Target Jump Point）**：
   当探索方向 $d$ 为对角线，且目标落在该对角线扇区之内（General Direction），若行或列的向位投影距离小于或等于对角静态跳点/障碍物距离：
   $$\Delta r \le |\text{distance}[r_c][c_c][d]| \quad \lor \quad \Delta c \le |\text{distance}[r_c][c_c][d]|$$
   为确保生成的路径严格贴合网格（Grid-aligned），截取二者行、列跨度的极小值作为纯对角行进距离 $\delta$：
   $$\delta = \min(\Delta r, \Delta c)$$
   合成出的目标跳点坐标 $T(r_t, c_t)$ 沿方向 $d = (\text{sign}_r, \text{sign}_c)$ 位移：
   $$r_t = r_c + \delta \cdot \text{sign}_r, \quad c_t = c_c + \delta \cdot \text{sign}_c$$
   移动代价更新项引入八角形度量（Octile Metric）中的对角步进权重 $\sqrt{2}$：
   $$g(T) = g(C) + \sqrt{2} \cdot \delta$$
   当且仅当 $\Delta r = \Delta c$ 时，$T$ 与 $G$ 重合；若 $\Delta r \neq \Delta c$，$T$ 将充当一个临时的中间转向跳点，从该点可沿直线基数方向直达目标。

---

## 3. JPS+ 运行时核心算法完整实现（Listing 14.3）

```cpp
// 静态方向剪枝表声明
extern const Direction ValidDirLookUpTable[8][5];

void JPS_Plus_Search(Node* startNode, Node* goalNode)
{
    // 初始化优先队列（OpenList）与状态记录
    OpenList.Clear();
    startNode->givenCost = 0.0f;
    startNode->finalCost = CalculateHeuristic(startNode, goalNode);
    startNode->parent = NULL;
    OpenList.Push(startNode);

    while (!OpenList.IsEmpty())
    {
        Node* curNode = OpenList.Pop();
        Node* parentNode = curNode->parent;

        // 目标检索命中
        if (curNode == goalNode)
        {
            return PathFound;
        }

        // 基于父节点入射方向提取出合法后继射线集合
        foreach (direction in ValidDirLookUpTable given parentNode)
        {
            Node* newSuccessor = NULL;
            float givenCost = 0.0f;

            // 判定条件 1：基数方向且目标在确切射线上，并且目标比静态跳点/墙体更近
            if (direction is cardinal &&
                goal is in exact direction &&
                DiffNodes(curNode, goalNode) <= abs(curNode->distances[direction]))
            {
                newSuccessor = goalNode;
                givenCost = curNode->givenCost + DiffNodes(curNode, goalNode);
            }
            // 判定条件 2：对角线方向，目标处于大致朝向上，且投影跨度未被障碍阻断
            else if (direction is diagonal &&
                     goal is in general direction &&
                     (DiffNodesRow(curNode, goalNode) <= abs(curNode->distances[direction]) ||
                      DiffNodesCol(curNode, goalNode) <= abs(curNode->distances[direction])))
            {
                // 动态构建中间目标跳点 (Target Jump Point)
                int minDiff = min(RowDiff(curNode, goalNode), ColDiff(curNode, goalNode));
                newSuccessor = GetNode(curNode, minDiff, direction);
                givenCost = curNode->givenCost + (SQRT2 * minDiff);
            }
            // 判定条件 3：标准静态跳点展开
            else if (curNode->distances[direction] > 0)
            {
                newSuccessor = GetNode(curNode, curNode->distances[direction], direction);
                givenCost = DiffNodes(curNode, newSuccessor);
                if (direction is diagonal)
                {
                    givenCost *= SQRT2;
                }
                givenCost += curNode->givenCost;
            }

            // 传统 A* 松弛处理 (Relaxation step)
            if (newSuccessor != NULL)
            {
                if (newSuccessor not on OpenList or ClosedList)
                {
                    newSuccessor->parent = curNode;
                    newSuccessor->givenCost = givenCost;
                    newSuccessor->finalCost = givenCost + CalculateHeuristic(newSuccessor, goalNode);
                    OpenList.Push(newSuccessor);
                }
                else if (givenCost < newSuccessor->givenCost)
                {
                    newSuccessor->parent = curNode;
                    newSuccessor->givenCost = givenCost;
                    newSuccessor->finalCost = givenCost + CalculateHeuristic(newSuccessor, goalNode);
                    OpenList.Update(newSuccessor);
                }
            }
        }
    }

    return NoPathExists;
}
```

---

## 4. 寻路搜索实例剖析（Figure 14.10 Trace）

### 4.1 空间拓扑与跳点分布全景还原

```
   c=0       c=1       c=2       c=3       c=4       c=5       c=6
r=5 [   ]     [   ]  T1[ 0, 0]  [###]   T2[ 0, 0]   [###]    G[Goal]
                     [  0    ]          [ -1    ]             (T3)
r=4 [   ]     [   ]  [ 0,-1]            [-1,-1]               [-1,-1, 0]
                     [ 0, 3]            [-2     ]             [-1, 0   ]
                                        [-3, 2  ]
r=3 [   ]     [   ]    [###]     [###]     [   ]    [###]      [   ]

r=2 [   ]     [   ]    [###]     [   ]     [   ]   [ 0, 0]    [ 2, 0]
                                                   [ 1   ]    [ 0   ]
r=1 [   ]     [   ]    [###]     [###]     [   ]   [-1, 0]    [-1,-1,-1, 0]

r=0 S[ 0, 3]  [   ]    [###]     [   ]     [   ]     [###]     [   ]
    [-1, 0]
    [ 0, 0]
```
*(注：`[###]` 代表不可通行的墙体单元；`S` 为起点，`G` 为终点；`T1, T2, T3` 为动态生成的目标跳点。标注的数值对应于该单元向各探索方向的已缓存跳点/障碍物距离度量。)*

### 4.2 运行时极度收敛的搜索过程

在整幅地图的探索生命周期中，A* 算法原本需要遍历海量的网格状态并频繁压栈：
1. **总考量节点极低**：在全图中，仅有 **10 个节点**被 JPS+ 提取并考量，其中 **7 个节点**是构成最终最优解路径所不可或缺的支撑骨架。
2. **顶层动态跳点生成机制**：当搜索流向东北/西北对角线时，由于目标点位于网格最右上边缘，在顶行连续合成了 3 个目标跳点（Target Jump Points: $T_1, T_2, T_3$），使得对角线移动在接触局部上边界时能够平滑退化/重定向为直行基数射线，最终精准闭环于终点 $G$。
3. **消除递归射线开销**：传统 JPS 每次从 Open 表弹出一个节点，都必须沿水平、垂直、对角线方向在 CPU 核心内运行 `while(!IsWall)` 循环，在长走廊或开阔区域会频繁击穿 L1/L2 缓存。JPS+ 通过一步内存偏移读取距离标量（如 `+3`、`-2`），直接跳转至目的地，避免了逐格寻址检测。

---

## 5. 工业级存储架构与优先队列极致工程优化

### 5.1 内存布局与缓存局部性（Data-Oriented Design & Cache-Friendliness）

传统网格寻路在进行碰撞检测时，需要跨行、跨列连续随机访问连续内存块，对 CPU 缓存极不友好。JPS+ 将几何边界信息彻底编译为网格内部的距离标量场。

*   **节点结构致密化压缩**：
    每个节点仅需持久化保存 8 个离散方向的偏移距离值。工业界实现通常使用 8 位有符号整型（`int8_t`）截断存储最大距离为 127 的跃迁步长，或者使用标准 16 位有符号整型（`int16_t`）：
    $$\text{Memory Per Cell} = 8 \times 2 \text{ bytes} = 16 \text{ bytes}$$
*   **Dirty-Bit 惰性重置策略**：
    为避免每次寻路前执行全图 $O(W \times H)$ 复杂度的 `memset` 开销，采用运行世代戳（Generation/Run Stamp）或 Dirty-bit 标记。节点内存一次性预分配，仅在节点访问周期与当前系统的 `PathfindingRunID` 不一致时，原地覆盖该节点的运行时代价变量。

### 5.2 桶排序优先队列（Bucket-Sorted Priority Queue）微架构设计

在极端实时环境（例如包含数千个单位移动逻辑的 RTS 游戏）中，基于标准二叉堆或斐波那契堆的 OpenList 插入（$O(\log N)$）与浮动调整将成为整个主线程的算力瓶颈。

```
              Bucket-Sorted Priority Queue (Cost Resolution: Δc = 0.1)
   Bucket Head Array (Direct Index: index = (int)(finalCost * 10))
  +--------+--------+--------+--------+--------+--------+--------+
  | [0.0]  | [0.1]  | [0.2]  |  ...   | [14.2] | [14.3] |  ...   |
  +--------+--------+--------+--------+--------+--------+--------+
                                          |
                                          V Preallocated LIFO Node Pool
                                       +----+    +----+    +----+
                                       |Node| -> |Node| -> |Node| -> NULL
                                       +----+    +----+    +----+
```

#### 桶结构工程设计参数
*   **代价刻度量化（Quantization Resolution）**：
    以 $\Delta c = 0.1$ 为步长划分连续的离散数组桶。若节点的 $f(n) = \text{finalCost}$，则其对应的桶索引直接映射为：
    $$\text{BucketIndex} = \left\lfloor \frac{\text{finalCost}}{0.1} \right\rfloor$$
*   **内部组织模式**：
    每个桶内部维护预分配且无序的内存数组，节点插入操作严格采用**后进先出（LIFO）**策略，单次入栈开销压减至 $O(1)$。
*   **次优性上界约束**：
    由于桶内未对 $[c, c + 0.1)$ 范围内的节点进行微观绝对偏序排序，最终输出路径与绝对理论最优解之间的代价漂移 $\epsilon$ 被严格约束在微小界限内：
    $$\text{Cost}(\text{Path}_{\text{computed}}) \le \text{Cost}(\text{Path}_{\text{optimal}}) + 0.1$$
    该误差在实际游戏渲染画面与物理平滑插值后完全不可感知。
*   **内存空间兑换吞吐（Space-Time Trade-off）**：
    *   *传统 A\**：由于在开阔区域入队的节点数量庞大，维持密集的桶阵列需要耗费 10 MB 至 100 MB 内存。
    *   *JPS+\*：得益于跳点极度收缩的拓扑剪枝，同一时间滞留在 OpenList 中的活动节点极少，仅需 1 MB 至 10 MB 缓冲内存即可驱动极致稳定的 $\sim 10\times$ 级吞吐吞吐加速。

---

## 6. 算法性能边界、多维对比与权衡分析（Evaluation & Trade-offs）

### 6.1 渐进性能与复杂性综合比对

| 核心维度 / 指标 | 原生 A* 算法 (Native A*) | 传统跳点搜索 (Vanilla JPS) | 预处理跳点增强 (JPS+) |
| :--- | :--- | :--- | :--- |
| **开阔地形加速比** | $1.0\times$ (基准线) | $\sim 10\times$ 相比 A* | **$100\times$ (提升达两个数量级)** |
| **密集迷宫地形加速比** | $1.0\times$ (基准线) | $\sim 2.0\times$ 相比 A* | **$2.5\times$ 相比 A* ($\sim 20\%$ 快于 JPS)** |
| **预处理时间复杂度** | $O(0)$ (无需预处理) | $O(0)$ (零离线预计算) | $O(K \cdot W \cdot H)$ (需多次扫描全图) |
| **静态网格内存占用** | 极小 (仅存通行标记) | 极小 (仅存通行标记) | **中等 (每网格单元附加 8 个距离标量)** |
| **运行时空间消耗 (OpenList)** | 庞大 (需压入大量展开网格) | 较小 (仅存储跳点) | **极小 (仅存核心跳点与少量目标跳点)** |
| **动态阻挡与网格突变支持** | 优秀 (动态修改瞬时生效) | 良好 (直接基于实时网格探测) | **严苛限制 (仅支持静态刚性地图拓扑)** |
| **移动代价权重约束** | 任意代价网格 (Non-uniform) | 仅支持统一代价 (Uniform-cost) | **仅支持统一代价 (Uniform-cost)** |

### 6.2 工业应用边界与架构裁决逻辑

1. **地图开阔度与算力增益正相关规律**：
   JPS+ 的加速效能与地图的开阔比率（Openness Ratio）呈强正相关。在大型开阔原野或大型室内大厅，JPS+ 几乎将网格搜索退化为射线端点的直连操作，取得相比传统 A* 达两个数量级的断层式领先；在高度碎片化、充斥锯齿状对角墙壁（Jagged Diagonal Walls）的迷宫环境下，由于跳点距离频繁衰减为零或一，算法退化为局部受限搜索，相比原生 JPS 仍保有约 20% 的性能优势。
2. **图拓扑的刚性假设**：
   JPS+ 的前置硬性约束是**全静态地图假设**。若游戏场景中存在频繁的动态门扉启闭、可破坏掩体或大体量动态障碍物，会导致预处理距离场大面积失效。在此类工业场景下，主流架构通常将 JPS+ 部署于低频静态底层寻路，而将动态避障职责移交至局部导向行为（Steering Behaviors）、倒易速度障碍物系统（RVO/ORCA）或动态导航网格局部切割系统（Dynamic NavMesh Carving）联合处理。
