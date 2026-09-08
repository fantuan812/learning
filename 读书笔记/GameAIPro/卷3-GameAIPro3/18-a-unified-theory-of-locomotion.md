---
type: Reference
title: "第18章 A Unified Theory of Locomotion"
description: "Game AI Pro 工业级精读：A Unified Theory of Locomotion。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第18章 A Unified Theory of Locomotion

> 来源：*Game AI Pro 3: Balanced Cuts of Game AI Professionals*, Chapter 18.  
> 原文作者 / 资源：[A Unified Theory of Locomotion](http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter18_A_Unified_Theory_of_Locomotion.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷3-GameAIPro3](README.md) ｜ [专栏首页](../README.md)

---

---

## 1. 综述与理论背景（Introduction & Architectural Motivation）

在现代 AAA 游戏工业界中，非玩家角色（AI Character / Agent）的运动控制（Locomotion Control）是一个极其复杂的多约束动力学规划问题。工业级角色运动需要同时应对全局拓扑可达性、局部动态避障、机动物理极限、平滑转向与编队协同等多种约束。

传统实现通常将不同的运动方案孤立设计与测试（如全局路径规划、局部导向行为、PID 控制器、基于速度障碍物的物理避障等）。当把这些异构系统集成至同一个 Agent 运行时，极易产生逻辑碎片化（Brittle Logic）、权重震荡（Oscillation）、死锁（Deadlock）以及调试困难、逻辑难以复用等严重工程缺陷。

**统一运动论（A Unified Theory of Locomotion）** 提供了一种形式化的元语言（Common Metatheory），将所有运动系统在数学与控制论层面归纳为两个正交维度：
1. **作用的运动状态物理量（Component Values of Motion）**：位置（Position $\mathbf{p}$）、速度（Velocity $\mathbf{v}$）、加速度（Acceleration $\mathbf{a}$）；
2. **施加的影响类型（Type of Influence）**：
   - **限制器（Restrictors）**：在状态可行空间（Feasible State Space）内剔除不可行域（Pruning Disallowed States），返回一个禁止域集合；
   - **导向器（Directors）**：在可行域内规划、指定一个理想的单一目标状态矢量（Target Ideal Vector）。

通过建立统一的形式化抽象，异构算法之间的冲突裁决（Arbitration）与组合（Combination）被转化为严格的集合运算与分层控制流水线。

---

## 2. 统一运动论形式化模型（Formal Theoretical Model）

### 2.1 状态空间与拓扑流形

将智能体在二维/三维连续欧氏空间中的机动状态定义为物理相空间流形（Phase Space Manifold）：

$$
\mathcal{S} = \mathcal{P} \times \mathcal{V} \times \mathcal{A} \subseteq \mathbb{R}^d \times \mathbb{R}^d \times \mathbb{R}^d \quad (d \in \{2, 3\})
$$

其中 $\mathbf{p} \in \mathcal{P}$ 为空间位置，$\mathbf{v} \in \mathcal{V}$ 为平移速度，$\mathbf{a} \in \mathcal{A}$ 为瞬时加速度。

### 2.2 限制器与导向器的严格数学定义

#### 限制器（Restrictor, $\mathcal{R}$）
定义一个限制器映射 $\mathcal{R}_X$，其作用于物理量状态空间 $X \in \{\mathcal{P}, \mathcal{V}, \mathcal{A}\}$，其输出为当前物理空间中的**禁止集合（Forbidden Set）**：

$$
\mathcal{R}_X: \mathcal{S} \to \mathcal{P}(X) \quad \text{其中 } \mathcal{R}_X(\mathcal{S}) = X_{\text{disallowed}} \subset X
$$

允许的状态可行空间（Feasible Admissible Domain）定义为补集：

$$
X_{\text{allowed}} = X \setminus X_{\text{disallowed}}
$$

#### 导向器（Director, $\mathcal{D}$）
定义一个导向器映射 $\mathcal{D}_X$，其作用是输出一个具体的、理想的单一目标物理状态矢量 $\mathbf{x}^*$：

