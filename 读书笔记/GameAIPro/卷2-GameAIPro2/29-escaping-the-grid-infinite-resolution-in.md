---
type: Reference
title: "第29章 Escaping the Grid: Infinite-Resolution Influence Mapping"
description: "Game AI Pro 工业级精读：Escaping the Grid: Infinite-Resolution Influence Mapping。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第29章 Escaping the Grid: Infinite-Resolution Influence Mapping

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 29.  
> 原文作者 / 资源：[Escaping the Grid: Infinite-Resolution Influence Mapping](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter29_Escaping_the_Grid_Infinite-Resolution_Influence_Mapping.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

## 1. 知识表示与空间推理体系概述 (Knowledge Representation & Spatial Reasoning)

在现代工业级游戏人工智能（Game AI）架构中，知识表示（Knowledge Representation）是支撑决策模型的核心支柱。该体系涵盖了智能体（Agent）用于存储、感知并检索物理世界环境信息的整套机制。知识表示的抽象能力与表达精度，直接决定了上层决策系统（例如行为树（Behavior Trees, BT）、效用系统（Utility Systems）以及分层任务网络（Hierarchical Task Networks, HTN））所能达到的战术高度。

空间分析与空间推理（Spatial Analysis & Spatial Reasoning）旨在量化处理在游戏连续或离散空间中随位置动态变化的数值。在空间推理工具链中，**影响图（Influence Map）** 是最为经典且广泛应用的核心技术之一。

```
+-------------------------------------------------------------------------+
|                  AI 决策层 (AI Decision Layer)                          |
|         行为树 (BT)  /  效用系统 (Utility)  /  分层任务网络 (HTN)         |
+-------------------------------------------------------------------------+
                                    │
                                    ▼ 空间战术查询 (Spatial Queries)
+-------------------------------------------------------------------------+
|                空间推理引擎 (Spatial Reasoning Engine)                  |
|  态势评估 (Tactical Analysis)  |  隐蔽点评估 (Cover)  |  威胁预测 (Threat)  |
+-------------------------------------------------------------------------+
                                    │
       ┌────────────────────────────┴────────────────────────────┐
       ▼                                                         ▼
[ 传统离散空间表征 (Grid/NavMesh) ]              [ 连续无网格影响图 (Infinite-Resolution) ]
- 规则 2D 网格 (Uniform Grid)                    - 基于点集的影响源 (Point-Based Sources)
- 导航网格图元 (NavMesh Polygons)                 - 连续衰减函数 (Continuous Falloff)
- 四叉树/层次网格 (Quadtree Subgrids)             - 空间划分加速 (k-d Tree Acceleration)
```

传统工业界实践普遍依赖规则二维网格（2D Regular Grid）甚至基于导航网格（NavMesh）的离散多边形图元来构建影响图。然而，离散化网格在处理精细微观战术、大规模开阔世界与三维高低差时，在内存占用（Memory Footprint）和刷新扩散（Propagation）计算开销上均会面临严重的性能与精度瓶颈。

**无限分辨率影响图（Infinite-Resolution Influence Mapping）** 彻底摒弃了离散栅格单元，采用连续数学衰减模型与动态点基系统（Point-Based Influence System），结合高维空间分割数据结构，在消除网格失真的同时，实现了任意连续空间坐标的极速精确查询。

---

## 2. 经典影响图架构剖析 (Influence Mapping Architecture)

### 2.1 影响图的三大核心要素
构建并驱动一个完整的影响图系统，必须包含以下三个基础正交要素：
1. **空间分布值（Spatially Varying Value）：** 依附于环境空间分布的标量或向量物理量（如玩家威胁度、敌方占有率、能见度系数、友军火力覆盖范围等）。
2. **传播机制（Propagation Method）：** 描述这些数值如何在空间介质中散布（Diffusion）、随时间推移衰减（Temporal Decay）或跨越拓扑结构传递的更新规则。
3. **查询机制（Query Mechanism）：** 提供给外部 AI 决策管线评估战场态势、搜索极值点、权衡最优战术站位（Tactical Positioning）的高效访问接口。

### 2.2 传播范式：放置与扩散 (Placement and Diffusion)
影响图数值更新过程由两个离散阶段构成：
* **放置（Placement）：** 将游戏世界的实时感知事件或瞬时实体状态注入到影响图指定位置。
* **扩散（Diffusion）：** 数值从放置源头向周围空间展开平滑过渡、模糊（Smear）或时空渗流的过程。

```
[感知/战术事件 (Perception Event)]
               │
               ▼ (Placement)
+-------------------------------+
|  影响源注入 (Influence Seed)  |
+-------------------------------+
               │
               ▼ (Diffusion / Blurring)
+-------------------------------+     时空衰减因子
|  多跳邻域扩散 (Spatial Spread) | <--- (Decay Factor $\alpha$)
+-------------------------------+
               │
               ▼
+-------------------------------+
| 最终影响图态势 (Resulting Map) |
+-------------------------------+
```

在特定应用场景下，系统不需要进行任何空间扩散处理。例如即时战术位置图（Tactical Map），仅记录当前帧各战斗单位的瞬时位置。此类数据在有效生命周期内支持直接查询；一旦态势过时（Stale），系统直接清空缓冲区并重新执行一次瞬时计算。

### 2.3 典型应用模式
* **占有率图（Occupancy Map）：** 表达敌人在某区域存在的概率分布。当侦测到敌方单位时，将相应区域赋值为高置信度，随后数值随时间不断衰减，并向相邻图元渗流扩展，以拟合目标在脱离视野后的潜在移动范围。
* **可见度图（Visibility Map）：** 基于视线几何测试（Line-of-Sight, LOS），动态度量场景各点被特定智能体直接观察到的概率或清晰程度。
* **面包屑轨迹化（Breadcrumbing）：** 该技术用于记录智能体的历史时空流动。更新时并不重置影响图，而是将前一帧的数值按比例衰减：
  $$V_{t} = \alpha \cdot V_{t-1} + V_{\text{new}}$$
  其中 $\alpha \in [0, 1)$ 为衰减系数。此方式将瞬时位置热度叠加形成历史热力图（Historical Heat Map），有效揭示玩家或巡逻单位的高频活动路径与战术咽喉点（Chokepoints）。

### 2.4 传统网格影响图推导与障碍阻断示例
在传统网格影响图中，放置点的值通常为最高饱和度，并向外沿网格拓扑逐格衰减；同时，不可通行的几何障碍物阻断扩散传播路径。

```
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 0 | 0 | ■ | 0 | 0 | 1 | 2 | 3 | 2 |
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 0 | 0 | ■ | 0 | 1 | 2 | 3 | 4 | 3 |
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 0 | 0 | ■ | 0 | 1 | ★ | 4 | 5 | 4 |  <-- ★: 实际位置 (Actual Location)
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 0 | 1 | 1 | 2 | 3 | 4 | 5 | 6 | 5 |
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 1 | 2 | ■ | ■ | ■ | ■ | 7 | 6 | ■ |  <-- ■: 阻断障碍物 (Obstacles)
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 7 |
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 0 | 1 | 2 | 3 | ■ | 6 | 7 | 9 | 8 |
+---+---+---+---+---+---+---+---+---+---+---+
| ■ | ■ | ■ | ■ | 1 | 2 | ■ | 7 | 8 | 9 | ☆ |  <-- ☆: 最后目击位置 (Last Known Location)
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 0 | 0 | 0 | 1 | ■ | 6 | 7 | 8 | 8 |
+---+---+---+---+---+---+---+---+---+---+---+
| 0 | 0 | 0 | 0 | 0 | 0 | ■ | 6 | 7 | 8 | 7 |
+---+---+---+---+---+---+---+---+---+---+---+
```

---

## 3. 经典离散网格的局限性 (Limitations of Classical Grid Approaches)

将细致广袤的游戏三维世界栅格化投影到二维网格时，面临着难以调和的计算几何与系统工程矛盾：

### 3.1 内存爆炸与分辨率约束
设游戏世界在物理空间各轴跨度为 $L \times W$，网格单元精度为 $c$（例如 $c = 0.5\text{ m}$），则单层影响图需分配的内存单元总量为：
$$N_{\text{cells}} = \left(\frac{L}{c}\right) \times \left(\frac{W}{c}\right)$$
内存占用随分辨率提高呈二次方增长（$\mathcal{O}(1/c^2)$）。对于 3D 空间，这一消耗将直接上升至 $\mathcal{O}(1/c^3)$。如果通过降低分辨率来节省内存，则会导致宏观网格完全抹平局部精细掩体（Cover Points）与战术走廊的几何特征，诱发智能体决策走位失真。

### 3.2 扩散传播的 CPU 算力瓶颈
网格扩散通常基于元胞自动机（Cellular Automata）或类似热传导卷积核运算。每个单元每轮更新必须读取周围 8 邻域（Moore 邻域）或 4 邻域（von Neumann 邻域）的数值。当传播半径跨越多个单元时：
* 更新单帧的运算量极速飙升，需要处理多达数百个关联单元；
* 尽管直接通过坐标哈希访问网格具备 $\mathcal{O}(1)$ 的寻址复杂度，但全图更新的总计算复杂度高达 $\mathcal{O}(k \cdot N_{\text{cells}})$（$k$ 为卷积核半径覆盖的格子数）；
* 大规模连续内存的遍历如果无法完全契合 CPU 缓存行（Cache Line），频繁的跨步访问还会引发严峻的 Cache Miss 问题。

### 3.3 稀疏树状与局部网格方案的工程缺陷
工业界曾尝试采用四叉树（Quadtree）或挂载于智能体周围的局部浮动网格（Localized Grids）来规避全局静态开销，但这引发了新的架构代价：
* **四叉树动态分配开销：** 随着战场影响源频繁位移，四叉树需高频进行节点内存的重构、分裂与合并（Split & Merge），由此引发的内存碎片和动态分配开销（Allocation Overhead）极易造成主线程卡顿；
* **局部网格缝合困难：** 浮动网格在边界相交重叠时，跨网格的扩散与数值融合（Blending）实现复杂度极高，且在智能体遍布全图的全局大战场形态下，局部网格的开销与维护复杂度均显著劣化，甚至劣于简单的单块静态大网格。

---

## 4. 基于点的影响力数学表征 (Point-Based Influence Formulation)

### 4.1 连续衰减模型构建
客观现实中，绝大多数战场影响源本质上都是基于空间孤立点的（智能体、手雷爆点、狙击枪枪口焰等）。这些实体在原点位置沉积最大影响峰值，随后沿空间径向向外围辐射。

当使用解析连续函数来描述这种衰减（Falloff Function）时，空间中任意查询点的影响值可以直接通过数学公式闭式求得，彻底消除了对离散网格的数据依赖。

设一维径向衰减函数为 $f(x)$，有效影响半径为 $r$。最直观的形式为线性衰减模型（Linear Decay）：
$$f(x) = 1 - \frac{x}{r}, \quad \text{其中 } x \le r \tag{29.1}$$
该函数保证了影响值在原点处为 $1$，并随欧几里得距离增加线性平滑单调递减，在半径边界 $r$ 处收敛为 $0$。

### 4.2 二维空间扩展与非线性可微性质
将该模型投影至二维笛卡尔空间，设位于坐标 $(x_0, y_0)$ 的单一点影响源半径为 $r$，其在任意坐标点 $(x, y)$ 处的空间影响函数表述为：
$$f(x, y) = \max\left(1 - \frac{\sqrt{(x - x_0)^2 + (y - y_0)^2}}{r}, \, 0\right) \tag{29.2}$$

引入 $\max(\cdot, 0)$ 算子截断了超出影响半径 $r$ 的非物理负值，保证了影响函数的紧凑局部支撑（Compact Support）。

更深入的数学洞察表明：**衰减函数并不局限于线性映射，任何具有明确偏导数的连续函数（Any Differentiable Function）均可无缝接入该体系**。可微性在高级空间查询（例如利用梯度下降搜索逃逸路线或最弱威胁方向）中具有决定性作用。

### 4.3 场强叠加与异构权重扩展
对于场景中存在的多个异构影响源，空间任意坐标点的综合影响场强可以直接通过线性叠加原理（Superposition Principle）进行合成。设影响源集合中第 $i$ 个影响源具有坐标 $(x_i, y_i)$、影响半径 $r_i$ 及幅度强度标量 $s_i$（Scale Factor），则空间任意位置 $(x, y)$ 的合成影响函数 $g(x, y)$ 为：
$$g(x, y) = \sum_{i} f_i(x, y) = \sum_{i} \max\left(1 - \frac{\sqrt{(x - x_i)^2 + (y - y_i)^2}}{r_i}, \, 0\right) \tag{29.3}$$

引入异构强度缩放因子 $s_i$ 后，各影响源的最大贡献峰值可动态差异化配置：
$$g(x, y) = \sum_{i} \max\left(s_i \left(1 - \frac{\sqrt{(x - x_i)^2 + (y - y_i)^2}}{r_i}\right), \, 0\right) \tag{29.4}$$

#### 影响源类型与核心参数矩阵
| 影响源战术类型 | 空间半径 $r_i$ | 初始强度 $s_i$ | 衰减函数类型 (Falloff Profile) | 梯度特性 (Derivative) |
| :--- | :--- | :--- | :--- | :--- |
| **重装步兵威胁 (Heavy Threat)** | 较大 (15.0m) | 强 (+10.0) | 连续线性衰减 (Linear) | 分段常数梯度，边界处一阶不可导 |
| **狙击手视线压制 (Sniper Fire)** | 极大 (60.0m) | 极强 (+25.0) | 高斯平滑分布 (Gaussian Profile) | 全域一阶连续可导 $C^1$ |
| **友军撤离点引力 (Evac Gravity)** | 中等 (20.0m) | 负值抑制 (-15.0) | 二次多项式衰减 (Quadratic) | 原点平滑，外缘渐近可导 |
| **破片手雷危险域 (Grenade AoE)** | 较小 (6.0m) | 瞬发饱和 (+50.0) | 阶梯/脉冲反比衰减 (Inverse Square) | 奇异点规避的局部紧支可导 |

### 4.4 连续无网格表达的核心优势与潜在代价

```
+-------------------------------------------------------------------------------+
|                       影响图范式核心指标对照分析矩阵                          |
+----------------------+----------------------------+---------------------------+
| 指标维度             | 经典离散网格模型           | 无限分辨率点基模型        |
+----------------------+----------------------------+---------------------------+
| 空间解析精度         | 受单元格尺寸 $c$ 硬性截断  | 理论无限连续分辨率        |
| 内存占用复杂度       | $\mathcal{O}(W \cdot H / c^2)$ 面积暴涨 | $\mathcal{O}(n)$ 线性正比于影响源数量 |
| 单点基础查询性能     | $\mathcal{O}(1)$ 数组随机访问 | 原始未优化前为 $\mathcal{O}(n)$ 线性全扫描 |
| 动态拓扑适应性       | 必须重构/重采样网格数据    | 仅需更新点坐标与半径参数  |
| 障碍几何交互难度     | 路径洪水填充天然契合       | 需要针对遮挡执行几何相交剪裁 |
+----------------------+----------------------------+---------------------------+
```

该模型解耦了空间精度与内存占用的绑定关系，但将单点查询的时间开销由离散网格的 $\mathcal{O}(1)$ 推高至原始状态下的 $\mathcal{O}(n)$（$n$ 为场景内影响源总数）。在工业级引擎开发中，必须引入高效的空间索引结构以压制查询开销。

---

## 5. 空间划分与超高速查询加速 (Spatial Partitioning & Query Acceleration)

为了将单点及局部查询由 $\mathcal{O}(n)$ 降低至实用量级，核心逻辑在于实现**平凡剔除（Trivial Rejection）**：在空间距离上其影响半径 $r_i$ 无法触碰查询区域的影响源，必须被底层架构快速过滤。

### 5.1 候选空间分割拓扑评估
* **沃罗诺伊图（Voronoi Diagrams）：** 基于 Fortune 算法可达到最佳构造时间 $\mathcal{O}(n \log n)$。然而，Voronoi 图的底层拓扑假设为互斥划分空间空间单元，使每个空间点只受单一生成元控制。这与战术影响图中多个影响源覆盖范围高频相互重叠（Overlap）的物理现实产生根本冲突，因而不适合直接作为加速结构。
* **四叉树 / 八叉树（Quadtree / Octree）：** 基于空间的规则划分。当场景中存在大范围浮动物体时，会导致同一个影响源跨越多个树节点，从而产生重复节点关联（Duplicate References）或繁琐的链表更新。
* **k-d 树（$k$-Dimensional Tree）：** 一种在多维空间中划分点集的高效二叉搜索树（Binary Search Tree）。每个节点天然对应一个影响源实体的物理点坐标，具有存储高度紧凑、构建稳定平衡、空间修剪精准的工程特性。

### 5.2 平衡 2D $k$-d 树构建流水线 (Construction Pipeline)
对于二维战术空间（$k=2$），系统在 $X$ 轴与 $Y$ 轴之间逐层交替确定分割轴（Splitting Axis）。通过递归选取输入点集的中位数（Median），保证构建出的树天然处于高度平衡状态。

```
                    输入点集 (Input Source Points)
                                 │
                                 ▼
                     [交替选择切分轴 Axis]
                       (Level 0: X-Axis)
                                 │
                                 ▼
                     [对当前轴坐标快速排序]
                        (QuickSelect)
                                 │
                                 ▼
                    [选取中位数点作为树节点]
                      Node = Points[Median]
                                 │
                 ┌───────────────┴───────────────┐
                 ▼                               ▼
     [递归处理前半段点集]               [递归处理后半段点集]
    (Left Subtree: < Median)           (Right Subtree: > Median)
    (Next Level: Y-Axis)               (Next Level: Y-Axis)
```

#### 形式化构建步骤
1. **确定切分轴：** 依深度轮换轴，设树深度为 $d$，切分轴方向 $\text{Axis} = d \bmod 2$（$0 \to X, 1 \to Y$）。
2. **排序点集：** 将当前分支涵盖的子点集沿当前轴进行标量投影排序。
3. **定位中位数：** 提取有序序列的中位数点 $\mathbf{P}_{\text{med}}$，初始化为当前 $k$-d 树节点。
4. **生成子分支：**
   * 低于中位数的左半部分点集，递归作为左子节点（$\text{Node.Left}$）；
   * 高于中位数的右半部分点集，递归作为右子节点（$\text{Node.Right}$）。

#### 结构形态示意图
对于点集 $A, B, C, D, E$ 在平面内的分布，构建交替分割的平衡 $k$-d 树：

```
[平面空间几何划分]                            [对应的 k-d 树拓扑结构]

  +Y                                                   (A) [Split on X]
   ^                                                   / \
   |       |             |                            /   \
   |   * D |             |                           /     \
   |       |             * C (Y-Split)             (B)     (C) [Split on Y]
   | - - - + - - - - - - + - - - - - - -           /       / \
   |       |   * B       |                        /       /   \
   |       |             |   * E                 (D)     (E)   NIL
   |       | * A         |
   |       | (X-Split)   |
   +----------------------------------> +X
```

### 5.3 $k$-d 树范围查询与分支剪枝算法 (Range Query Algorithm)
在执行战术影响求值时，需检索所有能触达查询坐标 $\mathbf{Q}(x, y)$ 的影响源。其关键数学约束为：

$$\text{dist}(\mathbf{P}_i, \mathbf{Q}) \le r_i$$

在算法实现中，若各影响源的最大半径为 $r_{\text{max}}$（或给定固定战术查询半径 $\text{radius}$），检索过程将以 $\mathbf{Q}$ 为圆心执行范围检索。

* **渐近复杂度保证：** 对于 $2\text{D } k\text{-d}$ 树，范围查询的最坏渐近时间复杂度为 $\mathcal{O}(2\sqrt{n})$（由 Lee 与 Wong 于 1977 年严格证明）；最佳下界达到 $\mathcal{O}(\log n)$。
* **工程剪枝关键细微性（Subtlety）：** 判定当前节点探索分支时，**绝对不能**仅仅基于查询点在分割轴哪一侧就执行单向二叉检索。若查询几何体（例如圆或包围盒 AABB）与当前分割平面发生重叠交叉，则**必须同时递归遍历左子树与右子树**。

#### C# 工业级范围查询实现 (Listing 29.1)
```csharp
public enum SplitAxis 
{
    X,
    Y
}

public class KdTreeNode 
{
    public float x;
    public float y;
    public SplitAxis SplitAxis;
    public KdTreeNode Left;
    public KdTreeNode Right;
}

public static class KdTreeSpatialQuery
{
    /// <summary>
    /// 在 k-d 树上递归检索落在指定圆半径范围内的所有节点
    /// </summary>
    /// <param name="node">当前遍历的 k-d 树节点</param>
    /// <param name="x">查询圆心 X 坐标</param>
    /// <param name="y">查询圆心 Y 坐标</param>
    /// <param name="radius">搜索物理半径</param>
    /// <param name="outpoints">接收命中的结果集容器</param>
    public static void FindPointsInRadius(
        KdTreeNode node,
        float x,
        float y,
        float radius,
        ref List<KdTreeNode> outpoints
    ) {
        // 递归终止边界校验
        if (node == null)
            return;

        // 1. 检验当前节点是否落在查询区域内（使用平方距离避免无谓的开方计算）
        float dx = x - node.x;
        float dy = y - node.y;
        float rsq = (dx * dx) + (dy * dy);
        
        if (rsq < (radius * radius))
        {
            outpoints.Add(node);
        }

        // 2. 计算查询中心点沿当前节点切分轴的带符号投影距离
        float axisdelta;
        if (node.SplitAxis == SplitAxis.X)
            axisdelta = node.x - x;
        else
            axisdelta = node.y - y;

        // 3. 几何边界穿透判定（非互斥条件，切勿加入 else 分支）
        // 若查询区域左/下边界跨过了切分平面，必须递归进入左子树
        if (axisdelta > -radius) 
        {
            FindPointsInRadius(node.Left, x, y, radius, ref outpoints);
        }
        
        // 若查询区域右/上边界跨过了切分平面，必须递归进入右子树
        if (axisdelta < radius) 
        {
            FindPointsInRadius(node.Right, x, y, radius, ref outpoints);
        }
    }
}
```

---

## 6. 时空影响传播机制 (Temporal Influence Propagation)

传统网格影响图极具魅力的特性是能够随着游戏时间推进，模拟态势在空间中的连续渗流与平滑弥散。无限分辨率无网格模型通过两类截然不同但各自优雅的数学范式，成功重建了这种时间维度的连续传播行为。

### 6.1 两阶段粒子环分裂模型 (Two-Pass Ring-Splitting Model)
该离散化点基模型模拟了波动方程与热传导扩散的微观物理特性：

```
     时间帧 T0                     时间帧 T1 (单次分裂迭代)
                         
                                       (S1) [新点]
                                         ^
                                         |
        (S0)             ===>     (S4) < (S0') > (S2) [新点]
     [原始单点]                           |  [衰减残存]
                                         v
                                       (S3) [新点]
```

#### 算法执行管线
1. **环形子节点派生（Generation Pass）：**
   在第一阶段，遍历场景中现有的每个活动影响源点 $S_k$。在其局部切空间（Tangent Space）外围生成 $M$ 个呈环状等距排列的新子影响点：
   $$\mathbf{P}_{\text{new}, j} = \mathbf{P}_k + \Delta r \begin{bmatrix} \cos\left(\frac{2\pi j}{M}\right) \\ \sin\left(\frac{2\pi j}{M}\right) \end{bmatrix}, \quad j \in \{0, \dots, M-1\}$$
2. **守恒衰减与能量平分（Energy Conservation Pass）：**
   对母体影响源施加时间衰减算子：
   $$s_k^{(t+1)} = \beta \cdot s_k^{(t)}, \quad \beta \in (0, 1)$$
   同时，为了维持辐射空间的总场强守恒，剩余能量向外扩散并均分给新生子节点：
   $$s_{\text{new}} = \frac{(1 - \beta) \cdot s_k^{(t)}}{M}$$
3. **阈值淘汰机制（Pruning Threshold）：**
   在后续各物理步长推进中，子节点会作为新的源点进一步向外衍生次级环结构。一旦某点分裂后的贡献权重跌落至预设浮点截断阈值（Tuned Epsilon Threshold, $\epsilon$）：
   $$s < \epsilon$$
   该点即被认定为在时间轴上彻底消亡（Expired），从全局活跃集合中彻底剥离释放，防止点集数量无序爆炸。

### 6.2 连续高斯解析近似 (Continuous Gaussian Distribution Function)
除动态分裂大量粒子点之外，另一种数学上更为紧凑高效的工程方案是：**直接引入含有时间变量的时变高斯解析分布（Time-Varying Gaussian Profile）**，用单一点源的参数演化完全等价连续扩散：

设点源产生于时间 $t=0$，其空间扩散过程满足二维各向同性热传导方程：
$$\frac{\partial g}{\partial t} = D \nabla^2 g$$

其连续闭式解可直接表达为动态高斯衰减函数：
$$g(\mathbf{x}, t) = \frac{S_0}{4\pi D (t + t_0)} \exp\left(-\frac{\|\mathbf{x} - \mathbf{x}_0\|^2}{4D(t + t_0)}\right)$$
其中：
* $S_0$ 为瞬时注入的总能量；
* $D$ 为介质扩散系数（Diffusion Coefficient）；
* $t_0$ 为平滑初值，用于防止 $t=0$ 时分母出现奇异点。

在此解析表述下，系统**无需频繁分配、裂变任何新的子节点**，依然维持 $\mathcal{O}(1)$ 的空间存储。该节点在空间中的影响半径随时间呈 $\sqrt{t}$ 渐近扩张，而中心峰值自然衰减，兼顾了数学精度与执行效率。

---

## 7. 复杂空间拓扑与障碍物阻断机制 (Handling Obstacles & Nontrivial Topologies)

在真实的 3D 游戏关卡环境中，存在复杂的非凸多边形障碍（建筑物、混凝土掩体、门窗与壕沟）。若无视阻挡，无网格影响场强将直接穿透实体墙壁，造成严重的 AI 战术误判（例如智能体误认为隔墙处于重度机枪压制之下，从而拒绝突击）。

```
+---------------------------------------------------------------------------------+
|                   视线阻断与非欧氏距离衰减处理流程                              |
+---------------------------------------------------------------------------------+

                      [发起连续空间查询 Query Point Q]
                                     │
                                     ▼
                [k-d 树粗筛：检索有效半径内影响源集 {Si}]
                                     │
                                     ▼
                        [逐个影响源遍历 Si -> Q]
                                     │
                                     ▼
               [几何射线投射 (Raycast / Line-of-Sight Test)]
                                     │
             ┌───────────────────────┴───────────────────────┐
             │ 存在障碍物遮挡                                │ 无遮挡自由空间
             ▼                                               ▼
   [拓扑绕障路径处理]                                [经典欧式衰减计算]
   - 方案 A: 立即阴影截断 (Hard Shadow, $g_i = 0$)       - $f_i(Q) = 1 - \frac{\|Q - S_i\|}{r_i}$
   - 方案 B: 沿 NavMesh 测地线距离测算 (Geodesic)    - 累加至总体场强
   - 方案 C: 障碍边界折射虚拟源派生 (Diffraction)
```

为在无网格框架下处理不可通行的复杂拓扑，工业界通常采用以下三种多层级解决方案：

1. **射线投射阴影截断（Raycast Visibility Cutoff）：**
   在空间查询时，对于 $k$-d 树返回的候选影响源执行快速物理光线投射或使用碰撞检测管线（Collision Pipeline）。若 $\text{RayCast}(\mathbf{x}_{\text{source}}, \mathbf{x}_{\text{query}})$ 命中关卡静态几何体阻挡，则当前影响源对该查询点的贡献直接置零（Hard Shadow）。
2. **测地线距离替代（Geodesic Distance on NavMesh）：**
   在结构复杂的走廊或室内场景中，欧几里得空间直线距离无法反映真实的步进代价。系统查询底层导航网格（NavMesh），使用多段折线测地线距离（Shortest Path Distance）$d_{\text{geodesic}}(\mathbf{x}_i, \mathbf{x})$ 替换欧氏距离 $\|\mathbf{x} - \mathbf{x}_i\|$ 代入公式：
   $$f(x, y) = \max\left(1 - \frac{d_{\text{geodesic}}(\mathbf{x}_i, \mathbf{x})}{r_i}, \, 0\right)$$
3. **虚拟衍射源（Virtual Diffraction Sources）：**
   当感知或威胁沿转角扩散时，系统在掩体转角边缘（Corner Edges）生成次级虚拟衍生源。新源的强度按衰减后的残余值计算，其有效辐射范围仅覆盖拐角背后的阴影锥形区域（Shadow Frustum），在保留连续数学解析特性的同时，逼真还原了声音与威胁绕过转角的物理弥散现象。

---

## 8. 空间优化查询：极值搜索与梯度推导 (Optimization Queries: Extrema & Gradients)

传统网格影响图搜索全局或局部最优解（如“寻找附近最隐蔽的掩护点”或“搜寻威胁度最小的突围撤退路径”）时，通常需要对网格图元执行暴力遍历或局部爬山（Hill Climbing）离散探测。

而在无网格连续表达下，影响函数由一系列可微函数解析叠加而成，可直接引入连续数学优化算法。

### 8.1 空间影响场梯度的解析表达
设空间某点处于多个可微衰减源 $f_i(\mathbf{x})$ 的作用域下，当前位置的合成场强为 $g(\mathbf{x}) = \sum_i f_i(\mathbf{x})$。由可微性，局部梯度向量（Gradient Vector）$\nabla g(\mathbf{x})$ 具备精确解析解：
$$\nabla g(x, y) = \begin{bmatrix} \frac{\partial g}{\partial x} \\ \frac{\partial g}{\partial y} \end{bmatrix} = \sum_{i \in \text{Active}} \nabla f_i(x, y)$$

对于标准单源线性衰减模型（在有效半径 $r_i$ 内部，且 $\mathbf{x} \ne \mathbf{x}_i$）：
$$f_i(\mathbf{x}) = s_i \left(1 - \frac{\|\mathbf{x} - \mathbf{x}_i\|}{r_i}\right)$$

其各偏导数可直接推导为：
$$\frac{\partial f_i}{\partial x} = -\frac{s_i}{r_i} \cdot \frac{x - x_i}{\sqrt{(x - x_i)^2 + (y - y_i)^2}} = -\frac{s_i}{r_i} \frac{x - x_i}{\|\mathbf{x} - \mathbf{x}_i\|}$$
$$\frac{\partial f_i}{\partial y} = -\frac{s_i}{r_i} \cdot \frac{y - y_i}{\sqrt{(x - x_i)^2 + (y - y_

---

> **技术来源**：《Game AI Pro 2: More Collected Wisdom of Game AI Professionals》第 29 章（第 335–342 页），主题聚焦于摆脱传统离散化网格与拓扑图束缚的连续空间影响图（Infinite-Resolution / Grid-Free Influence Maps）、多维空间剪枝、障碍物感知传播、解析梯度求导与局部极值优化。

---

## 1. 影响源空间精简与拓扑聚类算法（Simplification Pipeline）

在连续空间中动态注入影响源（Influence Sources）后，影响源节点的数量往往随时间步扩散急剧增长。为保证空间索引（k-d 树，k-d Tree）的遍历与重构效率，必须执行定期的空间降采样与节点合并——即**有损精简（Lossy Simplification）**流程。该步骤旨在以微小的精度损失换取节点总数的显著压降，维持稳定的实时计算吞吐。

```
[原始密集影响源集合 (k-d Tree)]
              │
              ▼ 针对每个节点 N 遍历
   [半球/球状邻域范围查询] ─── 邻居计数 k < k_threshold ──► [保留原节点，不作修改]
              │
              │ k >= k_threshold
              ▼
   [计算邻域节点空间质心 (Centroid)]
              │
              ▼
    [质心位置影响强度采样判定] ─── SampleValue < T_prune ──► [取消合并，避免失真]
              │
              │ SampleValue >= T_prune
              ▼
[剪枝原邻域点集与测试节点 N]
              │
              ▼
[在质心处生成并插入等效新影响源节点]
```

### 1.1 节点邻域判定与几何质心重构

精简算法依次遍历当前 k-d 树内的所有活跃节点。针对任意待测候选节点 $N_c = (\mathbf{x}_c, s_c, r_c)$：
1. **有效衰减邻域查询（Neighborhood Radius Query）**：
   以衰减函数（Falloff Function）的临界阻断半径（Limiting Radius）$r_c$ 执行多维范围查询，获取能够对待测节点产生影响贡献的近邻节点集合 $\mathcal{N}(N_c)$。
2. **邻居阈值筛选**：
   若集合势 $|\mathcal{N}(N_c)| < K_{\min}$（预设邻域容限），则认为该区域影响源密度较低，测试节点 $N_c$ 予以原样保留。
3. **空间质心（Centroid）计算**：
   若邻域节点密度达到或超过阈值，独立解算其空间坐标的算术均值（以 2D 空间为例）：
   $$\bar{x} = \frac{1}{|\mathcal{N}|} \sum_{j \in \mathcal{N}} x_j, \quad \bar{y} = \frac{1}{|\mathcal{N}|} \sum_{j \in \mathcal{N}} y_j$$
4. **质心采样与等效聚类剪枝（Pruning & Resampling）**：
   在质心位置 $\mathbf{c} = (\bar{x}, \bar{y})$ 对当前场强进行连续函数采样。若综合影响值满足预设幅值阈值：
   $$g(\mathbf{c}) \ge \Phi_{\mathrm{prune}}$$
   将集合 $\mathcal{N}(N_c) \cup \{N_c\}$ 中的所有节点从空间索引中完全移除，并在坐标 $\mathbf{c}$ 处生成并插入一个新的代表性影响源节点，该节点继承原点集的等效综合衰减半径与能量幅值。

---

## 2. 障碍物感知与非平凡拓扑空间传播（Obstacle-Aware Propagation）

在平坦、空旷的几何体上，无网格影响图表现优异。但在包含复杂阻隔（如墙体、立柱、狭窄走廊）的非平凡拓扑（Nontrivial Topologies）中，传统无约束向外辐射会穿透阻隔或破坏能量守恒。

### 2.1 能量守恒流体隐喻与元胞自动机模型

为了使影响能够绕过障碍物、呈现“沿着障碍物边缘流动”（Bounce off and Flow）的动力学特征，系统引入**分裂衰减与能量守恒模型（Energy Conservation via Branching）**：

* **元胞自动机（Cellular Automata, CA）视角**：每个影响源视为一个移动元胞自动机，携带标量能量。在单步时间扩散中，该自动机将能量拆解并分发给未受遮挡的空间后代（Descendants）。
* **光子映射（Photon Mapping）与核密度估计（Kernel Density Estimation, KDE）同构**：借鉴图形学中光子碰撞反弹并沉淀能量的数学机制，无网格影响传播本质上是非参数核密度估计在动力学碰撞约束下的蒙特卡洛积分近似。

### 2.2 障碍物规避传播状态机拓扑

```
                    ┌─────────────────────────┐
                    │   母影响源节点产生       │
                    │   总能量 E_0 沿法向扩散 │
                    └────────────┬────────────┘
                                 │
                                 ▼
                    ┌─────────────────────────┐
                    │ 针对各离散子采样方向:   │
                    │    计算探测目标点 P_k   │
                    └────────────┬────────────┘
                                 │
                 ┌───────────────┴───────────────┐
                 ▼                               ▼
       [射线检测/碰撞查询]             [射线检测/碰撞查询]
       P_k 位于障碍物内部 (Occluded)   P_k 位于有效空旷空间 (Unobstructed)
                 │                               │
                 ▼                               ▼
       [剔除该采样点，不予生成]        [收集至有效放置集合 S_valid]
                 │                               │
                 └───────────────┬───────────────┘
                                 │
                                 ▼
                    ┌─────────────────────────┐
                    │ 计算有效集合势 M=|S_valid| │
                    │ 能量重分配:             │
                    │ E_new = (E_0 * Decay)/M │
                    └────────────┬────────────┘
                                 │
                                 ▼
                    ┌─────────────────────────┐
                    │ 生成 M 个新子节点并注入 │
                    │ 走廊呈现类似流体高压喷射 │
                    └─────────────────────────┘
```

若某母节点能量向 $N$ 个预设离散方向投影，其中 $M$ 个方向未被遮挡（$M \le N$），则仅在未阻挡点放置新影响源，且每个新源的能量分配严格遵循：
$$E_{\mathrm{child}} = \frac{E_{\mathrm{parent}} \cdot \gamma_{\mathrm{decay}}}{M}$$
当影响源被迫穿过狭窄走廊时，被阻挡方向的能量被集中压入少数通路方向，表现出流体动力学中“受限管流流速加快、推进更远”的高压流动效应。

---

## 3. 解析求导与梯度下降空间优化（Continuous Optimization Queries）

影响图在决策体系中的核心价值不仅在于点查询（Point Query），更在于空间推理（Spatial Reasoning）中的极值定位：
* **局部极小值查询（Local Minimization）**：如规避敌方威胁、搜索最安全的伏击/掩体点。
* **局部极大值查询（Local Maximization）**：如搜索敌方火力最密集处以投放范围打击（AOE）。
* **约束优化（Constrained Optimization）**：在限定感知半径或多边形区域内求解极值。

### 3.1 线性衰减影响场的数学解析推导

若空间中存在 $K$ 个影响源，每个影响源具有初始强度 $s_i$、作用半径 $r_i$ 和中心坐标 $(x_i, y_i)$，则定义全场连续累加影响函数 $g(x, y)$ 为：

$$g(x, y) = \sum_{i} \max\left( s_i \left[ 1 - \frac{\sqrt{(x - x_i)^2 + (y - y_i)^2}}{r_i} \right], 0 \right) \tag{29.5}$$

由于求导算子的线性性质，累加函数的导数即为各单项影响函数导数的线性叠加：

$$g'(x, y) = \sum_{i} f_i'(x, y) \tag{29.6}$$

对于位于有效作用半径之内的点，即 $\sqrt{(x - x_i)^2 + (y - y_i)^2} \le r_i$ 且 $> 0$ 时，单源函数 $f_i(x, y)$ 对分量 $x$ 与 $y$ 的偏导数解析形式为：

$$\frac{\partial}{\partial x} f_i(x, y) = -\frac{s_i (x - x_i)}{r_i \sqrt{(x - x_i)^2 + (y - y_i)^2}} \tag{29.7}$$

$$\frac{\partial}{\partial y} f_i(x, y) = -\frac{s_i (y - y_i)}{r_i \sqrt{(x - x_i)^2 + (y - y_i)^2}} \tag{29.8}$$

最终合成的空间梯度矢量为：

$$\nabla g(x, y) = \left( \sum_{i \in \mathcal{A}} \frac{\partial f_i(x, y)}{\partial x}, \;\; \sum_{i \in \mathcal{A}} \frac{\partial f_i(x, y)}{\partial y} \right) \tag{29.9}$$

其中集合 $\mathcal{A} = \left\{ i \;\middle|\; 0 < \sqrt{(x - x_i)^2 + (y - y_i)^2} \le r_i \right\}$。若待测点恰与影响源原点重合，其导数未定义，工程上通常加入极小偏移量 $\epsilon$（如 $10^{-6}$）或直接平滑梯度以规避奇异点。

### 3.2 局部极小值梯度下降算法实现

```csharp
// ============================================================================
// 算法 29.2：基于解析梯度的局部极小值梯度下降算法（Gradient Descent Implementation）
// ============================================================================
Vector2 FindLocalMinimum(Vector2 originalPoint, float searchRadius, float stepScale, int stepLimit)
{
    float maxRadius = searchRadius + NODE_MAX_RADIUS;
    // 从空间 k-d 树高效检索所有可能对搜索域产生贡献的影响节点
    List<InfluenceNode> nodeList = FindNodesInRadius(originalPoint, maxRadius);
    
    Vector2 searchPoint = originalPoint;
    float minimum = InfluenceAtPoint(searchPoint, nodeList);

    for (int step = 0; step < stepLimit; ++step) 
    {
        // 若当前强度已归零，说明已脱离任何负面/危险影响区域，提前收敛退出
        if (minimum <= 0.0f) 
        {
            break;
        }

        // 计算当前点的解析梯度向量 ∇g(x, y)
        Vector2 gradientXY = GradientAtPoint(searchPoint, nodeList);

        // 沿负梯度方向执行迭代步进 (若为最大化求解，则改为沿正梯度方向累加)
        Vector2 newSearchPoint = searchPoint - (gradientXY * stepScale);

        // 边界约束验证：强制约束步进范围在指定半径内
        if (Vector2.Distance(newSearchPoint, originalPoint) > maxRadius) 
        {
            break;
        }

        // 重新采样步进后的综合场强值
        float newSum = InfluenceAtPoint(newSearchPoint, nodeList);

        // 局部最优更新与步进判定
        if (newSum < minimum) 
        {
            searchPoint = newSearchPoint;
            minimum = newSum;
        } 
        else 
        {
            // 若未能获得场强优化，可选择动态收缩 stepScale，或判定为已到达局部鞍点/极值点并收敛退出
            break;
        }
    }

    return searchPoint;
}
```

---

## 4. 三维立体空间扩展（3D Generalization）

传统基于 2D 网格（2D Grid）或导航网格（NavMesh）的影响图在应对重叠多层建筑、高低纵深立交桥、空中单位三维对抗时，往往面临维度灾难或难以拓扑化投影的瓶颈。

### 4.1 空间表象方案对比矩阵

| 特征维度 | 规则体素网格（3D Voxel Grid） | 导航网格多边形投影（NavMesh Polygons） | 无限分辨率无网格方案（Infinite-Resolution k-d Tree） |
| :--- | :--- | :--- | :--- |
| **内存占用** | $\mathcal{O}(W \cdot H \cdot D)$ 灾难性爆炸；极高 | $\mathcal{O}(N_{\mathrm{poly}})$，高度压缩但无法表达真正 3D 浮空场 | $\mathcal{O}(N_{\mathrm{sources}})$，空间稀疏自适应，无多余内存浪费 |
| **拓扑适应度** | 依赖粗暴体素剖分，有体素阶梯伪影 | 局限于行走表面，空中立体作战能力为零 | 完全独立于网格几何，原生支持任意 3D 自由连续空间 |
| **传播开销** | 3D 邻接遍历，计算随分辨率成立方倍增 | 依赖多边形拓扑边遍历，寻路面不规则时代价高昂 | 沿 3D 球形离散射线分裂，需结合积极的剪枝优化 |
| **分辨率精度** | 受体素尺寸限制，边界呈阶梯状锯齿 | 受三角化晶格划分精度硬性限制 | **理论无限分辨率**，全空间各向同性连续可微 |

### 4.2 3D 扩展关键技术实现

1. **三维 k-d 树（3D k-d Tree）**：
   超平面分割轴依次在 $X \to Y \to Z$ 三轴循环轮换（Split Axis Round-Robin），节点分割依据中位数算法保证平衡树结构。
2. **三维欧氏衰减与求导扩展**：
   三维空间影响贡献函数为：
   $$g(x, y, z) = \sum_{i} \max\left( s_i \left[ 1 - \frac{\sqrt{(x - x_i)^2 + (y - y_i)^2 + (z - z_i)^2}}{r_i} \right], 0 \right)$$
   其 $Z$ 轴解析偏导分量同构于 $X$、$Y$ 分量：
   $$\frac{\partial}{\partial z} f_i(x, y, z) = -\frac{s_i (z - z_i)}{r_i \sqrt{(x - x_i)^2 + (y - y_i)^2 + (z - z_i)^2}}$$
3. **稀疏几何下的作用半径膨胀替代策略**：
   在 3D 空间中，每次时间扩散若单纯派生子节点，节点数量将呈 $\mathcal{O}(r^3)$ 爆炸。对于空旷稀疏环境，可采用**半径膨胀（Radius Expansion）**机制取代分裂派生，即直接逐帧增大已有母节点的有效覆盖半径 $r_i$ 并按比例衰减标量幅值 $s_i$，从而彻底抑制节点暴增。

---

## 5. 工业级落地选型评估模型（Suitability Considerations）

无限分辨率影响图并不是常规网格方案的无条件上位替代，两者在计算密集度、内存模型与访问模式上存在根本差异。在工业级管线选型中，需综合评估以下四个维度：

```
                              [架构选型评估矩阵]
                                      │
            ┌─────────────────────────┴─────────────────────────┐
            ▼                                                   ▼
   [倾向于: 传统网格/体素影响图]                       [倾向于: 无限分辨率无网格影响图]
   - 高密度影响源集中聚集                             - 节点广域离散稀疏分布
   - 极高频局部空间密集采样 (如成百上千单位同时探查)   - 评估查询点稀疏分布、分布空间宽阔
   - 逐帧高频细微局部变动 (需要 O(1) 写入)           - 批量推演与更新模式 (Batch Update)
   - 强调确定性与绝对无损数值                         - 容忍微小统计误差，追求平滑解析梯度
```

### 5.1 选型评估四大支柱

#### 1. 影响源密集度（Influence Source Density）
* **瓶颈**：若数千至上万个影响源集中在极小重叠半径内，k-d 树的范围查询退化为 $\mathcal{O}(N)$ 暴力搜索，遍历与连续积分求和开销过大。
* **优势场景**：数万个影响源在空间上呈广域离散分布（Spatially Distributed）。此时 k-d 树拥有极高的空间剪枝率，绝大多数分支能被直接剔除。

#### 2. 查询点密集度与分布特征（Query Point Density）
* **瓶颈**：若大量 AI 代理集中在特定狭小区域高频发出极值优化查询，每一次梯度下降都涉及多步迭代与重复范围搜索。
* **权衡建议**：此时固定网格的 $\mathcal{O}(1)$ 内存查表机制具有压倒性吞吐优势。若查询点较为分散或仅有关键中枢战术层调用，无网格机制则能体现其免离散化的灵活性。

#### 3. 动态更新频率（Update Frequency）
* **瓶颈**：无网格方案中，每次完整的传播与精简流程之后都需要完全重建（Rebuild）或局部重平衡 k-d 树。
* **工程最佳实践**：必须采用**批处理管线（Batch Processing Pipeline）**。将更新周期固定为战术低频节拍（如 5–10 Hz），严禁在游戏单帧内针对单一节点频繁触发树结构重平衡。

#### 4. 精确度与确定性需求（Precision Constraints）
* **权衡判定**：无限分辨率无网格影响图在本质上是一种基于统计近似（Statistical Approximation）与空间重采样的启发式方法。其合并与精简机制属于有损压缩。对于要求场强严格确定、绝对无浮点统计漂移的场景，应优先选用刚性网格影响图。

---

## 6. 系统架构设计全景与参考文献

### 6.1 模块分层系统架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      Gameplay AI & Tactical Systems                      │
│     [行为树 (BT)]    [效用系统 (Utility)]    [分层任务网络 (HTN)]       │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ 空间推理与极值查询 (Min/Max Query)
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Continuous Spatial Reasoning & Optimization Layer           │
│  - Gradient Descent Solver (Cauchy 1847)                                │
│  - Partial Differentiation Vector Field (∂f/∂x, ∂f/∂y, ∂f/∂z)           │
│  - Continuous Spatial Sampler                                           │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ 批量重构 / 范围检索 (FindNodesInRadius)
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     Dynamic Spatial Index (k-d Tree)                    │
│  - k-Dimensional Hyperplane Partitioner (Bentley 1975)                  │
│  - Lossy Centroid Simplification Engine                                 │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ 能量传播 / 射线遮挡测试
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│              Obstacle-Aware Propagation & Physics Environment            │
│  - Photon Mapping Flow Analog (Jensen 2001)                             │
│  - Raycast Collision & Occlusion Query Provider                         │
│  - Kernel Density Estimator (Rosenblatt 1956)                           │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.2 参考文献与理论出处

* **[Bentley 75]** Bentley, J. L. 1975. *Multidimensional binary search trees used for associative searching*. Communications of the ACM 18(9): 509.
* **[Cauchy 47]** Cauchy, A. 1847. *Méthode générale pour la résolution des systèmes d’équations simultanées*. Compte Rendu des Séances de L’Académie des Sciences XXV, Vol. Série A 25: 536–538.
* **[Fortune 86]** Fortune, S. 1986. *A sweepline algorithm for Voronoi diagrams*. In Proceedings of the Second Annual Symposium on Computational Geometry, Yorktown Heights, NY, pp. 313–322.
* **[Jensen 01]** Jensen, H. W. 200
