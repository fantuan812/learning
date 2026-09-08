---
type: Reference
title: "第41章 The Heat Vision System for Racing AI: A Novel Way to Determine Optimal Track Positioning"
description: "Game AI Pro 工业级精读：The Heat Vision System for Racing AI: A Novel Way to Determine Optimal Track Positioning。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第41章 The Heat Vision System for Racing AI: A Novel Way to Determine Optimal Track Positioning

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 41.  
> 原文作者 / 资源：[The Heat Vision System for Racing AI: A Novel Way to Determine Optimal Track Positioning](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter41_The_Heat_Vision_System_for_Racing_AI.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

> **原著作者**：Nic Melder  
> **核心归属**：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 41  
> **技术主题**：赛车人工智能（Racing AI）、热视觉系统（Heat Vision System）、空间推理（Spatial Reasoning）、赛道横向偏移行驶线优化（Track Positioning Optimization）

---

## 1. 传统赛车 AI 战术决策的瓶颈与范式转移 (Introduction & Problem Statement)

在主流竞速游戏开发中，赛车 AI 的赛道定位（Track Positioning）与局部路径规划多采用基于离散代理响应的反应式架构（Reactive Architecture）。典型流程为：扫描自车周围的车辆列表，通过规则判定或效用系统（Utility Systems）选定单一主要影响目标（Primary Observed Vehicle），针对该目标做出超车（Overtake）、尾流跟随/跟车（Draft）、阻挡（Block）或避让（Avoid）等离散战术行为，并最终通过导向行为（Steering Behaviors）或横向位移控制器偏离基准最佳行驶线（Ideal Racing Line）。

```
传统单目标决策模型缺陷示意：
[前车 B] (距自车 35m) 
   |
[前车 A] (距自车 15m) 
   |  <-- AI 决定向左变道绕过 A
[自车 AI]
   |  <-- 绕过 A 后立刻返回理想行驶线
   x  <-- 返回后瞬间发现前车 B，被动再次向左变道超车 (导致横向振荡与低效机动)
```

这种“单车辆单反应”模式在车流稀疏时表现稳定，但在高密度车群（Vehicle Pack / Traffic Cluster）中暴露出了核心缺陷：
- **视野割裂与决策抖动（Oscillating Maneuvers）**：AI 针对紧贴前方的车辆完成超车并回归最佳线后，才触发对更前方下一辆慢车的判定，进而被迫再次切出变道。人类顶尖车手在密集车阵中会规划单次连续机动（Single Unified Maneuver）同时超越多车。
- **状态机膨胀与行为冲突**：当左侧并排一辆车、前方紧贴一辆慢车、后方跟随一辆企图超车的快车时，有限状态机（FSM）或分层行为树（Behavior Trees）难以优雅融合避撞、阻挡与切线需求，往往导致高频状态切换（Thrashing）。

**热视觉系统（Heat Vision System）**由战略类即时策略游戏（RTS）的影响力图（Influence Map / Heat Map）启发而来，将复杂的二维车阵战术决策投影并降维为一维动态势场（1D Potential Field）。该系统不显式切换“超车”或“尾流”状态，而是将赛道特征、几何障碍与战术意图全部栅格化写入一条横跨赛道宽度的动态热度线（Heat Line），使多样化的战术驾驶机动自然涌现（Emergent Behaviors）。

---

## 2. 热视觉系统核心数学模型与数据拓扑 (The Heat Vision System)

### 2.1 连续坐标向归一化一维势场的投影

系统在当前目标车辆（Target Vehicle，即执行决策的 AI 自车）所在的纵向赛道截面（Track Cross-Section）定义一条连续的“热度线”（Heat Line）。

设当前目标车所在的赛道中心线弧长（Arc Length）为 $s$，该横截面对应的物理赛道宽度为 $W(s)$。热线物理跨度定义在横向局部坐标系 $x \in [-W(s)/2, +W(s)/2]$ 上。

工业实践中，为了兼顾空间连续性与 Cache 命中率，热线被栅格化为一个固定长度的连续一维浮点数组：
$$\mathcal{H} = [h_0, h_1, h_2, \dots, h_{N-1}], \quad h_i \in \mathbb{R}$$
其中 $N$ 为离散采样栅格数（工业标准通常取 $N = 64$ 或 $128$），浮点数组的索引 $i \in [0, N-1]$ 对应归一化赛道横向偏量 $u \in [0, 1]$：
$$u_i = \frac{i}{N - 1}$$
映射回物理横向坐标 $x_i$：
$$x_i = \left( u_i - 0.5 \right) \cdot W(s)$$

```
赛道横向截面热度线 (Heat Line) 拓扑：
左侧赛道边缘 (u=0.0)                                       右侧赛道边缘 (u=1.0)
|-----------------------------------------------------------------------|
| h[0] | h[1] | ... | h[k] (最佳行驶线) | ... | h[m] (避障高热) | ... | h[N-1] |
|-----------------------------------------------------------------------|
 <-------------------------- 物理宽度 W(s) ---------------------------->
```

### 2.2 势场代数结构：热度的语义定义

热度值（Heat）本质是标量代价函数（Cost Function）：
- **正热量（Positive Heat, $+H$）**：表征“危险”或“阻抗”。包括不可通行的物理障碍（并排车辆）、潜在碰撞风险区、非理想行驶区域。热度越高，AI 越倾向于远离。
- **负热量 / 移除热量（Negative Heat / Heat Removal, $-H$）**：表征“吸引”或“战术增益”。包括最佳行驶线（Racing Line Attraction）、尾流吸力锥（Drafting Zone）、阻挡对手轨迹的压制点（Blocking Spot）。

系统的核心目标是构建全局合成热场 $H_{\text{total}}(u)$，并在考虑横向运动学边界的前提下，求解代数极小值点：
$$u^* = \arg\min_{u \in \mathcal{U}_{\text{feasible}}} H_{\text{total}}(u)$$

---

## 3. 热线合成流水线与多通道写入 (Writing into the Heat Line)

热度线生成分为严谨的三阶段解耦流水线：**视锥与距离剔除（Culling）** $\rightarrow$ **战术几何检测（Testing）** $\rightarrow$ **多通道热签名绘制（Multi-pass Heat Signature Rasterization）**。

```
+-------------------------------------------------------------+
|                Stage 1: 空间范围剔除 (Culling)               |
|      - 纵向距离门限滤波 (|Δs| < 50m)                          |
|      - 特殊边界放行 (后方超高速逼近车辆)                     |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|                Stage 2: 战术情境检测 (Testing)              |
|      - IsOnTrack?                                           |
|      - IsAlongsideTarget?                                   |
|      - IsDraftCandidate? (距离、相对速度、进入角)           |
|      - IsOvertakeCandidate? (快速逼近前车)                   |
|      * 阶段解耦支持其他系统（如脱轨自救行为树）复用检测特征 * |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|          Stage 3: 多通道热签名写入 (Multi-pass Writing)      |
|      Pass 1: 理想行驶线静态引导 (Racing Line Base Attraction) |
|      Pass 2: 物理车体刚性排斥场 (Position/Collision Footprint) |
|      Pass 3: 战术阻挡冷却场 (Tactical Blocking Heat Removal)  |
|      Pass 4: 空气动力学尾流锥 (Aerodynamic Drafting Cone)     |
+-------------------------------------------------------------+
```

### 3.1 阶段 1：距离与语义剔除 (Culling)

为降低空间推理开销，系统首先执行过滤：
1. **纵向距离窗剔除**：仅保留沿赛道中心线投影距离在目标车前后一定范围内的车辆（如纵向距离差 $|\Delta s| \le 50\text{ m}$）。
2. **特殊博弈放行规则**：若后方车辆速度显著高于自车（$\Delta v = v_{\text{rear}} - v_{\text{target}} > v_{\text{threshold}}$），即便纵向距离超出基准阈值，依然保留于候选集，确保防守阻挡行为能提前做出反应。

### 3.2 阶段 2：战术情境检测 (Testing)

针对保留的每一个观察车辆（Observed Vehicle），提取高阶战术特征标记：
- **是否在赛道内（`isOnTrack`）**：若已被撞出路肩，则不施加常规行车势场。
- **是否横向并排（`isAlongside`）**：观察车与目标车的车身包围盒在纵向上存在重叠或极近。
- **是否适宜尾流跟车（`isDraftCandidate`）**：处于目标车前方合理跟车距离窗口内，且自身绝对车速高于预设阈值。
- **是否适宜超车（`isOvertakeCandidate`）**：目标车正以较高相对速度逼近观察车。

**架构设计权衡（Architectural Trade-off）**：  
将 Testing 与 Writing 显式解耦至关重要。若车辆因打转失控（`isSpun`）或冲出赛道，顶层决策行为树将屏蔽热视觉系统，转入“脱轨自救状态机”，此时 Testing 阶段计算的 `isOnTrack` 与姿态检测结果可直接复用，避免重复几何计算。

### 3.3 阶段 3：热签名多通道栅格化数学推导 (Heat Signatures)

#### 通道 A：理想行驶线基底场 (Racing Line Pass)
在没有任何外部车辆干扰时，AI 应严格贴近离线烘焙的理想行驶线 $u_{\text{ideal}}$。该通道写入一个以 $u_{\text{ideal}}$ 为极小值的非对称二次函数或高斯吸引谷：
$$H_{\text{racing}}(u) = -A_{\text{ideal}} \cdot \exp\left( -\frac{(u - u_{\text{ideal}})^2}{2\sigma_{\text{ideal}}^2} \right)$$
其中 $A_{\text{ideal}}$ 控制赛道向心牵引强度，$\sigma_{\text{ideal}}$ 控制允许偏航的带宽。

#### 通道 B：物理车体刚性排斥场 (Position Footprint Pass)
不可与观察车处于同一物理空间。若观察车投影在热线上的中心坐标为 $u_{\text{obs}}$，其热签名呈现为平顶梯形或高阶排斥“热山（Heat Hill）”：
$$H_{\text{pos}}(u) = A_{\text{pos}}(\Delta s) \cdot \max\left( 0, 1 - \left| \frac{u - u_{\text{obs}}}{w_{\text{spread}}} \right|^{p} \right)$$
- **幅值衰减函数 $A_{\text{pos}}(\Delta s)$**：热山高度随观察车与自车的纵向距离差 $|\Delta s|$ 衰减。与自车并排（$\Delta s \to 0$）时，$A_{\text{pos}}$ 达到极大值，构成近乎不可逾越的无限势垒；随着纵向距离拉大，$A_{\text{pos}}$ 按反比例或指数平滑衰减。
- **横向扩展半宽 $w_{\text{spread}}$**：取决于两车包围盒横向物理宽度外扩安全裕量。

#### 通道 C：空气动力学尾流锥衰减场 (Drafting Cone Pass)
对于前方适宜跟车的观察车，其车尾后方会投射出随距离渐狭（或渐宽）、强度减弱的尾流锥（Drafting Cone）。在自车热线上表现为一个热度移除槽（负热量）：
$$H_{\text{draft}}(u) = -A_{\text{draft}} \cdot \mathcal{F}_{\text{dist}}(\Delta s) \cdot \mathcal{F}_{\text{angle}}(\theta) \cdot \exp\left( -\frac{(u - u_{\text{obs}})^2}{2\sigma_{\text{draft}}^2} \right)$$
- $\mathcal{F}_{\text{dist}}(\Delta s)$ 为沿纵向的衰减系数，在最佳尾流距离达到峰值；
- $\mathcal{F}_{\text{angle}}(\theta)$ 衡量目标车是否偏离观察车车尾中心轴角 $\theta$。

```
热签名波形分布剖析：
  Heat Value
    ^
+H  |           [Position 排斥热山] 
    |                 /-----\
    |                /       \
    |               /         \
 0 -+--------------/-----------\-------------------------> Track Normalized Width (u)
    |    \       /                 \       /
    |     \     /                   \     /
    |      \---/                     \---/
-H  |  [Racing Line 引导谷]       [Drafting 尾流吸引槽]
```

---

## 4. 连续性重构与图形学平滑滤波 (Smoothing the Output)

由于离散车辆测试的边界效应（如刚跨过纵向距离阈值的离散跳变）以及不同形状几何叠加，初始合成的一维热线 $\mathcal{H}_{\text{raw}}$ 常呈现严重的阶跃与不连续高频噪点。

若直接在此粗糙曲面上寻优，极小值求解算法极易陷入虚假死锁，导致导向轮在连续帧间发生高频颤振（Snagging / Lateral Jitter）。

### 4.1 高斯平滑与核卷积滤波

工业级管线中，使用一维离散高斯核 $K$ 或箱式滤波（Box Filter）对热线数组执行一次卷积平滑：
$$h_i^{\text{smooth}} = \sum_{k=-M}^{M} K(k) \cdot h_{i+k}$$
标准 3 抽头二项式平滑核：
$$K = \left[ \frac{1}{4}, \frac{1}{2}, \frac{1}{4} \right]$$
或 5 抽头离散高斯核：
$$K = \left[ \frac{1}{16}, \frac{4}{16}, \frac{6}{16}, \frac{4}{16}, \frac{1}{16} \right]$$

**边界处理**：在赛道左边界（$i=0$）与右边界（$i=N-1$）采用截断归一化（Clamped Normalization）或镜像填充（Mirror Reflection），防止赛道边缘势能失真。

---

## 5. 极小值动力学搜索算法 (Determining the Desired Track Position)

获得平滑热线 $\mathcal{H}_{\text{smooth}}$ 后，核心任务是决策出目标横向坐标 $u^*$。

### 5.1 全局极小值陷阱与并排物理阻隔

直觉方案是在数组中直接扫描全局最小值点（Global Minimum）：
$$u_{\text{global\_min}} = \arg\min_{i} h_i^{\text{smooth}}$$
**该方案在物理运动学上完全不可行**：
如果目标车的当前位置与该全局极小值点之间横亘着一座巨大的“热山”（例如旁边正好有一辆车并排行驶），盲目命令赛车横向跨越会导致严重的侧向撞车碰撞。此时，这堵“热山”形成了不可穿透的动态动力学硬约束。

同时，赛车也不能被微小的局部折痕（Tactical Local Crease）永久卡死。AI 需要翻越微小的次级波动，寻找邻近区域的最优解。

```
势能曲面动力学粒子滚落示意：
      Heat H(u)
        ^                     并排车辆构成的巨型热山 (Solid Boundary)
        |                              / \
        |                             /   \
        |                            /     \          全局最小值 (不可越过！)
        |     自车起点              /       \              |
        |       (O)                /         \             v
        |      /   \    微小起伏  /           \          _____
        |     /     \  (可通过)  /             \        /     \
        |    /       \  _/\_    /               \      /       \
        |   /         \/    \  /                 \____/         \
        +--/-----------------\/---------------------------------------> u (横向位置)
            <--- 动量粒子滚向此处的局部极小值点，被巨山阻隔不再向右
```

### 5.2 虚拟质量-弹簧-阻尼粒子滚落算法 (Ball-Rolling with Momentum & Friction)

为兼顾“越过微小噪点起伏”与“被高热山壁反弹/阻挡”的要求，系统采用**带阻尼与动量的物理仿真寻优模型**：将自车的决策目标点抽象为一个在热曲面 $H(u)$ 上滑动的质点（Virtual Ball）。

定义质点坐标为 $p \in [0, 1]$，虚拟速度为 $v \in \mathbb{R}$。

#### 控制方程组 (Governing Equations)
质点受到的下坡驱动力由热度曲线的负空间梯度驱动：
$$F_{\text{gradient}} = -\frac{\partial H(u)}{\partial u}$$
加入黏性摩擦力（Friction Force）以消耗能量避免无限振荡，并引入质量惯性（Mass / Momentum）：
$$F_{\text{friction}} = -\mu \cdot v$$
根据牛顿第二定律，离散时间步更新方程为：
$$\begin{cases}
a_t = -\dfrac{1}{m} \dfrac{\partial H}{\partial u}(p_t) - \gamma v_t \\
v_{t+\Delta t} = v_t + a_t \Delta t \\
p_{t+\Delta t} = p_t + v_{t+\Delta t} \Delta t
\end{cases}$$
其中 $\gamma = \frac{\mu}{m}$ 为阻尼系数。

#### 离散差分与动力学行为特征
在离散网格上，空间导数采用中心差分（Central Difference）估算：
$$\frac{\partial H}{\partial u}(p_t) \approx \frac{h_{\text{index}(p_t) + 1} - h_{\text{index}(p_t) - 1}}{2 \cdot \Delta u}$$

- **微小局部极值（Small Bumps/Creases）**：质点凭借动量 $m \cdot v^2$ 直接冲过微小的能量峰值，避免了静态梯度下降极易陷入局部极小（Snagging）的缺陷。
- **高热山脉（Large Heat Hill）**：质点动能不足以爬升势能差 $\Delta H_{\text{hill}} > \frac{1}{2} m v^2$，质点在山脚减速、停滞并回滚，形成严格的物理边界防护。
- **稳态收敛**：质点最终静止时的坐标即为期望的最佳赛道横向偏量（Desired Track Offset）。该偏量被传递给底层的横向导向控制器（Lateral Steering Controller，如纯追踪算法 Pure Pursuit 或 PID 转向环）。

---

## 6. 纵向速度解耦与归因溯源映射 (Implementation & Attribution)

转向（Steering）仅构成了赛车行为的一个自由度；车速控制（Throttle/Brake）同样决定了战术机动的成败。

例如在安全车引导圈（Parade Lap）或极度密集的跟车工况下，热视觉系统在横向给出了跟随前车的低热度目标，但如果纵向速度控制器不知道自车正在“跟车谁”，AI 就会以全油门撞向前车尾部。

### 6.1 影响源反向映射表 (Attribution Array)

为了打通横向定位与纵向速度规划（Speed Matching）之间的数据断层，工业实现中引入了**归因溯源数组（Attribution Array）**，该数组与热线同维度拓扑对齐：
$$\mathcal{A} = [a_0, a_1, \dots, a_{N-1}], \quad a_i \in \text{VehicleID} \cup \{\text{None}\}$$

在多通道写入阶段，每当一个通道在栅格 $i$ 施加了主导热变化量（如最大排斥或最大尾流），系统将产生该贡献的观察车辆指针或唯一 ID 记录在 $a_i$ 中：
$$a_i = \arg\max_{V_k} \left| \Delta h_i^{(V_k)} \right|$$

```
热线与车辆归因映射对齐：
Index i:    [  0  |  1  | ... |  34  |  35  |  36  | ... | 127 ]
Heat H:     [ 1.2 | 1.1 | ... | -2.5 | -2.8 | -2.1 | ... | 0.4 ]
Attribution:[None |None | ... | Car_B| Car_B| Car_B| ... | Car_C]
                                 ^
                                 |-- AI 选定 index 35 为最佳横向点
                                 |-- 溯源查询直接获知主导车辆为 Car_B
                                 |-- 联动机制：触发对 Car_B 的纵向匹配速度算法
```

### 6.2 纵向速度联动与可视化调试优势
1. **纵向控制解耦联动**：质点最终稳定在栅格 $i^*$，系统直接通过查询 $a_{i^*}$ 检索出当前对其战术位置施加最深影响的车辆对象。若 $a_{i^*}$ 属于一个尾流通道贡献者，自车纵向决策模块立即切换为“自适应巡航（ACC）/ 速度对齐”策略；若属于避碰贡献者，则触发降速让行逻辑。
2. **生产管线级调试（Runtime Debugging）**：在游戏开发 HUD 视图中，图形渲染管道可直接将热线以彩色条带形式投影在赛道地表（高热显红、低热显绿），并在目标点上空拉出一条射线连接到归因车辆 $a_{i^*}$，极大地降低了密集车阵中 AI 战术意图不可追踪的黑盒排障成本。

---

## 7. 车手个性化、难度动态调配与参数矩阵 (Driver Personality & Calibration)

热签名的几何参数并非全局硬编码常量，而是由车手个性化配置（Driver Personality Profiles）与动态游戏难度缩放（Dynamic Difficulty Adjustment, DDA）驱动的参数化多维向量。

### 7.1 签名特征多维形变矩阵

对于任一热签名，其在空间中具备纵向（Along-track Length）、横向（Cross-track Width）及强度衰减（Falloff Curve）等独立调配轴：

| 热签名类型 (Signature Type) | 空间调配轴 (Axes) | 个性化特征映射 (Driver Personality Mapping) |
| :--- | :--- | :--- |
| **位置排斥场 (Position Heat)** | 横向宽度 $w_{\text{spread}}$ | **激进程度 (Aggressiveness)**：激进车手 $w_{\text{spread}}$ 紧缩（如仅向外扩 0.5m），允许极其极限贴近超车；保守车手 $w_{\text{spread}}$ 极大（外扩 2.0m），超车时留出巨大横向安全富余。 |
| **位置排斥场 (Position Heat)** | 纵向影响窗 $L_{\text{dist}}$ | **车手感知敏锐度 (Awareness)**：高难度 AI 感知距离远，提前变线；低难度 AI 感知滞后，产生“贴身肉搏”的戏剧化效果。 |
| **尾流吸引锥 (Drafting Cone)** | 锥体有效长度 $L_{\text{draft}}$ | **战术执行倾向 (Tactical Affinity)**：高阶车手具备深长的尾流判定区，倾向于长距离巡航蓄力；业余车手尾流锥短小，极少主动吸风。 |
| **尾流吸引锥 (Drafting Cone)** | 扩散张角 $\theta_{\text{angle}}$ | **横向追尾容差**：张角较宽时，允许 AI 在较大偏离角下依然被吸入前车正后方。 |
| **阻挡防守场 (Blocking Pass)** | 响应横向死区与增益 | **防守反击倾向 (Defensiveness)**：防守型车手在后车变道时，在对应横向位置大幅降低热度，产生主动“压车位”横移。 |

---

## 8. 工业级 C++ 生产实现参考 (Engine-Ready Implementation)

以下代码展示了热视觉系统的核心引擎级实现规范，包括数据布局、平滑滤波与带阻尼的质点动力学寻优过程：

```cpp
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>

class HeatVisionSystem {
public:
    static constexpr int32_t K_HEAT_LINE_SAMPLES = 128;
    static constexpr float K_INVALID_DISTANCE = -1.0f;

    struct VehicleState {
        uint32_t id;
        float trackS;           // 沿赛道纵向距离 (米)
        float trackNormalizedU; // 归一化横向偏量 [0.0 = 左边缘, 1.0 = 右边缘]
        float speed;            // 沿赛道绝对车速 (m/s)
        bool  isOnTrack;        // 是否在赛道边界内
    };

    struct DriverProfile {
        float aggressiveness;   // [0.0 = 极保守, 1.0 = 极激进]
        float draftingAffinity; // [0.0 = 从不尾流, 1.0 = 深度尾流]
        float lateralSafetyMargin; // 横向安全间距基准 (米)
    };

private:
    float m_heatLine[K_HEAT_LINE_SAMPLES];
    float m_smoothedHeatLine[K_HEAT_LINE_SAMPLES];
    uint32_t m_attributionMap[K_HEAT_LINE_SAMPLES];

    float m_trackWidth;
    DriverProfile m_profile;

public:
    HeatVisionSystem(float trackWidth, const DriverProfile& profile)
        : m_trackWidth(trackWidth), m_profile(profile) {
        Reset();
    }

    void Reset() {
        std::fill(std::begin(m_heatLine), std::end(m_heatLine), 0.0f);
        std::fill(std::begin(m_smoothedHeatLine), std::end(m_smoothedHeatLine), 0.0f);
        std::fill(std::begin(m_attributionMap), std::end(m_attributionMap), 0);
    }

    /// <summary>
    /// 步骤 1: 栅格化基底最佳行驶线
    /// </summary>
    void WriteIdealRacingLine(float idealU, float strength = 1.0f) {
        const float sigma = 0.08f; // 行驶线波谷标准差
        for (int32_t i = 0; i < K_HEAT_LINE_SAMPLES; ++i) {
            float u = static_cast<float>(i) / (K_HEAT_LINE_SAMPLES - 1);
            float diff = u - idealU;
            // 写入负热量作为基底吸引场
            m_heatLine[i] -= strength * std::exp(-(diff * diff) / (2.0f * sigma * sigma));
        }
    }

    /// <summary>
    /// 步骤 2: 观察车辆多通道热签名栅格化
    /// </summary>
    void ProcessObservedVehicle(const VehicleState& target, const VehicleState& obs) {
        if (!obs.isOnTrack) return;

        float deltaS = obs.trackS - target.trackS;
        if (std::abs(deltaS) > 50.0f) return; // 距离剔除

        // 1. 物理包围盒排斥山峰 (Position Heat Mountain)
        // 激进程度越低，横向外扩半径越广
        float widthMargin = m_profile.lateralSafetyMargin * (1.5f - 0.8f * m_profile.aggressiveness);
        float normalizedSpread = widthMargin / m_trackWidth;
        
        // 纵向距离越近，排斥势能呈反比急剧增大
        float distanceWeight = 1.0f / (std::max(std::abs(deltaS), 1.0f) / 5.0f);
        float positionPeakHeat = 15.0f * distanceWeight;

        int32_t centerIndex = static_cast<int32_t>(obs.trackNormalizedU * (K_HEAT_LINE_SAMPLES - 1));
        int32_t spreadSamples = static_cast<int32_t>(normalizedSpread * K_HEAT_LINE_SAMPLES);

        int32_t minIdx = std::max(0, centerIndex - spreadSamples);
        int32_t maxIdx = std::min(K_HEAT_LINE_SAMPLES - 1, centerIndex + spreadSamples);

        for (int32_t i = minIdx; i <= maxIdx; ++i) {
            float u = static_cast<float>(i) / (K_HEAT_LINE_SAMPLES - 1);
            float distNorm = std::abs(u - obs.trackNormalizedU) / (normalizedSpread + 1e-5f);
            float heatAdded = positionPeakHeat * std::max(0.0f, 1.0f - distNorm * distNorm);
            
            m_heatLine[i] += heatAdded;
            if (heatAdded > 1.0f) {
                m_attributionMap[i] = obs.id; // 登记影响源
            }
        }

        // 2. 尾流吸引锥 (仅当观察车辆在前方且具备尾流价值时)
        if (deltaS > 4.0f && deltaS < 30.0f && m_profile.draftingAffinity > 0.05f) {
            float draftSigma = 0.035f;
            float draftDepth = 4.0f * m_profile.draftingAffinity * (1.0f - (deltaS / 30.0f));
            for (int32_t i = 0; i < K_HEAT_LINE_SAMPLES; ++i) {
                float u = static_cast<float>(i) / (K_HEAT_LINE_SAMPLES - 1);
                float diff = u - obs.trackNormalizedU;
                float draftReduction = draftDepth * std::exp(-(diff * diff) / (2.0f * draftSigma * draftSigma));
                m_heatLine[i] -= draftReduction;
            }
        }
    }

    /// <summary>
    /// 步骤 3: 5 抽头高斯平滑滤波，消除微观离散抖动
    /// </summary>
    void SmoothHeatLine() {
        static constexpr float Kernel[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };
        for (int32_t i = 0; i < K_HEAT_LINE_SAMPLES; ++i) {
            float accum = 0.0f;
            float weightSum = 0.0f;
            for (int32_t k = -2; k <= 2; ++k) {
                int32_t sampleIdx = i + k;
                if (sampleIdx >= 0 && sampleIdx < K_HEAT_LINE_SAMPLES) {
                    float w = Kernel[k + 2];
                    accum += m_heatLine[sampleIdx] * w;
                    weightSum += w;
                }
            }
            m_smoothedHeatLine[i] = accum / weightSum;
        }
    }

    /// <summary>
    /// 步骤 4: 虚拟质量-弹簧-阻尼质点滚落动力学寻优
    /// </summary>
    /// <param name="currentVehicleU">自车当前横向位置归一化坐标 [0, 1]</param>
    /// <param name="outInfluencerVehicleId">输出造成该决策的主要关联车体 ID</param>
    /// <returns>期望的最佳横向偏量归一化坐标 [0, 1]</returns>
    float FindOptimalTrackPosition(float currentVehicleU, uint32_t& outInfluencerVehicleId) {
        float ballPos = std::clamp(currentVehicleU, 0.0f, 1.0f);
        float ballVel = 0.0f;

        const float mass = 1.0f;
        const float dampingGamma = 8.0f; // 黏性阻尼耗散动能
        const float dt = 0.016f;         // 模拟迭代子步长
        const int32_t maxIterations = 60;
        const float du = 1.0f / (K_HEAT_LINE_SAMPLES - 1);

        for (int32_t step = 0; step < maxIterations; ++step) {
            // 计算当前连续位置对应的数组采样点中心差分梯度
            float clampedPos = std::clamp(ballPos, du, 1.0f - du);
            int32_t idx = static_cast<int32_t>(clampedPos * (K_HEAT_LINE_SAMPLES - 1));
            
            float grad = (m_smoothedHeatLine[idx + 1] - m_smoothedHeatLine[idx - 1]) / (2.0f * du);
            
            // 下坡受力与阻尼受力
            float force = -grad - dampingGamma * ballVel;
            float accel = force / mass;

            ballVel += accel * dt;
            ballPos += ballVel * dt;

            // 赛道物理边界约束
            if (ballPos <= 0.0f) { ballPos = 0.0f; ballVel = 0.0f; }
            if (ballPos >= 1.0f) { ballPos = 1.0f; ballVel = 0.0f; }

            // 速度极小收敛判定
            if (std::abs(ballVel) < 1e-4f && std::abs(force) < 1e-3f) {
                break;
            }
        }

        int32_t finalIdx =
