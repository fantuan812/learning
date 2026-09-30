---
type: Reference
title: "第9章 Obstacle avoidance for robots of multiple sizes and forms in Horizon Zero Dawn"
description: "Game AI Pro 工业级精读：Obstacle avoidance for robots of multiple sizes and forms in Horizon Zero Dawn。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第9章 Obstacle avoidance for robots of multiple sizes and forms in Horizon Zero Dawn

> 来源：*Game AI Pro 4 (Online Edition 2021)*, Chapter 9.  
> 原文作者 / 资源：[Obstacle avoidance for robots of multiple sizes and forms in Horizon Zero Dawn](http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter09_Obstacle_avoidance_for_robots_of_multiple_sizes_and_forms_in_Horizon_Zero_Dawn.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷4-OnlineEdition2021](README.md) ｜ [专栏首页](../README.md)

---

> **原作者**：Carles Ros Martínez（Guerrilla Games）  
> **出处**：*Game AI Pro 4 (Online Edition 2021), Chapter 09*  
> **重构与工业级生产实战解读**

---

## 1. 导论：传统避障模型的破局 (Introduction)

在传统类人射击游戏（如《杀戮地带》系列 / *Killzone*）中，AI 实体的物理与碰撞包围体通常被高度简化为**圆柱体（Cylinder）**或**圆形（Circle）**投影。这一几何假设极具工程优势，因为圆形具有天然的**旋转不变性（Rotational Invariance）**：
- 任意旋转朝向下，圆形的轮廓与投影半径完全不变；
- 机器人间的碰撞检测与导向行为（Steering Behaviors）无需跟踪实体的朝向变动，计算复杂度极低。

但在《地平线：零之曙光》（*Horizon Zero Dawn*）的开放世界生态中，机械兽（Robots）的设计直接借鉴了动物界的形态（如长颈鹿形态的长颈兽、暴龙形态的雷霆牙、迅猛龙形态的观察者等），其体型呈现出极端的**长条形（Elongated Shapes）**与跨数量级的尺寸差异（从数十厘米到数十米）。

```
【传统圆形包围体局限性】 vs 【矩形 OBB 真实拟合】

     ( 虚空缝隙 )                    [ 贴近绕行 ]
   /-------------\                 +-------------+
  /    +-----+    \                |   Watcher   |
 |     | W-1 |     |               +-------------+
 |     +-----+     |                      ^
 |                 |                      | (紧贴无穿插)
 |     +-----+     |                      v
  \    | W-2 |    /                +-------------+
   \-------------/                 |   Watcher   |
    (包围圆过大)                   +-------------+
```

如果强行使用包围圆去包裹长条形实体，圆形半径必须取其外接圆半径（Circumcircle Radius）。这会导致两个长条形实体在并行或相遇时，即便几何体本身距离甚远，其外接圆也早已重叠，迫使 AI 提前避让，产生严重的“空气墙”阻碍感（如图 1 所示）。对于雷霆牙（Thunderjaw）这种庞然大物，此现象会被数倍放大。为了实现机械兽之间逼真的近距离周旋与贴身穿插，Guerrilla Games 彻底放弃了圆形假设，确立了基于**有向矩形（Rectangular OBB）**的**速度障碍法（Velocity Obstacles, VO）**架构。

### 核心妥协与工程权衡 (Engineering Trade-offs)
严格的带朝向长条几何体避障理论上属于三维非线性规划（Non-linear Programming）问题：随着避障实体改变运动方向 $\vec{v}$，其本体必然会发生转向旋转 $\omega$，从而改变未来的包围盒空间截面。
- **理论代价**：在速度空间对“不断改变旋转朝向的矩形”构建精确时空碰撞体积，计算成本极端高昂，难以在 30/60 fps 的主机环境中多智能体并发执行；
- **Guerrilla 的工程假设**：在生成 VO 时，**仅考虑矩形的初始瞬时朝向（Initial Orientation），并假设其在本次规划的速度方向上不发生刚体旋转**；
- **瑕疵与兜底修复**：当两只机械兽距离极近且突发急转时，该假设偶发微小的穿模（Clipping）。开发团队并未推倒算法引入旋转积分，而是通过**在候选速度代价评分函数（Scoring Function）中重度惩罚在障碍物贴身处的旋转行为**，极低成本地消解了穿模现象。

---

## 2. 强相关障碍物的保守筛选策略 (Gathering the Most Relevant Obstacles)

在大型开放世界中，同屏实体与障碍物数量庞大。为每个障碍物计算高维 Minkowski 和及速度锥，性能开销极高。避障系统必须先执行快速剪枝，将待求解集合缩减至高危子集。

在《地平线：零之曙光》的生产管线中：
- 每一帧每个实体**最多仅筛选 5 个最具相关性的障碍物**纳入 VO 求解管线；
- 筛选核心指标遵循 Karamouzas（2014）的时空理论：**基于碰撞时间（Time-to-Collision），而非欧氏空间距离（Spatial Distance）**。

### 2.1 保守碰撞时间数学推导 (Conservative Collision Time)
系统通过极其低廉的代数运算，评估实体 $a$ 与障碍物 $b$ 在“最坏假设”（实体 $a$ 以最大极限速率向障碍物 $b$ 迎头对冲）下的保守碰撞时间 $t_{\text{conservative}}$：

$$t_{\text{conservative}}(a, b) = \frac{\text{rough\_gap}(a, b)}{\text{max\_approach\_speed}(a, b)}$$

其中，粗略间距 $\text{rough\_gap}(a, b)$ 基于两者的外接圆半径定义：

$$\text{rough\_gap}(a, b) = d_{ab} - (r_a + r_b)$$

- $d_{ab} = \|\vec{p}_a - \vec{p}_b\|$：实体 $a$ 与 $b$ 几何中心位置的欧氏距离；
- $r_a, r_b$：实体 $a$ 与 $b$ 对应矩形的外接圆半径（Circumcircle Radii）。

最大迎面逼近速度 $\text{max\_approach\_speed}(a, b)$ 为：

$$\text{max\_approach\_speed}(a, b) = s_{a\text{max}} + \vec{v}_b \cdot (\vec{p}_a - \vec{p}_b)_u$$

