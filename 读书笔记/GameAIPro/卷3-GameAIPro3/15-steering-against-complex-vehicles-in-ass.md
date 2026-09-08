---
type: Reference
title: "第15章 Steering against Complex Vehicles in Assassin’s Creed Syndicate"
description: "Game AI Pro 工业级精读：Steering against Complex Vehicles in Assassin’s Creed Syndicate。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第15章 Steering against Complex Vehicles in Assassin’s Creed Syndicate

> 来源：*Game AI Pro 3: Balanced Cuts of Game AI Professionals*, Chapter 15.  
> 原文作者 / 资源：[Steering against Complex Vehicles in Assassin’s Creed Syndicate](http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter15_Steering_against_Complex_Vehicles_in_Assassin’s_Creed_Syndicate.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷3-GameAIPro3](README.md) ｜ [专栏首页](../README.md)

---

> **研读文献**：*Game AI Pro 3: Balanced Cuts of Game AI Professionals*, Chapter 15: *Steering against Complex Vehicles in Assassin’s Creed Syndicate* (Eric Martel)

---

## 1. 系统概述与工业界技术背景 (Introduction & System Overview)

在现代 3A 开放世界游戏（如《刺客信条：枭雄》*Assassin’s Creed Syndicate*）中，城市生态的真实感对非玩家角色（Non-Player Characters, NPCs）的智能化提出了严苛要求。维多利亚时期的伦敦街头充斥着大量动态行进与停驻的马车（Horse-Drawn Carriages）。此类载具具有以下特征：
- 结构修长（Elongated Profile）；
- 铰接多刚体拓扑（Articulated Multi-Body Topology：包含车体与 0 到 2 匹马匹）；
- 高度动态性（动态变道、受惊、交火中马匹脱落死亡等）。

传统的基于半径（Radius-Based）的导向行为（Steering Behaviors）在面对细长型、非凸多刚体组合障碍物时产生严重失真。为此，育碧（Ubisoft）技术团队为类人 NPC（Humanoids）设计了一套**基于动态凸包生成（Dynamic Convex Hull Generation）的通用多边形障碍物局部避障架构**。该方案既避免了常规球体碰撞体的过度避让缺陷，又解决了多刚体组合缝隙引起的局部极小值抖动问题。

---

## 2. 空间表征挑战与设计权衡 (Spatial Representation & Trade-offs)

### 2.1 传统避障几何代理的缺陷

经典局部导向行为（Reynolds 1999, Buckland 2005）通常依赖简化的球体或胶囊体生成横向推力（Lateral Push Force）和制动推力（Braking Force）：

```
           [ 理想行进方向 ]
                 ▲
                 │
           ┌─────┴─────┐
      推力 │   Agent   │
      ◄────┤     ●     │
           └─────┬─────┘
                 │
                 ▼
             ( 避障圆 )
```

在引入狭长马车资产时，传统方法暴露了明显的表现瓶颈：

```
1. 经典单一包围圆 (Bounding Circle)      2. 单一包围椭圆 (Bounding Ellipse)
          . - ~ ~ ~ - .                           . - ~ ~ ~ - .
      . '               ' .                   . '               ' .
    /      ┌─────────┐      \               /      ┌─────────┐      \
   /       │ 马车厢  │       \             |       │ 马车厢  │       |
  |        └───┬─────┘        |            |       └───┬─────┘       |
  |            │              |            |           │             |
  |        ┌───┴─────┐        |            |       ┌───┴─────┐       |
   \       │  马匹   │       /             \       │  马匹   │       /
    \      └─────────┘      /               . '    └─────────┘    ' .
      . '               ' .                   ' - ~ ~ ~ ~ ~ - '
          ' - ~ ~ ~ - '
   [缺陷] 侧向过度避让，形成宽阔无人区     [缺陷] 沿长轴侧向冗余间隙依然严重

3. 多重重叠椭圆 (Multiple Ellipses)     4. 动态外接凸包 (Convex Hull) [本方案]
          . - ~ ~ - .                             ┌───────────────┐
        /  ┌───────┐  \                           │ ┌───────────┐ │
       |   │车  厢 │   |                          │ │  马车厢   │ │
        \  └───┬───┘  /                           │ └───┬───────┘ │
          . - ┼ ~ ~ - .                           │     │(缝隙桥接)│
        /  ┌───┴───┐  \                           │ ┌───┴───────┐ │
       |   │ 马 匹 │   |                          │ │   马匹    │ │
        \  └───────┘  /                           │ └───────────┘ │
          ' - ~ ~ - '                             └───────────────┘
   [缺陷] 凹陷缝隙形成局部极小值死锁       [优势] 无凹角、紧贴边缘、单障碍物平滑绕行
```

| 表征方案 | 几何拟合度 | 侧向安全冗余空间 | 局部极小值陷阱 (Local Minima) | 计算复杂度 | 综合评价 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **单外接圆 (Single Circle)** | 极低 | 极大（导致 NPC 严重绕远） | 无 | $\mathcal{O}(1)$ | 废弃（破坏街区真实感） |
| **单外接椭圆 (Single Ellipse)** | 中 | 较大（短轴仍无法贴合侧边） | 无 | $\mathcal{O}(1)$ | 废弃（侧向间距依然失真） |
| **多重局部椭圆 (Multiple Ellipses)** | 高 | 极小 | **严重**（马与车厢缝隙致使 NPC 振荡锁死） | $\mathcal{O}(k)$ | 废弃（易产生行为 Bug） |
| **动态统一凸包 (Convex Hull)** | **极高** | **完全由碰撞代理厚度决定** | **无**（完全消除凹陷区域） | $\mathcal{O}(V)$ | **工业落地最优解** |

---

## 3. 全局导航与局部避障拓扑协同 (Global Pathfinding vs. Local Steering)

为了支持大密度停泊与多车通行，系统在全局导航网格（NavMesh）与局部导向行为（Steering Behaviors）之间建立了分层协同机制。

### 3.1 导航网格时间分片动态修补 (Time-Sliced NavMesh Patching)