$$
\mathcal{D}_X: \mathcal{S} \to X \quad \text{其中 } \mathbf{x}^* \in X
$$

导向器的最优输出必须满足由所有限制器构成的可行空间约束：

$$
\mathbf{x}^* \in X_{\text{allowed}}
$$

### 2.3 运动系统分类正交矩阵

| 物理维度 / 影响类型 | 限制器（Restrictor - 剔除不可行域） | 导向器（Director - 提供目标标称值） |
| :--- | :--- | :--- |
| **位置层（Position $\mathbf{p}$）** | **导航网格（NavMesh Constraints）**<br>环境不可通行区域（Non-walkable Geo）与碰撞体边界几何 | **全局寻路器（Path Planning）**<br>$A^*$ / HPA* / Theta* 计算出的路径拓扑拐点序列、局部目标路标点（Subgoals） |
| **速度层（Velocity $\mathbf{v}$）** | **互惠速度障碍区（RVO / ORCA）**<br>上下级碰撞锥（Collision Cones）所禁止的速度空间矢量子集；情境导向（Context Steering）中的“危险度（Danger Map）”高危射线 | **导向行为（Steering Behaviors）**<br>如 Seek、Flee、Arrive 输出的期望速度矢量 $\mathbf{v}_{\text{des}}$；**PID 速度控制器（PID Controller）**；情境导向中的“兴趣图（Interest Map）”峰值抽取 |
| **加速度层（Acceleration $\mathbf{a}$）** | **机动物理极限限制器**<br>驱动电机/动力学摩擦圆（Friction Circle）、最大制动减速度、角加速度侧倾临界约束 | **力驱动控制器（Force-based Controller）**<br>基于弹簧阻尼系统（Spring-Damper）、力导向群集控制输出的推力矢量 $\mathbf{F} = m\mathbf{a}^*$ |

---

## 3. 级联依赖单向传播机理（Cascading Value Dependencies）

运动学（Kinematics）与动力学（Dynamics）之间存在内在的微积分映射关系：

$$
\mathbf{v}(t) = \frac{\mathrm{d}\mathbf{p}(t)}{\mathrm{d}t}, \quad \mathbf{a}(t) = \frac{\mathrm{d}\mathbf{v}(t)}{\mathrm{d}t}
$$

统一运动论在此基础上形式化推导出了**跨物理维度的单向级联诱导效应（One-Way Cascading Dependency Principle）**。

```
+-------------------------------------------------------------------------+
|                  物理量级联依赖模型 (Cascading Dependencies)             |
+-------------------------------------------------------------------------+
|                                                                         |
|  [ 位置层 Position ]                                                    |
|         |                                                               |
|         | 限制器诱导 (Disallowed positions induce disallowed velocities)|
|         v                                                               |
|  [ 速度层 Velocity ]                                                    |
|         |                                                               |
|         | 限制器诱导 (Disallowed velocities induce disallowed accel.)   |
|         v                                                               |
|  [ 加速度层 Acceleration ]                                              |
|                                                                         |
|  =====================================================================  |
|  逆向非对称规则 (The Converse Does NOT Hold):                           |
|  - 速度层约束 (如无法倒退) 不缩减位置流形的可达性 (仅影响规划代价/耗时)   |
|  - 加速度层约束 (如低加速度) 不改变可达速度上限 (仅影响达到速度的过渡时长) |
+-------------------------------------------------------------------------+
```

### 3.1 限制器的向下诱导机制（Downstream Restriction Propagation）

#### 位置限制器诱导速度限制器（$\mathcal{R}_P \implies \mathcal{R}_V^{\text{induced}}$）
设 $\mathbf{p}_t$ 为当前帧位置，$\Delta t$ 为当前控制步长（Time Horizon / Timestep）。如果速度矢量 $\mathbf{v}$ 使得 Agent 在下一个时间步进入位置限制器的禁止域 $\mathcal{R}_P(\mathcal{S})$，则该速度矢量 $\mathbf{v}$ 必须被速度域禁止：