- $s_{a\text{max}}$：实体 $a$ 的物理标量最大速度（Maximum Movement Speed）；
- $\vec{v}_b$：障碍物 $b$ 的当前速度矢量（Linear Velocity）；
- $(\vec{p}_a - \vec{p}_b)_u = \frac{\vec{p}_a - \vec{p}_b}{\|\vec{p}_a - \vec{p}_b\|}$：由障碍物 $b$ 指向实体 $a$ 的单位位移向量。

> **物理内涵**：若障碍物 $b$ 正在高速反向远离 $a$，点积为负，最大逼近速度下降；若障碍物 $b$ 迎面冲向 $a$，点积为正，最大逼近速度陡增。此公式虽未考虑精确轮廓与转角，但作为高吞吐粗筛指标，极其轻量且可靠。

```
【时空关联度胜于空间距离算例】

           v_c (迎面驶来)
          / \
         | C | (距离 d_ac = 2.5)
          \ /
           |
           v
                                    v_b (同向远离)
                                   +-------+
                                   |   B   | --->
                                   +-------+ (距离 d_ab = 5.0)
         +---+
         | A | (s_amax = 2.0)
         +---+

计算对比：
- 障碍物 B 较远 (d_ab = 5.0)，但迎面速度分量投影为 +1.5：
  t_conservative(A, B) = (5 - (1 + 1)) / (2 + 1.5) ≈ 0.86s
- 障碍物 C 较近 (d_ac = 2.5)，但处于脱离态，速度投影为 -1.5：
  t_conservative(A, C) = (2.5 - (1 + 1)) / (2 - 1.5) = 1.0s

结论：尽管 B 的距离是 C 的两倍，但 B 的碰撞威胁度显著高于 C，优先进入 VO 构建流水线。
```

### 2.2 三大责任委派与剪枝规则 (Delegating Avoidance Responsibility)
在多智能体互避中，若所有实体均对等避让，极易引发高频振荡与活锁。系统定义了三大规则，强制将避让责任“全权委托”给对方：

1. **后方 120° 视角盲区裁剪 (Blind Cone Filtering)**：
   虽然感知系统在底层拥有全向视野（Omni-perception），但为了符合动物生理认知并降低规划频次，实体后方 **120° 扇形夹角（Back Arc）** 内的所有障碍物直接剔除。实体不处理追尾风险，追尾责任由后方逼近者承担。
2. **等级森严的特权阶级机制 (Avoidance Priority & Danger Area)**：
   高阶巨型机械兽（如雷霆牙）拥有压倒性的移动特权，绝不对小型机械兽（如观察者）让行。
   - **执行逻辑**：低避让优先级的实体对高优先级实体透明，高阶实体计算 VO 时直接忽略低阶实体；
   - **危险区投影机制（Danger Area）**：高阶实体前向投射动态危险区碰撞体；低阶或静止实体一旦检测到自己落入危险区，由自身的状态机强制触发“跳步避让（Move Aside）”动作脱离路径。
3. **终点边界外部过滤 (Destination Boundary Filtering)**：
   对于位于当前寻路目标路点（Target Destination）之外的障碍物，一律不予建构 VO。
   - 在工业界，传统 VO 会使用截断锥（Truncated Cone）来忽略远期碰撞，而 Guerrilla 直接利用“忽略目标点之外的目标”这一空间截断代替时空截断；
   - 《地平线》的寻路系统采用动态**平滑拉绳算法（Continuous String Pulling）**不断前移目标路点，保证了该筛选逻辑的连贯性。

---

## 3. 矩形 Minkowski 和与速度锥构建 (Building the Velocity Obstacles)

速度障碍（Velocity Obstacle, Fiorini 1998）的核心几何思想是：**通过 Minkowski 和（Minkowski Sum），将双刚体碰撞检测问题降维为“几何点 vs 扩张刚体”的空间相交问题**。

### 3.1 凸多边形 Minkowski 和生成
设观察者 $A$ 的当前长方形轮廓集合为 $R_A$，雷霆牙 $B$ 的长方形轮廓集合为 $R_B$。以 $A$ 为原点，对 $B$ 进行 Minkowski 扩张：

$$R_{B \oplus (-R_A)} = \{ \vec{b} - \vec{a} \mid \vec{b} \in R_B, \vec{a} \in R_A \}$$

```
【Minkowski 和生成示意图：橡皮筋缠绕法】

                     +---------+ (雷霆牙顶点附着观察者矩形)
                     |  [Ra]   |
               +-----+---------+-----+
               |                     |
               |      Thunderjaw     |
               |        (Rb)         |
         +-----+---------+-----+-----+
         |  [Ra]         |     |
         +---------------+     +---------+
                         |      \  外凸包 (Convex Hull)
                         +-------+=========> 弹性橡皮筋包裹形成的
                                             扩张多边形 (Minkowski Sum)
```

**工程实现：**
1. 将观察者矩形 $R_A$ 取负，平移并定位于雷霆牙 $R_B$ 的 4 个顶点上；
2. 提取这 4 组矩形（共 16 个顶点）构成的极值点集合；
3. 执行二维凸包算法（Convex Hull，如 Graham Scan 或 Monotone Chain），生成的凸多边形边界即为该帧障碍物在位移空间中的不可穿透区。

### 3.2 速度锥与速度偏移 (Cone Construction & Offset)
1. **构造无限射线锥**：以观察者几何中心 $\vec{p}_a$ 为原点，向扩张多边形的所有顶点作切线射线，包络形成一个无限延伸的投影锥（Infinite Ray Cone）；
2. **速度空间偏移**：将上述几何锥的顶点从空间原点沿雷霆牙当前速度矢量 $\vec{v}_b$ 进行空间平移：

$$VO_{a|b} = \{ \vec{v}_a \mid \exists t > 0, \; \vec{p}_a + t(\vec{v}_a - \vec{v}_b) \in R_{B \oplus (-R_A)} \}$$

- 只要观察者选择的实际速度矢量 $\vec{v}$ 落在 $VO_{a|b}$ 内部，若两者维持当前速度不变，必定在未来某一时间点发生物理相撞；
- 若 $\vec{v}$ 落在 $VO_{a|b}$ 外部，在双机匀速假设下，几何上绝对不会发生碰撞。

> **关于锥体截断（Truncation）的决策**：标准算法（如 HRVO/RVO）通常采用时空截断圆弧截断 VO，以容忍远期潜在冲突。但在《地平线》的实际表现中，策划需要机械兽具备“极早规避”的稳健本能。因此，**Guerrilla 保留了无限射线锥（Untruncated VO）**，将远期冲突的剥离全权交由前置阶段的“保守碰撞时间”与“目标路点边界裁剪”处理。