当载具阻断通行路径时，静态寻路模块必须拒绝该路径请求（Path Rejection）。频繁触发 NavMesh 重新多边形化（Re-triangulation）会导致 CPU 尖峰。系统采用**时间分片修补（Time-Slicing）**与**信号驱动状态机**协同管理：

```
       [载具运动中]
            │
            ▼ (载具启动信号)
    ┌────────────────┐
    │ 激活局部导向系统 │ ──> NavMesh 保持原状，由局部动态凸包处理高频避障
    └────────────────┘
            │
            ▼ (载具完全停止)
    ┌────────────────┐
    │ 加入修补队列   │ ──> 时间分片逐帧处理 NavMesh 切割与重新多边形化
    └────────────────┘
            │
            ▼ (NavMesh 动态切洞完成)
    ┌────────────────┐
    │ 注销局部导向系统 │ ──> 载具固化为静态障碍层，局部避障卸载开销
    └────────────────┘
```

```
[全局与局部拓扑协调状态转换]

       +------------------+                   OnVehicleMoved
       |  Veh_Stationary  | ------------------------------------------------+
       | (NavMesh Blocked)|                                                 |
       +------------------+                                                 |
                 ^                                                          v
                 | NavMesh Patch Finished                          +-----------------+
                 | (Deactivate Obstacle)                           |   Veh_Moving    |
                 |                                                 | (Steering Active|
       +--------------------+      Vehicle Stopped Completely      |  NavMesh Clear) |
       |  Veh_TimeSlicing   | <------------------------------------+-----------------+
       | (Queue Re-triang.) |
       +--------------------+
```

---

## 4. 2D 有向包围盒降维与代理膨胀 (Rigid Body Simplification & Minkowski Sum)

### 4.1 3D 刚体降维与 2D OOBB 提取

开放世界中的类人 NPC 无法脱离地面飞行，因此 3D 避障向量可以直接向导航平面（Top View 平面）投影。
1. 物理引擎提取车辆子部件的 3D 有向包围盒（3D Object-Oriented Bounding Box, 3D OOBB）。
2. 忽略子部件的翻滚角（Roll）与俯仰角（Pitch），仅保留偏航角（Yaw）。
3. 将底盘及马匹各部件的包围盒正交投影为 **2D OOBB**，极大压缩内存占用与几何求交开销。

### 4.2 闵可夫斯基和（Minkowski Sum）空间膨胀

为了省去每次针对 NPC 碰撞胶囊体半径的边界展开运算，系统在凸包生成前对底层的 2D OOBB 预先进行几何膨胀。

设 NPC 角色宽度的一半（即碰撞胶囊体半径）为 $R_{\text{agent}}$。对于 2D OOBB 的各半长轴向量 $\mathbf{e}_x, \mathbf{e}_y$，执行沿法向的外扩膨胀：

$$\mathcal{O}_{\text{expanded}} = \mathcal{O}_{\text{raw}} \oplus \mathcal{B}_{R_{\text{agent}}}$$

其中 $\mathcal{B}_{R}$ 为半径 $R$ 的球形结构元素。在工程实现中，直接将 OOBB 的半宽扩展 $R_{\text{agent}}$：

$$\mathbf{HalfSize}' = \mathbf{HalfSize} + \begin{bmatrix} R_{\text{agent}} \\ R_{\text{agent}} \end{bmatrix}$$

**工程收敛**：经此变换，NPC 在后续算法中被退化为**无半径质点（Single Point Mass）**，其避障测试直接转换为质点与膨胀凸多边形的包含与射线求交判定。

---

## 5. 多边形拓扑拓扑合并与轮廓构建 (Contour Construction)

```
        部件拓扑关系判定分支 (Topology Classification)
                          │
         ┌────────────────┼────────────────┐
         ▼                ▼                ▼
   [情况 A: 部分相交]  [情况 B: 相互离散]  [情况 C: 完全包含]
   Partial Overlap   Disconnection   Complete Overlap
         │                │                │
 沿外边界顺时针追踪   桥接双向最近邻边    剔除内嵌部件几何
 (Weiler-Atherton)    (Closest Edges)     (Fast Ingestion)
```

### 5.1 循环索引访问器 (Circular List Movement)

为保证顶点数组在任意正负偏移下平滑循环寻址，且消除运行时由于模运算性质不一致带来的越界隐患，设计了标准索引推进接口：

```cpp
/**
 * @brief 环形双向数组索引安全环绕计算
 * @param currentValue 当前顶点索引
 * @param increment    步进值 (可正可负)
 * @param listSize     轮廓数组总顶点数
 * @return 环绕规范化后的合法索引
 */
uint circularIncr(uint currentValue, int increment, uint listSize)
{
    return (currentValue + listSize + increment) % listSize;
}
```

### 5.2 离散刚体桥接策略 (Disconnection Merging)

当马车与脱节马匹之间出现刚体断开（Disconnection）时：
1. 判定所有边线互不相交，且所有顶点互不包含。
2. 搜索两几何部件间欧氏距离最近的一对边：$E_1(A, D)$ 与 $E_2(B, C)$。
3. 剔除边 $AD$ 与 $BC$，在顶点序列中建立双向缝合跳转。
4. 保证生成的大多边形保持顺时针（Clockwise, CW）环绕顺序。

### 5.3 相交多边形拓扑合并算法 (Weiler-Atherton 变体实现)

系统采用增量式单步吸收架构（Incremental Growth）：令主轮廓逐一与子刚体合并，复杂度受限于子部件总数（$n \le 3$）。

```
        V0(curr) ───────> I0 (交点入点) ───────> V1(alt)
                           │                        │
                           ▼ (Swap curr <-> alt)    │
                         V0(alt)                    ▼
                           │                       I1 (交点出点)
                           └──────────────────────> │
                                                    ▼ (Swap back)
```