$$
\mathcal{R}_V^{\text{induced}}(\mathbf{p}_t, \Delta t) = \left\{ \mathbf{v} \in \mathcal{V} \;\middle|\; (\mathbf{p}_t + \mathbf{v}\Delta t) \in \mathcal{R}_P(\mathcal{S}) \right\}
$$

#### 速度限制器诱导加速度限制器（$\mathcal{R}_V \implies \mathcal{R}_A^{\text{induced}}$）
设 $\mathbf{v}_t$ 为当前速度。任何导致进入禁止速度域的瞬时加速度 $\mathbf{a}$ 同样被加速度域禁止：

$$
\mathcal{R}_A^{\text{induced}}(\mathbf{v}_t, \Delta t) = \left\{ \mathbf{a} \in \mathcal{A} \;\middle|\; (\mathbf{v}_t + \mathbf{a}\Delta t) \in \mathcal{R}_V^{\text{combined}}(\mathcal{S}) \right\}
$$

### 3.2 逆向非对称独立性（The Asymmetry Axiom）

统一运动论明确确立了逆向非对称原则：**高阶动力学限制器绝不能影响低阶物理量的拓扑可达空间**。
1. **速度限制并不削减位置可达集**：例如坦克型（Tank Controls）底盘或非全向轮角色无法直接横向或快速向后移动（$\mathbf{v}_{\text{backward}}$ 受到极大限制），但这并不剥夺角色到达后方位置 $\mathbf{p}_{\text{back}}$ 的物理可能性，仅意味着在状态流形中该路径积分耗费更多的时间与机动步骤。
2. **加速度限制并不削减速度可达集**：加速度幅值的极小阈值（如载具牵引力受限）只改变加速度到目标速度的时间积分持续时间（$\int \mathbf{a} \, \mathrm{d}t$），而不限制最大稳态巡航速度。
3. **退化特例（Edge Case）**：仅在单向绝对不可逆加速度控制（如单一固定推力器且无转向自由度）的极端退化系统下此公理失效，此类工业特例在常规游戏 AI 移动控制中予以剥离处理。

---

## 4. 异构运动系统的仲裁与融合拓扑（Arbitration & Combination Architecture）

为将不同范式的算法融合（例如将全局 $A^*$、局部 RVO 避障、情境导向 Context Steering 以及底层 PID 追踪器整合），系统拆分为两个正交的流水线：**限制器集合并求交流水线**与**导向器双层仲裁加权流水线**。

```
+----------------------------------------------------------------------------------------------------+
|                               运动系统拓扑与数据流执行图                                             |
+----------------------------------------------------------------------------------------------------+
|                                                                                                    |
|  [ 限制器管线 - Restrictors ]                                                                      |
|  +---------------------+   +---------------------+                                                 |
|  | NavMesh Restrictor  |   |    RVO Avoidance    |                                                 |
|  |     (Position)      |   |     (Velocity)      |                                                 |
|  +----------+----------+   +----------+----------+                                                 |
|             |                         |                                                            |
|    (Induced Velocity)                 |                                                            |
|             |                         |                                                            |
|             +------------> ( \cup ) <-+  (并集操作: 汇聚所有不可行速度)                             |
|                              |                                                                     |
|                              v                                                                     |
|                 [ V_disallowed = ∪ R_v ]                                                           |
|                              |                                                                     |
|                              |-----------------------+                                             |
|                                                      | (硬约束掩码 / 投影过滤)                     |
|  [ 导向器管线 - Directors ]                          |                                             |
|  +---------------------+   +---------------------+   |                                             |
|  |   Seek / Arrive     |   | Formation Following |   |                                             |
|  |   (Steering Dir 1)  |   |   (Steering Dir 2)  |   |                                             |
|  +----------+----------+   +----------+----------+   |                                             |
|             \                         /              |                                             |
|              \                       /               |                                             |
|               v                     v                |                                             |
|      +-----------------------------------------+     |                                             |
|      | 高级仲裁器 (High-Level Arbitration)     |     |                                             |
|      | - 基于环境与上下文的效用评估/状态机切换 |     |                                             |
|      | - 动态分配权重: w_1, w_2                |     |                                             |
|      +--------------------+--------------------+     |                                             |
|                           |                          |                                             |
|                           v                          |                                             |
|      +-----------------------------------------+     |                                             |
|      | 低级组合器 (Low-Level Combinator)       |     |                                             |
|      | - 优先级切断截断累加 (Prioritized Sum)   |     |                                             |
|      | - 标度加权求和: v_des = ∑ w_i * v_i     |     |                                             |
|      +--------------------+--------------------+     |                                             |
|                           |                          |                                             |
|                           v                          v                                             |
|      +-----------------------------------------+                                                   |
|      |        可行空间投影 / 极值抽取器         |<---+                                             |
|      |  (Projection onto V_allowed = V \ V_dis)|                                                   |
|      +--------------------+--------------------+                                                   |
|                           |                                                                        |
|                           v Target Velocity v*                                                     |
|      +-----------------------------------------+                                                   |
|      |       底层反馈控制器 (PID Controller)   |                                                   |
|      +--------------------+--------------------+                                                   |
|                           |                                                                        |
|                           v Final Motor Command a* / F*                                            |
|                  [ 物理底盘执行机构 Actuators ]                                                    |
+----------------------------------------------------------------------------------------------------+
```