---

## 4. 复合 VO 拓扑合并与极值空间采样 (Selecting the Avoidance Velocity)

当场景中并存多个威胁（如同时避让雷霆牙 $b$ 与另一只观察者 $c$）时，实体速度必须同时避开所有生成的锥体。

```
【复合 VO 与多边形外轮廓图解】

      速度空间 V
           \               VO_ac
            \             /-----\
             \  VO_ab    /       \
              \         /  重叠区 \
               \-------+-----------+-------------> (边界 E)
                        \         /
                         \       /
                          \-----/
               =========================> 复合 VO (Combined VO) 外轮廓周长
```

### 4.1 复合 VO（Combined VO）拓扑合并管线
Guerrilla 没有采用重度计算库（如 Clipper 或 BSP 树）来做完整的二维多边形布尔并集，而是针对“由射线和线段组成的一组平面多边形”设计了轻量级边界步进算法：

```
+-------------------------------------------------------------+
|               复合 VO 边界构建算法 (Combined VO Pipeline)     |
+-------------------------------------------------------------+
|  步骤 1: 遍历每个 VO 的每条边缘射线/线段 E_i                 |
|  步骤 2: 计算 E_i 与其它所有 VO 边缘的有向几何相交点        |
|  步骤 3: 判定 E_i 的起点（顶点 Apex）是否被其它任意 VO 包含 |
|  步骤 4: 沿 E_i 步进，穿过交点时更新“包含深度计数器”        |
|          (Containment Counter: In -> Out / Out -> In)       |
|  步骤 5: 仅将包含计数器严格等于 0 的线段提取出来            |
|          -> 形成外轮廓边缘段 (Exposed Segments)             |
+-------------------------------------------------------------+
```
> **边界特判处理**：针对边缘共线（Overlapping Edges）、交点完全重合（Coincident Intersections）、多锥顶点共用（Coincident Apexes）等退化边界情况，引入了极小 $\epsilon$ 偏置与重合剪枝。

### 4.2 极值速度候选采样空间 (Candidate Velocities Generation)
根据 Guy（2009）与 Snape（2011）的数学证明：在凸最优化条件下，避碰代价最小的极限最优解，**必然严格落在复合 VO 边界的边缘（Edge）或顶点（Vertex）上**。

系统在复合多边形外边缘上提取离散候选集 $\mathcal{V}_{\text{cand}}$：
1. **拓扑顶点**：复合 VO 外轮廓的所有几何角点；
2. **射线投影视向交点**：期望速度单位方向乘以最大速率 $\vec{v}_{du} \cdot s_{\text{max}}$ 构成的射线与边缘 $E$ 的几何交点；
3. **正交最近投影点**：期望速度矢量 $\vec{v}_d$ 正交投影到边缘 $E$ 上的最近点（Closest Point）；
4. **动力学极限截断点**：边缘 $E$ 与标量半径分别为 $s_d$（期望速度大小）、$s_{\text{min}}$（最小速率限制）以及 $s_{\text{max}}$（最大速率限制）的同心圆弧的交点。

```
【动力学速度约束窗口过滤】

                      \       / (Combined VO 边界)
                 . --- + ----+ --- .  <-- s_max (最大速率圈)
               /       |      \      \
              /        * C1    * C2   \
             |         |        \      |
             |    +----+---------+--*  | <-- s_d (期望速率圈)
             |    |    |  [v_d]  |  C3 |
              \   |    |         |    /
               \  . ---+---------+-- . <-- s_min (最小移动极限)
                 \     | (丢弃)  |  /
                   --- + ------- + -
                       \        /
                        \ 顶点 /
```

在候选集生成后，系统立即应用底层运动学窗口（Kinematic Constraints）过滤：凡是模长不在 $[s_{\text{min}}, s_{\text{max}}]$ 区间内的候选速度，一律从评估池中丢弃。

---

## 5. 工业级启发式代价评分模型 (Scoring the Candidate Velocities)

经过拓扑剪枝后，剩余有效速度均能实现完全无碰撞。此时需使用代价评分函数（Objective Function）评选出一个符合动画物理与行为逻辑的极优解。

Guerrilla 经过多代机械兽迭代，最终在《地平线》中实装了如下工业经验目标函数：

$$\text{effort} = (1 - p_o)(400 \alpha_d + 50 \beta_d + 70 \gamma) + 0.5(1 - p_t)(400 \alpha_c + 50 \beta_c)$$

该公式的目标是**极小化行为代价（Minimize Effort）**。

### 5.1 归一化特征量全解表
所有特征输入均线性映射归一化至 $[0, 1]$：

| 变量 | 物理含义 | 计算定义 | 设计目的与行为导向 |
| :--- | :--- | :--- | :--- |
| $\alpha_d$ | 期望方向偏角 | $1 - (\vec{v}_{\text{cand}} \cdot \vec{v}_d) / (\|\vec{v}_{\text{cand}}\| \|\vec{v}_d\|)$ | 惩罚偏离任务目标朝向的行为，权值极大 ($400$) |
| $\beta_d$ | 期望速率偏差 | $|\|\vec{v}_{\text{cand}}\| - \|\vec{v}_d\|| / s_{\text{max}}$ | 惩罚加减速偏离，促使尽量以巡航速度行驶 |
| $\alpha_c$ | 当前方向偏角 | $1 - (\vec{v}_{\text{cand}} \cdot \vec{v}_{\text{curr}}) / (\|\vec{v}_{\text{cand}}\| \|\vec{v}_{\text{curr}}\|)$ | 维持当前移动惯性，压制高频抖动 |
| $\beta_c$ | 当前速率偏差 | $|\|\vec{v}_{\text{cand}}\| - \|\vec{v}_{\text{curr}}\|| / s_{\text{max}}$ | 约束加速度突变，保证动画平滑过渡 |
| $\gamma$ | 逆向角速度惩罚项 | 二值变量：$\text{sign}(\Delta \theta) \neq \text{sign}(\omega_{\text{curr}}) \implies 1 \text{ 否则 } 0$ | **强力阻止反向变向扭头**，防止机械兽在障碍物边缘反复横跳 |
| $p_t$ | 目标点接近度 | 二值变量：处于终点门限内设为 $1$，否则为 $0$ | 终点冲刺态：关闭惯性项，允许大角度减速调整进入站点 |
| $p_o$ | 静态障碍贴近度 | 沿期望直线轨迹至静态 VO 碰撞距离的线性归一化量 | **动态权重天平**：遇大阻碍时降低绝对目标权，提升绕行连贯度 |