```cpp
/**
 * @brief 增量式多边形轮廓顺时针追踪合并算法 (Incremental Contour Merge)
 */
// 初始化指针与容器
currVtx = newShapeToAdd;
altVtx = existingContour;

// 1. 查找第一个处于另一多边形外部的顶点作为安全起始点
firstOutsideIndex = firstOutsideVtx(currVtx, altVtx);
nextVtx = currVtx[firstOutsideIndex];
nextIdx = circularIncr(firstOutsideIndex, 1, currVtx.size());

mergedContour.push(nextVtx);

while (!looped)
{
    // 2. 收集线段 (nextVtx -> currVtx[nextIdx]) 与交替多边形 altVtx 的所有交点
    intersections = collectIntersections(nextVtx, currVtx[nextIdx], altVtx);

    if (intersections.empty())
    {
        // 无交点：继续沿当前多边形前行
        nextVtx = currVtx[nextIdx];
        nextIdx = circularIncr(nextIdx, 1, currVtx.size());
    }
    else
    {
        // 存在交点：按沿当前行进方向的距离选取最近交点
        intersectionIdx = findClosest(nextVtx, intersections);
        nextVtx = intersections[intersectionIdx];

        // 沿顺时针拓扑跳转，交点处保存了穿入对象的下一个目标顶点索引
        nextIdx = intersections[intersectionIdx].endIdx;

        // 切换追踪边界 (Swap current and alternate polygon)
        swap(currVtx, altVtx);
    }

    // 3. 回环判定：闭合多边形完成判定
    if (mergedContour[0].equalsWithEpsilon(nextVtx))
    {
        looped = true;
    }
    else
    {
        mergedContour.push(nextVtx);
    }
}
```

---

## 6. 凸包平坦化算法与数学证明 (Convex Hull Flattening)

合并出的综合多边形轮廓依然包含凹角（Concave Sections，如车身与马颈连接处产生的槽口），NPC 一旦落入此区域仍会陷入震荡。系统在生成的轮廓基础上，执行**反向外折角展开算法（Unfolding Algorithm）**。

### 6.1 凸角数学判定 (Z-Cross Product Criterion)

对于顺时针（Clockwise, CW）排列的 2D 平面多边形顶点序列，设当前被测试顶点为 $V_i$，其前驱邻接点为 $V_{i-1}$，后继邻接点为 $V_{i+1}$。

定义从中心点向两侧发出的向量：
$$\mathbf{v}_{\text{left}} = V_{i-1} - V_i$$
$$\mathbf{v}_{\text{right}} = V_{i+1} - V_i$$

其二维平面外积（叉乘标量结果，即三维外积的 $Z$ 轴分量）定义为：

$$Z_{\text{cross}}(\mathbf{v}_{\text{left}}, \mathbf{v}_{\text{right}}) = (\mathbf{v}_{\text{left}})_x \cdot (\mathbf{v}_{\text{right}})_y - (\mathbf{v}_{\text{left}})_y \cdot (\mathbf{v}_{\text{right}})_x$$

```
     Vi-1
       ^
        \  v_left
         \
          Vi ─────────> Vi+1
                 v_right

 顺时针排列下：
 1. 凸角（Interior Angle <= 180°）：v_left 旋转至 v_right 表现为顺时针旋转，Z_cross >= 0
 2. 凹角（Interior Angle >  180°）：局部产生内陷凹陷，Z_cross < 0 (必须剔除当前顶点 Vi)
```

### 6.2 剪枝回退迭代实现 (Pruning & Backstepping)

若 $V_i$ 被剔除，其两侧邻接点 $V_{i-1}$ 与 $V_{i+1}$ 将直接相连，可能形成新的凹角。因此算法必须强制**向后回退一个索引（Step Back）**重新校验。

```cpp
/**
 * @brief 基于几何外积符号剔除凹点生成绝对凸包 (Graham Scan-Style Inline Prune)
 */
convexHull = contour.copy();

// 自顶向下倒序遍历以安全处理动态删除
for (index = convexHull.size() - 1; index >= 0; --index)
{
    leftIndex  = circularIncr(index, -1, convexHull.size());
    rightIndex = circularIncr(index,  1, convexHull.size());

    goingLeft  = convexHull[leftIndex]  - convexHull[index];
    goingRight = convexHull[rightIndex] - convexHull[index];

    // 检测 2D 叉乘 Z 分量
    if (zCross(goingLeft, goingRight) < 0)
    {
        // 发现凹顶点，执行移除
        convexHull.removeAtIndex(index);

        // 核心回退逻辑：确保缝合后的新夹角得到二次校验
        index = min(convexHull.size() - 1, index + 1);
    }
}
```

```
[凹角剔除过程示意图]

      V[i-1]                               V[i-1]
        \                                    \
         \                                    \   (新边直连)
          \                                    \
           V[i] (凹点, Z_cross < 0)  ======>    \
          /                                      \
         /                                        \
        /                                          \
      V[i+1]                               V[i+1]
```

---

## 7. 动态避障几何测试管线 (Runtime Steering Pipeline)

完成凸包生成后，NPC 的局部避障模块进入逐帧执行管线。NPC 将凸包作为统一的宏观几何障碍体进行排斥力结算：

```
                    [ 逐帧避障输入 ]
             (NPC Position & Target Vector)
                          │
                          ▼
            [ 空间状态分类: 点在多边形内判定 ]
                          │
         ┌────────────────┴────────────────┐
         ▼                                 ▼
   [点在凸包内部]                    [点在凸包外部]
(NPC 或 Target 位于凸包内)         (两点均在安全区域)
         │                                 │
         ▼                                 ▼
  [可通行走廊判定]                  [射线与外边求交碰撞预测]
(处于 OOBB 与 Convex Hull 之间)             │
         │                        ┌────────┴────────┐
         ▼                        ▼                 ▼
   允许低速自由穿越         [预测将发生碰撞]   [航向安全无交点]
   (豁免强推斥力)                 │                 │
                                  ▼                 ▼
                          [计算横向推斥力]      [保持原航向]
                        (沿凸包最近外边界切线)
```

1. **内外拓扑定性 (Containment Test)**：
   - 凸包与内部各 2D OOBB 之间的中介空隙在物理世界中是实际可通行的（Walkable Area）。
   - 若 NPC 或其移动目标恰好落在该区域，避障系统豁免全局排斥，允许角色贴紧车体侧缘穿梭。