### 4.1 限制器的数学组合：集合求并（Union of Disallowed Domains）

由于限制器的输出语义是**禁止状态空间（Forbidden Subspace）**，多个限制器的复合遵循**逻辑或（Logical OR）**，即在几何上取禁止空间的**并集（Union）**：

$$
\mathcal{R}_V^{\text{combined}} = \mathcal{R}_V^{\text{induced}}(\mathcal{R}_P) \cup \mathcal{R}_V^{\text{RVO}} \cup \mathcal{R}_V^{\text{ContextSteering}} \cup \dots = \bigcup_{k=1}^m \mathcal{R}_{V, k}
$$

最终角色在速度层面的可行解空间为基准速度流形对复合禁止域的差集：

$$
\mathcal{V}_{\text{allowed}} = \mathcal{V}_{\text{base}} \setminus \mathcal{R}_V^{\text{combined}}
$$

此运算保证了系统的安全底线（Safety Invariant）：**任意限制器判定为不安全的矢量，组合后绝对不可被选用**。

### 4.2 导向器的两层融合架构（Two-Tier Director Architecture）

不同于限制器的纯集合运算，导向器输出的是理想矢量。统一运动论将其合并解耦为两个明确阶段：

#### 第一层：高级仲裁器（High-Level Arbitration）
高级仲裁器运行在宏观语义层，监测 Agent 的上下文环境（感知黑板、编队拓扑、交火状态）。它并不直接操作速度数值，而是决定**哪些导向器激活**以及**各导向器的执行优先级与权重分布**。
* **语义仲裁示例**：当 Agent 处于拥挤通道中时，避障导向器（Collision Avoidance Director）的仲裁优先级被拉至最高，而编队跟随导向器（Formation Director）被抑制；在开阔平原巡逻时，编队与路径追踪导向器获取主要权重。

#### 第二层：低级组合器（Low-Level Combinator）
低级组合器负责根据高级仲裁给出的策略，将多个独立的几何矢量聚合成一个标称期望矢量 $\mathbf{v}_{\text{desired}}$。工业界核心包含两种模式：
1. **加权矢量累加模式（Weighted Vector Summation）**：
   通过配置权重系数 $w_i \ge 0$ 控制灵敏度（Sensitivity）与个性（Personality）：
   $$
   \mathbf{v}_{\text{raw}} = \sum_{i=1}^{n} w_i \cdot \mathcal{D}_{V, i}(\mathcal{S})
   $$