### 5.2 核心权重设计哲学 (Design Rationale)
1. **减速避让优先于大幅转向（Figure 6 现象）**：
   在公式中，方向偏差权重系数（$400$）远大于速度偏差权重（$50$）。当通道变窄或前方受阻时，机械兽更倾向于**降低速度尾随或平滑减速穿行**，而不是立刻大角度转向，避免长条形身体发生横向扫动造成穿模。
2. **参数 $p_o$ 的防死锁机制**：
   若机械兽面对连续巨型障碍群（如一堵机械墙或巨石），$p_o$ 趋向于 $1$，会主动抑制对全局目标 $\vec{v}_d$ 的贪婪吸附，促使实体顺着 VO 边缘滑动，从而顺利跳出局部极小值点（Local Minima）。

---

## 6. 底层路径适配与异常容灾体系 (System Integration & Resilience)

在工业级游戏引擎中，算法求解往往必须兼容遗留框架。

```
+---------------------------------------------------------+
|                  AI 执行周期 (Per-Tick Update)          |
+---------------------------------------------------------+
|  [寻路系统 Pathfinding]                                  |
|         │ (生成全局路径拐点 Polyline Path)               |
|         ▼                                               |
|  [连续拉绳 String Pulling]                               |
|         │ (输出平滑的目标速度矢量 v_d)                   |
|         ▼                                               |
|  [VO 求解管线 (本章节核心)]                             |
|         │ (求解最佳候选避障速度 v_avoid)                 |
|         ▼                                               |
|  [路径覆写 Hack (Path Overwrite)]                        |
|         │ (临时动态平移路径当前段以匹配 v_avoid 运动)    |
|         ▼                                               |
|  [动画机/移动控制器 Animation & Locomotion Component]   |
+---------------------------------------------------------+
```

### 6.1 速度与路径系统的妥协桥接 (Path Fitting Hack)
《地平线》的底层移动控制器并不直接接受连续的速度向量驱动，而是完全建立在**路径跟随（Path Following）**机制之上。
- **工程解决手段**：算法求解出最佳避碰速度矢量 $\vec{v}_{\text{avoid}}$ 后，系统会在当前单帧内，**反向将实体正在跟随的路径段（Path Segment）进行动态偏移扭曲**，使路径跟随器的切线方向刚好与 $\vec{v}_{\text{avoid}}$ 严密吻合。虽然工程实现不够优雅，但彻底打通了避让系统与动画根骨骼位移（Root Motion）的衔接。

### 6.2 全局死锁与几何无解降级 (Fallback & Fail-Safe Strategy)
当环境极度拥挤，所有候选速度全落入禁行区或被物理动力学完全剔除时，系统执行两级安全回退机制：

```
                    候选速度评估
                         │
                   [无一有效解?]
                   /           \
                 (是)          (否)
                 /               \
       返回零速度 Vector.Zero     提交最优 v_avoid
               │
      持续停滞并累计计时器
               │
        [超时未脱困?]
        /           \
      (是)          (否)
      /               \
强制下发期望速度 v_d    继续停滞等待环境解阻
(破局脱离，接受偶发硬挤)
```

这种两级降级机制既保证了实体不会在死锁中彻底休眠，又避免了穿模异常的永久残留。

---

## 7. 生产环境工程微调精粹 (Odds and Ends)

在主算法框架之外，Guerrilla 还落地了两个极低计算成本但对最终动态视觉表现至关重要的技巧：

### 7.1 半秒速度移动平均窗 (Half-Second Velocity Smoothing)
在复杂的网络同步或物理模拟中，刚体瞬时速度矢量 $\vec{v}_b$ 存在大量高频抖动（Frame-to-Frame Noise）。若直接代入 VO 公式，会导致生成的射线锥频繁颤抖：
- **方案**：系统维护一个**过去 0.5 秒（Half-second）的历史滑动时间窗口**，用于计算障碍物的移动平均速度 $\bar{\vec{v}}_b$：

$$\bar{\vec{v}}_b = \frac{1}{T} \int_{t-0.5}^{t} \vec{v}_b(\tau) d\tau$$

- **效果**：不仅彻底消除了数学锥抖动，还为 AI 带来了更符合生理认知规律的“反应延迟感”（Reaction Delay），面对猎物突发变向，机械兽不会做出不自然的超瞬时反应。

### 7.2 智能体失败停滞拟真化 (Skyrim-Inspired Idle Prolongation)
在城市集聚区中，多 NPC 挤塞会导致频繁寻路失败：
- **借鉴实践**：开发团队受《上古卷轴 V：天际》（*The Elder Scrolls V: Skyrim*）启发，当城市居民 NPC 在避障中无法选出有效移动速度而被迫静止时，AI 行为树不会报错或高频重试，而是**主动拉长该实体的 Idle 闲置行为计时**；
- **视觉反馈**：这一设计让玩家直观地认为居民是“走累了在路旁稍事歇息”，将底层几何求解失败巧妙包装为自然闲适的社交生活画卷。

---

## 8. 完整避障管线 C++ 工业生产参考实现