2. **碰撞前瞻预测 (Collision Prediction)**：
   - 若 NPC 与目标均在凸包外侧，系统使用 NPC 速度前瞻向量（Velocity Projection Ray）与凸包顺时针外边集合进行快速线段求交。
   - 一旦预判相交，系统沿着与当前碰撞边平行的外侧方向生成**侧向偏转力（Lateral Steering Force）**，并依据碰撞接触时间（Time To Impact, TTI）施加**线性制动力（Braking Deceleration）**，引导 NPC 顺滑掠过整个铰接车辆复合体。

---

## 8. 架构总结与设计模式映射

本章技术方案在游戏 AI 工程中展现了经典的架构分层与设计模式：

```
+-------------------------------------------------------------------------+
|                  NPC Decision Level: Behavior Trees                     |
+-------------------------------------------------------------------------+
                                    │
                                    ▼
+-------------------------------------------------------------------------+
|             Global Navigation Level: NavMesh (Time-Sliced)              |
+-------------------------------------------------------------------------+
                                    │
                         Signal System (State Sync)
                                    ▼
+-------------------------------------------------------------------------+
|             Local Steering Level: Articulated Vehicle Avoidance         |
|                                                                         |
|   Physics Engine          Minkowski Sum              Polygon Topology   |
|  [3D OOBB Extraction] -> [Agent Expansion] -> [Weiler-Atherton Merge]   |
|                                                              │          |
|                                                              ▼          |
|  Dynamic Steering Push <- Ray Intersection <- [Convex Hull Prune]       |
+-------------------------------------------------------------------------+
```

1. **表示与执行解耦模式（Representation-Execution Decoupling）**：物理引擎的底层表示（3D 铰接多刚体）与 AI 避障表示（2D 动态膨胀凸包）高度解耦，降低了局部决策的数学与时间复杂度。
2. **空间代理聚合模式（Spatial Proxy Aggregation）**：利用凸包包裹动态组合对象，彻底抹除子部件之间的几何凹陷与缝隙，从源头上消除了多刚体局部极小值抖动（Local Minima Chattering）问题。
3. **分层时间尺度隔离模式（Hierarchical Time-Scale Decoupling）**：以局部导向（微秒级响应）平滑掩盖全局导航网格多边形重构（跨帧时间分片）的时延，保障了工业级 3A 开放世界在密集交互下的高帧率与鲁棒性。

---

---

## 1. 架构总览与避障系统拓扑 (Architecture Overview & System Topology)

在 AAA 开放世界游戏（如《刺客信条：枭雄》/ *Assassin's Creed Syndicate*）中，城市街道充斥着形态复杂、多节铰接的运动载具（马车、列车等）。传统的导向行为（Steering Behaviors）多采用前向探针系统（Feeler/Raycast System）配合球体或胶囊体包围盒进行碰撞预测。然而，面对非凸多边形（Non-convex/Concave Polygons）、动态改变构型以及具有内部交互点（如 NPC 攀爬、劫持马车）的复杂刚体时，传统射线检测极易出现穿模、拐角死锁或剧烈振荡。

本技术体系引入基于**多边形轮廓凸包化（Convex Hull Approximation）**、**物理宽相并行剪枝（Broad-phase Physics Parallel Pruning）**与**局部拓扑导航（Local NavMesh Triangulation）**的混合移动规划架构。

### 1.1 系统拓扑架构图

```
+-----------------------------------------------------------------------------+
|                          Gameplay AI Decision Layer                         |
|    [Behavior Trees] / [Utility Systems] / [Hierarchical Task Network (HTN)] |
+---------------------------------------+-------------------------------------+
                                        | Movement Intent & Goals
                                        v
+-----------------------------------------------------------------------------+
|               Dynamic Spatial Pipeline (Broad-phase & Query)                |
|  +-------------------------------------+  +-------------------------------+ |
|  | Multi-threaded Physics Broad-phase  |  | Lazy Evaluation & Hull Cache  | |
|  |     AABB-AABB Collision Pairs       |  | Concave Contour -> Convex Hull| |
|  +------------------+------------------+  +---------------+---------------+ |
+---------------------|-------------------------------------|-----------------+
                      | Potential Obstacle Pairs            | Convex Hull Vertices
                      v                                     v
+-----------------------------------------------------------------------------+
|            Steering & Avoidance Engine (Narrow-phase Reasoning)             |
|                                                                             |
|   1. Point-in-Polygon Query (Ray Casting / Even-Odd Rule)                   |
|   2. Clearance Segment Analysis (Radius Expansion / Left-Right Offset)      |
|   3. State Space Segmentation:                                              |
|      +------------------------+-------------------------+                   |
|      | Outside-to-Outside     | Outside-to-Inside       |                   |
|      | (Contour Exploration)  | (Closest Entry Edge)    |                   |
|      +------------------------+-------------------------+                   |
|      | Inside-to-Outside      | Inside Cavity Local Nav |                   |
|      | (Time-Reversed Exit)   | (Triangulated NavMesh)  |                   |
|      +------------------------+-------------------------+                   |
+---------------------------------------+-------------------------------------+
                                        | Desired Steering Force / Heading
                                        v
+-----------------------------------------------------------------------------+
|                       Locomotion & Motor Execution                          |
|             Velocity Prediction, Kinematic Blending & Braking               |
+-----------------------------------------------------------------------------+
```

---

## 2. 障碍物检测与宽相空间推理 (Obstacle Detection & Broad-phase Spatial Reasoning)

### 2.1 基于物理引擎 AABB 的并行剪枝

绝大多数导向行为采用基于 NPC 局部的射线探测系统（Feeler System），在大量 NPC 并发计算时会导致严重的时钟周期浪费与缓存不命中（Cache Misses）。

本架构将探测责任下放至底层物理引擎（Physics Engine）：
1. **并行宽相测试（Parallel Broad-phase）**：为所有动态载具与 NPC 注册并更新轴对齐包围盒（Axis-Aligned Bounding Boxes, AABB）。利用物理系统的 Sweep and Prune (SAP) 或层次包围盒（Bounding Volume Hierarchy, BVH）在多线程任务池中并行计算相交对（Intersecting Pairs）。
2. **玩法层直接解耦**：玩法逻辑代码（Gameplay Code）仅针对通过宽相碰撞测试的候选实体对进行细相（Narrow-phase）几何推理，大幅压缩每帧需要进行几何判定的实体基数。

