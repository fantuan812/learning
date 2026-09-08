---
type: Reference
title: "第21章 Techniques for Formation Movement using Steering Circles"
description: "Game AI Pro 工业级精读：Techniques for Formation Movement using Steering Circles。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
tags:
  - game-ai
  - game-ai-pro
  - decision-making
  - navmesh
  - architecture
  - state-machines
status: stable
verified: []
maturity: L2
updated: 2026-09-07
---

# 第21章 Techniques for Formation Movement using Steering Circles

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 21.  
> 原文作者 / 资源：[Techniques for Formation Movement using Steering Circles](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter21_Techniques_for_Formation_Movement_Using_Steering_Circles.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

> **作者**：Stephen Bjore  
> **出处**：《Game AI Pro: Collected Wisdom of Game AI Professionals》第 21 章  
> **核心领域**：运动控制（Movement）、路径规划（Pathfinding）、多智能体编队控制（Formation Control）

---

## 1. 概述与核心命题（Introduction）

在开阔地形（Open Terrain）中驱动群体移动在工程实现上相对简单，但在工业级游戏（如即时战略 RTS、战术射击或大规模模拟游戏）中，极具挑战性的核心问题是：**如何在编队具备最小转弯半径（Turn Radius）物理约束的前提下，生成一条平滑轨迹，使得编队不仅精确抵达目标空间位置，而且严格对齐目标朝向（Orientation）**。

传统刚性编队（Rigid Formations）在拐弯或掉头时常出现严重的抖动（Jittering）、局部穿模、旋转角速度超限甚至智能体失控回旋的现象。本方案拓展了 Relic 经典战术游戏《英雄连》（*Company of Heroes*）在 GDC 2007 上提出的转向圆思想（Steering Circles，Jurney et al. 2007），将编队高阶转向问题解耦为两大核心阶段：

1. **宏观全局几何路径生成（Generate the Path）**：基于始末位置与朝向，通过线性代数与平面三角学，构造带曲率约束的几何无缝切线路径（非编队独占，亦可广泛应用于刚体载具、单体巡航 AI 等）；
2. **微观编队沿路径流动导向（Navigate the Formation along the Path）**：在宏观路径引导下，维持微观多智能体拓扑动态形态（Dynamic Formation Topology），提供面向列队或排队特性的弹性跟随算法。

```
+-------------------------------------------------------------------------+
|                          输入参数与物理运动约束                           |
|  {formation.pos, formation.dir, target.pos, target.dir, Turn Radius r}  |
+-------------------------------------------------------------------------+
                                    │
                                    ▼
+-------------------------------------------------------------------------+
|                  阶段一：基于转向圆生成平滑切线路径                      |
|  1. 始末圆心判定 (c1, c2) & 重叠逆转分支 (d < 2r)                        |
|  2. 拓扑分支判定 (异侧切线 Transverse / 同侧外公切线 Direct)            |
|  3. 离散化圆弧-直线-圆弧路径点序列 (Discretized Waypoints)              |
+-------------------------------------------------------------------------+
                                    │
                                    ▼
+-------------------------------------------------------------------------+
|                  阶段二：微观编队拓扑动态导航 (Navigation)               |
|  - 解耦“编队槽位目标（Slot Target）”与“实体寻路（Entity Pathfinding）”    |
|  - 模式 A：纵列模式 (Column Formation) -> 追求列内平滑流式连接           |
|  - 模式 B：横排模式 (Band Formation)   -> 追求转弯时严格横排对齐         |
+-------------------------------------------------------------------------+
```

---

## 2. 宏观几何路径生成（Generate the Path）

路径规划阶段的目标是构建一条由**“起点圆弧 $\to$ 公共切线段 $\to$ 终点圆弧”**构成的平滑连续复合轨迹。该模型本质上是连续运动学中杜宾斯路径（Dubins Path）的工程化实现，但通过投影向量法与显式法向量分类大幅削减了暴力三角求根的运算开销。

### 2.1 输入参数与代数定义

系统初始状态具备五个核心输入量：
* $\mathbf{P}_{\text{start}} \in \mathbb{R}^2$（即 `formation.pos`）：编队当前位置；
* $\hat{\mathbf{D}}_{\text{start}} \in \mathbb{R}^2$（即 `formation.dir`）：编队当前单位朝向向量；
* $\mathbf{P}_{\text{target}} \in \mathbb{R}^2$（即 `target.pos`）：目标锚点位置；
* $\hat{\mathbf{D}}_{\text{target}} \in \mathbb{R}^2$（即 `target.dir`）：编队在目标点必须达到的单位朝向向量；
* $r \in \mathbb{R}^+$：编队允许的最小转弯半径（Steering Circle Radius）。

算法最终需精确解算出以下四个关键几何特征：
1. $\mathbf{C}_1$（即 `c1`）：起点转向圆圆心；
2. $\mathbf{E}_1$（即 `c1_exit`）：编队脱离起点圆弧、切入中继直线段的脱离点（Breakaway Point）；
3. $\mathbf{C}_2$（即 `c2`）：终点转向圆圆心；
4. $\mathbf{E}_2$（即 `c2_enter`）：编队由直线段切入终点圆弧的汇入点（Joining Point）。

---

### 2.2 圆心定位计算（Calculating $\mathbf{C}_1$ and $\mathbf{C}_2$）

定义从起始位置指向目标位置的全局位移位矢为：
$$\mathbf{V}_{\text{dir}} = \mathbf{P}_{\text{target}} - \mathbf{P}_{\text{start}}$$

#### 2.2.1 起点圆心 $\mathbf{C}_1$ 求解
为确保初始切线方向严格等于 $\hat{\mathbf{D}}_{\text{start}}$，圆心必处于与该向量垂直的法线上。二维平面中单位向量 $\hat{\mathbf{D}} = (x, y)$ 存在两个标准正交法向量：
* **左法向量（Left Perpendicular）**：$\hat{\mathbf{N}}_{\text{left}} = (-y, x)$
* **右法向量（Right Perpendicular）**：$\hat{\mathbf{N}}_{\text{right}} = (y, -x)$

为了使圆弧朝向目标位移方向自然弯曲，取与 $\mathbf{V}_{\text{dir}}$ 夹角为锐角（点积大于零）的正向投影向量：
$$\mathbf{N}_{\text{form}} = \begin{cases} \hat{\mathbf{N}}_{\text{left}}(\hat{\mathbf{D}}_{\text{start}}), & \text{若 } \hat{\mathbf{N}}_{\text{left}}(\hat{\mathbf{D}}_{\text{start}}) \cdot \mathbf{V}_{\text{dir}} > 0 \\ \hat{\mathbf{N}}_{\text{right}}(\hat{\mathbf{D}}_{\text{start}}), & \text{其他} \end{cases}$$

将所得单位法向量缩放至转弯半径 $r$ 并叠加至起点坐标，得到起点转向圆圆心：
$$\mathbf{C}_1 = \mathbf{P}_{\text{start}} + r \cdot \mathbf{N}_{\text{form}}$$

#### 2.2.2 终点圆心 $\mathbf{C}_2$ 求解
同理，终点切线必须以 $\hat{\mathbf{D}}_{\text{target}}$ 终接。由于轨迹是反向汇入目标，其几何参考基准应沿位移反方向（即 $-\mathbf{V}_{\text{dir}}$）展开：
$$\mathbf{N}_{\text{targ}} = \begin{cases} \hat{\mathbf{N}}_{\text{left}}(\hat{\mathbf{D}}_{\text{target}}), & \text{若 } \hat{\mathbf{N}}_{\text{left}}(\hat{\mathbf{D}}_{\text{target}}) \cdot (-\mathbf{V}_{\text{dir}}) > 0 \\ \hat{\mathbf{N}}_{\text{right}}(\hat{\mathbf{D}}_{\text{target}}), & \text{其他} \end{cases}$$
$$\mathbf{C}_2 = \mathbf{P}_{\text{target}} + r \cdot \mathbf{N}_{\text{targ}}$$

#### 2.2.3 奇异重叠分支（Overlapping Circles Fallback）
若直接生成的两转向圆发生空间几何重叠，即两圆心欧氏距离：
$$d = \|\mathbf{C}_2 - \mathbf{C}_1\|_2 < 2r$$
此时在两圆之间**无法构建外切或内切直线通道**（出现几何不可达死锁）。

**工业级解决方案**：直接反转两个法向量的偏置方向：
$$\mathbf{N}_{\text{form}} \leftarrow -\mathbf{N}_{\text{form}}, \quad \mathbf{N}_{\text{targ}} \leftarrow -\mathbf{N}_{\text{targ}}$$
$$\mathbf{C}_1 \leftarrow \mathbf{P}_{\text{start}} + r \cdot \mathbf{N}_{\text{form}}, \quad \mathbf{C}_2 \leftarrow \mathbf{P}_{\text{target}} + r \cdot \mathbf{N}_{\text{targ}}$$
*物理语义*：该操作强制智能体先朝反方向转弯绕出局部狭窄死区，以“大回环”策略争取足够的物理转向机动空间，从而保证 $\|\mathbf{C}_2 - \mathbf{C}_1\| \ge 2r$。

---

### 2.3 脱离点与切入点求解（Calculating $\mathbf{E}_1$ and $\mathbf{E}_2$）

判定切线形态的关键依据是法向量方位分类。根据 $\mathbf{N}_{\text{form}}$ 与 $\mathbf{N}_{\text{targ}}$ 分别属于相应基准向量的左侧还是右侧，分为**异侧公切线（内公切线）**与**同侧公切线（外公切线）**两大求解分支。

```
             【异侧切线 (Opposite Sides)】                      【同侧切线 (Same Side)】
         (Left-Right 或 Right-Left 交叉内切)                  (Left-Left 或 Right-Right 外平移)

                  C1_exit                                    C1_exit               C2_enter
                 o-------                                   o---------------------o
                /        \                                 /                       \
               /    a1    \                              c1 o                       o c2
           c1 o-----/------\                             |       d = c2 - c1       |
              \    /        \                            +------------>------------+
               \  /          o C2_enter                               d.perp
                \/          /
                 \         /
                  \-------o c2
```

#### 2.3.1 分支一：法向量异侧（Opposite Sides，内公切线）
当编队需要在起点与终点经历反向转向（例如左转后接右转，即 $\mathbf{N}_{\text{form}} = \text{Right}, \mathbf{N}_{\text{targ}} = \text{Left}$ 或反之），其切线段 $\overline{\mathbf{E}_1\mathbf{E}_2}$ 与中心连线 $\overline{\mathbf{C}_1\mathbf{C}_2}$ 相互交叉并交于两线段的中点。

几何推导如下：
1. 线段 $\overline{\mathbf{C}_1\mathbf{E}_1} \perp \overline{\mathbf{E}_1\mathbf{E}_2}$，且 $\overline{\mathbf{C}_2\mathbf{E}_2} \perp \overline{\mathbf{E}_1\mathbf{E}_2}$；
2. 构造直角三角形，其斜边长为中心距之半 $\frac{d}{2} = \frac{1}{2}\|\mathbf{C}_2 - \mathbf{C}_1\|_2$，邻边长为转向半径 $r$；
3. 计算内切偏移角 $a_1$：
   $$a_1 = \arccos\left(\frac{r}{\frac{1}{2}d}\right) = \arccos\left(\frac{2r}{d}\right)$$
4. 计算基准连线角 $a_2$（向量 $\mathbf{C}_2 - \mathbf{C}_1$ 相对 X 轴的正向偏角）：
   $$a_2 = \operatorname{atan2}((\mathbf{C}_2 - \mathbf{C}_1)_y, (\mathbf{C}_2 - \mathbf{C}_1)_x)$$
5. 终点基准连线角 $b_2$（向量 $\mathbf{C}_1 - \mathbf{C}_2$ 相对 X 轴的正向偏角）：
   $$b_2 = \operatorname{atan2}((\mathbf{C}_1 - \mathbf{C}_2)_y, (\mathbf{C}_1 - \mathbf{C}_2)_x)$$
   且令 $b_1 = a_1$。

通过符号决策修正圆心角 $a_3, b_3$：

| 条件判定 | 起点脱离角 $a_3$ | 终点切入角 $b_3$ |
| :--- | :--- | :--- |
| **Diagram A**：$\mathbf{N}_{\text{form}} = \text{Right} \land \mathbf{N}_{\text{targ}} = \text{Left}$ | $a_3 = a_2 + a_1$ | $b_3 = b_2 + b_1$ |
| **Diagram B**：$\mathbf{N}_{\text{form}} = \text{Left} \land \mathbf{N}_{\text{targ}} = \text{Right}$ | $a_3 = a_2 - a_1$ | $b_3 = b_2 - b_1$ |

根据极坐标变换输出脱离点 $\mathbf{E}_1$ 与汇入点 $\mathbf{E}_2$：
$$\mathbf{E}_1 = (c_{1x} + r\cos(a_3), \; c_{1y} + r\sin(a_3))$$
$$\mathbf{E}_2 = (c_{2x} + r\cos(b_3), \; c_{2y} + r\sin(b_3))$$

*(注：原书第 4 页公式排印处写为 `c2_enter(x,y) = (c2.x + r * sin(b3), ...)`，在此按解析几何规范纠正为标准余弦 $x$、正弦 $y$ 极坐标形式。)*

---

#### 2.3.2 分支二：法向量同侧（Same Side，外公切线）
当 $\mathbf{N}_{\text{form}}$ 与 $\mathbf{N}_{\text{targ}}$ 处于同侧（同为 Left 或同为 Right）时，两转向圆半径相同，公切线平行于连心线 $\mathbf{d} = \mathbf{C}_2 - \mathbf{C}_1$。此时无需调用反三角函数：

1. 计算中心连线位矢：$\mathbf{d} = \mathbf{C}_2 - \mathbf{C}_1$；
2. 构造长度为 $r$ 的正交法向偏置位矢 $\mathbf{d}_{\text{perp}}$：
   $$\mathbf{d}_{\text{perp}} = \begin{cases} 
   r \cdot \frac{\hat{\mathbf{N}}_{\text{left}}(\mathbf{d})}{\|\mathbf{d}\|}, & \text{若 } \mathbf{N}_{\text{form}} = \text{Right} \\
   r \cdot \frac{\hat{\mathbf{N}}_{\text{right}}(\mathbf{d})}{\|\mathbf{d}\|}, & \text{若 } \mathbf{N}_{\text{form}} = \text{Left}
   \end{cases}$$
3. 直接通过向量平移求得切点：
   $$\mathbf{E}_1 = \mathbf{C}_1 + \mathbf{d}_{\text{perp}}$$
   $$\mathbf{E}_2 = \mathbf{C}_2 + \mathbf{d}_{\text{perp}}$$

---

### 2.4 离散化路径点序列生成（Path Discretization）

轨迹生成完成后，系统将其栅格化/离散化为路标点队列（Waypoints），由三段有序序列构成：
$$\mathcal{P}_{\text{final}} = \text{Arc}(\mathbf{P}_{\text{start}} \to \mathbf{E}_1 \mid \mathbf{C}_1) \cup \text{Segment}(\mathbf{E}_1 \to \mathbf{E}_2) \cup \text{Arc}(\mathbf{E}_2 \to \mathbf{P}_{\text{target}} \mid \mathbf{C}_2)$$

**圆弧采样推进朝向规则**：
* 当 $\mathbf{N} = \text{Right}$ 时，智能体绕圆心按**顺时针（Clockwise）**方向积分步进；
* 当 $\mathbf{N} = \text{Left}$ 时，智能体绕圆心按**逆时针（Counter-Clockwise）**方向积分步进。

---

## 3. 编队动态导航控制（Navigate the Formation）

在获得宏观路径点集后，编队系统的核心任务是驱动多智能体沿路径推进。**必须明确：算法计算出的“编队槽位（Formation Slots）”是各单位的寻路导航目标（Pathfinding Targets），而非强制物理附着的硬性几何位姿**。

```
        【解耦架构模型 (Decoupled Formation Architecture)】

    +--------------------------+
    | 宏观几何路径管理器        |
    | (Steering Circle Path)   |
    +--------------------------+
                 │
                 ▼
    +--------------------------+       局部避障 / 威胁规避
    | 编队虚拟槽位生成器        |       (Local Flocking / RVO)
    | (Column / Band Mode)     |                 │
    +--------------------------+                 ▼
                 │ 驱动目标                     ┌────┐
                 └───► [ Slot Target ] ───────► │单位│ 运动控制输出
                                                └────┘
```

该解耦架构带来了极佳的生产环境适应性：
* **局部扰动鲁棒性**：当单位遭遇局部小障碍物、敌方火力骚扰或发生资源采集交互时，可临时脱离槽位进行局部避障或索敌（如结合动态避障算法 RVO 或局部导向行为 Steering Behaviors）；
* **自愈能力**：脱离事件结束后，单位重新寻路追赶自身对应的动态槽位，自动融入编队体系。

编队始终以第一排（Front Row）为基准：根据当前速度步进更新编队锚点位置，并将第一排所有单位槽位严格正交排布在编队行进方向两侧。对于后续各排，作者提出了两种经典的运动流派：

---

### 3.1 纵列跟随模式（“Column” Formation）

纵列模式的设计思想是**“保列弃排”**。每个后排槽位仅追踪紧随其同一列的前方槽位，形成类似机械蛇或载具车队（Convoy）的流体运动。

```
  [第一排 (严格正交)]        [转向机动中：各列独立流体弯曲]
        ●---★---●                     ●
           / \                         \   ★
          /   \                         \ /
        ○   ☆   ○                        ●---☆
        |   |   |                         \   \
        ○   ☆   ○                          ○   ○
  (直线对齐状态：横平竖直)                 (排结构打散，列内保持平滑距离)
```

#### 算法执行步骤：
1. **基准排更新**：基于编队线速度与下一路径目标点更新第一排几何位置，第一排垂直于行进方向对称展开；
2. **列内追踪递推**：对于排索引 $i \in [2, M]$，遍历所有列 $j \in [1, N]$：
   * 取当前槽位 $\mathbf{S}_{i, j}$ 与前置槽位 $\mathbf{S}_{i-1, j}$；
   * 计算指向前置槽位的单位方向向量：
     $$\hat{\mathbf{V}}_{\text{track}} = \frac{\mathbf{S}_{i-1, j} - \mathbf{S}_{i, j}}{\|\mathbf{S}_{i-1, j} - \mathbf{S}_{i, j}\|}$$
   * 将当前槽位沿 $\hat{\mathbf{V}}_{\text{track}}$ 前向推进，直到其与前置槽位保持设定的安全间隔距离 $d_{\text{col}}$：
     $$\mathbf{S}_{i, j} \leftarrow \mathbf{S}_{i-1, j} - d_{\text{col}} \cdot \hat{\mathbf{V}}_{\text{track}}$$

* **工业优缺点评估**：
  * **优点**：转弯流动感极强，能够紧凑通过狭窄通道（Chokepoints），极少产生转弯离心错位。
  * **缺点**：在急转弯或转向圆段，由于内外圈位移弧长差异，横向排拓扑完全解体，失去仪仗或防线整齐度。

---

### 3.2 横带保持模式（“Band” Formation）

横带模式的设计思想是**“排拓扑刚性保持”**。编队在转弯时力求保持各排与前排形态平行，主要用于重装方阵、罗马方阵（Phalanx）或具有齐射（Volley Fire）战术需求的前线步兵。

```
  [转弯判定：根据第一排位移方向与垂直法向点积确定转向趋势]
  
       第二排向左转 (Turn Left)                第二排向右转 (Turn Right)
  ┌───────────────────────────────┐       ┌───────────────────────────────┐
  │  1. 锚定最左侧单位:           │       │  1. 锚定最右侧单位:           │
  │     S[i, 1] 靠拢 S[i-1, 1]    │       │     S[i, N] 靠拢 S[i-1, N]    │
  │  2. 从左向右横向对齐:         │       │  2. 从右向左横向对齐:         │
  │     沿 +rPerpendicular 展开   │       │     沿 -rPerpendicular 展开   │
  │     S[i, j] <- S[i, j-1] + D  │       │     S[i, j] <- S[i, j+1] + D  │
  └───────────────────────────────┘       └───────────────────────────────┘
```

#### 算法执行步骤：
1. **转向方向判别（Turn Direction Detection）**：
   * 提取当前排与上一排同列位矢 $\mathbf{V}_{\text{rel}} = \mathbf{S}_{i-1, k} - \mathbf{S}_{i, k}$；
   * 计算其右手正交法向量：$\mathbf{r}_{\text{perp}} = \hat{\mathbf{N}}_{\text{right}}(\mathbf{V}_{\text{rel}})$；
   * 获取第一排当前的移动速度位矢 $\mathbf{V}_{\text{lead}}$，计算投影标量：
     $$\text{TurnSide} = \mathbf{V}_{\text{lead}} \cdot \mathbf{r}_{\text{perp}}$$
   * **决策分支**：若 $\text{TurnSide} > 0$ 判定为**右转（Turning Right）**；否则判定为**左转（Turning Left）**。

2. **左转时的排重构（Left Turn Workflow）**：
   * **内圈锚定**：将该排**最左侧单位** $\mathbf{S}_{i, 1}$ 沿向量 $\mathbf{S}_{i-1, 1} - \mathbf{S}_{i, 1}$ 推进至紧贴前排设定距离；
   * **行级级联展开**：对于后续单位 $j \in [2, N]$，以其左侧单位 $\mathbf{S}_{i, j-1}$ 为基准，严格沿 $\mathbf{r}_{\text{perp}}$ 方向排列对齐：
     $$\mathbf{S}_{i, j} = \mathbf{S}_{i, j-1} + d_{\text{row}} \cdot \frac{\mathbf{r}_{\text{perp}}}{\|\mathbf{r}_{\text{perp}}\|}$$

3. **右转时的排重构（Right Turn Workflow）**：
   * **内圈锚定**：将该排**最右侧单位** $\mathbf{S}_{i, N}$ 沿向量 $\mathbf{S}_{i-1, N} - \mathbf{S}_{i, N}$ 推进至紧贴前排设定距离；
   * **行级级联展开**：从倒数第二位向左遍历 $j = N-1$ 递减至 $1$，以其右侧单位 $\mathbf{S}_{i, j+1}$ 为基准，沿 $-\mathbf{r}_{\text{perp}}$ 方向排列对齐：
     $$\mathbf{S}_{i, j} = \mathbf{S}_{i, j+1} - d_{\text{row}} \cdot \frac{\mathbf{r}_{\text{perp}}}{\|\mathbf{r}_{\text{perp}}\|}$$

4. **全局级联**：该过程从第二排自前向后逐排（Row-by-Row）迭代计算，直至整军完成位姿重构。

---

## 4. 工业级 C++ 工程实现参考

以下为基于现代 C++17 编写的高性能工业级路径求解与编队更新系统：

```cpp
#include <cmath>
#include <vector>
#include <optional>
#include <algorithm>

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    Vector2 operator+(const Vector2& rhs) const { return {x + rhs.x, y + rhs.y}; }
    Vector2 operator-(const Vector2& rhs) const { return {x - rhs.x, y - rhs.y}; }
    Vector2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    
    float LengthSq() const { return x * x + y * y; }
    float Length() const { return std::sqrt(LengthSq()); }
    
    Vector2 Normalized() const {
        float len = Length();
        return (len > 1e-5f) ? Vector2{x / len, y / len} : Vector2{0.0f, 0.0f};
    }

    static float Dot(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }
    
    // 返回垂直向量
    Vector2 LeftPerpendicular() const { return {-y, x}; }
    Vector2 RightPerpendicular() const { return {y, -x}; }
};

enum class Side { Left, Right };

struct SteeringCirclePath {
    Vector2 c1;
    Vector2 c1_exit;
    Vector2 c2;
    Vector2 c2_enter;
    Side start_side;
    Side target_side;
    float turn_radius;
};

class FormationPathGenerator {
public:
    static std::optional<SteeringCirclePath> CalculatePath(
        const Vector2& start_pos, const Vector2& start_dir,
        const Vector2& target_pos, const Vector2& target_dir,
        float radius) 
    {
        Vector2 dir_vec = target_pos - start_pos;
        if (dir_vec.LengthSq() < 1e-4f) return std::nullopt;

        // 1. 计算初始偏置法向量
        Vector2 form_perp = (Vector2::Dot(start_dir.LeftPerpendicular(), dir_vec) > 0.0f) 
                            ? start_dir.LeftPerpendicular() 
                            : start_dir.RightPerpendicular();
        form_perp = form_perp.Normalized();

        Vector2 target_perp = (Vector2::Dot(target_dir.LeftPerpendicular(), dir_vec * -1.0f) > 0.0f)
                              ? target_dir.LeftPerpendicular() 
                              : target_dir.RightPerpendicular();
        target_perp = target_perp.Normalized();

        Vector2 c1 = start_pos + form_perp * radius;
        Vector2 c2 = target_pos + target_perp * radius;

        // 2. 检查两圆是否重叠 (Overlap Resolution)
        float d_dist = (c2 - c1).Length();
        if (d_dist < 2.0f * radius) {
            // 反转转向圆
            form_perp = form_perp * -1.0f;
            target_perp = target_perp * -1.0f;
            c1 = start_pos + form_perp * radius;
            c2 = target_pos + target_perp * radius;
            d_dist = (c2 - c1).Length();
            
            // 若仍然重叠，则无法生成合法 Dubins 路径
            if (d_dist < 2.0f * radius) return std::nullopt;
        }

        Side form_side = (Vector2::Dot(form_perp, start_dir.LeftPerpendicular()) > 0.9f) ? Side::Left : Side::Right;
        Side targ_side = (Vector2::Dot(target_perp, target_dir.LeftPerpendicular()) > 0.9f) ? Side::Left : Side::Right;

        Vector2 c1_exit{0.0f, 0.0f};
        Vector2 c2_enter{0.0f, 0.0f};

        // 3. 几何切点求解
        if (form_side != targ_side) {
            // 分支 A: 异侧内公切线
            float a1 = std::acos((2.0f * radius) / d_dist);
            Vector2 c2_c1 = c2 - c1;
            Vector2 c1_c2 = c1 - c2;
            float a2 = std::atan2(c2_c1.y, c2_c1.x);
            float b2 = std::atan2(c1_c2.y, c1_c2.x);

            float a3 = (form_side == Side::Right) ? (a2 + a1) : (a2 - a1);
            float b3 = (form_side == Side::Right) ? (b2 + a1) : (b2 - a1);

            c1_exit = Vector2{c1.x + radius * std::cos(a3), c1.y + radius * std::sin(a3)};
            c2_enter = Vector2{c2.x + radius * std::cos(b3), c2.y + radius * std::sin(b3)};
        } else {
            // 分支 B: 同侧外公切线
            Vector2 d_vec = c2 - c1;
            Vector2 d_perp = (form_side == Side::Right) 
                             ? d_vec.LeftPerpendicular().Normalized() * radius 
                             : d_vec.RightPerpendicular().Normalized() * radius;

            c1_exit = c1 + d_perp;
            c2_enter = c2 + d_perp;
        }

        return SteeringCirclePath{c1, c1_exit, c2, c2_enter, form_side, targ_side, radius};
    }
};
```

---

## 5. 编队模式技术选型与生产权衡（Trade-offs）

在实际商用项目架构设计中，两种编队模式的技术特性与应用场景对比如下：

| 评估维度 | 纵列模式（Column Formation） | 横带模式（Band Formation） |
| :--- | :--- | :--- |
| **拓扑稳定性** | 破坏排对齐（Rows Broken），保全列紧密连接 | 保持排对齐（Rows Preserved），列间距弹性伸缩 |
| **转弯空间占用** | 轨迹呈单轨流体收敛，通过性强，横向占用极小 | 转弯时产生较大扇形横向横扫面积（Sweep Area） |
| **几何计算负载** | 极低（仅需简单的列内一维距离追随投影） | 适中（需对排转向进行点积投影分析并执行单向排序展开） |
| **战术表现** | 适合长途行军、狭窄隘口行进、机械化纵队行车 | 适合重装步兵防线压进、方阵行军、列队齐射战斗群 |
| **大角度拐弯表现** | 单位运动平滑，无突兀跳步 | 外侧单位在急弯中需以极大速度补偿以追平横线 |

---

## 6. 障碍物碰撞规避与未来演进架构（Obstacle Avoidance & Scalability）

针对复杂动态战场环境，原书提出了宏观与微观分层处理哲学：

```
                           +------------------------+
                           |  环境障碍物感知系统     |
                           +------------------------+
                                       │
                    ┌──────────────────┴──────────────────┐
                    ▼                                     ▼
        【小尺度障碍 (Small Obstacles)】         【大尺度障碍 (Large Obstacles)】
        (树木、弹坑、散落残骸)                   (基地建筑、山体悬崖、巨型沟堑)
                    │                                     │
                    ▼                                     ▼
    ┌───────────────────────────────┐     ┌───────────────────────────────┐
    │ 宏观编队规划忽略                 │     │ 编队整体包围盒膨胀 (Inflate)    │
    │ 由槽位解耦系统交付各单位局部避障 │     │ 将编队作为刚体整体在宏观全局  │
    │ (如 Dynamic Flocking / RVO)   │     │ 导航网格 (NavMesh) 上进行寻路 │
    └───────────────────────────────┘     └───────────────────────────────┘
```

1. **小尺度微观障碍物（Small-scale Obstacles）**：
   宏观路径生成器无需感知细碎掩体与小树木。依托“槽位仅作为寻路目标（Pathfinding Target）”的松耦合设计，单位运行局部避障算法（如 Reciprocal Velocity Obstacles, RVO 或力导向行为），在绕过障碍后利用行为树（Behavior Trees）平滑返回槽位。
2. **大尺度宏观障碍物（Large-scale Obstacles）**：
   当遭遇建筑、山体或不可通行深水时，单个转向圆算法无法闭环。工业界标准管线是将整个编队视为一个具有外接包围几何体（Bounding Box / Capsule）的元实体（Meta-Entity），在全局导航网格（NavMesh）上执行高阶 A* 寻路；**转向圆技术退化为高阶寻路输出的多边形通道内部用于平滑拐弯的角点平滑算子（Corner Smoothing Operator）**。

---

## 7. 结语（Conclusion）

基于转向圆的编队移动技术以简洁的解析几何与向量投影推导，成功攻克了多智能体系统在转弯半径约束下“位姿双约束（Position & Heading）”的难题。该算法避免了复杂的非线性优化求解，计算消耗极低且数值稳定性极高，是实现大规模 RTS 战术移动与载具协同演进的工业级基石方案。
