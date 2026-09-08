---
type: Reference
title: "第15章 Subgoal Graphs for Fast Optimal Pathfinding"
description: "Game AI Pro 工业级精读：Subgoal Graphs for Fast Optimal Pathfinding。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第15章 Subgoal Graphs for Fast Optimal Pathfinding

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 15.  
> 原文作者 / 资源：[Subgoal Graphs for Fast Optimal Pathfinding](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter15_Subgoal_Graphs_for_Fast_Optimal_Pathfinding.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

**Subgoal Graphs for Fast Optimal Pathfinding**

---

## 1. 体系概述与工业界基准（Introduction & Industry Benchmarks）

在现代复杂游戏（如 RTS、CRPG、开放世界）中，成百上千个智能体（Agents）需要在大规模栅格地图上进行实时导航。传统的 $A^*$ 寻路算法直接运行在低级栅格图（Grid Graph）上时，由于搜索空间随地图尺寸呈二次方甚至更高阶增长，且存在大量对称性冗余路径，会造成海量的节点扩展（Node Expansions），导致严重的 CPU 计算瓶颈与内存占用。

**子目标图（Subgoal Graphs）** 是一种通过离线预处理栅格拓扑结构，将连续/离散障碍物边缘的关键凸角抽象为稀疏图拓扑的高性能空间表示架构。在运行时，寻路系统完全忽略绝大多数底层栅格单元，仅在由关键子目标（Subgoals）构成的抽象拓扑网络上进行启发式搜索，并在找到高级子目标路径后，通过无局部障碍的规则投影快速将其还原为底层移动路径。

### 1.1 工业级评测对比（Game Benchmark Comparison）

在《龙腾世纪：起源》（*Dragon Age: Origins*）、《星际争霸》（*StarCraft*）、《魔兽争霸 III》（*Warcraft III*）及《博德之门 II》（*Baldur's Gate II*）的标准工业测试集上，子目标图系列算法展现出了相对栅格原生 $A^*$ 的压倒性加速比，同时保持了**严格的数学最优性（Strict Optimality）**。

| 拓扑架构变种 (Subgoal Graph Variant) | 预处理时间 (Preprocessing Time) | 内存占用 (Memory Used) | 运行期 $A^*$ 加速比 (Speedup vs. Grid $A^*$) | 最优性保证 (Optimal?) |
| :--- | :--- | :--- | :--- | :--- |
| **标准栅格 $A^*$ (Baseline)** | $0\text{ s}$ | 基准内存 | $1.0\times$ (均值 $12.69\text{ ms}$) | 是 (Yes) |
| **简单子目标图 (Simple Subgoal Graphs, SSG)** | $0.022\text{ s}$ | $1.172\text{ MB}$ | **$24\times$ 更快** | 是 (Yes) |
| **两级子目标图 (Two-Level Subgoal Graphs, TSG)** | $2.031\text{ s}$ | $1.223\text{ MB}$ | **$71\times$ 更快** | 是 (Yes) |
| **$N$ 级子目标图 ($N$-Level Subgoal Graphs)** | $2.195\text{ s}$ | $1.223\text{ MB}$ | **$112\times$ 更快** | 是 (Yes) |

> **竞赛技术地位**：在 2012 年与 2013 年国际栅格路径规划大赛（Grid-Based Path Planning Competitions, GPPC）中，两级子目标图（TSG）被评定为**非支配项（Nondominated Entry）**——即在所有参赛算法中，若有算法在绝对运行时间上超越 TSG，其代价必然是牺牲了路径最优性（次优解）或消耗了高出数倍乃至数个数量级的预处理内存。

---

## 2. 空间形式化表征与数学机理（Preliminaries & Spatial Reasoning）

### 2.1 栅格拓扑与非质点碰撞约束

环境被形式化为均一代价的八邻域栅格（Uniform-cost Eight-neighbor Grid）。障碍物（Obstacles）由连通的被阻挡栅格构成。
智能体从栅格中心向相邻栅格中心移动：
1. **移动代价值（Edge Weights）**：
   - 正交/基准方向（Cardinal Moves: 沿坐标轴）：移动长度为 $c_c = 1$。
   - 对角方向（Diagonal Moves）：移动长度为 $c_d = \sqrt{2}$。
2. **转角切边约束（Corner-Cutting Prevention Rule）**：
   智能体在物理空间中具有体积（非质点，Non-point Agent）。因此，智能体能够执行对角移动从栅格 $u$ 到 $v$ 的充要条件是：**其共享的两个正交邻域栅格必须同时为非阻挡（Unblocked）状态**。

```
   +---+---+         [B2] 欲对角移动至 [A1]
 1 |A1 |B1 |         - A2 被阻挡 (Blocked)
   +---+---+         - B1 未阻挡 (Unblocked)
 2 |xxx|B2 |         -> 禁止移动: 发生切角物理碰撞！
   +---+---+
     A   B
```

### 2.2 空间度量方程（Distance Metrics）

对于任意两点 $s = (x_s, y_s)$ 与 $r = (x_r, y_r)$，定义坐标差绝对值：
$$\Delta x = |x_s - x_r|, \quad \Delta y = |y_s - y_r|$$

#### 1. 欧几里得距离（Euclidean Distance）
$$h_{\text{Euclidean}}(s, r) = \sqrt{\Delta x^2 + \Delta y^2}$$
欧几里得距离在连续空间与无权图上具有可采纳性（Admissible），但由于忽略了八邻域栅格必须按 $1$ 与 $\sqrt{2}$ 步进的几何限制，其下界估计过于松弛。

#### 2. 八方向距离 / 八角距离（Octile Distance）
在不存在任何障碍物的理想八邻域栅格上，两点间的最优路径必然由双向复合位移组成：
$$\Delta_{\min} = \min(\Delta x, \Delta y), \quad \Delta_{\max} = \max(\Delta x, \Delta y)$$
- 对角移动步数：$N_{\text{diag}} = \Delta_{\min}$
- 正交移动步数：$N_{\text{card}} = \Delta_{\max} - \Delta_{\min}$

八角距离启发式定义为：
$$h_{\text{octile}}(s, r) = (\Delta_{\max} - \Delta_{\min}) \cdot 1 + \Delta_{\min} \cdot \sqrt{2} = (\max(\Delta x, \Delta y) - \min(\Delta x, \Delta y)) + \sqrt{2}\min(\Delta x, \Delta y)$$

**启发式信息性（Informedness）证明机理**：
由于在实数域恒有：
$$(\Delta_{\max} - \Delta_{\min}) + \sqrt{2}\Delta_{\min} \ge \sqrt{(\Delta_{\max} - \Delta_{\min} + \Delta_{\min})^2 + \Delta_{\min}^2} = \sqrt{\Delta x^2 + \Delta y^2}$$
即：
$$h_{\text{octile}}(s, r) \ge h_{\text{Euclidean}}(s, r)$$
八角距离是八邻域栅格图上**紧确（Tighter）的可采纳启发式函数**。使用八角距离进行 $A^*$ 搜索时，启发式梯度更陡峭，能更大幅度地剪除无效的搜索前沿，显著缩减状态扩展树（State Expansion Tree）。

---

## 3. 简单子目标图（Simple Subgoal Graphs, SSG）

### 3.1 连续视线图（Visibility Graphs）的栅格离散化困境

在计算几何中，连续二维多边形环境下的最优寻路通常依赖视线图（Visibility Graphs）。视线图的顶点集为障碍物的凸顶点，边为所有相互可见的顶点对，边权重为欧氏距离。
将视线图直接移植到栅格领域面临两大核心瓶颈：
1. **搜索分支因子过高（High Branching Factor）**：空间内任意可见的角落两两互联，导致 $A^*$ 搜索树的局部出度极大，破坏了缓存局部性并导致开集（Open Set）剧烈膨胀。
2. **可见性检测代价高昂（Costly Line-of-Sight Checks）**：无论离线构建还是运行时接入（将起点 $s$ 和终点 $g$ 连入图谱），均需要进行昂贵的浮点 Bresenham 或射线投射（Raycasting）遍历。

简单子目标图（SSG）正是为解决上述问题而设计的栅格化凸角抽象图。

### 3.2 子目标（Subgoal）的严格形式化定义

一个无阻挡栅格单元 $s$ 被判定为一个**子目标（Subgoal）**，当且仅当满足以下局部几何拓扑条件：
1. 栅格 $s$ 自身未被阻挡（Unblocked）。
2. 存在至少一个对角相邻栅格 $t$ 被阻挡（Blocked）。
3. 与 $s$ 和 $t$ 共同相邻的两个正交栅格**均未被阻挡**（Both Unblocked）。

```
  Case 1: 满足子目标几何特征         Case 2: 连续障碍边界（非凸角，不生成）
     +---+---+                          +---+---+
     | s | A |    s: 未阻挡             | s |xxx|    A: 被阻挡
     +---+---+    A, B: 未阻挡          +---+---+    -> 违反条件3
     | B | t |    t: 被阻挡             | B | t |    -> s 不是子目标
     +---+---+                          +---+---+
   (s 即为绕行转折关键点)
```

### 3.3 启发式可达性（$h$-Reachability）与直接可达性（Direct-$h$-Reachability）

#### 1. 启发式可达（$h$-Reachable）
若栅格图中两点 $u, v$ 之间存在一条最短路径，其路径代价严格等于两点间的八角距离：
$$dist_{\text{grid}}(u, v) = h_{\text{octile}}(u, v)$$
则称 $u$ 与 $v$ 为 **$h$-可达（$h$-Reachable）**。这意味着智能体在两点间移动时，仅需采用两种固定方向的矢量组合（一个正交方向和一个对角方向），其间没有任何凸出障碍物迫使智能体产生回退或偏航绕行。

#### 2. 直接启发式可达（Direct-$h$-Reachable）
若两个 $h$-可达的子目标之间盲目连边，会导致严重的拓扑冗余与极高的出度（如图 15.3 所示，点 $D3$ 与 $H5$ 虽然 $h$-可达，但两者之间的最短路径包含了中间子目标 $F5$）。

为建立极度精简的稀疏拓扑，引入**直接启发式可达（Direct-$h$-Reachability）**：
> **严格定义**：单元 $s$ 与 $r$ 是直接 $h$-可达的，当且仅当它们是 $h$-可达的，且它们之间的**任何一条**最短路径均不穿过任何其他子目标。
> 
> **几何等价定义（平行四边形定则）**：从 $s$ 到 $r$ 的所有无冗余最短路径在栅格空间中张成一个以 $s$ 与 $r$ 为对角顶点的**平行四边形闭合区域（Parallelogram-shaped Area）**。$s$ 与 $r$ 直接 $h$-可达，当且仅当该平行四边形区域内部不包含任何子目标，且区域内所有移动路径均未被障碍物阻断。

```
        s +---------------+
         /               /
        /   内部无子目标  /
       /   无内部阻挡     /
      +---------------+ r
      (s 与 r 张成的平行四边形搜索面)
```

**数学剪枝优势**：
- **任意顺序等价性**：在直接 $h$-可达的两个栅格之间移动时，正交步进与对角步进可以以**任意置换顺序**执行，绝不会触发几何碰撞。
- **出度极小化**：若平行四边形内存在子目标，边将被强制局部化分段（例如原本的长边 $(D3, H5)$ 被自然剪除，保留两段原子边 $(D3, F5)$ 与 $(F5, H5)$）。分支因子相比视线图呈数量级下降。

---

## 4. 全局搜索流水线与执行拓扑（Search Pipeline & Execution）

在简单子目标图上的整体寻路架构分为离线图构建、在线动态连边、抽象空间 $A^*$ 寻路以及底层几何解算四个流水线阶段：

```
                [离线阶段 Preprocessing]
                  底层栅格地图 (Grid Map)
                           │
                           ▼ 扫描转角
                  生成子目标集 (Subgoals)
                           │
                           ▼ 动态规划投影
            构建简单子目标图 (SSG: Direct-h-Edges)
 ══════════════════════════╪════════════════════════════
                [在线阶段 Query Pipeline]
                  输入: 起点 s, 终点 g
                           │
                           ▼
                  直连快速路径测试 (Bresenham Shortcut)
                    ├──[无障碍直达]──> 输出底层平滑路径并返回
                    └──[存在障碍]
                           │
                           ▼
                  将 s, g 接入 SSG (连接 Direct-h 子目标)
                           │
                           ▼
                  运行抽象 A* 搜索 (八角距离启发式)
                           │
                           ▼
                  高级子目标序列: <s, sub_1, sub_2, ..., g>
                           │
                           ▼
                  逐段正交/对角生成 (任意序栅格展开)
                           │
                           ▼
                  输出全局最优底层路径 (Optimal Low-level Path)
```

### 4.1 边界异常与快速前向测试（Fast Shortcut Path）

在线搜索阶段存在一个关键边界情况：若起点 $s$ 与终点 $g$ 自身不是子目标，且彼此直接 $h$-可达，但标准接入算法仅负责将非子目标与子目标连边，将导致 $s$ 与 $g$ 无法直接互联。

**工程处理原则**：
在执行耗时的子目标连接探测前，系统优先执行一条确定性的**双向投射测试（Bresenham-like Test）**：
- 尝试以“先沿对角方向行进 $\Delta_{\min}$ 步，再沿正交方向行进 $\Delta_{\max} - \Delta_{\min}$ 步”构造试探路径。
- 若此试探路径全程未被阻挡，则**直接判定 $s$ 与 $g$ 连通**，直接将该路径作为全局最优路径返回，跳过整个子目标图的图搜索阶段。
- 仅当试探路径阻挡时，方将 $s$ 与 $g$ 作为临时节点接入 SSG 执行抽象图搜索。

---

## 5. 直接 $h$-可达子目标的高性能动态规划辨识算法

为了在离线建图与在线查询时以极低延迟计算某单元 $s$ 能够直连的所有子目标，算法设计了基于**空间净空度（Clearance Values）**的定向动态规划扇区扫描算法。

### 5.1 空间净空度（Clearance Values）定义

对于给定栅格单元 $s$ 与移动方向 $d \in \{\text{8个基准/对角方向}\}$：
$$\text{Clearance}(s, d)$$
表示从 $s$ 出发沿方向 $d$ 连续步进，**在遭遇第一个子目标（包含该子目标）或遭遇物理阻挡障碍（不包含障碍）之前**所能进行的最大有效移动步数。

```
例：Clearance 判定图解
  s ──> ──> ──> [Subgoal] ──> [Blocked]
  └──────┬──────┘
    Clearance = 3 (在遇到子目标处即刻截断)
```

### 5.2 扫描架构的两阶段解构

空间被以 $s$ 为中心的 8 条主轴射线分割为 8 个卦限扇区（Octants）。算法分两阶段完成全域遍历：

```
                    North (N)
                        │  c
                 NW     │    /  NE
                   \    │   / 
                     \  │  /  d
          West (W)────  s  ──── East (E)
                     /  │  \
                   /    │   \
                 SW     │     SE
                        │
                    South (S)
```

#### 阶段一：单向轴向探测（Phase 1: One-Direction Exploration）
沿 8 个主方向（正交与对角）直接查询 $\text{Clearance}(s, d)$：
- 若步进终止于一个子目标栅格（即步进至 $s + (\text{Clearance}(s, d) + 1) \cdot \vec{d}$ 为子目标），则将该子目标加入直接 $h$-可达列表。

#### 阶段二：复合扇区扫描（Phase 2: Combined Direction Area Sweep）
系统并行/迭代遍历由一个正交方向 $c$ 和一个对角方向 $d$ 组成的 8 个复合扇区（如正东-东北扇区）：
- 从点 $s$ 沿对角方向 $d$ 逐行外推基准点 $u_k = s + k \cdot \vec{d}$。
- 在每个基准点 $u_k$ 上，沿正交方向 $c$ 投射扫描线（Scanline）。

### 5.3 扫描线长度截断定则（Three Truncation Rules）

为了保证扫描线覆盖的区域与 $s$ 构成的几何体严格属于直接 $h$-可达的并集，扫描线在向外延展时必须遵循以下三项数学约束：

1. **定则一：子目标/阻挡截断（Subgoal/Obstacle Stopping Rule）**
   扫描线沿 $c$ 方向延伸时，在触碰到障碍物前一个栅格截断；若触碰到子目标，则记录该子目标并立即终止该线的进一步延展。
   *(数学推论：越过子目标或障碍后的空间点，其与 $s$ 张成的平行四边形必然包含该子目标或障碍，不再满足直接 $h$-可达定义)*。
2. **定则二：单调单向递减律（Monotonic Length Upper-Bound Rule）**
   设当前扫描线索引为 $k$，其最大允许延伸长度为 $L_k$，前一条扫描线实际长度为 $L_{k-1}$，则恒有：
   $$L_k \le L_{k-1}$$
   *(数学推论：直接 $h$-可达区是无数个以 $s$ 为原点的平行四边形区域的拓扑并集，其轮廓在正交投影方向上必须单调收缩)*。
3. **定则三：子目标阶梯衰减律（Subgoal Decrement Refinement Rule）**
   若前一条扫描线 $k-1$ 的末端是因**命中子目标**而终止的，则当前扫描线的长度上限必须强制执行阶梯递减：
   $$L_k \le L_{k-1} - 1$$

---

## 6. 工业级算法伪代码与工程实现架构

以下为依据上述数学定则编写的直接 $h$-可达子目标辨识算法的完整工业级 C++ 风格实现。

```cpp
// 拓扑空间核心数据结构定义
struct Cell {
    int x;
    int y;
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
};

enum Direction {
    NORTH = 0, NORTH_EAST, EAST, SOUTH_EAST,
    SOUTH, SOUTH_WEST, WEST, NORTH_WEST
};

struct SweepDirectionPair {
    Direction cardinal;
    Direction diagonal;
};

// 预定义 8 个扇区的组合映射
const SweepDirectionPair SECTOR_DEFINITIONS[8] = {
    { EAST,  NORTH_EAST }, { NORTH, NORTH_EAST },
    { NORTH, NORTH_WEST }, { WEST,  NORTH_WEST },
    { WEST,  SOUTH_WEST }, { SOUTH, SOUTH_WEST },
    { SOUTH, SOUTH_EAST }, { EAST,  SOUTH_EAST }
};

/**
 * @brief 从源栅格 s 检索所有直接 h-可达的子目标集合
 * @param s 起始栅格单元
 * @param gridMap 地图上下文环境
 * @return std::vector<Cell> 直接可达的子目标容器
 */
std::vector<Cell> IdentifyDirectHReachableSubgoals(const Cell& s, const GridMap& gridMap) {
    std::vector<Cell> directSubgoals;

    // ─────────────────────────────────────────────────────────────
    // 阶段一：单向探测 (Phase 1: Cardinal & Diagonal Axis Probing)
    // ─────────────────────────────────────────────────────────────
    for (int d = 0; d < 8; ++d) {
        Direction dir = static_cast<Direction>(d);
        int clr = gridMap.GetClearance(s, dir);
        
        // 沿 dir 方向前移 (clr + 1) 步探测是否存在子目标
        Cell target = gridMap.Step(s, dir, clr + 1);
        if (gridMap.IsValid(target) && gridMap.IsSubgoal(target)) {
            directSubgoals.push_back(target);
        }
    }

    // ─────────────────────────────────────────────────────────────
    // 阶段二：复合扇区动态规划扫描 (Phase 2: Sector Dynamic Sweeping)
    // ─────────────────────────────────────────────────────────────
    for (const auto& sector : SECTOR_DEFINITIONS) {
        Direction cardDir = sector.cardinal;
        Direction diagDir = sector.diagonal;

        // 获取源点沿对角方向的扩展上限
        int maxDiagonalSteps = gridMap.GetClearance(s, diagDir);
        
        // maxLineLength 维护定则二与定则三的长度递减上界
        // 初始值为无穷大 (由第一根轴向扫描线奠定基准)
        int maxAllowedLineLength = std::numeric_limits<int>::max();

        Cell currentDiagOrigin = s;

        for (int diagStep = 1; diagStep <= maxDiagonalSteps; ++diagStep) {
            currentDiagOrigin = gridMap.Step(currentDiagOrigin, diagDir, 1);

            // 获取当前对角起源点沿正交方向的基础净空度
            int rawClearance = gridMap.GetClearance(currentDiagOrigin, cardDir);
            
            // 依据定则二施加单调性上界约束
            int currentLineLength = std::min(rawClearance, maxAllowedLineLength);

            // 检查当前扫描线是否直接触及子目标（定则一）
            Cell terminalCell = gridMap.Step(currentDiagOrigin, cardDir, currentLineLength + 1);
            bool endsInSubgoal = false;

            if (currentLineLength < maxAllowedLineLength) {
                // 说明长度由 rawClearance 决定，有能力检测终点栅格是否为子目标
                if (gridMap.IsValid(terminalCell) && gridMap.IsSubgoal(terminalCell)) {
                    directSubgoals.push_back(terminalCell);
                    endsInSubgoal = true;
                }
            }

            // 更新下一轮迭代的长度上限约束
            if (endsInSubgoal) {
                // 依据定则三：前线末端为子目标，步长单调递减 1
                maxAllowedLineLength = currentLineLength - 1;
            } else {
                // 依据定则二：前线因障碍截断或单调性受限，维持当前长度
                maxAllowedLineLength = currentLineLength;
            }

            // 剪枝优化：若允许长度耗尽，该扇区无需继续外推
            if (maxAllowedLineLength <= 0) {
                break;
            }
        }
    }

    return directSubgoals;
}
```

---

## 7. 空间复杂度分析与工业落地建议

### 7.1 算法复杂度解构

- **离线预处理耗时**：
  在典型 $512 \times 512$ 游戏地图中，子目标数量通常仅占总栅格数的 $1\% \sim 3\%$。简单子目标图的预处理（计算 Clearance 与 Direct-$h$-Edges）仅耗时约 $0.022\text{ s}$，其开销足以支持在关卡加载甚至切片运行时（Time-sliced Runtime）执行。
- **内存消耗**：
  相较于视线图需要为全图维护 $O(V^2)$ 条显式连边，SSG 严格限定仅在直接 $h$-可达子目标之间建立局部稀疏边。实测内存占用在基准游戏场景中仅增加约 $1.17\text{ MB}$，完全适配移动端或主机内存敏感型项目。
- **运行时分支因子优化**：
  直连 $h$-可达性测试排除了跨越凸角的伪长边，将节点的平均出度从视线图的几十甚至上百，骤降至个位数级别（通常每个子目标连边数为 $3 \sim 6$ 条）。由于搜索树规模指数级衰减，A* 在 SSG 上的扩展次数极少，直接斩获 **24 倍** 的实时加速。

### 7.2 现代商业游戏引擎工程集成规范

1. **混合层次架构（Hierarchical Routing Strategy）**：
   在宏观规划层面运行子目标图（SSG/TSG），产出粗粒度的关键凸角路线；在执行层面，利用局部的转向行为（Steering Behaviors，如 RVO2、ORCA 避障）驱动角色沿子目标之间的直连段平滑移动。
2. **动态对象与阻挡变动应对（Dynamic Obstacle Handling）**：
   对于静态建筑、地形高低差，离线固化 SSG 拓扑；对于门、可破坏掩体等动态栅格，可通过仅标记受局部影响的卦限扇区并触发局部 Clearance 重新投影，实现局部的轻量级拓扑修补，规避全图重构的高额开销。

---

*(Engineering Specification: Hierarchical Subgoal Graphs for Fast Optimal Pathfinding)*

---

## 1. 系统架构全景与理论基础 (System Architecture & Foundations)

在工业级游戏引擎（如 Unreal Engine、Unity 及自研高性能引擎）中，基于八连通栅格（Eight-Neighbor Grid）的大规模路径规划面临核心瓶颈：栅格节点爆炸引发的状态空间搜索开销过大，以及传统 $A^*$ 启发式搜索在平坦开阔区域产生的大量对称性节点扩展（Symmetry Expansion）。

**子目标图（Subgoal Graphs, SSG）**通过将栅格空间解构为障碍物拐角处的离散子目标点集合，并依据启发式可达性（$h$-reachability）构建抽象拓扑网络，将寻路开销从底层几何栅格剥离。为进一步突破大规模复杂拓扑的吞吐上限，**两层子目标图（Two-Level Subgoal Graphs, TSG）**与**通用多层图架构（$N$-Level Graphs）**在 SSG 基础上引入了严格的分层抽象机制与节点收缩机制（Node Contraction），在数学上严格保证路径全局最优性（Path Optimality）的前提下，实现极高阶的图裁剪率（Graph Pruning Rate）。

```
+-----------------------------------------------------------------------------------+
|                            应用与决策层 (Decision Layer)                           |
|        行为树 (Behavior Trees) / 状态机 (FSM) / 分层任务网络 (HTN Planner)           |
+-----------------------------------------------------------------------------------+
                                         │ 寻路请求 Request(s, t)
                                         ▼
+-----------------------------------------------------------------------------------+
|                      多层子目标图引擎 (Subgoal Graph Engine)                        |
|                                                                                   |
|  [阶段 1: 运行时拓扑动态嫁接]                                                         |
|  Query Start/Goal ──> IdentifyConnectingVertices() ──> 动态构建局部搜索子图         |
|                                                                 │                 |
|  [阶段 2: 抽象核心图搜索 (Core Graph Search)]                    ▼                 |
|  Top-Level Core Graph ◄── 高层抽象 $A^*$ 搜索 (High-Level Path on TSG / N-Level)   |
|                                                                 │                 |
|  [阶段 3: 几何路径反解与平滑 (Refinement & Steering)]            ▼                 |
|  DFS/Sweep-line 还原直接/间接 $h$-可达线元 ──> 栅格级路径 ──> 导向行为 (Steering)  |
+-----------------------------------------------------------------------------------+
                                         │
                                         ▼ 预计算离线烘焙 (Offline Precomputation)
+-----------------------------------------------------------------------------------+
|                        图分层构建管线 (Baking Pipeline)                            |
|  Grid Geometry ──> 基础 SSG ──> 节点收缩 (Contraction) ──> TSG / N-Level Hierarchy|
+-----------------------------------------------------------------------------------+
```

---

## 2. 直接启发式可达性识别算法 (Direct-h-Reachability Recognition)

### 2.1 理论机理与几何间隙 (Clearance)
在八连通栅格网络中，两节点 $u, v$ 称为**启发式可达（$h$-reachable）**，当且仅当存在一条从 $u$ 到 $v$ 的八向网格无碰撞路径，其几何代价值严格等于其八向启发式评估距离（Octile Heuristic Distance）$h(u, v)$：

$$h(u, v) = (\sqrt{2} - 1) \cdot \min(\Delta x, \Delta y) + \max(\Delta x, \Delta y)$$

其中 $\Delta x = |u.x - v.x|$，$\Delta y = |u.y - v.y|$。

若在此类最短路径中，所有对角移动（Diagonal Move）与轴向移动（Cardinal Move）可以按固定顺序（如纯对角移动后沿单一轴向移动，或反之）自由交换且其包围的平行四边形/矩形区域内无任何障碍阻挡，则称两节点为**直接启发式可达（Direct-$h$-reachable）**。

`Clearance(s, dir)` 定义为从栅格单元 $s$ 出发，沿特定基向量方向 $dir$ 移动且不与静态阻挡相交的最大自由栅格步数。

### 2.2 扇区扫描实现 (Listing 15.1)
以下算法在以起始单元 $s$、基轴方向 $c$、对角方向 $d$ 构成的扇区内，通过连续扫描线间隙衰减检测，精确搜寻所有直接 $h$-可达的子目标点：

```cpp
// Listing 15.1. 识别指定扇区区域内的所有直接 h-可达子目标点
SubgoalVector GetDirectHReachable(cell s, cardinal_dir c, diagonal_dir d)
{
    SubgoalVector list = {};
    int maxLineLength = Clearance(s, c);
    int nDiagMoves = Clearance(s, d);

    for (int i = 1; i <= nDiagMoves; ++i)
    {
        s = neighbor_of(s, d);
        int l = Clearance(s, c);
        
        // 维护扫描线最大有效投影边界
        if (l < maxLineLength)
        {
            maxLineLength = l;
        }

        // 定位扇形扫描前沿的候选子目标栅格
        cell s_prime = cell_moves_away(s, c, l + 1);
        if (is_subgoal(s_prime))
        {
            list.add(s_prime);
        }
    }

    return list;
}
```

---

## 3. 两层子目标图：理论证明与拓扑收缩 (Two-Level Subgoal Graphs)

### 3.1 两层性质 (Two-Level Property, TLP) 的数学形式化
两层子目标图（TSG）将一阶子目标集合 $V_{SSG}$ 严格划分为两个不相交子集：**全局子目标（Global Subgoals, $G$）**与**局部子目标（Local Subgoals, $L$）**，即：

$$V_{SSG} = G \cup L, \quad G \cap L = \emptyset$$

TSG 满足**两层性质 (Two-Level Property, TLP)**：
> 设 $s, t \in V_{SSG}$ 为图中的任意两个子目标（无论其为局部还是全局），$d_{SSG}(s, t)$ 为两者在原始底层子目标图中的最短路径长度。定义导出的临时评估图：
> 
> $$G_{eval}(s, t) = \big(G \cup \{s, t\}, \; E_{TSG} \cap \{(u,v) \mid u, v \in G \cup \{s, t\}\}\big)$$
> 
> 则在 $G_{eval}(s, t)$ 上的最短路径长度 $d_{eval}(s, t)$ 恒满足：
> 
> $$d_{eval}(s, t) = d_{SSG}(s, t)$$

**工业界工程推导意义**：当求解任意起点与终点的最短路径时，除了与起点和终点直接 $h$-可达的局部子目标点外，整个图搜索过程可以安全地将网络中所有其他局部子目标点及其附属边从内存激活集合中彻底剔除，而绝不丧失全局最优解精度。

```
[原始 SSG 拓扑]                     [收缩后的 TSG 拓扑]
    (A1)======(D1)                     (A1)======(D1)
     || \    / ||                       :  \    /  :
     ||  \  /  ||                       :   \  /   :
     ||   \/   ||  ───── 节点收缩 ────>   :    \/    :
     ||   /\   ||   (A3, D3 降为局部)     :    /\    :
     ||  /  \  ||                       :   /  \   :
    (A3)======(D3)                     [A3]======[D3]
     ||        ||                       :          \\  <-- 新增 h-可达捷径边
     ||        ||                       :           \\     (Shortcut Edge)
    (H1)======(H5)                     (H1)=========(H5)

图例说明:
  (O)  全局子目标 (Global Subgoals)
  [O]  局部子目标 (Local Subgoals，仅在直接关联起止点时接入)
  ===  常规直接 h-可达边
  ---  受限/降级边
  \\\  收缩时引入的补偿捷径边 (h-reachable Shortcuts)
```

### 3.2 节点收缩 (Node Contraction) 算法详解
TSG 的离线构建本质上是拓扑图的贪心节点收缩过程。若移除某节点 $s$ 会破坏剩余图在某两邻居间的距离守恒，且该两邻居之间满足 $h$-可达条件，则通过在两点间添加一条**捷径边（Shortcut Edge）**以维持 TLP，从而成功收缩 $s$。

```cpp
// Listing 15.2. 从单层 SSG 离线构建 TSG 拓扑网络
void ConstructTSG(SSG S, SubgoalList& G, SubgoalList& L, EdgeList& E)
{
    G = S.subgoals; // 初始状态：所有子目标均初始化为全局子目标
    L.clear();      // 局部子目标初始集为空
    E = S.edges;    // 继承底层 SSG 边集

    for (auto s_it = G.begin(); s_it != G.end(); )
    {
        vertex s = *s_it;
        EdgeList E_plus = {}; // 候选补充捷径边集
        bool local = true;    // 标记当前节点 s 是否具备收缩为 Local 的可行性

        // 获取 s 在当前动态演变图 E 中的所有邻接顶点
        VertexVector neighbors = Neighbors(s, E);

        // 遍历所有无序邻居对 (p, q)
        for (size_t i = 0; i < neighbors.size(); ++i)
        {
            for (size_t j = i + 1; j < neighbors.size(); ++j)
            {
                vertex p = neighbors[i];
                vertex q = neighbors[j];

                // 在排除 s 以及当前所有局部点 L 的子图上计算最短路
                // 计算: d = dist_{E \ ({s} U L)}(p, q)
                float d = ShortestPathExcluding(p, q, E, s, L);

                // 若移除 s 破坏了最短路径约束（即经由 s 的路径更短）
                if (d > Cost(p, s) + Cost(s, q))
                {
                    // 验证 p 与 q 是否具备几何上的 h-可达性
                    if (IsHReachable(p, q))
                    {
                        // 记录该补偿捷径边，若 s 降级成功则合并入拓扑
                        E_plus.add(Edge(p, q, OctileDistance(p, q)));
                    }
                    else
                    {
                        // 节点 s 构成了 p 与 q 间不可绕过且非 h-可达的唯一桥梁
                        local = false;
                        break;
                    }
                }
            }
            if (!local) break;
        }

        if (local)
        {
            // 成功收缩：分类为局部节点并固化捷径边
            s_it = G.erase(s_it);
            L.add(s);
            E.append(E_plus);
        }
        else
        {
            ++s_it;
        }
    }
}
```

---

## 4. 多层泛化拓扑：$N$-Level Graphs

### 4.1 通用图抽象与约束属性 $P$
将 TSG 的两层构造理论推广到任意无向图 $G = (V, E)$，需要对收缩公理进行泛化：
1. **顶点与层级定义**：局部顶点归类为 Level 1 节点，全局顶点归类为 Level 2 节点。
2. **递归升阶**：对由 Level 2 顶点及其导出子图继续执行节点收缩，剥离出的局部节点保留在 Level 2，提升剩余的全局节点为 Level 3，依此类推，形成严格的阶梯式嵌套：

$$V_{\text{Level } 1} \subset V_{\text{Level } 2} \subset \dots \subset V_{\text{Level } N} = V_{\text{Core}}$$

3. **约束属性 $P$ (Property $P$)**：
   在通用图中，不能任意添加无约束捷径边，否则图将退化为稠密的节点对距离矩阵（Pairwise Distance Matrix），引发分支因子（Branching Factor）急剧膨胀和缓存命中率下降。因此，新增边必须严格满足属性 $P$：
   - 边长严格等于底层原图的真实测地线最短距离。
   - 边所代表的抽象跳跃能够在底层拓扑上以轻量级算法（如局部无回溯 DFS、直接光线投影）快速复原。
   - 边的密集度不显著增大高层搜索时的分支度数。
   - 在 SSG 体系中，$P \equiv \text{Direct-}h\text{-Reachable}$ 或 $h\text{-Reachable}$。若无法设计合理的属性 $P$，可令 $P = \emptyset$（即不允许补充边），此时收缩仅剔除具备等价旁路冗余的节点。

### 4.2 拓扑层次与节点激活结构
以多层图架构对图节点进行重新分层组织，其多级核心映射拓扑如下：

```
Level 3 (Core)       (E)===================(H)===================(G)
                    /  \                  /   \                  /  \
                   /    \                /     \                /    \
Level 2          (D)     \              /       \             (C)     \
                /   \     \            /         \           /   \     \
Level 1       (A)   (K)   (I)        (J)         (F)       (L)   (B)   ...
```

当针对任意两点（例如 $F \in \text{Level 1}$ 与 $B \in \text{Level 1}$）发起全局寻路时，自底向上的动态激活机制仅向上接入有限的有向关联弧段，屏蔽与本次查询无关的大量中间拓扑。

---

## 5. 运行时搜索管线与连接点动态嫁接 (Runtime Search Pipeline)

### 5.1 动态连接点拓扑装配算法 (Listing 15.3)
在执行高层 $A^*$ 寻路前，系统必须将起点 $s$ 和终点 $t$ 逐级接入核心图（Core Graph）。核心图包含所有最高层级（即 $Level = N$）的顶点集合。接入过程采用受限的层级广度优先拓展（Bounded Hierarchical BFS/Dijkstra）：

```cpp
// Listing 15.3. 在查询期动态识别必须激活并合并到核心搜索图的连接顶点
VertexList IdentifyConnectingVertices(vertex s, Graph G, int graphLevel)
{
    // open 表按顶点层级 (level) 升序排列的优先队列
    PriorityQueue<vertex> open; 
    VertexList closed = {};

    open.push(s);

    while (!open.empty())
    {
        vertex p = open.pop_smallest_level();

        // 成功触达当前多层图的顶层核心层级，中止当前分支向下探索
        if (p.level == graphLevel)
        {
            break;
        }

        if (!closed.contains(p))
        {
            closed.add(p);

            // 仅向更高层级的邻近拓扑方向进行辐射扩展 (Upward Search)
            for (vertex q : G.neighbors_of(p))
            {
                if (q.level > p.level && !closed.contains(q))
                {
                    open.push(q);
                }
            }
        }
    }

    return closed;
}
```

### 5.2 完整运行时多阶段寻路执行流程
多层子目标图在工业级游戏中的单次完整寻路帧生命周期如下：

```
[输入: Start(s), Goal(t)]
           │
           ▼
[阶段 1: 几何快速连通性检测 (Trivial Reachability)]
检测 s 与 t 是否在底层栅格上直接满足 Direct-h-reachable？
    ├── 是 ──> [直接生成单段平滑线元，直接返回全局最优解，耗时 O(1)]
    └── 否 ──> 步入图搜索阶段
                   │
                   ▼
[阶段 2: 动态多层嫁接 (Hierarchical Projection)]
分别对 s 与 t 调用 IdentifyConnectingVertices(s, G, N) 及 (t, G, N)
将返回的激活顶点集 V_active 与核心图 G_core 合并为局部临时搜索图 G_temp
                   │
                   ▼
[阶段 3: 高层抽象图 A* 搜索 (Abstract Path Search)]
在 G_temp 上运行 A* 算法，启发函数采用八向距离 h(u, v)
提取高层子目标路径点序列: Path_High = <s, v_1, v_2, ..., v_k, t>
                   │
                   ▼
[阶段 4: 路径细化与几何反解 (Edge Refinement)]
遍历 Path_High 中的每对连续顶点对 (v_i, v_{i+1})：
    ├── 若 (v_i, v_{i+1}) 为直接 h-可达 ──> 展开为基础轴向与对角位移
    └── 若 (v_i, v_{i+1}) 属于收缩引入的非直接可达边：
            调用局部深度优先搜索 (DFS) / 双向扫描快速求解栅格连续路径
                   │
                   ▼
[阶段 5: 最终航路点提交 (Steering Output)]
输出平滑后的栅格连续世界坐标航路点集至导向行为 (Steering Behaviors) 模块
```

---

## 6. 与收缩层次结构 (Contraction Hierarchies, CH) 的技术对比

两层及多层子目标图与经典交通路网加速算法——**收缩层次结构（Contraction Hierarchies, CH）**共享了节点收缩的数学渊源，但在游戏引擎架构与空间搜索特征上存在本质区别：

| 架构对比维度 | 经典收缩层次结构 (Contraction Hierarchies) | 多层子目标图 ($N$-Level Subgoal Graphs) |
| :--- | :--- | :--- |
| **底层空间拓扑** | 稀疏的一般有向图（真实世界道路网络） | 八连通规则二维空间网格（Grid Maps） |
| **层级离散粒度** | **全序严格单调**：每个等级仅包含 $1$ 个顶点 ($|Level_k| = 1$) | **块状聚合分层**：每一级容纳大量具有相同几何地位的顶点集合 |
| **边收缩约束 ($P$)** | **无限制**：只要保持最短路必加 Shortcut，易引发边爆炸 | **强几何约束**：新增边必须为 $h$-可达或直接 $h$-可达，限制边集规模 |
| **搜索机制** | 双向向上搜索（Bidirectional Upward Search） | 单向/双向高层核心图 $A^*$ 搜索，辅助以动态连接嫁接 |
| **内存与还原开销** | 需完整存储大量 Shortcut 展开解压树链 | 借助底层栅格隐式几何属性，在线 DFS 还原路径，内存占用极小 |

---

## 7. 工业级工程落地要点与性能调优 (Production Engineering Guidelines)

### 7.1 内存紧凑型拓扑存储设计
在 64 位工业级游戏引擎中，为保证运行时对 L1/L2 Cache 友好并减少内存碎片，禁止使用基于指针的图节点链式分散存储。推荐采用**连续平铺数组（Flat-Array Contiguous Storage）**与**数据导向设计（Data-Oriented Design, DOD）**组织多层子目标图：

```cpp
// 紧凑打包的子目标节点元数据 (8 Bytes 对齐)
struct SubgoalVertex
{
    uint16_t grid_x;        // 空间栅格 X 坐标
    uint16_t grid_y;        // 空间栅格 Y 坐标
    uint8_t  level;         // 所处分层 (Level 1 ~ Level N)
    uint8_t  flags;         // 标志位 (是否为动态障碍影响、是否离线锁定)
    uint16_t edge_offset;   // 边表中起始偏移量
};

// 紧凑边结构 (Flat CSR 拓扑表示)
struct SubgoalEdge
{
    uint32_t target_vertex; // 目标节点在顶点数组中的下标
    float    cost;          // 路径代价权重 (Octile Distance)
    uint8_t  edge_type;     // 0: Direct-h, 1: Indirect-h Shortcut
};
```

### 7.2 节点收缩处理顺序对拓扑质量的影响
如前文算法所述，SSG 的收缩结果并不唯一，最终 TSG/多层图的核心节点规模与预计算时遍历顶点的顺序强相关。在工程实现中，应避免采用栅格线性扫描顺序遍历子目标，推荐采用基于**节点重要度评估函数**的优先队列驱动收缩序列：

$$I(v) = E_{\text{shortcuts}}(v) - E_{\text{incident}}(v) + \text{LevelDepth}(v)$$

- $E_{\text{shortcuts}}(v)$：收缩该节点预计需要引入的全新捷径边数量（Edge Difference）。
- $E_{\text{incident}}(v)$：当前节点关联的原始边数量。
- 优先收缩重要度评估值较低的顶点，能够有效遏制拓扑构建期边数量膨胀，最大限度压缩最终最高层核心图的节点总数。

### 7.3 动态障碍物应对模式 (Dynamic Obstacles Mitigation)
在工业级应用中，环境常包含动态阻挡物（如可破坏掩体、关闭的闸门）。当动态阻挡破坏局部的 $h$-可达性时：
1. **局部退化策略 (Local Fallback)**：对受影响半径 $R$ 内的子目标点打上脏标记（Dirty Flag）。
2. **混合路由 (Hybrid Routing)**：当寻路查询穿过脏标记区域时，系统在宏观层通过多层子目标图推进至局部边界，在受动态阻挡干扰的局部包围盒内退化调用底层分层 A* 或导航网格寻路（NavMesh Recast Local Repair），在吞吐效率与动态适应性之间维持工业级平衡。