### 2.2 点在任意多边形内外部检测算法推导

在确认潜在碰撞后，系统必须判断 NPC 当前位置与目标位置（Interaction Point / Destination）是否位于载具多边形轮廓内部。本系统利用**奇偶规则（Even-Odd Rule / Jordan Curve Theorem）**的射线交叉数判定。

#### 2.2.1 数学推导与原理

令二维平面多边形轮廓由顺时针排布的闭合顶点环构成：
$$\mathcal{P} = \{ \mathbf{v}_0, \mathbf{v}_1, \dots, \mathbf{v}_{n-1} \}, \quad \mathbf{v}_n = \mathbf{v}_0$$

从待测试点 $\mathbf{P} = (P_x, P_y)$ 发射一条沿 $+X$ 轴正方向的水平半无限射线 $\mathcal{R}(t)$：
$$\mathcal{R}(t) = \mathbf{P} + t \begin{pmatrix} 1 \\ 0 \end{pmatrix} = \begin{pmatrix} P_x + t \\ P_y \end{pmatrix}, \quad t \ge 0$$

取射线右边界截断点 $\mathbf{P}_{\text{ext}} = (X_{\max} + \epsilon, P_y)$，其中：
$$X_{\max} = \max_{0 \le i < n} \{ v_{i,x} \}, \quad \epsilon > 0$$

对于多边形的每一条边线段 $S_i = \overline{\mathbf{v}_i \mathbf{v}_{i+1}}$，计算射线与线段的交点集合 $\mathcal{I}$：
$$\mathcal{I} = \{ S_i \cap \mathcal{R} \mid i \in [0, n-1] \}$$

- 若 $|\mathcal{I}| \pmod 2 = 1$（奇数次相交），则点 $\mathbf{P}$ 严格位于多边形 $\mathcal{P}$ 内部；
- 若 $|\mathcal{I}| \pmod 2 = 0$（偶数次相交），则点 $\mathbf{P}$ 严格位于多边形 $\mathcal{P}$ 外部。

#### 2.2.2 生产环境伪代码实现

```cpp
// Listing 15.4: 校验点是否位于任意多边形内部的伪代码实现
// 保证 testedXValue 严格超越多边形 AABB 边界，设置安全偏移量 epsilon = 5
float testedXValue = findMaxXValue(vertices) + 5.0f;

// 沿水平方向发射水平测试线段，收集与所有边的交点数
std::vector<Vector2> intersections = collectIntersections(
    point.x,
    point.y,
    testedXValue,
    point.y,
    vertices
);

// 奇偶校验判定：奇数为内部 (1)，偶数为外部 (0)
return intersections.size() % 2;
```

---

## 3. 动态凸包避障机理与空间拓扑状态机 (Movement around & within Convex Hull)

复杂马车系统常具备非凸凹腔（Concave Pockets），直接基于凹几何导航会导致 NPC 陷入死局。系统对车辆外形构建**虚拟凸包外壳（Virtual Convex Hull Shell）**。

### 3.1 空间相对位置状态分类矩阵

```
                       +-----------------------------------+
                       |    NPC Current Position (Start)   |
                       +-----------------+-----------------+
                                         |
                 +-----------------------+-----------------------+
                 | Outside Hull                                  | Inside Hull
                 v                                               v
+---------------------------------+             +---------------------------------+
| Destination Position            |             | Destination Position            |
+----------------+----------------+             +----------------+----------------+
| Outside Hull   | Inside Hull    |             | Outside Hull   | Inside Hull    |
| (Section 15.4.2| (Section 15.4.3|             | (Section 15.4.4| (Local NavMesh |
|  Case 1)       |  Case 2)       |             |  Case 3)       |  Triangulation)|
+----------------+----------------+             +----------------+----------------+
```

---

### 3.2 场景一：起点与目标点均在凸包外部 (Outside to Outside)

当 $\mathbf{P}_{\text{start}} \notin \mathcal{C}$ 且 $\mathbf{P}_{\text{end}} \notin \mathcal{C}$（$\mathcal{C}$ 为凸包点集）：

#### 3.2.1 穿透检测与代理半径膨胀 (Clearance Offset)
在 $\mathbf{P}_{\text{start}}$ 与 $\mathbf{P}_{\text{end}}$ 之间构造位移线段 $L$。
1. **统一膨胀模型**：将凸包 $\mathcal{C}$ 沿各边外法线向外平移代理人半径 $R_{\text{agent}}$（即 Minkowski Sum 思想）：
   $$\mathcal{C}' = \mathcal{C} \oplus \mathcal{B}(R_{\text{agent}})$$
   若线段 $L \cap \mathcal{C}' = \emptyset$，则直线通行无阻。
2. **非对称/异质代理优化**：当场景内 NPC 半径各异时，重新生成全局 Minkowski 和成本过高。系统改为将测试线段 $L$ 分别向 NPC 的左右局部基向量（Right Vector / Left Vector）偏移 $R_{\text{agent}}$：
   $$L_{\text{offset}} = L \pm (R_{\text{agent}} \cdot \hat{\mathbf{n}}_{\perp})$$
   直接以偏置线段与原凸包 $\mathcal{C}$ 求交。

#### 3.2.2 绕行转向角极小化推导 (Minimize Heading Deflection)
若检测到碰撞边 $E_k = \overline{\mathbf{v}_k \mathbf{v}_{k+1}}$，系统提取交点两端顶点。因凸包顶点严格按顺时针（Clockwise, CW）存储：
- 向右绕行探索：顶点索引递减 $k, k-1, k-2, \dots$
- 向左绕行探索：顶点索引递增 $k+1, k+2, k+3, \dots$

系统目标是寻找**可视且偏航角变化最小**的最远凸包顶点 $\mathbf{v}^*$：

$$\mathbf{v}^* = \arg\min_{\mathbf{v}_j \in \mathcal{V}_{\text{visible}}} \arccos\left( \frac{\mathbf{v}_j - \mathbf{P}_{\text{start}}}{\|\mathbf{v}_j - \mathbf{P}_{\text{start}}\|} \cdot \frac{\mathbf{P}_{\text{end}} - \mathbf{P}_{\text{start}}}{\|\mathbf{P}_{\text{end}} - \mathbf{P}_{\text{start}}\|} \right)$$

