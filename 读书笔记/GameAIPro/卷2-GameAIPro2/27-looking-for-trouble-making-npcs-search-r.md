---
type: Reference
title: "第27章 Looking for Trouble: Making NPCs Search Realistically"
description: "Game AI Pro 工业级精读：Looking for Trouble: Making NPCs Search Realistically。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第27章 Looking for Trouble: Making NPCs Search Realistically

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 27.  
> 原文作者 / 资源：[Looking for Trouble: Making NPCs Search Realistically](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter27_Looking_for_Trouble_Making_NPCs_Search_Realistically.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

---

## 1. 系统架构全景与设计范式 (System Architecture & Design Paradigms)

在潜行类、战术射击类以及开放世界 AAA 游戏（如《光环 / Halo》、《孤岛危机 / Crysis》、《除暴战警 / Crackdown》）中，非玩家角色（Non-Player Character, NPC）对丢失目标的搜索行为是决定 AI 可信度（Believability）与拟真感的核心要素。

当玩家脱离 NPC 的直接视线（Line of Sight, LoS）后，生硬的直接索敌或完全失去警觉的木桩行为会彻底打破玩家的沉浸感。工业级 NPC 搜索系统必须兼顾**群组战术协同（Group Coordination）**、**空间推理（Spatial Reasoning）**与**感知开销控制（Sensing & Performance Optimization）**。

### 1.1 系统拓扑结构 (System Topology)

整个搜索体系分为两个核心层级：
1. **战术协调器层（Search Coordinator Layer - 宏观决策与黑板）**：以单例或区域子系统的形式存在，负责维护全队共享的战术黑板（Blackboard），统一调度点位生成算法（TPS / Spatial Queries）、聚类候选掩体、分配点位状态，并维护全局搜索生命周期。
2. **单体代理层（Individual NPC Agent Layer - 微观执行）**：每个 NPC 基于行为树（Behavior Trees, BT）或有限状态机（Finite State Machine, FSM）驱动，负责沿导航网格（NavMesh）寻路、利用时间片射线进行被动空间排查、执行缝隙探测（Gap Detection）并混合程序化观察动画。

```
+-----------------------------------------------------------------------------------+
|                        战术协调器 (Search Coordinator)                             |
|  +-----------------------------------------------------------------------------+  |
|  | 黑板数据 (Blackboard):                                                       |  |
|  | - 目标最后已知位置 (LKP) / 预估位置 (Estimated Target Pos)                   |  |
|  | - 搜索点位拓扑池 (Search Spot Pool: Free, In-Progress, Searched)              |  |
|  | - 组内注销/超时计时器 (Group Timeout Watchdogs)                              |  |
|  +-----------------------------------------------------------------------------+  |
|                                       |                                           |
|       [点位生成: 基于 TPS / 掩体投影]   |   [点位评分与分配: Utility Scoring]       |
|                                       v                                           |
|  +-----------------------------------------------------------------------------+  |
|  | 候选搜索点空间索引 (Spatial Index / Candidate Point Set)                     |  |
|  +-----------------------------------------------------------------------------+  |
+--------------------+--------------------------------------+-----------------------+
                     |                                      |
                     | 分配点位 (Spot Assignment)            | 分配点位 (Spot Assignment)
                     v                                      v
       +----------------------------+         +----------------------------+
       |   NPC Agent 1 (执行者)      |         |   NPC Agent 2 (掩护/协同)   |
       | +------------------------+ |         | +------------------------+ |
       | | 运动控制 (Locomotion)  | |         | | 战术架枪 (Cover Aim)   | |
       | +------------------------+ |         | +------------------------+ |
       | | 时间片视野排查 (LoS)   | |-------->| | 广播清除 (Invalidate)  | |
       | +------------------------+ | 反馈清除 | +------------------------+ |
       | | 缝隙探测 (Gap Detect)  | |         | | 运动控制 (Locomotion)  | |
       | +------------------------+ |         | +------------------------+ |
       +----------------------------+         +----------------------------+
```

---

## 2. 搜索类型学与触发机制 (Search Typology & Trigger Mechanics)

根据环境刺激的类型以及 NPC 是否确认目标的敌对属性，搜索系统严格划分为两种行为模式，以匹配不同的设计诉求。

### 2.1 搜索类型对比矩阵 (Search Typology Matrix)

| 战术维度 | 谨慎搜索 (Cautious Search) | 激进搜索 (Aggressive Search) |
| :--- | :--- | :--- |
| **敌意确认状态** | 未知。感知到环境扰动，但无法断定敌我。 | 明确已知。确认目标为敌对实体且正在交战/潜行。 |
| **典型触发源** | 抛掷酒瓶、碰倒场景道具、轻微脚步声。 | 枪声、爆炸、盟友无线电求救、视野中跟丢玩家。 |
| **移动速度模式** | 步行（Walk），降低机动速度。 | 奔跑（Run）接近预估区，近距离转为警戒架枪慢步。 |
| **姿态与动画图** | 枪口朝下/收枪，缓慢环顾四周（Glancing around）。 | 举枪瞄准（Weapon Raised），高戒备扫描，随时准备开火。 |
| **搜索阶段跨越** | 仅执行第一阶段（Narrow Search），完成后即复位。 | 执行第一阶段后，无缝推进至第二阶段（Broad Search）。 |
| **协同参与人数** | 严格限制为 **1 人**（避免全员惊动，便于玩家逐个击破）。 | 允许多人（通常限制 **2~3 人** 同时向核心点突进）。 |
| **终止与放弃机制** | 初始扰动点排查完毕后立即终止。 | 所有点位清空或全局搜索计时器超时后递减退出。 |

### 2.2 触发事件与感知输入 (Triggers & Sensory Inputs)

1. **初始间接刺激触发（Initial Stimulus-Based Trigger）**：
   - 处于非警戒状态（Unaware）的 NPC，在未建立直接视线（No LoS）时，通过感知系统（Perception System）捕获声音信号。
   - 分流逻辑：
     $$\text{Stimulus} = \begin{cases} \text{Hostile (Gunfire, Explosion)} & \implies \text{进入 Aggressive Search} \\ \text{Distraction (Prop Physics, Footsteps)} & \implies \text{进入 Cautious Search} \end{cases}$$
2. **目标丢失触发（Losing a Target）**：
   - NPC 原本与目标保持视线接触（Combat 状态或警戒视线建立），随后目标通过掩体转折切断视线。
   - 此时 NPC 拥有目标的**最后已知位置（Last Known Position, LKP）**与**最后已知速度向量（Last Known Velocity, $\mathbf{v}_{\text{last}}$）**。

### 2.3 航位推测（Dead Reckoning）的工程陷阱与“人为直觉”外推

常规运动学推导中，最直观的方法是通过线性外推预测玩家在 $t$ 秒后的空间坐标：
$$\mathbf{p}_{\text{extrapolated}} = \mathbf{p}_{\text{LKP}} + \mathbf{v}_{\text{last}} \cdot \Delta t$$

#### 工业级实现的失效缺陷：
- **非线性机动**：玩家极少沿完全笔直的轨迹移动，通常在拐弯后立刻变向或下蹲静音移动。
- **不可通行域碰撞（NavMesh Invalidation）**：由于缺少严格的几何约束，单纯的数学外推坐标 $\mathbf{p}_{\text{extrapolated}}$ 极易落在静态碰撞体内（如穿墙、进入掩体模型内部）或直接悬空在场景导航网格之外。若每帧发起 NavMesh Raycast 或物理引擎投射（Physics Sweep）进行拓扑有效性修正，将造成沉重的计算负载。

#### 权威解决方案：“直觉作弊”机制 (Intuitive Target Tracking)
工业界 AAA 大作（《Halo》、《Crysis》等）采用更加高效且拟真的工程技巧：**赋予 AI 短暂的“全知直觉窗口”**。
- 在玩家切断直接视线后的窗口时间 $T_{\text{cheat}} \in [2.0\,\text{s},\, 3.0\,\text{s}]$ 内，NPC 系统直接在底层静默获取玩家的真实物理世界坐标 $\mathbf{p}_{\text{player}}(t)$。
- 在此 $2\sim3$ 秒内，NPC 行为表现为继续向玩家拐弯后的真实动态位置精准追踪，呈现出人类特有的“推测与预判”直觉感，彻底免去复杂的路径投射纠偏，同时规避了穿墙或盲目撞墙的穿模 Bug。
- 窗口期 $T_{\text{cheat}}$ 耗尽时，系统将目标丢失瞬间的最新采样点冻结为最终的预估搜索锚点 $\mathbf{p}_{\text{est}}$。

---

## 3. 搜索生命周期与两阶段推进 (Phases of Searching)

搜索系统的状态转移由严格的阶段拓扑驱动，兼顾单体表现与群体协调。

```
                   [感知刺激 / 丢失目标]
                           |
                           v
        +--------------------------------------+
        |         阶段判定与广播电报            |
        |  Telegraphing (播放特定语音/警觉动画)  |
        +--------------------------------------+
                           |
            +--------------+--------------+
            |                             |
     [Cautious 刺激]               [Aggressive 刺激]
            |                             |
            v                             v
+-----------------------+     +-------------------------------+
| Phase 1: 谨慎狭窄搜索   |     | Phase 1: 激进狭窄搜索          |
| - 单一 NPC 领命排查    |     | - 2~3 人突进 LKP/预估点        |
| - 伴随对讲机/环境音效   |     | - 其余成员移动至掩体建立交叉火力 |
| - 闲置 NPC 退出警戒    |     +-------------------------------+
+-----------------------+                     |
            |                                 v
            |                        [未能发现目标，推进]
            |                                 |
            |                                 v
            |                 +-------------------------------+
            |                 | Phase 2: 宏观扩散搜索 (Broad)  |
            |                 | - 战术协调器生成点位池         |
            |                 | - Utility 评分分发搜索点位    |
            |                 | - 移动中动态射线排查 (LoS)     |
            |                 | - 缝隙探测 (Gap Detection)    |
            |                 +-------------------------------+
            |                                 |
            |           +---------------------+---------------------+
            |           |                                           |
            |      [发现目标]                                  [搜索超时/点位耗尽]
            |           |                                           |
            v           v                                           v
+-------------------------------+                     +-------------------------------+
|       切入 Combat 战斗        |                     |   平滑递减复位 (Natural Filter) |
|   (Direct Line of Sight)      |                     |   (错开时间，拒绝蜂巢思维同步)   |
+-------------------------------+                     +-------------------------------+
```

### 3.1 第一阶段：针对核心位置的狭窄搜索 (Phase 1: Narrow Search)

所有搜索的起点均为最后已知位置（LKP）或扰动源核心。

#### 3.1.1 谨慎模式下的 Phase 1
- **单兵行动原则**：战术协调器仅允许 **1 名 NPC** 申领调查任务。
- **行为管线**：
  1. 寻路至能与扰动源建立直接视线的位置，或直接移动至该坐标。
  2. 到达后播放原地四处张望（Glance around）或使用无线电向同伴通报（"Checking disturbance..."）的动画与音频。
  3. 未被许可调查的邻近 NPC 直接丢失兴趣，保持默认闲置/巡逻行为。
- **突现式玩法机制（Emergent Gameplay）**：
  该机制允许玩家利用声东击西策略逐个引开守卫并暗杀。若调查人员在规定时间 $T_{\text{missing}}$ 内未返回并停止通报，其死亡/消失位置将反向生成一个二次刺激点，促使其他 NPC 再次前来进行二次调查。

#### 3.1.2 激进模式下的 Phase 1
- **战术多兵协同**：协调器限制 **2 至 3 名 NPC** 组成突击编组同时向 $\mathbf{p}_{\text{est}}$ 推进，展现高度协同。
- **动态机动与压制分工**：
  - 突进成员采取混合速度控制：远距离奔跑（Run），一旦逼近并建立视线后切换为举枪慢速前压（Walk with weapon raised）。
  - 非突进成员不可进入随机游荡，必须在后方建立战术阵位：举枪瞄准可能的目标出口、进入附近的掩体（In Cover）提供火力掩护（Overwatch），保持全员高戒备。
- 到达目标点后，突进 NPC 播放警戒确认动画并广播：“目标脱离！展开全面搜索！”。此时，系统正式将状态推进至 **Phase 2**。

---

## 4. 第二阶段：基于战术点位系统（TPS）的宏观扩散搜索 (Phase 2: Broad Search)

第二阶段为广域扫荡搜索（Broad Search），其底层依赖于离散化战术点位系统（Tactical Point System, TPS）或由占用网格（Occupancy Maps）降维生成的点阵空间。

### 4.1 候选搜索点位生成算法 (Generation of Search Spots)

搜索的本质是“排查一切能够藏匿目标的物理空间”。因此，**提供防御遮蔽的掩体背面是最佳的候选隐藏点**。

```
                      [真实目标 / 玩家隐藏中]
                                *
                 +-----------------------------+
                 |       掩体 (Cover Wall)     |
                 +-----------------------------+
                   [o]   [o]   [o]   [o]   [o]    <-- 有效搜索点 (Valid Search Spots: 避开视角)
               ===================================
               
               
                              [X]                 <-- 目标最后预估位置 (Target's Estimated Pos)
               
                   [x]   [x]   [x]   [x]   [x]    <-- 无效搜索点 (Invalid: 暴露在预估视角内)
                 +-----------------------------+
                 |       掩体 (Cover Wall)     |
                 +-----------------------------+
```

#### 点位生成几何流程：
1. **掩体关联采样**：遍历以目标预估位置 $\mathbf{p}_{\text{est}}$ 为圆心、半径为 $R_{\text{search}}$ 的环境掩体（Cover Nodes）。
2. **可视性判定**：计算从预估位置 $\mathbf{p}_{\text{est}}$ 向掩体点位 $\mathbf{s}_k$ 发射的视线向量：
   $$\text{Vis}(\mathbf{p}_{\text{est}}, \mathbf{s}_k) = \text{RaycastClear}(\mathbf{p}_{\text{est}} + \mathbf{h}_{\text{eye}}, \mathbf{s}_k + \mathbf{h}_{\text{eye}})$$
   - 若 $\text{Vis} == \text{True}$：该点暴露在目标直线下，无法作为藏匿点，予以剔除。
   - 若 $\text{Vis} == \text{False}$：该点处于视觉盲区（Obscured），标记为**有效候选搜索点**。
3. **导航网格随机补充**：若场景中掩体密度不足，在半径 $R_{\text{search}}$ 范围内的拐角处（Around corners）和巷道隐蔽处（Alleys），利用 NavMesh 采样随机生成补充候选点，确保初始点位池规模满足搜索周期消耗。

### 4.2 战术协调器分配管线与效用系统（Utility System）评分模型

每个处于 Phase 2 的 NPC 每隔固定周期或到达当前点位后，向战术协调器申请下一个最优搜索点。

#### 4.2.1 点位生命周期状态机 (Spot State Machine)
- `FREE`：有效候选，待排查。
- `IN_PROGRESS`：已被某一 NPC 锁定，正在前往中（互斥锁，禁止双人分配同一目标点）。
- `SEARCHED`：已完成物理抵达或被动态视线扫过，从待选池中注销。

#### 4.2.2 效用评分模型 (Utility Scoring Formulation)
当 NPC $i$（坐标 $\mathbf{p}_{\text{npc}}$）向协调器请求点位时，协调器对所有标记为 `FREE` 的点位 $S = \{\mathbf{s}_1, \mathbf{s}_2, \dots, \mathbf{s}_m\}$ 执行效用评估。

得分最高者胜出：
$$S^* = \arg\max_{\mathbf{s}_j \in S} U(\mathbf{s}_j, \mathbf{p}_{\text{npc}}, \mathbf{p}_{\text{est}}, \mathbf{p}_{\text{actual}})$$

效用函数构建如下：
$$U(\mathbf{s}_j) = - \left( w_{\text{npc}} \cdot \|\mathbf{s}_j - \mathbf{p}_{\text{npc}}\| + w_{\text{est}} \cdot \|\mathbf{s}_j - \mathbf{p}_{\text{est}}\| + w_{\text{actual}} \cdot \|\mathbf{s}_j - \mathbf{p}_{\text{actual}}\| \right)$$

在实际工程中，通常采用归一化效用加权（分数越高越优）：
$$U(\mathbf{s}_j) = w_1 \cdot f_{\text{norm}}\left(\|\mathbf{s}_j - \mathbf{p}_{\text{npc}}\|\right) + w_2 \cdot f_{\text{norm}}\left(\|\mathbf{s}_j - \mathbf{p}_{\text{est}}\|\right) + w_3 \cdot f_{\text{norm}}\left(\|\mathbf{s}_j - \mathbf{p}_{\text{actual}}\|\right)$$
其中 $f_{\text{norm}}(d) = 1.0 - \text{clamp}\left(\frac{d}{D_{\max}}, 0, 1\right)$。

#### 权重矩阵设计原则与工业级微调 (Weight Tuning Constraints)：
- **自身距离权重 $w_1$ (Weight NPC Distance)**：主导权重（高）。使 NPC 优先搜索其周围的点位，形成空间局部聚集，避免多个 NPC 在场景中长途交叉折返。
- **预估距离权重 $w_2$ (Weight Estimated Target Distance)**：核心战术权重（中高）。确保群体的搜索重心向玩家最后失踪的区域靠拢。
- **真实距离权重 $w_3$ (Weight Actual Player Distance - "作弊引导")**：**极细微微调权重（Subtle Weight，$w_3 \ll w_1, w_2$）**。
  - *设计机理*：引入极低幅度的真实玩家坐标加权，能够在数学上赋予 AI 类似人类潜意识的直觉偏向，引导整体搜索阵型缓缓向玩家真实藏匿区域偏移。
  - *临界控制*：$w_3$ 严禁过大。若权重过高，所有 NPC 将径直扑向玩家躲藏的障碍物，破坏公平性并导致设计穿帮（Cheat-feeling）。

---

## 5. 空间推理、视觉剔除与移动感知增强 (Spatial Reasoning & Gap Detection)

### 5.1 动态视线扫掠排查 (Passive Raycast Clearing)

NPC 在前往分配点位 $\mathbf{s}_{\text{target}}$ 的移动过程中，其视锥体（Field of View, FoV）会覆盖场景中的其他点位。若必须让 NPC 物理走到每个点位才能确认安全，搜索效率将极其低下且显得极为迟钝。

```
[NPC 朝目标移动中] 
      \
       \ (视锥体内)
        +----------------> [点位 A: LoS 命中掩体遮挡] -> 状态维持 FREE
         \
          +--------------> [点位 B: LoS 无障碍直达] ---> 状态即刻置为 SEARCHED! (广播通知所有盟友)
```

#### 工程性能优化：时间片分时检测 (Timeslicing Raycasts)
- 场景中未搜索点可能多达数十个，若每个 NPC 每帧对视锥内的点位执行物理光线投射，物理管线将迅速过载。
- **工业解决方案**：
  - 对每个 NPC 的视野排查实施分时轮询（Timeslicing），限定检测频率为每 $1.0\sim2.0$ 秒一次。
  - 在角速度未发生剧烈突变的前提下，1~2 Hz 的射频足以保证在 NPC 行进过程中平滑清除暴露的点位。
  - 被清除的点位立即在战术协调器中标记为 `SEARCHED`，其他正欲将该点作为候选目标的 NPC 将直接跳过该点，实现组内知识共享。

### 5.2 移动中的缝隙与转角检测 (Gap and Corner Detection)

单纯依赖点对点导航移动，NPC 会像机械轨道车一样呆板。通过在移动过程中引入前置横向缝隙探测，NPC 能在行进过程中对侧方的走廊、门洞、小巷做出拟真观察反应。

```
                          [ 建筑掩体 / 墙体 ]            [ 建筑掩体 / 墙体 ]
                                             |  开阔缝隙  |
                                             |  (Gap/Alley)|
                            [Ray L: 命中]    |  [Ray R: 穿透无碰撞]
                                  ^          |      ^
                                  |          |      |
                                  +----+-----+------+
                                       |
                              [NPC 前进路径向量]
                                       ^
                                       |
                                     (NPC)
```

#### 探测算法实现机制：
1. **射线构建**：
   沿 NPC 移动速度方向向前预留距离 $d_{\text{ahead}}$，分别向路径切线垂直的左右两侧发射横向探测射线：
   $$\mathbf{P}_{\text{origin}} = \mathbf{P}_{\text{npc}} + d_{\text{ahead}} \cdot \hat{\mathbf{v}}$$
   $$\mathbf{R}_{\text{left}} = \mathbf{P}_{\text{origin}} - d_{\text{side}} \cdot \hat{\mathbf{n}},\quad \mathbf{R}_{\text{right}} = \mathbf{P}_{\text{origin}} + d_{\text{side}} \cdot \hat{\mathbf{n}}$$
   其中 $\hat{\mathbf{v}}$ 为移动单位切线向量，$\hat{\mathbf{n}}$ 为水平法向量。
2. **缝隙触发判定**：
   - 正常沿墙行走时，两侧射线均命中静态障碍物（Ray Hit）。
   - 一旦某一侧射线未命中任何碰撞（Ray Miss / Max Distance），表明该侧出现未知缺口（Gap Detected）。
3. **表现层行为分流（Procedural Glance Behaviors）**：
   检测到缝隙后，AI 系统从以下行为中按权重随机选取一种执行，打破模式僵化：
   - **完全拐入（Move to Inspect）**：脱离主路径几步，走到缝隙口彻底排查。
   - **身体转向（Animated Body Turn）**：保持整体向前移动，上半身播放转身瞄准动画。
   - **仅头部张望（Head-only Glance）**：IK 驱动骨骼仅头部偏向缝隙快速扫视。
   这一探测动作可瞬间将缝隙深处原本不可见的搜索点纳入视野并直接清除。

---

## 6. 系统收敛与平滑退出机制 (Ending the Search & Depletion)

搜索不可能无限期持续，其退出机制直接决定了群体 AI 是否具备拟真的人性化特质。

### 6.1 点位耗尽与缓冲策略 (Spot Depletion Strategies)
若动态视线扫除机制运作高效，战术协调器的点位池可能在短时间内被全数置为 `SEARCHED`。此时协调器可采用两种策略：
1. **点位复原（Recycle Old Spots）**：将时间戳最早变为 `SEARCHED` 的点位重新标记为 `FREE`，供 NPC 二次折返确认。
2. **动态扩容（Radius Expansion）**：以更大的搜索半径 $R_{\text{search}} \leftarrow R_{\text{search}} + \Delta R$ 重新发起 TPS 采样，扩展排查纵深。

### 6.2 搜索终止状态与非蜂巢思维退出 (Natural Filtering Out)

搜索通常因两种情况终止：
1. **成功发现目标**：视线检测到玩家，直接中断搜索树，触发全队无线电并切入 Combat 战斗状态树。
2. **搜索无果收工**：点位彻底耗尽或战术协调器全局计时器超时（Search Timeout reached）。

#### 关键设计模式：异步平滑退出（Anti-Hive-Mind Asynchronous Decay）
- **反模式（Anti-Pattern）**：全局计时器归零瞬间，所有参与搜索的 NPC 在同一帧停止警觉并集体收枪返回巡逻。这会给玩家强烈的“虚拟傀儡/蜂巢思维（Hive Mind）”脱节感。
- **工业级平滑退出方案**：
  - 战术协调器宣告搜索结束，注销全局任务分配。
  - 各 NPC 代理增加随机的反应延迟时间：
    $$T_{\text{exit\_delay}} \sim \mathcal{U}(t_{\min}, t_{\max})$$
  - NPC 必须将当前的原子动作完整执行完毕（如走完当前路径段，或完成一次原地环视骨骼动画），再通过一段自言自语（如：“Must have been the wind...”）平滑淡出到普通巡逻状态。NPC 之间错落有致地退出警觉，呈现出自然的个体独立思考过程。

---

## 7. 工业级 C++ 核心代码实现 (Production-Grade C++ Implementation)

以下代码展示了符合战术空间搜索、效用评分、时间片射线排查与缝隙检测的工程实现。

```cpp
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

// ==========================================
// 数学几何基础定义
// ==========================================
struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vector3() = default;
    Vector3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

    Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float LengthSq() const { return x * x + y * y + z * z; }
    float Length() const { return std::sqrt(LengthSq()); }

    Vector3 Normalized() const {
        float len = Length();
        return len > 0.0001f ? Vector3(x / len, y / len, z / len) : Vector3();
    }

    static float Distance(const Vector3& a, const Vector3& b) {
        return (a - b).Length();
    }
};

// 射线碰撞判定模拟接口
struct RaycastHit {
    bool bHit = false;
    Vector3 Point;
    Vector3 Normal;
};

// 引擎底层物理上下文代理
class PhysicsScene {
public:
    static bool Raycast(const Vector3& start, const Vector3& end, RaycastHit& outHit) {
        // 模拟物理射线追踪（生产环境中对接 PhysX / Havok）
        return false; 
    }
};

// ==========================================
// 搜索点位与枚举定义
// ==========================================
enum class ESearchSpotState : uint8_t {
    Free,
    InProgress,
    Searched
};

struct SearchSpot {
    uint32_t SpotID;
    Vector3 Position;
    ESearchSpotState State;
    float LastInspectedTime;

    SearchSpot(uint32_t id, const Vector3& pos)
        : SpotID(id), Position(pos), State(ESearchSpotState::Free), LastInspectedTime(0.0f) {}
};

// ==========================================
// 战术搜索协调器 (Search Coordinator)
// ==========================================
class AISearchCoordinator {
public:
    AISearchCoordinator() = default;

    void InitializeSearch(const Vector3& estimatedTargetPos, const std::vector<Vector3>& coverNodes) {
        m_TargetEstimatedPos = estimatedTargetPos;
        m_SearchSpots.clear();
        m_IsSearchActive = true;
        uint32_t idCounter = 0;

        // 依据掩体遮挡关系生成第一批搜索点
        for (const auto& coverPos : coverNodes) {
            RaycastHit hit;
            Vector3 eyeOffset(0.0f, 1.7f, 0.0f);
            // 射线被遮挡，说明此掩体背后能够避开预估视线，是有效隐匿点
            if (PhysicsScene::Raycast(m_TargetEstimatedPos + eyeOffset, coverPos + eyeOffset, hit)) {
                m_SearchSpots.emplace_back(++idCounter, coverPos);
            }
        }
    }

    // 效用系统评分挑选最佳搜索点 (Utility Scoring Pipeline)
    SearchSpot* RequestBestSearchSpot(const Vector3& npcPos, const Vector3& actualPlayerPos) {
        if (!m_IsSearchActive) return nullptr;

        SearchSpot* bestSpot = nullptr;
        float bestScore = -std::numeric_limits<float>::infinity();

        // 权值超参数定义
        const float w_NpcDist = 1.0f;       // 自身就近偏好 (高)
        const float w_EstDist = 0.8f;       // 预估点聚集偏好 (中高)
        const float w_ActualDist = 0.15f;   // 真实玩家偏好 (细微作弊引导)
        const float maxSearchRadius = 50.0f;

        for (auto& spot : m_SearchSpots) {
            if (spot.State != ESearchSpotState::Free) {
                continue;
            }

            float dNpc = Vector3::Distance(npcPos, spot.Position);
            float dEst = Vector3::Distance(m_TargetEstimatedPos, spot.Position);
            float dActual = Vector3::Distance(actualPlayerPos, spot.Position);

            // 归一化效用评分 (距离越近分数越高)
            auto NormScore = [maxSearchRadius](float dist) {
                return 1.0f - std::min(dist / maxSearchRadius, 1.0f);
            };

            float utilityScore = w_NpcDist * NormScore(dNpc) +
                                 w_EstDist * NormScore(dEst) +
                                 w_ActualDist * NormScore(dActual);

            if (utilityScore > bestScore) {
                bestScore = utilityScore;
                bestSpot = &spot;
            }
        }

        if (bestSpot) {
            bestSpot->State = ESearchSpotState::InProgress;
        }
        return bestSpot;
    }

    void InvalidateSpot(uint32_t spotID) {
        for (auto& spot : m_SearchSpots) {
            if (spot.SpotID == spotID && spot.State != ESearchSpotState::Searched) {
                spot.State = ESearchSpotState::Searched;
                break;
            }
        }
    }

    std::vector<SearchSpot>& GetAllSpots() { return m_SearchSpots; }
    bool IsActive() const { return m_IsSearchActive; }
    void TerminateSearch() { m_IsSearchActive = false; }

private:
    Vector3 m_TargetEstimatedPos;
    std::vector<SearchSpot> m_SearchSpots;
    bool m_IsSearchActive = false;
};

// ==========================================
// 单体 NPC 代理搜索行为实现 (NPC Agent)
// ==========================================
class NPCAgent {
public:
    NPCAgent(uint32_t id, AISearchCoordinator* coordinator)
        : m_AgentID(id), m_Coordinator(coordinator), m_CurrentTargetSpot(nullptr) {}

    void Update(float deltaTime, float globalGameTime, const Vector3& actualPlayerPos) {
        if (!m_Coordinator || !m_Coordinator->IsActive()) {
            return;
        }

        // 1. 若无任务，申请新点位
        if (!m_CurrentTargetSpot) {
            m_CurrentTargetSpot = m_Coordinator->RequestBestSearchSpot(m_Position, actualPlayerPos);
            if (!m_CurrentTargetSpot) {
                // 点位耗尽，触发个体平滑退出
                InitiateGracefulExit(globalGameTime);
                return;
            }
        }

        // 2. 模拟向目标点寻路运动
        MoveTowards(m_CurrentTargetSpot->Position, deltaTime);

        // 3. 时间片分时检测：视野扫掠清除 (1.0s ~ 2.0s 轮询一次)
        if (globalGameTime - m_LastLoSCheckTime >= 1.5f) {
            SweepAndClearVisibleSpots();
            m_LastLoSCheckTime = globalGameTime;
        }

        // 4. 移动中缝隙与拐弯探测 (Gap Detection)
        DetectGapsAndGlance();

        // 5. 到达判定
        if (Vector3::Distance(m_Position, m_CurrentTargetSpot->Position) < 1.0f) {
            m_Coordinator->InvalidateSpot(m_CurrentTargetSpot->SpotID);
            m_CurrentTargetSpot = nullptr; // 准备索取下一个点
        }
    }

private:
    void MoveTowards(const Vector3& target, float dt) {
        Vector3 dir = (target - m_Position).Normalized();
        m_ForwardVector = dir;
        m_Position = m_Position + dir * (m_MoveSpeed * dt);
    }

    // 利用视野扫掠直接清除无需物理抵达的点位
    void SweepAndClearVisibleSpots() {
        Vector3 eyePos = m_Position + Vector3(0.0f, 1.7f, 0.0f);
        auto& allSpots = m_Coordinator->GetAllSpots();

        for (auto& spot : allSpots) {
            if (spot.State == ESearchSpotState::Searched) continue;

            Vector3 toSpot = spot.Position - m_Position;
            float dist = toSpot.Length();

            // 视锥与距离剪裁 (60度夹角，20米可视范围)
            if (dist < 20.0f) {
                Vector3 toSpotDir = toSpot.Normalized();
                float dot = m_ForwardVector.x * toSpotDir.x + m_Forward
```