```cpp
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>

struct Vector2 {
    float x = 0.f;
    float y = 0.f;

    Vector2 operator+(const Vector2& b) const { return {x + b.x, y + b.y}; }
    Vector2 operator-(const Vector2& b) const { return {x - b.x, y - b.y}; }
    Vector2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    float Dot(const Vector2& b) const { return x * b.x + y * b.y; }
    float Length() const { return std::sqrt(x * x + y * y); }
    Vector2 Normalized() const {
        float len = Length();
        return (len > 1e-5f) ? (*this * (1.0f / len)) : Vector2{0.f, 0.f};
    }
};

struct Obstacle {
    Vector2 position;
    Vector2 velocity;
    float circumcircle_radius;
    int avoidance_priority;
    bool is_static;
};

struct Agent {
    Vector2 position;
    Vector2 velocity;
    Vector2 desired_velocity;
    float circumcircle_radius;
    float max_speed;
    float min_speed;
    float angular_velocity;
    int avoidance_priority;
};

class HorizonObstacleAvoidanceSystem {
public:
    // 工业管线主入口
    Vector2 CalculateAvoidanceVelocity(
        const Agent& self, 
        const std::vector<Obstacle>& all_obstacles,
        const Vector2& target_destination,
        float dt) 
    {
        // Step 1: 筛选最多 5 个高危障碍物
        std::vector<Obstacle> relevant_obstacles = FilterMostRelevant(self, all_obstacles, target_destination);

        if (relevant_obstacles.empty()) {
            return self.desired_velocity;
        }

        // Step 2: 针对每个障碍物构建未截断矩形 VO（概念示意结构体）
        std::vector<VelocityObstacleCone> individual_vos;
        for (const auto& obs : relevant_obstacles) {
            individual_vos.push_back(BuildRectangularVO(self, obs));
        }

        // Step 3: 合并为复合 VO (Combined VO) 外边缘
        std::vector<Segment2D> combined_vo_edges = BuildCombinedVOEdges(individual_vos);

        // Step 4: 若期望速度完全无碰撞，直接通行
        if (!IsInsideAnyVO(self.desired_velocity, individual_vos)) {
            return self.desired_velocity;
        }

        // Step 5: 提取极值边界候选速度并执行运动学窗口过滤
        std::vector<Vector2> candidate_velocities = GenerateCandidateVelocities(self, combined_vo_edges);

        if (candidate_velocities.empty()) {
            // 容灾策略：返回零速度停滞
            return Vector2{0.f, 0.f};
        }

        // Step 6: 代价评分模型极小化求解
        Vector2 best_velocity = candidate_velocities[0];
        float min_effort = std::numeric_limits<float>::max();

        for (const auto& v_cand : candidate_velocities) {
            float score = EvaluateEffortScore(self, v_cand, target_destination);
            if (score < min_effort) {
                min_effort = score;
                best_velocity = v_cand;
            }
        }

        return best_velocity;
    }

private:
    struct VelocityObstacleCone {
        Vector2 apex;
        Vector2 ray_dir_left;
        Vector2 ray_dir_right;
    };

    struct Segment2D {
        Vector2 start;
        Vector2 end;
    };

    std::vector<Obstacle> FilterMostRelevant(
        const Agent& a, 
        const std::vector<Obstacle>& obstacles,
        const Vector2& target_dest) 
    {
        struct CandidateRecord {
            Obstacle obs;
            float conservative_time;
        };
        std::vector<CandidateRecord> scored_candidates;

        for (const auto& b : obstacles) {
            // 规则 A: 优先级裁剪（低等级不引起高等级避让）
            if (b.avoidance_priority < a.avoidance_priority) continue;

            Vector2 to_b = b.position - a.position;
            float dist_ab = to_b.Length();
            if (dist_ab < 1e-4f) continue;

            // 规则 B: 视角盲区裁剪（后向 120° 弧度扇区剔除）
            Vector2 forward = a.velocity.Length() > 0.1f ? a.velocity.Normalized() : a.desired_velocity.Normalized();
            Vector2 dir_to_b = to_b.Normalized();
            if (forward.Dot(dir_to_b) < -0.5f) { // cos(120°) = -0.5
                continue;
            }

            // 规则 C: 超出当前目标点边界裁剪
            Vector2 to_dest = target_dest - a.position;
            if (to_b.Dot(to_dest.Normalized()) > to_dest.Length()) {
                continue;
            }

            // 计算保守碰撞时间
            float rough_gap = dist_ab - (a.circumcircle_radius + b.circumcircle_radius);
            Vector2 dir_ba = (a.position - b.position).Normalized();
            float max_approach_speed = a.max_speed + b.velocity.Dot(dir_ba);

            float t_conservative = (max_approach_speed > 0.01f) 
                ? (rough_gap / max_approach_speed) 
                : std::numeric_limits<float>::max();

            scored_candidates.push_back({b, t_conservative});
        }

        // 按保守碰撞时间升序排序
        std::sort(scored_candidates.begin(), scored_candidates.end(), 
            [](const CandidateRecord& lhs, const CandidateRecord& rhs) {
                return lhs.conservative_time < rhs.conservative_time;
            });

        // 截取前 5 个最危险障碍物
        std::vector<Obstacle> result;
        size_t count = std::min(size_t(5), scored_candidates.size());
        for (size_t i = 0; i < count; ++i) {
            result.push_back(scored_candidates[i].obs);
        }
        return result;
    }

    VelocityObstacleCone BuildRectangularVO(const Agent& a, const Obstacle& b) {
        // 工程略图：此处执行 Minkowski Sum 凸包构建并计算切线射线锥
        VelocityObstacleCone cone;
        cone.apex = b.velocity; 
        return cone;
    }

    std::vector<Segment2D> BuildCombinedVOEdges(const std::vector<VelocityObstacleCone>& vos) {
        // 射线-线段相交求交点、深度计数器步进过滤，提取外暴露轮廓线段
        return std::vector<Segment2D>();
    }

    bool IsInsideAnyVO(const Vector2& v, const std::vector<VelocityObstacleCone>& vos) {
        // 检测点是否在任意锥内
        return false; 
    }

    std::vector<Vector2> GenerateCandidateVelocities(const Agent& a, const std::vector<Segment2D>& edges) {
        std::vector<Vector2> candidates;
        // 搜集顶点、期望速度投影交点、极值同心圆弧交点
        // 并且严格过滤：模长在 [min_speed, max_speed] 外的一律剔除
        return candidates;
    }

    // 式 (4): 工业调优代价评分函数
    float EvaluateEffortScore(const Agent& a, const Vector2& v_cand, const Vector2& dest) {
        float cand_speed = v_cand.Length();
        float desired_speed = a.desired_velocity.Length();
        float curr_speed = a.velocity.Length();

        Vector2 cand_dir = v_cand.Normalized();
        Vector2 des_dir = a.desired_velocity.Normalized();
        Vector2 curr_dir = (curr_speed > 1e-4f) ? a.velocity.Normalized() : des_dir;

---

在 AAA 级开放世界游戏（以 Guerrilla Games 开发的《地平线：零之曙光》（*Horizon Zero Dawn*）为代表）中，动态避障系统（Obstacle Avoidance System）既要处理巨型机械兽等非刚体、长宽比极大的复杂胶囊/矩形刚体，又要应对城市密集人群的拟真流动。

传统的基于射线投射（Raycasts）或纯圆形速度障碍（Velocity Obstacles, 简称 VO）的解决方案，往往会在长条形实体之间留下过大的不真实间隙，或在窄道与密集人群中引发高频震荡与不自然的避障循环。本文深入拆解《地平线：零之曙光》基于 VO 的工程落地实践，剖析其在**特殊行为打断**、**异构几何碰撞处理**、**静态导航网格解耦策略**、**主机端多线程运行时性能剖析**以及**算法局限性与机器学习未来演进**等维度的系统级设计。

---

## 一、 平民特殊行为与动态避障异常抑制

在基于速度障碍的局部反应式避障（Reactive Local Avoidance）中，当智能体（Agent）的目标速度方向被障碍物长期遮蔽，且速度障碍锥（VO Cone）在速度空间中连续滑移时，极易诱发智能体陷入极限切向死循环。在平民（Civilians/NPCs）的移动表现上，最典型的异常现象即为**360 度打转型绕行（360-degree loop avoidance）**。

### 1.1 360 度循环绕行的成因与检测机制

在 VO 空间内，可行动速度集合（Admissible Velocities）为：

$$V_{free} = V_{pref} \setminus \bigcup_{i} VO_{A|B_i}$$

当优先速度（Preferred Velocity）$\mathbf{v}_{pref}$ 恰好落在障碍锥中心线附近时，由于动态实体的相对速度微调，数值计算在障碍锥的左侧切线与右侧切线之间连续震荡；若智能体施加了基于运动学平滑的朝向转向约束（Yaw Rate Damping），智能体便会沿着障碍物边缘画出一个完整的圆周运动。这种行为不仅严重破坏沉浸感，还会极大地浪费路径开销。

工业级管线中采用针对转向累积角与角速度的打断检测器（Loop Detector）：

```
                  +-----------------------------------+
                  |   计算当前帧转向角增量 DeltaYaw     |
                  +-----------------+-----------------+
                                    |
                                    v
                  +-----------------------------------+
                  |  累加同向旋转角度: AccumYaw       |
                  |  (若符号反转则衰减/重置累加器)     |
                  +-----------------+-----------------+
                                    |
                    [ AccumYaw >= Threshold (如 270°) ? ]
                           /                     \
                        YES                       NO
                         |                         |
                         v                         v
        +--------------------------------+   +-------------------+
        |  强制介入: 剥夺位移速度        |   | 维持当前 VO 速度  |
        |  强制进入停留状态 (Forced Stop) |   +-------------------+
        +--------------------------------+
                         |
                         v
        +--------------------------------+
        |  叠加延长驻留技巧 (Prolonged   |
        |  Stop Trick)，重采样通行空间   |
        +--------------------------------+