其中可见性约束条件为：
$$\overline{\mathbf{P}_{\text{start}} \mathbf{v}_j} \cap \text{Edges}(\mathcal{C}) = \emptyset$$

```
                                 v_top
                                   /\
                                  /  \
                                 /    \
                 v_end          /      \
                   *-----------*        \
                  /             \        \
                 /  Convex Hull  \        \
                /                 \        \
  Start        /                   \        * v_target
    o--------->*--------------------+-----> Destination (Intended Heading)
      \      v_intersect             \
       \                              \
        \                              \
         +----------------------------->* v_best (Minimized Angle Difference)
```

若相邻顶点的连线与凸包其它边相交，或偏离目标朝向角过大，则停止探索。对应文献中 Figure 15.10 的情形，线段的终止端点即为最佳引导锚点。

---

### 3.3 场景二：目标点位于凸包内部 (Movement into the Convex Hull)

在交互玩法中，NPC 需跳上运行的马车平台或抢占驾驶座，目标点 $\mathbf{P}_{\text{end}}$ 位于虚拟凸包内部。

#### 3.3.1 直线连通性判定
虚拟凸包并非物理阻挡，而是引导外壳。若线段 $\overline{\mathbf{P}_{\text{start}} \mathbf{P}_{\text{end}}}$ 与载具真实凹轮廓（Contour $\mathcal{P}_{\text{concave}}$）**无交点**，NPC 直接直线移动进入（Figure 15.11）。

```
+-------------------------------------------------------------+
| Virtual Convex Hull Boundary (Dotted)                       |
|        . - - - - - - - - - - - - - - - - - - .              |
|      '                                         '            |
|     '      +-------------------+                '           |
|    '       | Real Vehicle Hull |   Destination   '          |
|   '        |   (Contour)       |        x         '         |
|  '         +-------------------+        ^          '        |
|  '                                      |           '       |
|   '                                     |          '        |
|    '                                    |         '         |
|      . - - - - - - - - - - - - - - - - -|- - - - .          |
+-----------------------------------------|-------------------+
                                          |
                                    Start o (No intersection with contour -> Straight walk)
```

#### 3.3.2 最近入口边与拓扑测地距离积分
若线段与真实轮廓发生碰撞，NPC 必须绕行进入凹腔内部：
1. **寻找凸包入口边（Entry Edge）**：
   在凸包 $\mathcal{C}$ 上检索与 $\mathbf{P}_{\text{end}}$ 欧几里得距离最近的边 $E_{\text{entry}} = \overline{\mathbf{v}_m \mathbf{v}_{m+1}}$：
   $$E_{\text{entry}} = \arg\min_{E \in \text{Edges}(\mathcal{C})} \text{dist}(\mathbf{P}_{\text{end}}, E)$$
2. **双向沿边测地拓扑积分（Geodesic Hull Iteration）**：
   从碰撞点所在边开始，沿顺时针与逆时针两个方向，分别向入口顶点 $(\mathbf{v}_m, \mathbf{v}_{m+1})$ 迭代累加边缘弧长：
   $$D_{\text{CW}} = \sum_{j=k}^{m-1} \|\mathbf{v}_{j+1} - \mathbf{v}_j\|, \quad D_{\text{CCW}} = \sum_{j=m}^{k-1} \|\mathbf{v}_{j+1} - \mathbf{v}_j\|$$
   选取无穿透碰撞且距离权值最小的最优路径顶点作为局部 Steering 目标。

#### 3.3.3 凹腔死区（Concave Cavity）的航行解法
当进入凸包与真实多边形之间的凹陷区域时，直视目标可能被车厢结构遮蔽。文献提出了两种工程解法：
- **沿壁行走启发式（Wall Hugging）**：NPC 贴紧载具外部轮廓，沿凹多边形相邻边缘连续机动，直至与目标点重新建立无障碍直视线段（Line of Sight, LoS）。
- **空间局部三角网格化（Local Triangulation）**：详见第 4 节。

---

### 3.4 场景三：起点位于凸包内部 (Movement Out of the Convex Hull)

当 NPC 位于凹腔内试图离开（例如从运行中的马车撤退、跳车逃脱）时（Figure 15.12）：

```
+-------------------------------------------------------------+
| Convex Hull Boundary                                        |
|        .-------------------------------------.              |
|       /                                       \             |
|      /     +---------------+                   \            |
|     /      |    Contour    |   Start            \           |
|    /       |  (Concave)    |     o               \          |
|   /        +---------------+      \               \         |
|  /                                 \ Step 1: Exit  \        |
| /                                   v Cavity        \       |
| \ - - - - - - - - - - - - - - - - - -* - - - - - - - /      |
+---------------------------------------\---------------------+
                                         \ Step 2: Outside steering
                                          v Destination
```

1. **几何时间反演推导（Time-Reversed Operations）**：
   将 3.3 节算法执行时间反演（Reversal）：将起点设为目标点，目标点设为起点逆向寻路，反转生成的路点路径向量。
2. **两阶段逃逸机制（Two-Stage Escape Pipeline）**：
   - **Phase 1**：在凹多边形与凸包间隙内，引导 NPC 穿过凸包边界外壳（虚拟多边形差集区域 $\mathcal{C} \setminus \mathcal{P}$）；
   - **Phase 2**：一旦 NPC 的空间坐标脱离凸包，系统无缝降级至 3.2 节（起点目标均在外）或 3.3 节常规避障逻辑。

---

## 4. 算法进阶：凸包剖分局部导航网格与动态预测 (Pushing It Further)

### 4.1 凸包剥离三角化与局部导航网格 (Triangulation from Convex Hull Data)

在从凹多边形计算凸包的过程中（常用算法如 Graham Scan 或 Melkman 算法），每当检测到一个凹陷拐角凹点时，算法会弹出（Pop）该非凸顶点以维持凸性。

#### 4.1.1 伴生三角化构造机理 (Accompanying Triangulation)