2. **优先级切断累加模式（Prioritized Truncated Accumulation）**：
   按优先级顺序 $\mathcal{D}_1 \succ \mathcal{D}_2 \succ \dots \succ \mathcal{D}_n$ 累加矢量，直至达到最大驱动力或最大速度阈值 $V_{\max}$，截断低优先级分量：
   $$
   \mathbf{v}_k = \mathcal{D}_{V, k}(\mathcal{S}), \quad \Delta \mathbf{v}_k = \mathrm{clamp}\left(\mathbf{v}_k, 0, V_{\max} - \left\|\sum_{j=1}^{k-1} \Delta \mathbf{v}_j\right\|\right)
   $$

#### 可行空间投影（Projection onto Admissible Space）
低级组合器计算出的标称速度必须投影至限制器留存的连续自由空间：

$$
\mathbf{v}^* = \arg\min_{\mathbf{v} \in \mathcal{V}_{\text{allowed}}} \|\mathbf{v} - \mathbf{v}_{\text{raw}}\|
$$

---

## 5. 核心算法与经典工业案例映射

### 5.1 互惠速度障碍区（Reciprocal Velocity Obstacles, RVO）作为速度限制器

在多 Agent 动态避障中，设 Agent $A$ 位于 $\mathbf{p}_A$，Agent $B$ 位于 $\mathbf{p}_B$，半径分别为 $r_A, r_B$。
基于 $B$ 的速度 $\mathbf{v}_B$，传统碰撞锥 $VO_{A|B}^{\tau}$ 在时间窗口 $\tau$ 内的禁止速度空间为：

$$
VO_{A|B}^{\tau} = \left\{ \mathbf{v} \;\middle|\; \exists t \in [0, \tau], \; t(\mathbf{v} - \mathbf{v}_B) \in \mathcal{B}(\mathbf{p}_B - \mathbf{p}_A, r_A + r_B) \right\}
$$

RVO 算法将相互避让的责任对半均摊，定义 RVO 禁止速度锥：

$$
RVO_{A|B}^{\tau} = \left\{ \mathbf{v} \;\middle|\; 2\mathbf{v} - (\mathbf{v}_A + \mathbf{v}_B) \in VO_{A|B}^{\tau} \right\}
$$

在统一运动论中，RVO 系统即严格实现为速度限制器：

$$
\mathcal{R}_V^{\text{RVO}} = \bigcup_{B \ne A} RVO_{A|B}^{\tau}
$$

### 5.2 PID 速度追踪器作为速度导向器

当空间规划与障碍限制完成后，最终平滑追踪期望速度 $\mathbf{v}^*(t)$ 的任务交由 PID 控制器执行。
设误差信号为：

$$
\mathbf{e}(t) = \mathbf{v}^*(t) - \mathbf{v}_{\text{current}}(t)
$$

PID 控制器输出目标指令加速度（或马达驱动推力）：

$$
\mathbf{a}^*(t) = K_p \mathbf{e}(t) + K_i \int_{0}^{t} \mathbf{e}(\tau)\,\mathrm{d}\tau + K_d \frac{\mathrm{d}\mathbf{e}(t)}{\mathrm{d}t}
$$

该系统以极小计算开销，增量驱动底层刚体动力学逼近高层规划出的理想速度，是标准的“加速度导向器 / 速度闭环执行器”。

---

## 6. 工业级 C++ 架构实现与代码示例

以下为遵循现代 C++17 标准、面向高性能工程落地构建的“统一运动论”核心框架代码。