```

### 1.2 延长驻留策略（Prolonged Stop Trick）与拟人化涌现

单纯将速度置零会导致实体在下一帧立即重新评估并恢复震荡。Guerrilla 引入了**延长驻留机制（Prolonged Stop Trick）**：
1. **强制降速至停滞**：一旦检测到潜在的 360 度回环倾向，立即切断推力并强制执行急停或过渡到 Idle/Turn-in-place 待机动画；
2. **驻留计时器锁定（Dwell Timer Lock）**：在随后的 $\Delta t_{dwell} \in [0.8s, 2.0s]$（加入随机扰动）内，智能体完全退出主动避障速度选择，保持原地静止或仅进行原地微调朝向；
3. **环境动态自然解耦**：在真实人群环境中，让道者（Yielding agent）的停滞为侵入者（Intruding agent）提供了充足的通行走廊（Clearance）。这种“遇阻即停、观察让行”的时序错开，模拟了人类社交力学中的礼让行为，使平民行为显著趋向真实。

---

## 二、 异构几何体的闵可夫斯基和计算策略

《地平线：零之曙光》以长条形机械兽（Rectangular-shaped Robots）为主，但在同一场景中往往共存着圆形胶囊体的人类平民、中小型机械与主角埃洛伊（Aloy）。

### 2.1 闵可夫斯基和（Minkowski Sum）的计算复杂度

速度障碍的核心数学基础是相对位移与碰撞边界的闵可夫斯基和。设智能体 $A$ 的几何体为 $\mathcal{A}$，障碍物 $B$ 的几何体为 $\mathcal{B}$：

$$\mathcal{A} \oplus \mathcal{B} = \{ \mathbf{a} + \mathbf{b} \mid \mathbf{a} \in \mathcal{A}, \mathbf{b} \in \mathcal{B} \}$$

在二维平面投影下：
* **圆与圆（Circle $\oplus$ Circle）**：半径分别为 $r_A, r_B$，结果为一个半径为 $R = r_A + r_B$ 的膨胀圆，在极坐标或切线空间下推导 VO 锥极度高效，只需计算正弦角 $\sin \theta = \frac{r_A + r_B}{\|\mathbf{p}_B - \mathbf{p}_A\|}$；
* **矩形与矩形（Rectangle $\oplus$ Rectangle）**：结果为一个拥有 8 个顶点的凸多边形（Minkowski Polygon），需对旋转定向包围盒（OBB）进行极端分离轴与顶点相加；
* **矩形与圆（Rectangle $\oplus$ Circle）**：形成一个“圆角矩形”（Rounded Rectangle / Stadium shape），其速度障碍锥的构造需要同时对四个顶点处的圆弧段以及四条平移边进行切线追踪，求解超越方程，计算开销极其昂贵。

### 2.2 工业级近似权衡：异构转同构

```
              真实物理投影                               工业级近似方案
  +--------------------------------+         +--------------------------------+
  |                                |         |                                |
  |   +---------+        _--_      |         |   +---------+     +---+        |
  |   | 矩形实体 |      / 圆形 \    |  ===>   |   | 矩形实体 |     |   | (外接   |
  |   | (机械兽) |     (  实体  )   |  转换   |   | (机械兽) |     +---+  矩形) |
  |   +---------+       \ 平民 /   |         |   +---------+                  |
  |                      ^--^      |         |                                |
  |                                |         |  统一降维至: 矩形 vs 矩形计算  |
  +--------------------------------+         +--------------------------------+