在执行顶点出栈时，被剔除的顶点 $\mathbf{v}_{\text{concave}}$ 与凸包当前构建边 $(\mathbf{v}_a, \mathbf{v}_b)$ 构成局部三角面片 $\Delta(\mathbf{v}_a, \mathbf{v}_{\text{concave}}, \mathbf{v}_b)$。
将所有弹出的拓扑面片直接压入网格缓冲区，无需执行昂贵的 Delaunay 三角化后处理，即可获得载具周围非凸间隙的**伴生局部导航网格（Local Navigation Mesh）**（Figure 15.13）。

```
           v_a
            +----------------------------------+ v_b (Convex Edge)
             \                                /
              \                              /
               \      Generated Local       /
                \      Triangle Face       /
                 \     (NavMesh Node)     /
                  \                      /
                   \                    /
                    +------------------+
                        v_concave
                 (Popped Contour Vertex)
```

#### 4.1.2 局部 A* 寻路求解
在生成的局部 NavMesh 上运行轻量级 A* 或贪心通道走廊算法（Funnel Algorithm），完美统一了：
- 凹腔内部死锁预防；
- 进入/离开凸包的高阶连续平滑移动路径（Continuous Curvature Trajectories）；
- 消除离散壁面贴靠带来的朝向抽搐问题。

---

### 4.2 高阶运动学预测与人群博弈行为学 (Velocity Prediction & Human Mimicking)

对于具有复杂拓扑结构的活动实体（如大型活动载具甚至多节机械长蛇），单纯考虑静态几何轮廓是不够的。

#### 4.2.1 速度场建模与复杂度权衡

$$V(t + \Delta t) = \mathbf{x}_{\text{obstacle}}(t) + \mathbf{v}_{\text{obs}}\Delta t + \frac{1}{2}\mathbf{a}_{\text{obs}}\Delta t^2$$

- **精细子部件级预测（Subpart Velocity Modeling）**：对铰接结构的每一节物理部件（例如铰链连接的车厢、车轮、马匹躯干）独立积分运动方程并预测位置。由于各子部件的自由度约束求解极为沉重，在海量 NPC 环境下运算开销过大。
- **刚体主速度简化模型（Rigid Body Velocity Approximation）**：工程实践证明，仅对障碍物主刚体中心维持一个统一线速度 $\mathbf{v}_{\text{obs}}$ 与角速度 $\boldsymbol{\omega}_{\text{obs}}$，即可满足 95% 以上的游戏交互拟真度。

#### 4.2.2 人群博弈运动模式分类 (Behavioral Modes)

系统通过 NPC 与载具前向速度向量的点积标量，建立拟人化决策分支：

| 相对运动拓扑关系 | 向量内积数学准则 | AI 决策意图与机动控制 |
| :--- | :--- | :--- |
| **同向行进 (Roughly Same Direction)** | $\hat{\mathbf{v}}_{\text{agent}} \cdot \hat{\mathbf{v}}_{\text{obs}} > \cos(45^\circ)$ | 协同变道，微调目标凸包顶点，弱化制动阻尼 |
| **反向对冲 (Opposite Direction)** | $\hat{\mathbf{v}}_{\text{agent}} \cdot \hat{\mathbf{v}}_{\text{obs}} < -\cos(45^\circ)$ | 极度警惕，侧向绕行至载具尾后，增大减速制动力度 |
| **垂直横截 (Perpendicular Crossing)** | $|\hat{\mathbf{v}}_{\text{agent}} \cdot \hat{\mathbf{v}}_{\text{obs}}| \le \cos(45^\circ)$ | **人类拟真行为（Human Mimicking）**：倾向于减速从**载具尾端（Rear Hull）**穿越，避免抢头 |

制动系数（Braking Factor）动态调制方程：
$$k_{\text{brake}} = f\left(\|\mathbf{v}_{\text{obs}}\|, \|\mathbf{v}_{\text{agent}}\|, \angle(\hat{\mathbf{v}}_{\text{agent}}, \hat{\mathbf{v}}_{\text{obs}})\right) \in [0.0, 1.0]$$

---

## 5. 工程实现与多代理共享缓存模式 (Engineering Implementation & Cache Patterns)

在开放世界高密度人流（High Density Crowds）中，多名 NPC 可能在同一帧同时规避同一辆马车。若每个 NPC 独立对障碍物执行一次凸包多边形重构，会导致大量重复的数学运算与带宽浪费。

### 5.1 惰性求值与共享多边形缓存 (Lazy Evaluation & Hull Cache)

系统采用**单例/子系统级障碍物几何缓存器（Obstacle Polygon Cache）**：
1. **Dirty Flag 机制**：载具刚体仅在发生形变、关节旋转偏移超阈值或时间步跨越时，标记其凸包数据为 Dirty。
2. **惰性求值（Lazy Evaluation）**：首个需要与该载具进行避障测试的 NPC 触发凸包生成，并将计算结果（凸包顶点链表、伴生三角剖分网格、AABB）存入每帧共享缓存区。
3. **多代理无锁共享（Read-Only Multi-Agent Access）**：同一 Tick 内后续所有的 NPC 仅需引用此已计算好的几何缓冲数据，空间复杂度为 $\mathcal{O}(M)$（$M$ 为障碍物数量），时间复杂度降至近乎纯查询级别。

### 5.2 核心避障推理执行流水线 (C++ 核心算法原型)