```cpp
#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <limits>
#include <optional>

// ============================================================================
// 基础数学定义与空间矢量结构
// ============================================================================
struct Vector2D {
    float x = 0.0f;
    float y = 0.0f;

    Vector2D() = default;
    Vector2D(float inX, float inY) : x(inX), y(inY) {}

    Vector2D operator+(const Vector2D& rhs) const { return { x + rhs.x, y + rhs.y }; }
    Vector2D operator-(const Vector2D& rhs) const { return { x - rhs.x, y - rhs.y }; }
    Vector2D operator*(float scalar) const { return { x * scalar, y * scalar }; }
    Vector2D operator/(float scalar) const { return { x / scalar, y / scalar }; }

    float Dot(const Vector2D& rhs) const { return x * rhs.x + y * rhs.y; }
    float SqrMagnitude() const { return x * x + y * y; }
    float Magnitude() const { return std::sqrt(SqrMagnitude()); }

    Vector2D Normalized() const {
        float mag = Magnitude();
        return (mag > 1e-5f) ? (*this / mag) : Vector2D{ 0.0f, 0.0f };
    }
};

// ============================================================================
// 物理维度枚举与抽象基类
// ============================================================================
enum class MotionAttribute {
    Position,
    Velocity,
    Acceleration
};

struct AgentState {
    Vector2D position{ 0.0f, 0.0f };
    Vector2D velocity{ 0.0f, 0.0f };
    Vector2D acceleration{ 0.0f, 0.0f };
    float maxSpeed = 5.0f;
    float maxForce = 10.0f;
    float radius = 0.5f;
};

// 限制器基类: 产出禁止域判别接口
class IVelocityRestrictor {
public:
    virtual ~IVelocityRestrictor() = default;
    // 返回给定速度采样是否被限制禁止 (true = Disallowed)
    [[nodiscard]] virtual bool IsDisallowed(const Vector2D& candidateVelocity, const AgentState& state) const = 0;
};

// 导向器基类: 产出目标物理矢量接口
class IVelocityDirector {
public:
    virtual ~IVelocityDirector() = default;
    // 计算并输出理想速度矢量
    [[nodiscard]] virtual Vector2D ComputeDesiredVelocity(const AgentState& state) const = 0;
};

// ============================================================================
// 具体实现 1: RVO 速度限制器 (Velocity Restrictor)
// ============================================================================
struct DynamicObstacle {
    Vector2D position;
    Vector2D velocity;
    float radius;
};

class RVOVelocityRestrictor : public IVelocityRestrictor {
public:
    explicit RVOVelocityRestrictor(float timeHorizon = 2.0f)
        : m_timeHorizon(timeHorizon) {}

    void SetObstacles(std::vector<DynamicObstacle> obstacles) {
        m_obstacles = std::move(obstacles);
    }

    bool IsDisallowed(const Vector2D& candidateVelocity, const AgentState& state) const override {
        for (const auto& obs : m_obstacles) {
            Vector2D relativePos = obs.position - state.position;
            Vector2D relativeVel = candidateVelocity * 2.0f - (state.velocity + obs.velocity);
            float combinedRadius = state.radius + obs.radius;

            // 碰撞锥射线相交几何判断 (VO / RVO 简化圆锥检测)
            float distSq = relativePos.SqrMagnitude();
            float radiusSq = combinedRadius * combinedRadius;

            if (distSq <= radiusSq) {
                return true; // 空间重叠碰撞
            }

            // 计算时间步域内的射线判别
            float rayDot = relativeVel.Dot(relativePos);
            if (rayDot > 0.0f) {
                float velMagSq = relativeVel.SqrMagnitude();
                if (velMagSq > 1e-5f) {
                    float projection = rayDot / velMagSq;
                    if (projection <= m_timeHorizon) {
                        Vector2D closestPoint = relativeVel * projection;
                        if ((closestPoint - relativePos).SqrMagnitude() <= radiusSq) {
                            return true; // 落在时间窗口内的速度障碍锥内部
                        }
                    }
                }
            }
        }
        return false;
    }

private:
    float m_timeHorizon;
    std::vector<DynamicObstacle> m_obstacles;
};

// ============================================================================
// 具体实现 2: 寻路航点巡航导向器 (Velocity Director)
// ============================================================================
class PathFollowDirector : public IVelocityDirector {
public:
    void SetTargetWaypoints(std::vector<Vector2D> waypoints) {
        m_waypoints = std::move(waypoints);
        m_currentIndex = 0;
    }

    Vector2D ComputeDesiredVelocity(const AgentState& state) const override {
        if (m_currentIndex >= m_waypoints.size()) {
            return { 0.0f, 0.0f };
        }

        Vector2D targetPos = m_waypoints[m_currentIndex];
        Vector2D toTarget = targetPos - state.position;
        float distance = toTarget.Magnitude();

        // 简易 Arrive 减速半径模型
        constexpr float arrivalRadius = 1.0f;
        if (distance < 1e-4f) {
            return { 0.0f, 0.0f };
        }

        float speed = (distance < arrivalRadius) ? (state.maxSpeed * (distance / arrivalRadius)) : state.maxSpeed;
        return toTarget.Normalized() * speed;
    }

    void AdvanceWaypointIfClose(const Vector2D& currentPos, float threshold = 0.5f) {
        if (m_currentIndex < m_waypoints.size()) {
            if ((m_waypoints[m_currentIndex] - currentPos).Magnitude() < threshold) {
                m_currentIndex++;
            }
        }
    }

private:
    std::vector<Vector2D> m_waypoints;
    size_t m_currentIndex = 0;
};

// ============================================================================
// 仲裁与组合管线 (Arbitrator & Combinator Engine)
// ============================================================================
struct DirectorArbitrationProfile {
    std::shared_ptr<IVelocityDirector> director;
    float weight = 1.0f;
    uint32_t priority = 0; // 优先级从高到低 (0最高)
};

class LocomotionArbitrator {
public:
    void RegisterRestrictor(std::shared_ptr<IVelocityRestrictor> restrictor) {
        m_restrictors.push_back(std::move(restrictor));
    }

    void RegisterDirector(std::shared_ptr<IVelocityDirector> director, float weight, uint32_t priority) {
        m_directors.push_back({ std::move(director), weight, priority });
    }

    // 执行每帧物理规划与融合
    Vector2D EvaluateLocomotion(const AgentState& state) const {
        if (m_directors.empty()) {
            return { 0.0f, 0.0f };
        }

        // 1. 低级组合器: 按优先级切断加权求和
        auto sortedDirectors = m_directors;
        std::sort(sortedDirectors.begin(), sortedDirectors.end(),
            [](const auto& a, const auto& b) { return a.priority < b.priority; });

        Vector2D accumulatedDesiredVelocity{ 0.0f, 0.0f };
        float totalWeight = 0.0f;

        for (const auto& entry : sortedDirectors) {
            Vector2D dirVel = entry.director->ComputeDesiredVelocity(state);
            accumulatedDesiredVelocity = accumulatedDesiredVelocity + (dirVel * entry.weight);
            totalWeight += entry.weight;

            if (accumulatedDesiredVelocity.Magnitude() >= state.maxSpeed) {
                break; // 达到最大速度饱和切断
            }
        }

        if (totalWeight > 1e-5f) {
            accumulatedDesiredVelocity = accumulatedDesiredVelocity / totalWeight;
        }

        // 截断至机动最大速度
        if (accumulatedDesiredVelocity.Magnitude() > state.maxSpeed) {
            accumulatedDesiredVelocity = accumulatedDesiredVelocity.Normalized() * state.maxSpeed;
        }

        // 2. 限制器复合空间投影: 验证组合禁止域 (并集操作)
        auto IsVelocityDisallowed = [this, &state](const Vector2D& testVel) {
            for (const auto& restrictor : m_restrictors) {
                if (restrictor->IsDisallowed(testVel, state)) {
                    return true; // 任意限制器否决即禁止
                }
            }
            return false;
        };

        if (!IsVelocityDisallowed(accumulatedDesiredVelocity)) {
            return accumulatedDesiredVelocity; // 理想速度完全符合可行空间
        }

        // 3. 约束投影采样 (Sampling Projection): 搜索最接近理想速度的允许速度
        constexpr int angularSamples = 32;
        constexpr int radialSamples = 4;
        Vector2D bestVelocity{ 0.0f, 0.0f };
        float minPenalty = std::numeric_limits<float>::max();

        for (int r = radialSamples; r >= 1; --r) {
            float speed = state.maxSpeed * (static_cast<float>(r) / radialSamples);
            for (int a = 0; a < angularSamples; ++a) {
                float theta = (2.0f * 3.1415926535f * static_cast<float>(a)) / angularSamples;
                Vector2D candidate{ std::cos(theta) * speed, std::sin(theta) * speed };

                if (!IsVelocityDisallowed(candidate)) {
                    float penalty = (candidate - accumulatedDesiredVelocity).SqrMagnitude();
                    if (penalty < minPenalty) {
                        minPenalty = penalty;
                        bestVelocity = candidate;
                    }
                }
            }
            // 若在外圈找到满足条件的较优解，则直接采纳
            if (minPenalty < std::numeric_limits<float>::max()) {
                return bestVelocity;
            }
        }

        return { 0.0f, 0.0f }; // 全被阻断时安全停机
    }

private:
    std::vector<std::shared_ptr<IVelocityRestrictor>> m_restrictors;
    std::vector<DirectorArbitrationProfile> m_directors;
};

// ============================================================================
// 底层控制闭环: 向量级 PID 速度控制器 (PID Controller)
// ============================================================================
class Vector2DPIDController {
public:
    Vector2DPIDController(float kp, float ki, float kd)
        : m_kp(kp), m_ki(ki), m_kd(kd) {}

    Vector2D Update(const Vector2D& targetVelocity, const Vector2D& currentVelocity, float deltaTime) {
        if (deltaTime <= 1e-5f) {
            return { 0.0f, 0.0f };
        }

        Vector2D error = targetVelocity - currentVelocity;
        m_integral = m_integral + error * deltaTime;
        Vector2D derivative = (error - m_lastError) / deltaTime;
        m_lastError = error;

        return (error * m_kp) + (m_integral * m_ki) + (derivative * m_kd);
    }

    void Reset() {
        m_integral = { 0.0f, 0.0f };
        m_lastError = { 0.0f, 0.0f };
    }

private:
    float m_kp;
    float m_ki;
    float m_kd;
    Vector2D m_integral{ 0.0f, 0.0f };
    Vector2D m_lastError{ 0.0f, 0.0f };
};
```

