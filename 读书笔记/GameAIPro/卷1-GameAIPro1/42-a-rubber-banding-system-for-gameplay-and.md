---
type: Reference
title: "第42章 A Rubber-Banding System for Gameplay and Race Management"
description: "Game AI Pro 工业级精读：A Rubber-Banding System for Gameplay and Race Management。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第42章 A Rubber-Banding System for Gameplay and Race Management

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 42.  
> 原文作者 / 资源：[A Rubber-Banding System for Gameplay and Race Management](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter42_A_Rubber-Banding_System_for_Gameplay_and_Race_Management.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

在竞速类游戏（Racing Games）的工业级开发中，**橡皮筋系统（Rubber-Banding System）**是用于动态调节竞争节奏、维持赛道紧张感与心流体验的核心机制。其设计目标在于：当 AI 赛车远离玩家时，通过动态缩放调整其性能表现，使其像被一条无形的“橡皮筋”牵引一样始终围绕玩家展开竞争。

若实现粗糙，该系统会导致极其严重的“AI 作弊感（Artificial Cheating）”，粉碎玩家对物理法则和竞技公平性的信任；若设计得当，系统能够润物细无声地维系比赛悬念。本技术文档系统化拆解基于驾驶技能与动力联合调控的高级橡皮筋系统、边界状态管理、分屏与群聚抑制算法，并介绍面向长距离赛事的全局比赛节奏系统（Race Pace System）。

---

## 1. 核心数学模型与分区分段实现（Rubber-Banding Implementation）

橡皮筋系统的核心是建立 **AI 车辆相对于玩家的赛道纵向距离 $\Delta s$** 与 **调节乘数（Rubber-Banding Multiplier, $M$）** 之间的映射关系。

设定赛道纵向距离差：
$$\Delta s = s_{\text{AI}} - s_{\text{Player}}$$
其中：
- $\Delta s > 0$ 表示 AI 车辆处于玩家前方；
- $\Delta s < 0$ 表示 AI 车辆处于玩家后方。

为防止在近身对抗中暴露调控痕迹，系统沿赛道纵向划分出五个核心影响区间（Regions a–e）。

```
           M (Multiplier)
           ^
    M_max -+--[Region a]--\
           |               \  [Region b: 逆向橡皮筋 Reverse Banding]
      1.0 -+----------------+========[Region c: 死区 Dead Zone]========+----------------
           |                                                            \  [Region d: 正向橡皮筋 Forward Banding]
    M_min -+-------------------------------------------------------------+--[Region e]--->
           |
         --+-------+---------------+--------------------+---------------+-------+-------> Δs
                 -d_rev_max      -d_rev_min               d_fwd_min       d_fwd_max  (Distance)
```

### 1.1 赛道分区定义

| 区域标识 | 区域名称 | 距离区间 $\Delta s$ | 调节性质 | 行为表现 |
| :--- | :--- | :--- | :--- | :--- |
| **Region a** | 逆向饱和区（Reverse Saturation） | $(-\infty, -d_{\text{rev\_max}}]$ | 恒定最大增益 | $M = M_{\text{max}}$，落后过远的 AI 维持最大追赶补偿。 |
| **Region b** | 逆向渐变区（Reverse Banding） | $(-d_{\text{rev\_max}}, -d_{\text{rev\_min}})$ | 线性/平滑正向增益 | 随着 AI 接近玩家，追赶增益逐渐衰减回 $1.0$。 |
| **Region c** | 绝对死区（Dead Zone） | $[-d_{\text{rev\_min}}, d_{\text{fwd\_min}}]$ | 完全禁用（$M = 1.0$） | 车辆处于近身对抗范围，物理与技能完全标准，杜绝破绽。 |
| **Region d** | 正向渐变区（Forward Banding） | $(d_{\text{fwd\_min}}, d_{\text{fwd\_max}})$ | 线性/平滑负向衰减 | 领先的 AI 随领先距离增加而持续降低性能。 |
| **Region e** | 正向饱和区（Forward Saturation） | $[d_{\text{fwd\_max}}, +\infty)$ | 恒定最小衰减 | $M = M_{\text{min}}$，领先过多的 AI 维持基准惩罚，防止彻底停滞。 |

### 1.2 传递函数（Transfer Functions）推导

#### 线性分段传递函数（Piecewise Linear Transfer Function）

$$M(\Delta s) = \begin{cases} 
M_{\text{max}}, & \Delta s \le -d_{\text{rev\_max}} \\
1.0 + (M_{\text{max}} - 1.0) \cdot \dfrac{-\Delta s - d_{\text{rev\_min}}}{d_{\text{rev\_max}} - d_{\text{rev\_min}}}, & -d_{\text{rev\_max}} < \Delta s < -d_{\text{rev\_min}} \\
1.0, & -d_{\text{rev\_min}} \le \Delta s \le d_{\text{fwd\_min}} \\
1.0 - (1.0 - M_{\text{min}}) \cdot \dfrac{\Delta s - d_{\text{fwd\_min}}}{d_{\text{fwd\_max}} - d_{\text{fwd\_min}}}, & d_{\text{fwd\_min}} < \Delta s < d_{\text{fwd\_max}} \\
M_{\text{min}}, & \Delta s \ge d_{\text{fwd\_max}}
\end{cases}$$

#### 平滑过渡（Sigmoid / Smoothstep）优化
线性映射在分界拐点处的一阶导数不连续，易导致 AI 在拐点边界产生突兀的加减速波动。工业级引擎中常采用三次 Hermite 插值（Smoothstep）或 Sigmoid 函数来保障曲率连续性（$C^1$ 或 $C^2$ 连续）：

$$S(t) = 3t^2 - 2t^3, \quad t \in [0, 1]$$

以正向渐变区（Region d）为例，平滑过渡乘数计算为：
$$t = \frac{\Delta s - d_{\text{fwd\_min}}}{d_{\text{fwd\_max}} - d_{\text{fwd\_min}}}$$
$$M(\Delta s) = 1.0 - (1.0 - M_{\text{min}}) \cdot S(t)$$

---

## 2. 动力调节与技能调节的双层控制架构（Power-Based vs. Difficulty-Based）

单纯调整赛车底层动力（Power-Based）会破坏物理一致性；单纯调整驾驶技能（Difficulty/Skill-Based）受限于物理上限与下限。最优解是采用**两级级联控制架构（Cascaded Two-Tier Control Architecture）**。

```
                       [目标调节乘数 M (Target Multiplier)]
                                       |
                   +-------------------+-------------------+
                   |                                       |
     [优先层: 驾驶员技能调节 (Skill-Based)]       [溢出层: 车辆动力调节 (Power-Based)]
                   |                                       |
    * 修改入弯刹车点 (Braking Point)            * 缩放引擎扭矩输出 (Torque Scale)
    * 修改弯道目标速度 (Corner Apex Speed)       * 修改最大极速上限 (Top Speed)
    * 修改油门开度斜率 (Throttle Ramp Rate)      * 修改轮胎抓地力补偿 (Tire Grip Bias)
                   |                                       |
                   +-------------------+-------------------+
                                       |
                             [输出到底层物理与行为树]
```

### 2.1 调控维度对比与分层原则

1. **优先调节驾驶员技能（Skill/Difficulty Adjustment）**：
   - **机制**：通过调节 AI 路径跟踪（Path Following）和油门/刹车决策的虚拟技能参数，使 AI“更早刹车”、“降低弯心速度”、“推迟出弯全油门时机”。
   - **优势**：AI 依然运行在与玩家完全相同的物理模型之下，没有额外施加外力或扭矩，玩家在旁观观察时不会察觉“作弊”。
   - **缺陷**：调节动态范围有限。当 AI 技能提升至 $1.0$（完美走线、极致晚刹车）时，无法再进一步提速；当技能降至系统设定的最低阈值时，若仍无法让玩家追上，则无法继续减速。

2. **溢出后介入车辆动力（Power-Based Adjustment）**：
   - **机制**：直接修改动力总成（Powertrain）输出，按比例缩放引擎马力、扭矩曲线或推力向量。
   - **规则**：**动力乘数仅在技能乘数达到物理边界（最大值或最小值）后才开始偏移**。

### 2.2 级联联动数学模型

设定总橡皮筋目标乘数为 $M_{\text{total}}$，拆分为技能乘数 $M_{\text{skill}} \in [S_{\text{min}}, S_{\text{max}}]$ 与动力乘数 $M_{\text{power}} \in [P_{\text{min}}, P_{\text{max}}]$：

$$\begin{cases}
\text{若 } M_{\text{total}} > 1.0 \text{ (需要加速追赶)}: \\
\quad M_{\text{skill}} = \min(M_{\text{total}}, S_{\text{max}}) \\
\quad M_{\text{power}} = \max\left(1.0, \dfrac{M_{\text{total}}}{S_{\text{max}}}\right) \\
\\
\text{若 } M_{\text{total}} < 1.0 \text{ (需要减速等待)}: \\
\quad M_{\text{skill}} = \max(M_{\text{total}}, S_{\text{min}}) \\
\quad M_{\text{power}} = \min\left(1.0, \dfrac{M_{\text{total}}}{S_{\text{min}}}\right)
\end{cases}$$

如下图所示，动力乘数（Power multiplier）在死区及中度距离内保持恒定为 $1.0$，仅在技能乘数（Difficulty multiplier）饱和后启动：

```
Multiplier
    ^
1.4 +--               /----------------- Power multiplier (动力乘数在技能饱和后启动)
1.2 +--        /-----+------------------ Difficulty multiplier (技能乘数优先响应)
1.0 +--=======+=======+=======
0.8 +--       | /-----+----------------- Difficulty multiplier
0.6 +--       |/------------------------ Power multiplier
      ----+---+---+---+---+---+---+---> Distance from Player (m)
        -300 -200 -100 0 100 200 300
```

---

## 3. 纯动力调节的工业级工程隐患及对策（Power-Based Issues & Mitigation）

若处理不当，直接修改动力参数会导致底层物理模拟出现严重异常：

### 3.1 动力过载导致抓地力崩溃（Traction Overwhelm & Spin-Out）
- **现象**：落后的 AI 获得 $1.2 \sim 1.4$ 倍动力增益后，在出弯深踩油门时瞬间突破摩擦圆（Circle of Friction）极限，导致驱动轮空转打滑、车辆失控掉头（Spin-out）。
- **工程解决方案**：
  1. **配置预设底盘（Setup for Max Power）**：在调校 AI 悬挂、齿轮比和抓地力时，直接以“最大橡皮筋增益”状态作为基准状态调校。
  2. **动态抓地力补偿（Dynamic Grip Scaling）**：将轮胎纵向/横向摩擦系数与动力增益进行正相关绑定：
     $$\mu_{\text{effective}} = \mu_{\text{base}} \cdot \max(1.0, M_{\text{power}} \cdot k_{\text{grip\_comp}})$$
  3. **虚拟牵引力控制（Traction Control Assist）**：在 AI 的导向行为与低级执行器中强制注入纵向滑移率约束（Slip Ratio Limiter）。

### 3.2 极端低功率下的动力瘫痪（Underpowered Stall & Shifting Deadlock）
- **现象**：当领跑 AI 处于极低动力（如 $M_{\text{power}} = 0.7$）且进入上坡路段时，发动机输出扭矩可能低于重力分量阻力与滚动阻力之和，导致车辆在陡坡失速停滞；同时自动换挡逻辑（Automatic Transmission Logic）可能陷入持续在低挡与高挡之间跳跃震荡的死循环。
- **工程解决方案**：
  - 设置基于路面坡度（Road Gradient）和当前车速的**动态动力下限保底机制**。
  - 在挡位决策黑板（Blackboard）中，基于轮上实际净扭矩而非额定扭矩进行升挡转速判断。

### 3.3 音效失真与转速爆表（Audio Pitch & Rev Limiter Glitches）
- **现象**：竞速游戏的发动机音效通常由车辆物理转速（RPM）和发动机负载率（Load）直接驱动合成。额外动力使转速爬升斜率异乎寻常地陡峭，听觉体验假；在最高挡位甚至持续撞断油线（Hit the Rev Limiter），引发高频尖锐的断油音效 Bug。
- **工程解决方案**：
  - 音频合成引擎（Audio Engine）读取**平滑后**的虚拟转速，对输出给音频总线的 RPM 进行一阶低通滤波（Low-pass Filter）。
  - 若 $M_{\text{power}} > 1.0$，对最高挡位的传动比或最终减速比进行动态虚拟微调，平抑超速撞限。

---

## 4. 熔断机制：必须禁用橡皮筋的边界情境（Disabling Rubber-Banding）

在特定赛道阶段与临界状态下，橡皮筋逻辑会破坏核心玩法，必须设计高优先级的**条件熔断机制（Failsafe Bypass）**。

```
[橡皮筋调节管理器 Update]
        |
        +---> [检查: 处于起跑第一弯阶段?] --------YES-----> [进入起跑展开模式 (禁用或反向补偿)]
        |
        +---> [检查: AI 车速是否低于启动阈值?] ----YES-----> [强制恢复基准动力 (1.0)]
        |
        +---> [检查: AI 是否即将套圈玩家?] --------YES-----> [冻结负向衰减 (平滑过渡至 1.0)]
        |
        NO (所有检查通过)
        |
        v
[执行常规多级橡皮筋计算]
```

### 4.1 起跑阶段与第一弯堆积（Race Start & First Corner Pileup）
- **诱因**：发车后，车群密集冲向首个弯角。若此时玩家起步较慢落后，前方处于领先的 AI 车辆会立刻触发正向橡皮筋（Forward Banding）减速。这会导致原本需要单列有序通过弯角的 AI 群体被迫减速扎堆，引发大面积追尾、撞车与路线死锁。
- **解决方案**：在起跑后的一定时间或通过第一个路网路点（Check Point/Way Point）之前，完全禁用正向橡皮筋；甚至施加轻微的反向增益（Reverse Banding to Front Cars），促使车群迅速拉开间距，以单列纵队（Single File）安全通过第一弯。

### 4.2 极低车速与事故重启（Low Speed & Post-Crash Recovery）
- **诱因**：领先的 AI 发生碰撞、调头或被撞出赛道后处于静止（车速 $v \approx 0$）。由于其在赛道投影距离仍大幅领先于玩家，正向橡皮筋会继续大幅削减其动力输出，导致车辆扭矩不足以起步脱困。
- **解决方案**：设定绝对速度阈值 $v_{\text{low}}$（如 $20\text{ km/h}$）。当 AI 车速 $v_{\text{AI}} < v_{\text{low}}$ 时，强制将 $M_{\text{power}}$ 重置并钳位在 $1.0$ 以上，直到其完全重新恢复巡航速度。

### 4.3 套圈处理机制（Being Lapped Scenario）
- **诱因**：在闭环环形赛道（Closed Circuits）中，高水平 AI 可能在赛程后段反超并套圈（Lap）落后玩家。在未做圈数解耦的沿线空间推理中，AI 会被系统判定为“领先玩家将近一整圈（数千米）”，进而施加极限负向衰减（最低技能 + 极限低动力）。结果就是跑在第一名的领头车在玩家眼前像拖拉机一样龟速爬行，破坏沉浸感。
- **解决方案**：基于检查点圈数差（Lap Differential $\Delta \text{Lap}$）进行逻辑切断：
  $$\text{若 } \Delta \text{Lap} \ge 1 \implies \text{完全绕过橡皮筋系统，切换至纯难度脚本或基准巡航模式。}$$

---

## 5. 多人同屏分屏模式架构（Split-Screen Multiplayer Handling）

在分屏多人游戏（Local Split-Screen）中存在多个真实玩家，单一的“玩家绝对参考点”失效。

```
赛道流向:  ======================================================>>>>>
             [AI_Behind]    [Player_2]    [AI_Mid]    [Player_1]    [AI_Ahead]
                 |              |            |            |             |
                 +<--(Δs_rev)---+            |            +---(Δs_fwd)->+
                 |                           |                          |
           [逆向橡皮筋]                 [无橡皮筋干预]              [正向橡皮筋]
        (参考 Player_2)                                         (参考 Player_1)
```

系统采用**区间双锚点解耦算法（Dual-Anchor Zone Partitioning Algorithm）**：
1. **排序与锚点识别**：获取所有玩家车辆在赛道上的沿线距离，标记处于最前方的玩家车辆 $P_{\text{foremost}}$ 与处于最后方的玩家车辆 $P_{\text{rearmost}}$。
2. **正向分区干预**：对于处于所有玩家前方的 AI 车辆（$s_{\text{AI}} > s(P_{\text{foremost}})$），以 $P_{\text{foremost}}$ 的位置为基准计算距离并施加正向橡皮筋（Forward Banding 减速）。
3. **逆向分区干预**：对于处于所有玩家后方的 AI 车辆（$s_{\text{AI}} < s(P_{\text{rearmost}})$），以 $P_{\text{rearmost}}$ 的位置为基准计算距离并施加逆向橡皮筋（Reverse Banding 加速）。
4. **内部中立区（Inter-Player Neutral Zone）**：对于处于两名（或多名）玩家之间的 AI 车辆，完全关闭橡皮筋系统（$M = 1.0$），保证玩家之间的缠斗以及与夹层 AI 的竞争不受外力干扰。

---

## 6. 前方 AI 群聚压缩消除技术（Anti-Bunching Techniques）

在正向橡皮筋生效时，由于越靠前的赛车减速比例越大，导致靠后的领先赛车快速追上前方赛车，前方 AI 车队会迅速压缩成密集方阵（Bunching）。
- **严重玩法缺陷**：当玩家好不容易追上前方车队时，面对的是挤作一团的多个对手，玩家往往能在一个弯道利用外切内或一次晚刹车瞬间超越数辆赛车。这种“批发式超车”剥夺了与对手逐一缠斗（Dogfight）的乐趣。

针对该工业通病，本书提出了两套经典的抗群聚算法：

```
[方案 1: 最远车辆距离拉伸 (Dynamic Scale Stretching)]
 传统:   [玩家] ----(d_fwd_max)----> [饱和惩罚] 导致中途车辆梯度骤变压缩
 方案1:  [玩家] ------------------------(D_max = max(d_max, Dist_furthest))------------------------> [平缓线性分布]

[方案 2: 基于质心的群体一致性减速 (Group Average Position)]
 方案2:  [玩家] ----------> [门限 Threshold] | [AI_1]   [AI_2]   [AI_3]
                                                   \       |       /
                                                [计算平均赛道位置 S_avg]
                                                           |
                                               [计算唯一样条乘数 M(S_avg)]
                                                           |
                                               [将 M 广播赋予该车队全员]
                                             (各车间距保持刚性，同比例平移减速)
```

### 6.1 动态尺度拉伸法（Dynamic Scale Stretching via Furthest Vehicle）
- **核心逻辑**：不使用固定的 $d_{\text{fwd\_max}}$ 作为负向乘数的最远点，而是动态跟踪前方所有 AI 中距离玩家最远的那辆车的位置 $d_{\text{furthest}}$。
- **距离尺度计算**：
  $$D_{\text{effective\_max}} = \max\left(d_{\text{fwd\_max\_config}}, \max_{i \in \text{FrontCars}}(s_i - s_{\text{Player}})\right)$$
- **效果分析**：通过动态扩展减速衰减的跨度分母，拉平了前方整个区间的调节梯度（Gradient），有效延缓并减弱了后车向前车压缩贴合的速度。

### 6.2 质心均值同步衰减法（Group Average Position Method）
- **核心逻辑**：将超过一定前向距离阈值（Distance Threshold）的前方 AI 车队视为一个“刚性集群（Cohort）”。
- **算法流程**：
  1. 筛选前方满足 $s_i - s_{\text{Player}} > d_{\text{threshold}}$ 的车辆集合 $\mathcal{C}$，集合内车辆数量为 $N$。
  2. 计算集群的几何质心（平均赛道距离）：
     $$\bar{s}_{\text{cohort}} = \frac{1}{N} \sum_{i \in \mathcal{C}} s_i$$
  3. 基于 $\Delta \bar{s} = \bar{s}_{\text{cohort}} - s_{\text{Player}}$ 计算出一个**全局统一的衰减乘数** $M_{\text{cohort}}(\Delta \bar{s})$。
  4. 将 $M_{\text{cohort}}$ 无差别地赋给集合 $\mathcal{C}$ 中的每一辆赛车。
- **效果分析**：由于所有车被施加完全相同的乘数，它们在橡皮筋作用下的性能衰减完全同步，保持原有的相对车距不变，彻底消除群聚效应。

---

## 7. 核心参数调校与关卡难度平衡（Tuning & Difficulty Calibration）

参数调校直接决定了玩家的宏观心理感知与关卡难度曲线：

```
                    [ 负向偏移配置: 难度较低模式 ]
                         d_fwd_min < 0
                             <---|
---------------------------------+=================[Player]=================> 赛道正向
                                 0
                             |--->
                    [ 正向偏移配置: 真实硬核模式 ]
                         d_fwd_min > 0
```

### 7.1 绝对死区（Region c）的调校准则
- **下限约束**：死区必须完全覆盖玩家的近景视野与后视镜感知区域。若 AI 在死区边缘出现异常加速，玩家会直接在后视镜中观察到 AI 获得超自然加速度；
- **上限约束**：死区不可设置过宽。若死区过宽，落后的 AI 在进入视野边缘前已停滞逆向增益，会导致玩家长时间陷入“孤立巡航”的枯燥状态。

### 7.2 利用橡皮筋基线实现全局难度平衡
可以通过平移橡皮筋的核心区间端点来实现动态难度自适应（DDA）或游戏难度档位调节：
- **休闲/简单模式（Negative Distance Offset）**：将正向橡皮筋的触发下限 $d_{\text{fwd\_min}}$ 设为负值（如 $-10\text{ m}$）。这意味着即便 AI 与玩家并驾齐驱（Side by Side）甚至轻微落后时，AI 的动力就已经处于受限压制状态，使玩家在直道拼极速和并排行驶时能轻松超车。
- **硬核/竞速模式（Extended Dead Zone & Scaled Max）**：扩大 $d_{\text{fwd\_max}}$ 的取值范围，使 AI 车队在赛道上保持分散布局，逼迫玩家必须历经多次孤立的长距离缠斗才能逐步提升名次。

---

## 8. 特殊领头羊效应与第一名调校（Special Case: First Place Rubber-Banding）

在赛车竞速中，处于第一名的车辆（领跑车）具备极大的物理先天优势：
- **前流与净空（Clear Air）**：前方没有任何障碍车辆切断走线；
- **无需执行避让与超车行为（No Defensive/Overtaking Maneuvers）**：AI 可以完全沿着预先烘焙的“完美几何赛车线（Ideal Racing Line）”全速行驶。

这就导致即使在没有任何橡皮筋加成的情况下，第一名通常也会迅速与第二名及整个后续车群拉开巨大的断层间距。

### 8.1 解决方案：基于冠亚军相对距离的独立橡皮筋（P1-to-P2 Rubber-Banding）

为解决领头羊逃逸问题，系统为第一名车辆注入**独立二级控制器**：
- **度量指标**：度量标准不再是第一名到玩家的距离，而是第一名与第二名（无论第二名是 AI 还是玩家）之间的即时赛道距离差：
  $$\Delta s_{\text{P1-P2}} = s_{\text{Rank 1}} - s_{\text{Rank 2}}$$
- **控制方程**：
  $$M_{\text{Rank 1}} = \text{Clamp}\left(1.0 - \alpha \cdot \frac{\Delta s_{\text{P1-P2}} - d_{\text{gap\_threshold}}}{d_{\text{gap\_max}}}, M_{\text{P1\_min}}, 1.0\right)$$
- **工程效果**：只要第一名甩开第二名超过预设间距阈值，其驾驶员技能参数（弯心减速度、油门激进度）即被动态压制，从而将领跑优势压缩在可控范围内，维持领奖台争夺悬念。

---

## 9. 拓展进阶：面向长距离耐力赛的比赛节奏系统（Race Pace System）

对于 30 至 50 圈的长距离耐力锦标赛，单纯的即时距离橡皮筋系统会导致整场比赛平铺直叙，缺乏现实赛事中跌宕起伏的策略感。此时需将橡皮筋底层基础设施升维扩展为全局**比赛节奏系统（Race Pace System）**。

### 9.1 多阶段比赛节奏状态机（Race Pace State Machine）

比赛节奏系统通过在不同宏观赛程阶段动态重写 AI 的基础基准技能与动力目标，实现拟真化的赛程推进：

```
[50圈耐力赛全局节奏配置]
 0                   10                               40                  50 (Laps)
 +-------------------+--------------------------------+-------------------+
 |  爆发推进阶段      |        策略巡航保胎阶段         |    终局冲刺冲线   |
 |  (Push Hard Pace) |        (Conserve & Cruise)     |    (Final Attack) |
 |  基准乘数: 1.05   |        基准乘数: 0.95          |    基准乘数: 1.08 |
 +-------------------+--------------------------------+-------------------+
```

### 9.2 进站窗口出入场圈突变（In-Lap / Out-Lap Pace Dynamics）
若赛事包含进站加油换胎（Pit Stops），比赛节奏系统将在出入站窗口动态激活**排刺状态（Pace Inversion）**：
- **入站圈（In-Lap）**：在进站前一圈，AI 抛弃保胎策略，驾驶风格切换为极具侵略性的排位赛模式（$M_{\text{pace}} = 1.08$），最大化压榨剩余轮胎性能建立窗口优势；
- **出站圈（Out-Lap）**：刚出站换上新胎的一圈，AI 借助抓地力优势实施推进行驶（Push Lap），待轮胎升温建立节奏后，平滑回落至正常巡航节奏。

### 9.3 混合叠加模型（Superposition Architecture）

最终赋予底层物理与控制器的综合调控系数由比赛节奏宏观系数与即时橡皮筋微观系数叠加决定：

$$M_{\text{final}} = \text{Clamp}\left( M_{\text{PaceSystem}}(t, \text{lap}) \times M_{\text{RubberBanding}}(\Delta s), \Omega_{\text{min}}, \Omega_{\text{max}} \right)$$

---

## 10. 完整工业级 C++ 生产架构实现

以下是一套可直接集成至主流商业游戏引擎（如 Unreal Engine 5 自定义 Movement Component 或自研物理/AI 架构）的工业级 C++ 实现，包含两级级联、质心防群聚、领头羊抑制及边界熔断机制。

```cpp
#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

// 橡皮筋运行期调校配置
struct FRubberBandingConfig
{
    // 逆向渐变区 (Reverse: AI 在后, 需要加速)
    float RevMaxDist = 200.0f;     // 最大逆向距离 (Region a 边界)
    float RevMinDist = 50.0f;      // 逆向生效起点 (Region c 边界)
    float MaxTotalMultiplier = 1.4f;

    // 正向渐变区 (Forward: AI 在前, 需要减速)
    float FwdMinDist = 50.0f;      // 正向生效起点 (Region c 边界)
    float FwdMaxDist = 200.0f;     // 最大正向距离 (Region e 边界)
    float MinTotalMultiplier = 0.6f;

    // 两级控制饱和界限
    float MaxSkillMultiplier = 1.2f;
    float MinSkillMultiplier = 0.8f;
    float MaxPowerMultiplier = 1.3f;
    float MinPowerMultiplier = 0.7f;

    // 边界熔断与抗群聚阈值
    float LowSpeedThreshold = 5.5f;       // 约 20 km/h (m/s)
    float BunchingGroupThreshold = 60.0f; // 触发质心防群聚的前方距离门限
    float P1GapThreshold = 40.0f;         // 第一名脱离惩罚距离门限
    float P1MaxGapDist = 150.0f;
    float P1MinMultiplier = 0.85f;
};

// 调控输出载荷
struct FRubberBandingOutput
{
    float SkillMultiplier = 1.0f;
    float PowerMultiplier = 1.0f;
    float CombinedMultiplier = 1.0f;
};

// 抽象赛车数据载体
struct FRaceVehicleContext
{
    uint32_t VehicleID;
    bool bIsHumanPlayer;
    float TrackDistance; // 沿赛道样条线的投影标量里程 (s)
    float CurrentSpeed;   // 标量车速 (m/s)
    int32_t CurrentLap;
    int32_t RaceRank;     // 1-based 名次
};

class RubberBandingSystem
{
public:
    explicit RubberBandingSystem(const FRubberBandingConfig& InConfig)
        : Config(InConfig) {}

    /**
     * 主轮询管线
     * @param Vehicles     全场所有车辆的上下文快照 (需预先按 TrackDistance 降序排序)
     * @param bIsStartPhase 标志当前是否处于起跑前几个弯的禁用期
     * @param OutOutputs   按 VehicleID 映射的输出控制系数
     */
    void Update(const std::vector<FRaceVehicleContext>& Vehicles,
                bool bIsStartPhase,
                std::vector<FRubberBandingOutput>& OutOutputs)
    {
        const size_t NumVehicles = Vehicles.size();
        OutOutputs.resize(NumVehicles);

        if (NumVehicles == 0) return;

        // 1. 扫描与定位玩家及特殊边界
        int32_t ForemostPlayerIdx = -1;
        int32_t RearmostPlayerIdx = -1;
        LocatePlayerExtremes(Vehicles, ForemostPlayerIdx, RearmostPlayerIdx);

        // 若无人类玩家，系统纯旁路放行
        if (ForemostPlayerIdx == -1)
        {
            SetPassthroughAll(OutOutputs);
            return;
        }

        const float ForemostPlayerDist = Vehicles[ForemostPlayerIdx].TrackDistance;
        const float RearmostPlayerDist = Vehicles[RearmostPlayerIdx].TrackDistance;
        const int32_t ForemostPlayerLap = Vehicles[ForemostPlayerIdx].CurrentLap;

        // 2. 预计算抗群聚机制所需参数 (前方集群质心)
        float CohortAvgDist = 0.0f;
        int32_t CohortCount = 0;
        for (const auto& Veh : Vehicles)
        {
            if (!Veh.bIsHumanPlayer && (Veh.TrackDistance - ForemostPlayerDist) > Config.BunchingGroupThreshold)
            {
                CohortAvgDist += Veh.TrackDistance;
                CohortCount++;
            }
        }
        if (CohortCount > 0)
        {
            CohortAvgDist /= static_cast<float>(CohortCount);
        }

        // 3. 逐车计算调控参数
        for (size_t i = 0; i < NumVehicles; ++i)
        {
            const auto& Veh = Vehicles[i];
            auto& Out = OutOutputs[i];

            // 人类玩家直接放行
            if (Veh.bIsHumanPlayer)
            {
                Out = FRubberBandingOutput{1.0f, 1.0f, 1.0f};
                continue;
            }

            // 熔断规则 A: 起跑安全区熔断
            if (bIsStartPhase)
            {
                Out = FRubberBandingOutput{1.0f, 1.0f, 1.0f};
                continue;
            }

            // 熔断规则 B: 极低车速脱困熔断
            if (Veh.CurrentSpeed < Config.LowSpeedThreshold)
            {
                Out = FRubberBandingOutput{1.0f, 1.0f, 1.0f};
                continue;
            }

            // 熔断规则 C: 套圈熔断 (AI 领先玩家圈数 >= 1)
            if (Veh.CurrentLap > ForemostPlayerLap)
            {
                Out = FRubberBandingOutput{1.0f, 1.0f, 1.0
```