```cpp
#include <vector>
#include <cmath>
#include <algorithm>
#include <cfloat>

struct Vector2 {
    float x, y;
    Vector2 operator-(const Vector2& o) const { return {x - o.x, y - o.y}; }
    Vector2 operator+(const Vector2& o) const { return {x + o.x, y + o.y}; }
    float Dot(const Vector2& o) const { return x * o.x + y * o.y; }
    float Length() const { return std::sqrt(x * x + y * y); }
    Vector2 Normalized() const { float len = Length(); return (len > 1e-5f) ? Vector2{x/len, y/len} : Vector2{0, 0}; }
};

struct ConvexHull {
    std::vector<Vector2> vertices; // 顺时针严格排序
};

class VehicleAvoidanceResolver {
public:
    // 计算最优规避引导方向
    static Vector2 ResolveAvoidanceHeading(
        const Vector2& startPos,
        const Vector2& targetPos,
        const ConvexHull& hull,
        float agentRadius)
    {
        Vector2 intendedDir = (targetPos - startPos).Normalized();
        
        // 1. 穿透求交检测
        int intersectedEdgeIndex = -1;
        float minIntersectParam = FLT_MAX;
        
        const size_t numVertices = hull.vertices.size();
        for (size_t i = 0; i < numVertices; ++i) {
            size_t nextIdx = (i + 1) % numVertices;
            Vector2 edgeStart = hull.vertices[i];
            Vector2 edgeEnd = hull.vertices[nextIdx];
            
            // 考虑 agentRadius 偏移的线段求交判定
            float t = SegmentIntersection(startPos, targetPos, edgeStart, edgeEnd);
            if (t >= 0.0f && t < minIntersectParam) {
                minIntersectParam = t;
                intersectedEdgeIndex = static_cast<int>(i);
            }
        }

        // 无碰撞：保持原始朝向
        if (intersectedEdgeIndex == -1) {
            return intendedDir;
        }

        // 2. 发生碰撞：沿顺时针和逆时针拓扑邻居展开双向探索
        // 依据顺时针性质：索引递减向右，索引递增向左
        int bestVertexIdx = -1;
        float maxDotProduct = -1.0f; // 极小化偏航角 -> 极大化朝向点积

        // 沿相交边端点分别向两端发散搜索最优可见端点
        int startEndIndex[2] = { intersectedEdgeIndex, static_cast<int>((intersectedEdgeIndex + 1) % numVertices) };

        for (int dir = 0; dir < 2; ++dir) {
            int step = (dir == 0) ? -1 : 1;
            int currIdx = startEndIndex[dir];

            for (size_t stepCount = 0; stepCount < numVertices / 2 + 1; ++stepCount) {
                Vector2 candPos = hull.vertices[currIdx];
                Vector2 toCand = (candPos - startPos).Normalized();

                // 检查候选顶点连线是否与凸包其他边自相交（视线可见性）
                if (IsLineOfSightClear(startPos, candPos, hull, currIdx)) {
                    float dot = toCand.Dot(intendedDir);
                    if (dot > maxDotProduct) {
                        maxDotProduct = dot;
                        bestVertexIdx = currIdx;
                    }
                } else {
                    // 一旦出现自交遮挡，单调性破坏，终止此方向探索
                    break;
                }
                currIdx = (currIdx + step + static_cast<int>(numVertices)) % static_cast<int>(numVertices);
            }
        }

        if (bestVertexIdx != -1) {
            return (hull.vertices[bestVertexIdx] - startPos).Normalized();
        }

        return intendedDir;
    }

private:
    static float SegmentIntersection(const Vector2& p1, const Vector2& p2, const Vector2& q1, const Vector2& q2) {
        Vector2 r = p2 - p1;
        Vector2 s = q2 - q1;
        float rxs = r.x * s.y - r.y * s.x;
        if (std::abs(rxs) < 1e-6f) return -1.0f; // 平行或共线

        Vector2 qp = q1 - p1;
        float t = (qp.x * s.y - qp.y * s.x) / rxs;
        float u = (qp.x * r.y - qp.y * r.x) / rxs;

        if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
            return t;
        }
        return -1.0f;
    }

    static bool IsLineOfSightClear(const Vector2& from, const Vector2& to, const ConvexHull& hull, int ignoreVertexIdx) {
        const size_t numVertices = hull.vertices.size();
        for (size_t i = 0; i < numVertices; ++i) {
            size_t nextIdx = (i + 1) % numVertices;
            if (static_cast<int>(i) == ignoreVertexIdx || static_cast<int>(nextIdx) == ignoreVertexIdx) {
                continue; // 忽略连接到自身的边
            }
            if (SegmentIntersection(from, to, hull.vertices[i], hull.vertices[nextIdx]) >= 0.0f) {
                return false;
            }
        }
        return true;
    }
};
```

---

## 6. 核心架构总结与技术边界分析 (Architectural Conclusion)

将复杂任意刚体、凹几何障碍物实时转化为**动态凸包外壳（Dynamic Convex Hull）**配合**局部三角网格剖分**，构成了《刺客信条：枭雄》工业级移动规划系统的核心基石。

### 6.1 方案收益与技术优势对比

| 评估维度 | 传统射线 Feeler 系统 | 预烘焙全景 NavMesh 动态挖洞 (Carving) | 本文方案：凸包外壳与伴生三角化 |
| :--- | :--- | :--- | :--- |
| **大载具边缘平滑度** | 频繁抖动，多探针易陷入凹角死区 | 高度依赖网格切片分辨率，边缘锯齿严重 | **极高**；严格遵循多边形外缘切线滑行 |
| **运行时 CPU 开销** | 密集射线开销随 NPC 数量线性发散 | 动态拓扑切割（NavMesh Cut）引发严重重度卡顿 | **极低**；单障碍物单帧仅需一次惰性求值与共享缓存 |
| **凹腔交互可行性** | 无法支持（被探针完全视为阻挡体） | 需动态重建镂空多边形，开销巨大 | **原生支持**；双向时间反演拓扑与局部三角网格完美接管 |
| **通用扩展能力** | 仅适应球形/胶囊体近似物体 | 限制在 NavMesh 上表面二维移动 | **通用性强**；从马车、列车到复杂可破坏物（Destructibles）均可复用 |

### 6.2 工业级设计准则

1. **几何降维表达与快速求交**：在细相检测前，充分利用物理引擎并行宽相消除 90% 以上无效碰撞对，细相计算中通过极值偏移水平射线实现高精度的点内外拓扑判定。
2. **伴生计算的零冗余架构**：在从原始轮廓剥离顶点生成凸包的同时，直接将废弃凹拐角保存为导航三角形网格，使单次几何运算同时解决“宏观规避”与“微观腔体寻路”两类问题。
3. **拟人化博弈与运动学融合**：避免绝对刚硬的几何反射，通过主刚体速度方向建模行人“避头穿尾”的博弈行为，将导向力平滑注入运动执行机构，呈现出拟真度极高的次时代开放世界城市人流生态。