```

针对混合几何的复杂性，工程管线做出了如下硬性取舍（Trade-off）：
1. **同构极速路径**：$Circle \oplus Circle$ 维持标准的闭式解析解（Closed-form analytical solution）；
2. **异构降维近似**：当碰撞涉及矩形与圆形交互时，**在构建 VO 阶段强制将圆形实体扩展为其轴对齐或基于当前朝向的最小外接矩形（Bounding Box）**；
3. **架构权衡依据**：在《地平线》的生态设计中，野外机械兽与人类平民同屏混杂避障的概率极低（多为战斗击杀逻辑或剧情脚本），为极低频的边界情况去维护一个高复杂度的“圆角矩形-矩形”解析求解器在工程投资回报率（ROI）上是不可接受的。

---

## 三、 静态几何体与导航网格（NavMesh）的解耦架构

大型开放世界 AI 的标准分层架构中，静态环境由导航网格（Navigation Mesh, NavMesh）表征，动态障碍由局部避障管线（Local Avoidance）负责。这两者的耦合程度直接决定了运行性能与稳定性。

### 3.1 完全解耦（Decoupling）的设计决策

在《地平线》中，**局部避障系统在执行速度空间采样时完全忽略了 NavMesh 的静态多边形与边界边界**。
* **驱动动力**：游戏绝大多数漫游与战斗均发生于地表开阔空间（Open Space），空间自由度极高；
* **工程取舍**：将海量静态几何线段实时引入 VO 计算锥，会导致计算开销呈几何级数爆炸，阻碍其在目标平台（PS4 基础硬件）上的多实体流畅运转。因此，工程团队选择省去静态 VO 支持，将算力预算倾斜给视觉、植被与战斗系统。

### 3.2 边界冲突处理与“挤压滑移”（Squishing）失效模式

由于避障完全不感知 NavMesh 边界，当动态回避推力将智能体推向 NavMesh 外缘时，NavMesh 的硬约束层（NavMesh Clamping/Constraint）将介入，强行截断速度分量，触发实体静止。在实体重新起步时，系统面临三种收敛状态：

| 收敛状态类别 | 触发场景与演化机制 | 视觉与行为表现 | 严重程度 |
| :--- | :--- | :--- | :--- |
| **状态 A：障碍物自然脱离** | 诱发回避的动态实体离开，行进路线自然净空。 | 实体恢复常规寻路逻辑，自然加速通过。 | 理想状态（无瑕疵） |
| **状态 B：反向背离避障** | 实体重新评估速度，找到朝向 NavMesh 内部的可用速度空间。 | 实体贴着 NavMesh 边缘平滑绕开障碍物。 | 优秀（符合预期） |
| **状态 C：持续正向推挤** | 动态实体持续逼近，避障系统持续生成朝向 NavMesh 边界外的规避速度向量。 | 实体受 NavMesh 强行 Clamp，在动态障碍与隐形墙之间**“挤压穿透”（Squishing through）**。 | 视觉瑕疵（Visual Glitch） |

在城市狭窄通道中，状态 C 会暴露较为明显的物理穿插与滑步感知。对于拥有大量狭小走廊、室内环境的关卡，不建议忽略静态几何。

### 3.3 静态障碍全解方案：Snape 线段速度障碍法

对于必须兼顾狭窄静态通道的系统，推荐引入 Snape 等人提出的静态线段速度障碍法（Segment VO）：

```
                       NavMesh 边界线段 (Edge: P1 -> P2)
                              ======================
                                    /        \
                                   /          \  静态线段速度障碍锥
                                  /            \ (Static Line-VO)
                                 /              \
                                /                \
                               v                  v
                       +----------------------------------+
                       |    智能体当前位置 (Agent Pos)     |
                       +----------------------------------+
```

对于 NavMesh 凸边界线段 $L = \mathbf{p}_2 - \mathbf{p}_1$，由于其自身速度 $\mathbf{v}_L = \mathbf{0}$，其在智能体速度空间诱发的障碍为以原点为顶点的无限延伸锥体：

$$VO_{A|L} = \{ \mathbf{v} \mid \exists t > 0, \; t \cdot \mathbf{v} \in (L \oplus -\mathcal{A}) \}$$

将邻近 NavMesh 边界离散为线段集合并构造静态线段 VO，利用一维投影法快速剔除无效速度，即可在避免边界穿透的同时维持代数统一性。

---

## 四、 工业级运行时性能与工程剖析

在 PlayStation 4（搭载低功耗 AMD Jaguar 8 核心 CPU，单核算力极其有限）的严苛硬件限制下，避障系统的每帧耗时必须严格被控制在亚毫秒级。

### 4.1 核心性能指标（PS4 平台基准测试）

在极端高压场景（如 20 只长条形巨型机械兽集群同时惊恐溃逃，相互之间密集交织交互）下，《地平线》避障模块的核心性能剖析如下：

$$T_{per\_agent} \approx 60 \, \mu\mathrm{s} \quad (\text{Target: PlayStation 4 CPU})$$

* **单实体开销**：单实体平均耗时约为 **$60\,\mu\mathrm{s}$**；
* **集群总耗时**：20 个密集实体的全量避障计算消耗约 $20 \times 60\,\mu\mathrm{s} = 1.2\,\mathrm{ms}$；
* **预算占比**：占整个运动系统（Movement Logic，剔除骨骼动画更新与 Blend Tree 计算外）总 CPU 时间的 **$\frac{1}{3}$**。

### 4.2 工业级避障管线执行流程

```
[ 阶段 1: 空间拓扑收集 ]
   ├── Spatial Hash 粗筛近邻动态实体 (Neighbor Gathering)
   └── 剔除视锥体外部或超出预警时间窗 (Tau) 的实体
            │
            ▼
[ 阶段 2: 几何降维与 Minkowski 空间构建 ]
   ├── 判断几何类型 (矩形 vs 矩形 / 圆形外接化)
   └── 构建相对速度障碍锥体 (Truncated VO Cones)
            │
            ▼
[ 阶段 3: 速度空间评估与采样 (Optimization / Sampling) ]
   ├── 设定优先速度 v_pref (来自于 Path Following)
   ├── 惩罚函数打分: Cost = ||v - v_pref|| + Penalty(v)
   └── 筛选最优安全速度 v_best
            │
            ▼
[ 阶段 4: 转向驱动与动力学平滑 ]
   ├── 360 度循环绕行打断逻辑判定 (Loop Interrupter)
   └── 输出目标加速度 / 线速度至动画移动控制器 (Locomotion Controller)