---

## 7. 工业落地实战建议与边界权衡（Engineering Guidelines）

### 7.1 采样分辨率与计算性能预算
在限制器对连续可行解空间进行切割时，解析求交（Analytical Intersection）在复杂多边形或非凸障碍下极易引起计算爆炸。工业界标准做法是结合**极坐标采样（Polar Sampling / Ray Marching）**或**基于掩码的视差/兴趣射线图（Context Steering Ray-buffers）**。通常控制在 16 至 32 条射线，将限制器的判定转化为常数级掩码标记。

### 7.2 限制器冲突与死锁穿透（Relaxation & Safety Fallback）
若环境内高密度动态物体导致：

$$
\mathcal{V}_{\text{allowed}} = \mathcal{V}_{\text{base}} \setminus \bigcup_{k} \mathcal{R}_{V, k} = \emptyset
$$

此时若输出零速度 $\mathbf{v} = \mathbf{0}$，角色会产生原地颤抖或死锁。工业级解决方案包含两套机制：
1. **时间视界松弛（Time-horizon Relaxation）**：动态调小 RVO 的 $\tau$ 预测窗口，允许短距离安全逼近；
2. **软约束软化（Soft Constraint Penalty）**：将限制器转化为高惩罚项的效用函数（Utility Objective Function），取穿透深度最小的最优解。

### 7.3 动画状态机（Animation State Machine）与运动规划解耦
高层规划输出的目标速度 $\mathbf{v}^*$ 与加速度 $\mathbf{a}^*$ 必须通过根骨骼位移匹配（Root Motion Matching）或惯性化插值（Inertialization Blend）送入动画状态机。若物理层与导向层帧率不匹配，需引入二次贝塞尔滤波或临界阻尼弹簧（Critically Damped Spring），防止 PID 输出的高频微冲量导致角色动画出现滑步或脚部抖动（Foot-skate & Jittering）。
