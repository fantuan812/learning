---
type: Reference
title: "第24章 Efficient Crowd Simulation for Mobile Games"
description: "Game AI Pro 工业级精读：Efficient Crowd Simulation for Mobile Games。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第24章 Efficient Crowd Simulation for Mobile Games

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 24.  
> 原文作者 / 资源：[Efficient Crowd Simulation for Mobile Games](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter24_Efficient_Crowd_Simulation_for_Mobile_Games.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

*Graham Pentheny 著｜工业级游戏 AI 架构重构与深度技术解析*

---

## 1. 行业背景与系统核心架构概述 (Introduction & Architectural Overview)

在现代游戏工业中，集群模拟（Crowd Simulation）一直是 AI 领域的核心技术挑战之一。伴随游戏设计复杂度的提升，场景中需要呈现成百上千由独立 AI 控制的智能体（Agents / Units）。设计一套兼具高真实感（Realistic）、强健壮性（Robust）且对关卡策划友好（Designer-friendly）的移动与集群控制系统，是高负载游戏（如塔防、RTS、MOBA 等）的技术命脉。

### 1.1 传统点对点寻路的性能瓶颈
传统游戏寻路方案（如基于导航网格 NavMesh 或路网图的 $A^*$ 寻路）通常为每个智能体独立计算路径。然而，在以集群为核心的游戏机制中，大量智能体往往朝向相同或有限的目标点移动，其行进轨迹具有高度重叠的空间局部性。
* **计算冗余**：为 $N$ 个智能体分别执行图遍历搜索会导致寻路耗时呈 $\mathcal{O}(N \cdot K)$ 线性攀升（$K$ 为单次寻路代价），引发严重的 CPU 耗时毛刺；
* **移动端算力瓶颈**：在算力、内存带宽及发热功耗严格受限的移动硬件平台上，大规模独立寻路计算往往导致游戏帧率暴跌，无法支持同屏海量单位。

### 1.2 向量流场与导向行为的解耦融合架构
经典移动端塔防游戏《坚守阵地 2》（*Fieldrunners 2*）构建了一套基于**向量流场（Vector Flow Fields）**与**导向行为（Steering Behaviors）**深度融合的双层架构：

```
+-------------------------------------------------------------------------------+
|                             全局宏观寻路层 (Global Flow Field)                  |
|                                                                               |
|   +-------------------+       +-------------------+       +---------------+   |
|   | 动态世界阻挡拓扑  | ----> | 逆向 Dijkstra 遍历| ----> | 离散化单位向量场 |   |
|   | (Grid Obstacles)  |       | (All-to-One Path) |       | (Static Field)|   |
|   +-------------------+       +-------------------+       +-------+-------+   |
+-------------------------------------------------------------------|-----------+
                                                                    | 双线性插值
+-------------------------------------------------------------------|-----------+
|                             局部微观集群动力学 (Local Steering Dynamics)       |
|                                                                   v           |
|   +-----------------------------------------------------------------------+   |
|   | 空间加速划分结构：松散四叉树 (Loose Quadtree) 邻域检索                 |   |
|   +-----------------------------------------------------------------------+   |
|                                       |                                       |
|   +-----------------------------------v-----------------------------------+   |
|   | 优先级截断累加管线 (Prioritized Greedy Force Accumulator):             |   |
|   | 1. 流场跟随力 (Flow Following)                                        |   |
|   | 2. 动态避障力 (Obstacle Avoidance - 基于侧步切向与动能比)             |   |
|   | 3. 分离力 (Separation - 动能缩放比例)                                 |   |
|   | 4. 对齐力 (Alignment)                                                 |   |
|   | 5. 凝聚力 (Cohesion)                                                  |   |
|   +-----------------------------------+-----------------------------------+   |
|                                       | 最大力阈值截断 (Max Force Clamping)   |
|                                       v                                       |
|   +-----------------------------------------------------------------------+   |
|   | 质点动力学积分 (Point-Mass Dynamics Integration):                      |   |
|   | a = F_steering / Mass  --->  v = Clamp(v + a * dt, MaxSpeed)          |   |
|   +-----------------------------------------------------------------------+   |
+-------------------------------------------------------------------------------+
```

该架构将“全局长距离导航”与“局部多智能体动态避碰”完全解耦：
1. **宏观层**：将全地图的静态导向信息预计算并离散存储于全局向量流场中。计算开销与智能体数量完全解耦，寻路耗时降为 $\mathcal{O}(1)$；
2. **微观层**：每个智能体被建模为携带物理属性的质点，依据局部邻域感知与自身物理属性，动态合成有限受限的转向力向量，驱动其平滑且具群体感的移动。

---

## 2. 空间离散化与网格拓扑建模 (Grid & Spatial Discretization)

世界坐标系的空间离散化是流场生成的基础。在《坚守阵地 2》的生产实践中，地图被均匀离散为二维正交网格（Orthogonal Grid）：

### 2.1 网格尺寸与穿透性约束
* **网格尺寸设计准则**：网格单元的边长被设定为**略大于场景中最大智能体的碰撞直径**：
  $$\text{CellSize} \ge \max_{u \in \text{Units}}(\text{Diameter}_u) + \epsilon$$
  此项工程决策确保了：只要流场算法在网格图上找到了一条可通行的单元序列，场景中**任何规格**的智能体均可物理穿过，从拓扑层面杜绝了大型单位在狭窄通道被卡死（Path Blocking）的死锁现象。
* **单元格通行状态**：
  每个网格单元 $C_{(x,y)}$ 被严格标记为以下二值状态之一：
  * **Open（通行）**：智能体可自由穿越的开放空间；
  * **Blocked（阻挡）**：被防御塔、地图掩体或自然障碍物占据，绝对不可通过。

---

## 3. 向量流场数学原理 (Vector Flow Field Theory)

### 3.1 连续流函数及其离散化表达
在流体动力学（Fluid Dynamics）中，流动状态由连续流函数（Flow Function）表征。在本系统中，给定一个或多个目标点集 $\mathcal{D} = \{\mathbf{d}_1, \mathbf{d}_2, \dots, \mathbf{d}_k\}$，定义流函数 $\mathbf{\Phi}: \mathbb{R}^2 \to \mathbb{R}^2$ 为空间任意位置沿最优测地线路径指向最近目标的连续归一化速度方向：
$$\mathbf{\Phi}(\mathbf{x}) = -\frac{\nabla \mathcal{C}(\mathbf{x})}{\|\nabla \mathcal{C}(\mathbf{x})\|}$$
其中 $\mathcal{C}(\mathbf{x})$ 为空间点 $\mathbf{x}$ 到目标集 $\mathcal{D}$ 的最小移动代价（Geodesic Path Cost）。

**向量流场（Vector Flow Field）**即为连续流函数 $\mathbf{\Phi}$ 在空间网格上的**离散化采样（Discretization）**。对于网格尺寸为 $m \times n$ 的地图，每个单元 $C_{(i,j)}$ 内部维护一个预计算的归一化二维方向向量 $\vec{V}_{(i,j)}$：
$$\vec{V}_{(i,j)} \in \mathbb{R}^2, \quad \|\vec{V}_{(i,j)}\| = 1.0 \quad \left(0 \le i < m,\ 0 \le j < n\right)$$

```
+-----------+-----------+-----------+
|     ↗     |     ↑     |     ↖     |
|  (i-1,j+1)|  (i, j+1) | (i+1,j+1) |
+-----------+-----------+-----------+
|     →     |     •     |     ←     |  <--- 单元中心存储单位向量
|  (i-1, j) |  (i,  j)  |  (i+1, j) |
+-----------+-----------+-----------+
|     ↘     |     ↓     |     ↙     |
|  (i-1,j-1)|  (i, j-1) | (i+1,j-1) |
+-----------+-----------+-----------+
```

### 3.2 共享机制与路径定义 (Path Definition)
* **路径（Path）的概念封装**：在架构实现上，一个唯一的**目标集 $\mathcal{D}$ 与其派生生成的 $m \times n$ 规格归一化向量场**被共同封装为一个“路径”对象；
* **实例共享**：流场表达的是全图到指定目标集的导向信息。因此，无论场景中存在 10 个还是 10,000 个目标相同的智能体，它们均**共享读取同一个流场**。智能体无需计算自身全局路径，彻底抹去了单位个体的路径搜索时间；
* **按需重算（Dirty Recompute）**：流场具有极高的时空局部稳定性。只有当**网格阻挡状态改变**（例如玩家在路径上放置/摧毁了防御塔、桥梁坍塌）或**目标点集发生移动**时，该流场才触发全局重算。世界中所有跟随该流场的智能体将在下一个物理帧中自动响应新流向，自适应调整移动轨迹。

### 3.3 连续流场近似：双线性向量插值 (Bilinear Flow Interpolation)
若智能体仅简单拾取当前所处网格的离散方向向量 $\vec{V}_{(i,j)}$，在跨越网格边界时将发生导向力瞬变，导致单位转向出现明显的“顿挫”与高频抖动。

为消除网格走样误差，系统采用低分辨率网格结合**双线性插值（Bilinear Interpolation）**重构连续流动场：

设智能体位于世界坐标 $\mathbf{p} = (p_x, p_y)$，将其映射至流场网格空间后，计算其最近的四个网格中心单元向量 $V_{00}, V_{10}, V_{01}, V_{11}$，及在网格内的归一化偏移参量 $u, v \in [0, 1)$：

```
       V_01                      V_11
        +-------------------------+
        |                         |
        |          p(u,v)         |
        |            *            |
        |                         |
        +-------------------------+
       V_00                      V_10
```

插值计算推导：
$$\vec{V}_{\text{interp}}(\mathbf{p}) = (1 - u)(1 - v)\vec{V}_{00} + u(1 - v)\vec{V}_{10} + (1 - u)v\vec{V}_{01} + uv\vec{V}_{11}$$
$$\vec{V}_{\text{sample}}(\mathbf{p}) = \frac{\vec{V}_{\text{interp}}(\mathbf{p})}{\|\vec{V}_{\text{interp}}(\mathbf{p})\|}$$

双线性插值使得游戏即便在粗粒度的网格上，依然能计算出平滑、有机的连续路径曲线，避免了提高网格分辨率所带来的指数级内存与计算膨胀。

---

## 4. 全局向量流场生成管线 (Generating the Flow Field)

流场的生成本质上是全图所有可通行网格到目标点集的**单源/多源最短路径反向求解问题**。《坚守阵地 2》生产管线中选用了基于**Dijkstra 算法**的逆向广度优先波前扩展实现。

### 4.1 算法执行流程与状态转移
1. **初始化**：
   * 分配全图代价表 $\text{Cost}[m][n]$ 并填充初始极大值 $\infty$；
   * 初始化小顶堆优先队列（或开放列表 `OpenList`）；
   * 将所有目标单元（Destinations）的代价设为 $0$，推入 `OpenList`，其自身流场向量设为 $\vec{0}$；
2. **全图波前扩展**：
   * 从 `OpenList` 中弹出当前代价最小的单元 $C_{\text{curr}}$；
   * 遍历 $C_{\text{curr}}$ 周围可通行的相邻单元 $C_{\text{nbr}}$（支持正交与对角扩展）；
   * 计算通过 $C_{\text{curr}}$ 到达 $C_{\text{nbr}}$ 的累计代价 $\text{Cost}_{\text{new}} = \text{Cost}(C_{\text{curr}}) + \text{Distance}(C_{\text{curr}}, C_{\text{nbr}})$；
   * 若 $\text{Cost}_{\text{new}} < \text{Cost}(C_{\text{nbr}})$，则更新代价，将 $C_{\text{nbr}}$ 的父指针回溯指向 $C_{\text{curr}}$，并将其加入/更新到 `OpenList`；
3. **计算离散向量**：
   * 遍历全部有效通行单元，将其流向量赋予指向具有最低累计代价的相邻节点的归一化方向向量：
     $$\vec{V}(C) = \frac{\mathbf{x}_{\text{lowest\_neighbor}} - \mathbf{x}_{C}}{\|\mathbf{x}_{\text{lowest\_neighbor}} - \mathbf{x}_{C}\|}$$
   * 算法遍历完所有联通单元（`OpenList` 变空）后终止，而非在触达某一起点时提前退出。

### 4.2 工业级工业 C++ 核心算法实现

```cpp
#include <vector>
#include <queue>
#include <cmath>
#include <limits>

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    Vector2() = default;
    Vector2(float _x, float _y) : x(_x), y(_y) {}

    Vector2 operator-(const Vector2& rhs) const { return Vector2(x - rhs.x, y - rhs.y); }
    Vector2 operator+(const Vector2& rhs) const { return Vector2(x + rhs.x, y + rhs.y); }
    Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
    
    float LengthSquared() const { return x * x + y * y; }
    float Length() const { return std::sqrt(LengthSquared()); }
    
    Vector2 Normalized() const {
        float len = Length();
        return (len > 1e-6f) ? Vector2(x / len, y / len) : Vector2(0.0f, 0.0f);
    }
};

struct CellNode {
    int x, y;
    float pathCost;

    bool operator>(const CellNode& other) const {
        return pathCost > other.pathCost;
    }
};

enum CellType { Open, Blocked };

class FlowFieldGrid {
public:
    int width;
    int height;
    std::vector<CellType> obstacles;
    std::vector<Vector2> flowField;
    std::vector<float> integrationField;

    FlowFieldGrid(int w, int h) 
        : width(w), height(h), 
          obstacles(w * h, CellType::Open),
          flowField(w * h, Vector2(0.0f, 0.0f)),
          integrationField(w * h, std::numeric_limits<float>::max()) {}

    inline int ToIndex(int x, int y) const { return y * width + x; }
    inline bool IsValid(int x, int y) const { return x >= 0 && x < width && y >= 0 && y < height; }

    void GenerateFlowField(const std::vector<std::pair<int, int>>& destinations) {
        // 1. 重置代价值为无穷大
        std::fill(integrationField.begin(), integrationField.end(), std::numeric_limits<float>::max());
        std::priority_queue<CellNode, std::vector<CellNode>, std::greater<CellNode>> openList;

        // 2. 种子节点初始化 (多源汇入)
        for (const auto& dest : destinations) {
            int idx = ToIndex(dest.first, dest.second);
            integrationField[idx] = 0.0f;
            openList.push({dest.first, dest.second, 0.0f});
        }

        // 邻域偏移矩阵 (包含正交与对角线 8 邻域)
        const int dx[] = { 0,  0, -1,  1, -1,  1, -1,  1 };
        const int dy[] = {-1,  1,  0,  0, -1, -1,  1,  1 };
        const float moveCost[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f };

        // 3. 逆向 Dijkstra 泛洪遍历
        while (!openList.empty()) {
            CellNode current = openList.top();
            openList.pop();

            int currIdx = ToIndex(current.x, current.y);
            if (current.pathCost > integrationField[currIdx]) {
                continue; // 过滤过时节点
            }

            for (int i = 0; i < 8; ++i) {
                int nx = current.x + dx[i];
                int ny = current.y + dy[i];

                if (!IsValid(nx, ny)) continue;
                int nbrIdx = ToIndex(nx, ny);
                if (obstacles[nbrIdx] == CellType::Blocked) continue;

                float newCost = current.pathCost + moveCost[i];
                if (newCost < integrationField[nbrIdx]) {
                    integrationField[nbrIdx] = newCost;
                    openList.push({nx, ny, newCost});
                }
            }
        }

        // 4. 代价场梯度下降推导流向量
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = ToIndex(x, y);
                if (obstacles[idx] == CellType::Blocked) {
                    flowField[idx] = Vector2(0.0f, 0.0f);
                    continue;
                }

                float minCost = integrationField[idx];
                Vector2 bestDirection(0.0f, 0.0f);

                for (int i = 0; i < 8; ++i) {
                    int nx = x + dx[i];
                    int ny = y + dy[i];

                    if (!IsValid(nx, ny)) continue;
                    int nbrIdx = ToIndex(nx, ny);
                    if (obstacles[nbrIdx] == CellType::Blocked) continue;

                    if (integrationField[nbrIdx] < minCost) {
                        minCost = integrationField[nbrIdx];
                        bestDirection = Vector2(static_cast<float>(dx[i]), static_cast<float>(dy[i]));
                    }
                }
                flowField[idx] = bestDirection.Normalized();
            }
        }
    }
};
```

---

## 5. 智能体物理模型与改进导向行为系统 (Agent Dynamics & Steering Behaviors)

在《坚守阵地 2》中，智能体被抽象为基于 Reynolds Boid 模型扩展的自主智能体（Autonomous Agents），采用质点动力学模型并组合改进的导向行为（Steering Behaviors）。

### 5.1 智能体物理属性集
系统为所有兵种配置统一的导向行为管线，通过下述物理属性的差异化配置衍生出截然不同的运动机能：

| 物理属性名词 | 英文标识 | 物理意义与数学定义 |
| :--- | :--- | :--- |
| **质量** | $m$ (`mass`) | 决定惯性，并参与动能计算：$E_k = \frac{1}{2}m\|\mathbf{v}\|^2$ |
| **最大驱动力** | $F_{\max}$ (`maxForce`) | 单个模拟步长允许施加的所有导向力的模长上限 |
| **机动度 (敏捷度)** | $\alpha$ (`agility`) | 定义为最大推力与质量之比：$\alpha = \frac{F_{\max}}{m} = a_{\max}$，决定转向加速度与刹车响应 |
| **最大速率** | $v_{\max}$ (`maxVelocity`)| 限制质点速度向量的绝对模长：$\|\mathbf{v}\| \le v_{\max}$ |
| **邻域感知半径** | $R_{\text{nbr}}$ (`neighborRadius`)| 空间查询范围，限定参与群聚与避障运算的邻近智能体子集 |

### 5.2 贪婪优先级力累加管线 (Prioritized Greedy Summation)
为了兼顾实时性能与行为稳定性，系统采用**截断式受限贪婪累加机制**。各转向行为按照固定的安全优先级顺序逐项计算并累加，一旦合力模长突破 $F_{\max}$，立即截断并丢弃低优先级行为：

```
       +------------------------------------+
       |          开始力累加: F_acc = 0       |
       +-----------------+------------------+
                         |
                         v
       +------------------------------------+
       |  计算行为力 F_i (按优先级降序遍历)   |
       +-----------------+------------------+
                         |
                         v
       +------------------------------------+
       |      F_remaining = F_max - |F_acc| |
       +-----------------+------------------+
                         |
           +-------------+-------------+
           |                           |
  |F_i| <= F_remaining        |F_i| > F_remaining
           |                           |
           v                           v
+-----------------------+   +------------------------------------+
|   F_acc += F_i        |   | F_acc += F_i.Normalized() *        |
|   继续下一个行为      |   |          F_remaining               |
+-----------------------+   | 终止力计算管线 (截断退出)          |
                            +------------------------------------+
```

优先级降序序列设计如下：
1. **流场跟随力（Flow-Field Following Force, $\mathbf{F}_{\text{flow}}$）**：保底到达目标的全局驱动力；
2. **动态避障力（Obstacle Avoidance Force, $\mathbf{F}_{\text{avoid}}$）**：防止单位间剧烈撞击的横向规避力；
3. **分离力（Separation Force, $\mathbf{F}_{\text{sep}}$）**：防止单位密集重叠的斥力；
4. **对齐力（Alignment Force, $\mathbf{F}_{\text{align}}$）**：群体速度方向趋同力；
5. **凝聚力（Cohesion Force, $\mathbf{F}_{\text{coh}}$）**：向邻域质心靠拢的引力。

### 5.3 生产级工业改良导向行为算子

#### 1. 连续流场跟随 (Flow-Field Following)
流场提供期望行进方向 $\mathbf{d}_{\text{flow}} = \vec{V}_{\text{sample}}(\mathbf{p})$。期望速度为 $\mathbf{v}_{\text{desired}} = \mathbf{d}_{\text{flow}} \cdot v_{\max}$，对应的转向驱动力为：
$$\mathbf{F}_{\text{flow}} = \mathbf{v}_{\text{desired}} - \mathbf{v}$$

#### 2. 动能缩放分离力 (Kinetic-Energy Scaled Separation)
经典 Reynolds 模型的分离力仅考虑空间几何距离 $\mathbf{r} = \mathbf{p} - \mathbf{p}_{\text{nbr}}$。但在不同体型混杂的战场中，会导致小型敏捷单位与大型装甲单位施加相同的互斥响应，产生视觉违和。

《坚守阵地 2》引入**动能比缩放（Kinetic Energy Ratio Scaling）**：
$$E_{k} = \frac{1}{2} m \|\mathbf{v}\|^2, \quad E_{k, \text{nbr}} = \frac{1}{2} m_{\text{nbr}} \|\mathbf{v}_{\text{nbr}}\|^2$$
$$\mathbf{F}_{\text{sep}} = \sum_{j \in \mathcal{N}} \left( \frac{\mathbf{p} - \mathbf{p}_j}{\|\mathbf{p} - \mathbf{p}_j\|^2} \cdot \frac{E_{k, j}}{E_k + \epsilon} \right)$$
* **物理表现**：质小、速慢的敏捷单位受到极其显著的斥力放大，在直面重型高动能单位时会主动大幅退让避开；反之，高动能单位几乎不受轻型单位干扰，保持稳步向前推进。

#### 3. 正交侧步避障力 (Side-Stepping Obstacle Avoidance)
为解决移动速度更快的单位被前方慢速单位挡住无法超车的问题，系统修改了传统正交阻挡反冲力，采用**速度法向侧步力（Side-stepping Force）**：

对于潜在发生碰撞的邻近单位，计算二者的相对速度 $\mathbf{v}_{\text{rel}} = \mathbf{v}_{\text{nbr}} - \mathbf{v}$ 与相对位移 $\mathbf{p}_{\text{rel}} = \mathbf{p}_{\text{nbr}} - \mathbf{p}$。

侧步力强制施加在自身速度矢量的垂直法向量方向 $\mathbf{v}^{\perp} = (-v_y, v_x)$：
$$\mathbf{F}_{\text{avoid}} = \operatorname{sgn}\left(\mathbf{p}_{\text{rel}} \cdot \mathbf{v}^{\perp}\right) \cdot \mathbf{v}^{\perp} \cdot \frac{\|\mathbf{v}_{\text{rel}}\|}{\|\mathbf{p}_{\text{rel}}\|}$$
该力诱导追赶者向邻居位移反方向进行平滑横向侧滑，实现智能化智能超车绕行。

---

## 6. 数值调优工程方法论 (Systematic Parameter Tuning)

在多层非线性导向力的相互作用下，多参数调节极易引发震荡或行为坍缩。《坚守阵地 2》确立了一套严格的单变量分级调谐流水线（Systematic Tuning Pipeline）：

```
+-------------------------------------------------------------------------+
| 第一阶段：标定最大速率 (v_max)                                           |
| - 将其余属性赋予基准固定值                                              |
| - 在孤立测试场景直观评估单位的行进速度与游戏节奏匹配度                  |
+------------------------------------+------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
| 第二阶段：调节最大驱动力 (F_max)                                         |
| - 保持单体环境，观察转向曲率与启停制动响应                              |
| - 控制敏捷度 alpha = F_max / m，确保消除由于向心力不足导致的拐角冲出边界 |
+------------------------------------+------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
| 第三阶段：标定邻域半径 (R_nbr)                                           |
| - 引入同质单位群（Homogeneous Group）观察群聚密度                       |
| - R_nbr 较小: 单位间隙紧密；R_nbr 较大: 群体整体离散松弛                |
+------------------------------------+------------------------------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
| 第四阶段：跨单位相对质量标定 (Relative Mass Calibration)                 |
| - 引入异质混合群体，调整各兵种间的质量分布 (Mass Ratio)                  |
| - 核心铁律：调整 mass 时必须等比调整 F_max，确保自身机动度 alpha 严格恒定|
|   (从而不破坏阶段二已锁定的单体转向特性)                                |
+-------------------------------------------------------------------------+
```

---

## 7. 移动平台极限性能优化 (Mobile Hardware Optimization)

在移动端芯片受限的 SIMD、弱浮点性能与内存带宽限制下，系统从算法级与指令级进行了针对性优化：

### 7.1 松散四叉树邻域剔除 (Loose Quadtree Spatial Indexing)
* **瓶颈诊断**：集群模拟中最严苛的性能消耗来自每帧对每个单位进行邻域检索（朴素判定复杂度为 $\mathcal{O}(N^2)$）。
* **数据结构架构**：采用**松散四叉树（Loose Quadtree）**维护单位位置。松散四叉树的节点包围盒比常规网格扩展了一定边界比例，在单位发生小范围位移时无需跨节点频繁重构树拓扑，显著降低动态维护开销；
* **查询剪枝**：将邻域检测限定在当前叶节点及相邻节点的空间包围盒内，把空间检索的平均时间复杂度压制在 $\mathcal{O}(N \log N)$ 乃至近似 $\mathcal{O}(N)$。

### 7.2 距离平方运算与开方消除 (Square-Root Minimization)
移动硬件上的浮点平方根开销（`sqrt` / `RSQRT`）极其昂贵。
* 在邻域距离阈值判定、分离力反比权重计算中，全面保留距离平方标量：
  $$\text{DistSq} = \|\mathbf{p}_A - \mathbf{p}_B\|^2 = (x_A - x_B)^2 + (y_A - y_B)^2$$
  仅当 $\text{DistSq} < R_{\text{nbr}}^2$ 时才判定进入感知范围，规避了成千上万次无谓的开方运算；
* 向量归一化仅在必须参与力累加的最终步骤中执行一次。

### 7.3 流场内存极限压缩方案 (Memory Optimization Strategies)
对于大地图，若每格存储 2 个 32 位浮点数（Vector2，占用 8 字节），在多目标流场环境下内存消耗较为显著。文中提出了两种压缩方案：

```
方案 A: 基准偏角量化 (Yaw Angle Compression)
+-----------------------------------------------------------------------+
| 存储: 每个 Cell 仅存储相对于基准向北向量 <0,1> 的顺时针偏角 theta       |
| 还原: V_x = -sin(theta),  V_y = cos(theta)                           |
+-----------------------------------------------------------------------+

方案 B: 离散枚举量化 (1-Byte Discrete Cardinal Encoding)
+-----------------------------------------------------------------------+
| 约束: 将流向离散限制为 8 方向或 16 方向 (如八方位角)                   |
| 存储: 单字节 uint8_t (0:北, 1:东北, 2:东, 3:东南, 4:南 ... )           |
| 压缩比: 内存由 8 Bytes/Cell 骤降至 1 Byte/Cell (降低 87.5% 内存开销)   |
+-----------------------------------------------------------------------+
```

### 7.4 任务级高度并行化模型 (Multi-threaded Concurrency Model)
针对移动端多核处理器，本架构在逻辑解耦设计上具备天然的并行化特性（Embarrassingly Parallel）：
1. **流场生成的线程级解耦**：不同目标集派生的向量流场之间完全相互独立。当需要重新生成多个流场时，可将各流场实例分发至不同的工作线程（Worker Threads）上并发执行 Dijkstra 泛洪计算；
2. **转向力计算的多核分发**：在特定物理帧中，当流场构建完成后，所有智能体的局部邻域检索与力合成管线互不读写对方的物理输出状态（只读上一帧状态或空间划分树结构），因此可利用 Job System 或多线程任务分块将集群均分并行化处理。

---

## 8. 架构优势、权衡对比与未来演进 (Trade-offs & Future Work)

### 8.1 方案优缺点工业级对比分析

| 评测维度 | 传统个体 A* 寻路 + RVO 避障 | 离散流场 (Flow Field) + 改良 Boids 导向行为 |
| :--- | :--- | :--- |
| **计算复杂度 (寻路)** | 随单位数量线性增长：$\mathcal{O}(N \cdot K)$ | 与单位数量无关：$\mathcal{O}(1)$ 查表与插值 |
| **同屏单位承载量** | 移动端通常受限在百级以内 | 可稳定承载数千（$1,000\sim 5,000+$）个活跃单位 |
| **内存空间占用** | 极低（仅维护路径点链表） | 随（网格节点数 $\times$ 独立目标集数）线性增加 |
| **多目标点支持度** | 优异（任意两点随时规划） | 较弱（目标点分散时需维护过多流场，开销骤增） |
| **行为扩展与表现力** | 需重写转向控制器与局部碰撞 | 高度模块化、支持力混合、群体有机涌现感极强 |

### 8.2 拓展演化方向 (Future Work)
1. **任意角度平滑流场（Any-Angle Flow Fields）**：
   原版方案基于网格的 8 邻域 Dijkstra 遍历，导向向量容易受到网格正交连线的度量失真影响。引入诸如 **Theta\***、**Field D\*** 等任意角度网格规划算法，可绕过网格拓扑棱角，产生更加平滑自如的自然流场；
2. **动态势场混合（Blending Static & Dynamic Flow Fields）**：
   在静态拓扑导航流场的基础之上，叠加由局部瞬时威胁（如范围伤害技能、环境陷阱、重型单位占据）派生的**动态排斥势场（Dynamic Repulsion Field）**。通过对静态底图与动态层进行向量场加权融合，使得上千个集群单位在完全不破坏宏观行进方向的前提下，呈现出绕开危险火线、穿行于枪林弹雨的宏大规避效果。