```

---

## 五、 算法演进：VO、ORCA 与动画驱动避障

局部避障算法历经数十年的工业迭代，形成了不同的技术路径与适用边界。

### 5.1 主流避障范式技术对比

| 评估维度 | 基础速度障碍（Basic VO） | 最佳相互碰撞规避（ORCA） | 动画驱动避障（Animation-Driven） |
| :--- | :--- | :--- | :--- |
| **核心算法原理** | 几何视锥体投影，假设对方保持恒定速度（非互惠）。 | 凸优化（2D 线性规划），双方各承担 $50\%$ 的避让责任。 | 基于动画剪辑轨迹（Motion Matching / Root Motion）的多轨迹预测。 |
| **计算复杂度** | 中（取决于速度空间离散采样密度）。 | 低至极低（线性时间复杂度 $\mathcal{O}(n)$，利用半平面交集求解）。 | 高（受限于动画姿态搜索与轨迹预测步数）。 |
| **震荡表现** | 存在高频抖动隐患，需额外阻尼。 | 数学上证明无碰撞震荡（Reciprocal Free）。 | 极佳的视觉连续性，完全无滑步。 |
| **长矩形扩展性** | 易于通过顶点投影处理长条多边形。 | 主要针对圆形胶囊体优化；凸多边形半平面扩展复杂。 | 直接依赖碰撞胶囊体或精确凸包测试。 |
| **动力学表现** | 类载具感（Vehicle-like），倾向机械化平移。 | 类质点力学感，难以直接映射人类肢体转移步态。 | 高度拟人，完美遵循步幅、脚步交替与重心偏移。 |

### 5.2 核心矛盾：速度驱动 vs 动画驱动的架构冲突

在现代游戏架构中，**基于速度转向的避障系统（Steering-based Avoidance）与基于根骨骼动画驱动的移动系统（Root Motion Animation-driven Locomotion）之间存在天然的对抗**：

1. **速度驱动系统（VO/ORCA）的假设**：假定智能体是一个全向或受运动学非完整约束（Non-holonomic）限制的质点，能够输出任意符合物理学连续性的加速度向量 $\mathbf{a}$ 与速度向量 $\mathbf{v}$；
2. **动画系统的离散约束**：动画驱动系统（如惯性化混合、运动匹配 Motion Matching）的真实表现建立在**离散的动画资产（Plant, Pivot, Lean, Stop, Start）**之上。脚步落地（Foot Plant）与物理动量限制了角速度突变；
3. **架构对抗表现**：当 VO 要求一个剧烈的左偏切向速度以避免碰撞时，动画状态机可能正处于“右脚承重跨步”阶段，无法产生即时横向位移，导致“滑步”（Foot Sliding）或动画播放与位移向量产生视觉脱节。这正是 VO 应用于长条机械兽（外观与行为本就类似重型载具/载具类机器人）非常自然，但应用于人类平民时却显得机械、虚假的核心技术根源。

---

## 六、 结论与次世代展望：从经典算法到机器学习

### 6.1 《地平线》实践的技术复盘

1. **矩形 VO 对细长实体的巨大价值**：长条形机械兽并排行走时，矩形 VO 能够维持紧凑、自然的间隙，彻底消除了使用外接大圆所带来的“空气墙悬空”假象。即使在计算中忽略部分旋转自由度，其视觉保真度仍远胜圆形胶囊体；
2. **恐怖谷效应与人类避障的特殊性**：人类观察者对“人类 NPC 避障”的容错率极低。在真实世界中，人类极少进行几何意义上的“完美全局平滑避让”，而是伴随着**频繁的判断延迟、次优路线选择，以及极其庞杂的失败补救动作（Recovery Behaviors）**（如侧身收肩、减速错步、眼神对视后停顿、身体倾斜触碰避让等）。

### 6.2 未来演进：模仿学习与神经网络人群模拟

纯手工编写规则（Heuristic Rules）和传统的控制论/运筹优化（Optimization-based）算法在处理复杂人类群集时已遭遇瓶颈。Guerrilla 及业界前沿正在将技术范式向**机器学习（Machine Learning）**转移：

```
                    次世代拟真人群管线
+-------------------------------------------------------+
|  真实世界人群多模态视频捕捉 (Real-world Video/MoCap)   |
+---------------------------+---------------------------+
                            │
                            ▼
+-------------------------------------------------------+
|  对抗生成模仿学习 (GAIL) / 强化学习 (Reinforcement Learning) |
|  学习潜空间交互分布: 提取情境感知特征 (Context/Age/Social) |
+---------------------------+---------------------------+
                            │
                            ▼
+-------------------------------------------------------+
|  轻量级神经网络推理引擎 (Inference Engine @ Runtime)   |
|  - 替代几何 VO：输入局部感知网格 (Spatial Grid)       |
|  - 直接输出动画状态潜变量 (Locomotion Latent Space)   |
|  - 涌现出自然的补救步态与社交避让行为                  |
+-------------------------------------------------------+

通过利用真实人群轨迹数据集进行模仿学习（Imitation Learning），结合上下文感知（环境、体型、心情、社交规范），使避障行为不再仅仅是一个求解 $\mathbf{v}_{best}$ 的几何投影问题，而是演进为一个由数据驱动的高维度肢体动作自然生成体系。

---

## 参考文献

* **[Anguelov 2013]** B. Anguelov. *Collision Avoidance for Preplanned Locomotion*, Game AI Pro: 297-305.
* **[Fiorini 1998]** P. Fiorini, and Z. Shiller. *Motion planning in dynamic environments using Velocity Obstacles*. International Journal of Robotics Research 17, no. 7 (July): 760-772.
* **[van den Berg 2011]** J. van den Berg, S. J. Guy, M. Lin, D. Manocha, C. Pradalier, R. Siegwart, and G. Hirzinger (eds.). *Reciprocal n-body Collision Avoidance*. Robotics Research: The 14th International Symposium ISRR, Springer Tracts in Advanced Robotics 70: 3-19. Springer-Verlag.
* **[Karamouzas 2014]** I. Karamouzas, B. Skinner, and S. J. Guy. *Universal Power Law Governing Pedestrian Interactions*. Physical Review Letters 113, no. 23 (December): 238701.
* **[Guy 2009]** S. J. Guy, J. Chhugani, C. Kim, N. Satish, M. Lin, D. Manocha, and P. Dubey. *ClearPath: Highly Parallel Collision Avoidance for Multi-Agent Simulation*. Proceedings of the 2009 ACM SIGGRAPH/Eurographics Symposium on Computer Animation: 177-187.
* **[Snape 2011]** J. Snape, J. van den Berg, S. J. Guy, and D. Manocha. *The Hybrid Reciprocal Velocity Obstacle*. IEEE Transactions on Robotics 27: 696-706.
