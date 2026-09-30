---
type: Reference
title: "第35章 A Simple and Practical Social Dynamics System"
description: "Game AI Pro 工业级精读：A Simple and Practical Social Dynamics System。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第35章 A Simple and Practical Social Dynamics System

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 35.  
> 原文作者 / 资源：[A Simple and Practical Social Dynamics System](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter35_A_Simple_and_Practical_Social_Dynamics_System.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

在当代包含大量非玩家角色（NPC）的游戏世界中，角色群体往往缺乏现实世界中随处可见的协调互动行为（Coordinated Interactions）——例如派对上朋友自然聚集交谈，或是久别重逢的亲友相互拥抱。社交互动是人类感知社会群体结构的核心窗口。若要打破“木讷背景板”的刻板印象、塑造高度可信的游戏世界，就必须在游戏 AI 底层构建一套能够实时表达非言语行为（Nonverbal Behavior）的社交动态机制。

本文系统性阐述一套基于**组件化复合实体架构（Component-Based Composite Entity Architecture）**的轻量、高效且实用的社交动态系统。该系统与行为树（Behavior Trees）、黑板（Blackboards）、动画混合器（Animation Blenders）及机动运动控制器（Locomotion Controllers）深度解耦融合，覆盖视线追踪、人际距离控制、姿态映射及手势同步等工业级非言语交互链条。

---

## 1. 社交动态的核心概念与非言语行为

### 1.1 什么是社交动态？
**社交动态（Social Dynamic）**指在两个或两个以上角色之间实时发生的任何社交互动。此类交互天然包含以下三个核心维度的动态变化：
1. **空间位置（Spatial Position）**：角色在交互空间内的动态落位与位移。
2. **时间协同（Timing）**：交互动作的发起、响应窗口、节拍与持续周期。
3. **朝向偏转（Orientation）**：交互主体之间的朝向对齐与视觉聚焦。

社交动态的本质不是孤立的动画片段播放，而是多智能体在三维空间中通过连续的非言语线索（Nonverbal Cues）实现的动态博弈与协同平衡。

```
                       ┌─────────────────────────┐
                       │  社交动态 (Social Dynamic) │
                       └────────────┬────────────┘
         ┌──────────────────────────┼──────────────────────────┐
         ▼                          ▼                          ▼
┌─────────────────┐        ┌─────────────────┐        ┌─────────────────┐
│ 空间位置 (Position)│        │  时间协同 (Timing) │        │ 朝向偏转 (Facing) │
│ - 距离维护       │        │ - 发起与响应节拍 │        │ - 共享原点对齐   │
│ - O型空间环形分布│        │ - 视线注视/移开周期│       │ - 视线与胸腔协同 │
└─────────────────┘        └─────────────────┘        └─────────────────┘
```

### 1.2 关键非言语交互维度

#### 1.2.1 视线控制（Gaze Control）
面部及视线朝向是传递角色认知状态的最主要媒介：
- **感知映射（Awareness Indication）**：若角色注视特定物体，向玩家隐式传达其“已感知（Aware）”该物体。
- **注意力与吸引力机制（Attention & Attraction）**：注视持续时间反映交互强度。长时间注视某一角色表明其产生兴趣或吸引力；反之，注视频繁偏离则映射出回避或羞怯。
- **视觉引导（Visual Steering）**：角色突发性的视线偏转可作为极具沉浸感的游戏引导手段，牵引玩家镜头关注环境中的突发事件。

#### 1.2.2 人际距离控制（Proxemic Control）
依据社会心理学中的**人际距离学（Proxemics）**理论，人类在社会交互中依据亲密程度、社交层级与情绪状态维持严格的空间边界。
- **亲密距离（Intimate Space）**：用于肢体接触、拥抱等近距交互。
- **个人/社交距离（Personal & Social Space）**：用于常规对话，维持放松舒适的非侵入范围。
在空间拓扑层面，智能体群体的空间落位普遍遵循 Adam Kendon 提出的 **“O型空间”（O-Frame）** 理论——智能体围绕一个共同关注的虚拟凸空间边缘环形分布，系统必须通过机动导向行为（Steering Behaviors）持续抑制位置震荡并动态调整环形拓扑。

#### 1.2.3 姿态表达（Posture）
角色的基础体态（Stance）直接反映社交层级、情绪与相互吸引力：
- **开放姿态（Open Posture）**：双臂自然下垂或丰富动作、站姿较宽、身体向交互目标前倾，表示友好与信任。
- **闭合/拘谨姿态（Closed Posture）**：双脚并拢、背部紧绷或后撤微仰，表示受到权威压制或防御心理。
- **微反应通道（Subtle Micro-responses）**：如肩部坍落（疲惫/沮丧）、眼睑低垂、或特定机体发光变化（如巡逻机械体眼部辉光减弱以映射阴郁情绪）。

#### 1.2.4 手势系统（Gesture）
手势属于伴随语音表达的非言语交际工具（Nonverbal Communication）：
- **节奏标点（Punctuation）**：在关键语义节点或情绪高点强化表达。
- **多角色协同接触（Coordinated Character Interaction）**：如同级握手、搭肩。此类行为在工程上面临严苛挑战，必须依靠**两段骨骼逆向运动学（Two-Bone Inverse Kinematics, 2-Bone IK）**修正网络延迟与动画位移偏差，防止穿模或接触悬空。

---

## 2. 复合组件架构设计（Component Architecture）

系统摒弃单体庞杂的大一统上帝类（Monolithic God Class），采用基于**组件组合（Composition over Inheritance）**与**事件驱动通信（Event-Driven Communication）**的解耦体系。

### 2.1 角色内部系统类结构与依赖拓扑

角色实体（`GameObject`）聚合了多种轻量级功能组件。各组件通过底层中介者 `MessageManager` 分发事件，并与 AI 决策层（行为树与黑板）形成松散交互。

```
 ┌────────────────────────────────────────────────────────────────────────┐
 │                              GameObject                                │
 └──────┬─────────────────────────────────────────────────────────────────┘
        │ contains
        ├────────────► [ AIComponent ] ◄──────────► [ Behavior Tree ]
        │                     │                              ▲
        │                     ▼                              │
        ├────────────► [ SocialComponent ] ◄────────► [  Blackboard   ]
        │                     │                              ▲
        │                     ├─────── queries ──────────────┤
        │                     │
        ├────────────► [ GazeComponent ] ───────┐
        │                                       ▼
        ├────────────► [ PostureComponent ] ───► [ AnimationComponent ]
        │                                       ▲
        ├────────────► [ GestureComponent ] ────┘
        │
        ├────────────► [ ProxemicComponent ] ───► [ LocomotionComponent ]
        │
        │ events
        ▼
 ┌────────────────────────────────────────────────────────────────────────┐
 │                            MessageManager                              │
 └────────────────────────────────────────────────────────────────────────┘
```

### 2.2 核心组件职责划分

| 组件名称 | 核心职责与设计边界 | 依赖项与协同接口 |
| :--- | :--- | :--- |
| **`SocialObjectComponent`** | **社交仲裁者**。挂载于世界物体或动态交互锚点。管理成员准入集（Set Membership）、广播交互可用性、维护共享社交原点、控制并发发言/资源流。 | 无刚性实体依赖；向外部智能体广播交互上下文。 |
| **`SocialComponent`** | **智能体内社交中枢**。轮询世界潜在交互、生成参与请求、同步黑板交互状态、转发视线与手势事件。 | 依赖黑板（`Blackboard`）、`MessageManager`。 |
| **`GazeComponent`** | **视线与脊柱链修正器**。作为 `AnimationComponent` 的监听器，在姿态渲染前以加权阻尼方式偏转头部与脊柱骨骼。 | 强断言依赖 `AnimationComponent`；监听 `SocialComponent` 的 LookAt 事件。 |
| **`ProxemicComponent`** | **人际距离与队形驱动器**。计算 O-Frame 虚拟圆周目标点，施加阻尼力并向位移系统发出避障导向请求。 | 强依赖 `LocomotionComponent`；受导航网格（NavMesh）约束。 |
| **`PostureComponent`** | **体态姿态选择与混合器**。根据黑板中的 `Mood` 状态值，在动画状态机或混合树（Blend Tree）中施加姿态偏置权值。 | 依赖 `AnimationComponent`、`Blackboard`。 |
| **`GestureComponent`** | **叠加手势触发器**。在基础运动层上进行加权叠加混合（Additive Blending），协同音频标记点触发，结合 2-Bone IK 处理接触。 | 依赖 `AnimationComponent`，需屏蔽冲突动画（如翻滚）。 |

---

## 3. 动态交互仲裁与共享社交原点机制

### 3.1 动态交互发起的双轨模型

在多人交互生命周期中，参与者可能随时进出。一个由 2 人发起的对话可能扩散至 6 人，随后最初的发起者相继离开，但会话本身依旧存续。因此，系统引入了与参与者生命周期分离的抽象上下文容器：

```
模式 A: 预设静态锚点（Static World Objects）
[场景烘焙: 热狗摊/圆桌] ──► 挂载 SocialObjectComponent ──► 广播 Interaction Slots ──► 吸引多 Agent 排队/交互

模式 B: 动态自发生成（Dynamic Proposal Spawning）
Agent A 满足断言:
  <idle> ∧ <wants_social> ∧ <sees_friend> ∧ <friend_also_wants_social>
            │
            ▼
[动态生成 GameObject 锚点]
            │
            └─► 挂载 SocialObjectComponent (成为社交中介)
                  ├─► 确立共享社交原点 (Shared Social Origin)
                  ├─► 将 Agent A & B 加入成员集 (Set Membership)
                  └─► 协调持续的交谈、视线交换与位置重构
```

### 3.2 共享社交原点（Shared Social Origin）与坐标映射

为确保动作动画在多人协作时严格对齐（如同跑酷动作中的 Moving Origin 机制），所有空间位移与姿态朝向均不直接依赖世界绝对坐标，而是建立在由 `SocialObjectComponent` 维护的**局部社交坐标系** $\mathcal{F}_{\text{social}}$ 下。

设社交原点在世界空间的位置为 $\mathbf{P}_{\text{origin}}$，其基底朝向由前向向量 $\mathbf{F}_{\text{origin}}$ 与上向向量 $\mathbf{U}_{\text{origin}}$ 构成。对于任意参与者 $i$，其在局部社交空间中的目标落点为 $\mathbf{p}_i^{\text{local}}$，则其对应映射至世界空间的导航目标为：

$$\mathbf{p}_i^{\text{world}} = \mathbf{P}_{\text{origin}} + \mathbf{R}_{\text{social}} \mathbf{p}_i^{\text{local}}$$

其中旋转变换矩阵 $\mathbf{R}_{\text{social}} = \begin{bmatrix} \mathbf{R}_{\text{right}} & \mathbf{U}_{\text{origin}} & \mathbf{F}_{\text{origin}} \end{bmatrix}$。所有参与者计算自身轨迹时，均向该统一基底对齐。

---

## 4. 关键交互系统深度技术实现

### 4.1 视线与脊柱运动链控制（Gaze Control）

#### 4.1.1 骨骼链分层阻尼分布算法
人眼在偏转注视时，并非仅转动头部，而是伴随颈椎、胸椎乃至腰椎的协同扭转。`GazeComponent` 作为后处理监听器，在动画姿态完成前向动力学（FK）解算后、提交渲染前进行骨骼四元数修正。

为了在离散骨骼链上模拟逼真的肌肉连动并消除视觉突变，沿脊柱向下到骨盆方向，骨骼旋转权值需呈衰减分布。

设从头部向骨盆逆序排列的脊柱骨骼序列为 $\{B_0, B_1, \dots, B_N\}$，其中 $B_0$ 为头骨（Head Bone），$B_N$ 为最底端脊椎骨。定义骨骼序号为 $k \in [0, N]$，其旋转权值计算公式为：

$$w_k = w_{\text{base}} \cdot \left(1 - \frac{k}{N + 1}\right)^\alpha$$

其中 $\alpha \ge 1.0$ 为衰减指数，$w_{\text{base}} \in [0, 1]$ 为总体注视混合权重。

```
            [Head: B0]      ──► 100% 目标偏转 (最大活动范围)
                │
            [Neck: B1]      ──►  75% 目标偏转
                │
           [Spine2: B2]     ──►  50% 目标偏转 (胸部扭转)
                │
           [Spine1: B3]     ──►  25% 目标偏转 (腰部微调)
                │
           [Pelvis: Root]   ──►   0% 保持基底朝向
```

#### 4.1.2 角度截断与局部四元数姿态叠加
设目标注视点在局部骨骼空间中的偏差角为 $(\theta_{\text{yaw}}, \theta_{\text{pitch}})$，必须将其严格截断在各骨骼的人体解剖舒适极限内：

$$\hat{\theta}_{\text{yaw}} = \operatorname{clamp}(\theta_{\text{yaw}}, -\theta_{\text{yaw}}^{\max}, \theta_{\text{yaw}}^{\max})$$

$$\hat{\theta}_{\text{pitch}} = \operatorname{clamp}(\theta_{\text{pitch}}, -\theta_{\text{pitch}}^{\max}, \theta_{\text{pitch}}^{\max})$$

每块骨骼的目标偏移四元数 $\mathbf{q}_{\Delta, k}$ 根据当前骨骼权重插值获得：

$$\mathbf{q}_{\Delta, k} = \operatorname{Slerp}(\mathbf{I}, \mathbf{q}_{\text{clamped}}, w_k)$$

$$\mathbf{Q}_{\text{final}, k} = \mathbf{Q}_{\text{anim}, k} \cdot \mathbf{q}_{\Delta, k}$$

#### 4.1.3 个性化调制与微动作（Idling & Modulations）
注视持续时间并非静态常量，而是与智能体黑板中的个性特征参数动态关联：
- **注视保持时间**：$T_{\text{hold}} = f(\text{Blackboard.Assertiveness}, \text{Interaction.Intensity})$。自信或强势角色的 $T_{\text{hold}}$ 显著更长。
- **微注视移开（Micro-Aversion）**：羞怯或心虚的角色（高 `Shyness`）会定期触发注视打断计时器，在 $[-15^\circ, -30^\circ]$ 水平夹角范围内短暂偏向地面，持续 $0.8 \sim 1.5\text{s}$ 后平滑回正。

---

### 4.2 人际距离与 O 型空间拓扑更新（Proxemics & O-Frame）

#### 4.2.1 O-Frame 空间环形算法推导
当 $M$ 个智能体围绕共同主题进行交互时，`SocialObjectComponent` 维护一个拟合圆环。所有参与者分布在半径为 $R$ 的圆周上，法向朝向圆心 $\mathbf{C}$。

```
                     Agent 1
                        ▲
                     [θ1]│
                         │
        Agent 4 ◄─────── C ───────► Agent 2
          [θ4]        (Center)       [θ2]
                         │
                         │
                         ▼ [θ3]
                     Agent 3
```

1. **动态半径调节**：为确保适宜的社交距离 $D_{\text{desired}}$（一般为 $1.2\text{m} \sim 1.8\text{m}$），根据参与者数量 $M$ 动态解算最小圆周半径：

   $$C = 2\pi R \approx M \cdot D_{\text{desired}} \implies R = \max\left(R_{\min}, \frac{M \cdot D_{\text{desired}}}{2\pi}\right)$$

2. **理想槽位角计算**：按进入次序或亲密度权重，第 $i$ 个智能体的平衡偏转角为：

   $$\phi_i = \phi_0 + i \cdot \frac{2\pi}{M}, \quad i \in [0, M-1]$$

   其在世界坐标系下的理想站位点为：

   $$\mathbf{p}_{\text{target}, i} = \mathbf{C} + R \cdot \left(\cos(\phi_i)\mathbf{u} + \sin(\phi_i)\mathbf{v}\right)$$

   其中 $\mathbf{u}, \mathbf{v}$ 为该 O-Frame 平面的正交基向量。

#### 4.2.2 导向行为力学模型与震荡抑制
若每当群组成员轻微位移时智能体都立即步进重定位，会导致严重的足底滑步（Foot Sliding）与抽搐抖动。系统引入弹簧-阻尼临界模型与位移死区（Deadzone）：

设当前智能体位置为 $\mathbf{x}$，当前机动速度为 $\mathbf{v}$，目标站位点为 $\mathbf{p}_{\text{target}}$。定义位置偏差向量 $\mathbf{e} = \mathbf{p}_{\text{target}} - \mathbf{x}$。

```
               Deadzone Thr (r_min)          Soft Falloff (r_max)
                      │                              │
[保持不动/不触发位移]   │      线性阻尼力接入过渡区     │   最大导向推力 (Max Steering)
◄─────────────────────┼──────────────────────────────┼────────────────────────►
0                     │                              │                     |e|
```

位置修正驱动力 $\mathbf{F}_{\text{steer}}$ 计算规则如下：

$$\mathbf{F}_{\text{steer}} = \begin{cases} 
\mathbf{0}, & \text{if } \|\mathbf{e}\| < r_{\text{deadzone}} \\
-k_p \mathbf{e} - k_d \mathbf{v}, & \text{if } r_{\text{deadzone}} \le \|\mathbf{e}\| \le r_{\text{max}} \\
\mathbf{F}_{\max} \cdot \frac{\mathbf{e}}{\|\mathbf{e}\|}, & \text{if } \|\mathbf{e}\| > r_{\text{max}}
\end{cases}$$

其中 $k_p$ 为弹性比例系数，$k_d$ 为阻尼系数（满足临界阻尼比 $\zeta = \frac{k_d}{2\sqrt{k_p}} \approx 1.0$，彻底消除位置振荡过冲）。

---

### 4.3 姿态混合与手势同步机制

#### 4.3.1 情绪驱动的动画混合树偏置（Mood Bias）
姿态由 `PostureComponent` 驱动，不直接硬切动画，而是通过连续变量控制混合空间：
1. 黑板中写入由社交上下文更新的情绪标量 $M \in [-1.0, 1.0]$（负向代表压抑、沮丧，正向代表高昂、开朗）。
2. 动画状态机建立双权值混合节点（Blend Node）：
   $$\mathbf{Pose}_{\text{posture}} = (1 - \beta) \cdot \mathbf{Pose}_{\text{somber}} + \beta \cdot \mathbf{Pose}_{\text{cheerful}}$$
   其中权重 $\beta = \frac{M + 1.0}{2.0}$。

#### 4.3.2 叠加手势管线与音频事件标记（Additive Gestures & Audio Events）
手势属于高频短效表达，与底层长效的行走、站立循环分离。

```
           [底层基础动画: Locomotion / Stance] ──┐
                                                 ├─► [局部姿态融合] ──► 骨骼姿态输出
[音频轨道 Event] ──► [手势片段 (Additive Gestures)] ─┘
```

手势执行遵循两项关键准则：
- **互斥通道仲裁（Masking/Exclusion）**：若基础动画处于强上肢动作状态（如闪避、翻滚、攀爬），手势发射器直接抑制触发；仅当上肢通道处于 `Idle` 或 `GentleLocomotion` 时，执行上半身骨骼分层叠加混合（Per-bone Masked Blending）。
- **语音音轨标记驱动（Audio Punctuated Cues）**：手势的发起不通过轮询检测，而是由音频管线在声伴音频轨道上打点（Audio Cue Markers）。音频播放到达时间戳时分发 `EV_PUNCTUATE_GESTURE` 事件，驱动 `GestureComponent` 触发动作。

#### 4.3.3 双骨骼反向运动学（2-Bone IK）接触修正
涉及角色接触（如握手、拍肩）时，由于不同角色体型、地面坡度及胶囊体间距的实时变动，静态叠加混合必然导致手掌悬空或穿透胸腔网格。

```
     Shoulder (P0)
         o
          \
           \  L1 (Upper Arm)
            \
             o Elbow (P1)
            /
           /  L2 (Forearm)
          /
         o Hand (P2) ──► Target Contact Point (P_target)
```

系统采用标准解析式 2-Bone IK 实时计算肩关节、肘关节与手腕目标朝向：
1. 给定目标接触点 $\mathbf{P}_{\text{target}}$，肩关节原点 $\mathbf{P}_0$，上臂长 $L_1$，前臂长 $L_2$。
2. 计算伸展距离 $d = \operatorname{clamp}(\|\mathbf{P}_{\text{target}} - \mathbf{P}_0\|, \epsilon, L_1 + L_2 - \epsilon)$。
3. 应用余弦定理直接解算肘关节弯曲内角 $\alpha_{\text{elbow}}$ 与肩关节提升角 $\alpha_{\text{shoulder}}$：
   $$\cos \alpha_{\text{elbow}} = \frac{L_1^2 + L_2^2 - d^2}{2 L_1 L_2}$$
   $$\cos \alpha_{\text{shoulder}} = \frac{L_1^2 + d^2 - L_2^2}{2 L_1 d}$$
4. 结合极向目标向量（Pole Vector，肘部弯曲约束平面）求解闭式解，确保手掌骨骼原点无缝吸附至对方角色的骨骼锚点（Socket）上。

---

## 5. 生产级 C++ 源码实现

以下为生产环境下视线修正与 O 型空间人际距离计算的核心模块实现。代码遵循现代化 C++17 规范，内含完整的断言防御、阻尼运算与姿态矩阵操作。

### 5.1 视线控制器：`GazeComponent.h / .cpp`

```cpp
// GazeComponent.h
#pragma once
#include <vector>
#include <cassert>
#include <algorithm>

struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    Vector3 Normalized() const {
        float l = Length();
        return l > 1e-5f ? Vector3{x / l, y / l, z / l} : Vector3{};
    }
    static float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vector3 Cross(const Vector3& a, const Vector3& b) {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
};

struct Quaternion {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
    static Quaternion Identity() { return {1.0f, 0.0f, 0.0f, 0.0f}; }
    static Quaternion AngleAxis(float angleRad, const Vector3& axis) {
        float half = angleRad * 0.5f;
        float s = std::sin(half);
        return {std::cos(half), axis.x * s, axis.y * s, axis.z * s};
    }
    Quaternion operator*(const Quaternion& q) const {
        return {
            w * q.w - x * q.x - y * q.y - z * q.z,
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w
        };
    }
    static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) {
        float cosHalfTheta = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
        Quaternion target = b;
        if (cosHalfTheta < 0.0f) {
            target = {-b.w, -b.x, -b.y, -b.z};
            cosHalfTheta = -cosHalfTheta;
        }
        if (cosHalfTheta >= 0.999f) {
            return {
                a.w + t * (target.w - a.w),
                a.x + t * (target.x - a.x),
                a.y + t * (target.y - a.y),
                a.z + t * (target.z - a.z)
            };
        }
        float halfTheta = std::acos(cosHalfTheta);
        float sinHalfTheta = std::sqrt(1.0f - cosHalfTheta * cosHalfTheta);
        float ratioA = std::sin((1.0f - t) * halfTheta) / sinHalfTheta;
        float ratioB = std::sin(t * halfTheta) / sinHalfTheta;
        return {
            a.w * ratioA + target.w * ratioB,
            a.x * ratioA + target.x * ratioB,
            a.y * ratioA + target.y * ratioB,
            a.z * ratioA + target.z * ratioB
        };
    }
};

class AnimationComponent;

class GazeComponent {
public:
    void Initialize(AnimationComponent* animComp);
    void UpdateGaze(float deltaTime);
    void SetLookAtTarget(const Vector3& targetPos, float duration, float intensity);
    void ClearGaze();

    // 姿态生成回调函数 (由 AnimationComponent 在姿态提交渲染前调用)
    void OnEvaluateAnimation(std::vector<Quaternion>& boneRotations);

private:
    AnimationComponent* animationComponent = nullptr;
    Vector3 currentTargetPos;
    float gazeTimer = 0.0f;
    float gazeWeight = 0.0f;
    float targetWeight = 0.0f;

    // 骨骼链 ID: [0] = Head, [1] = Neck, [2] = SpineUpper, [3] = SpineLower
    std::vector<int> spineBoneIndices;
    const float maxYawRad = 1.221f;   // 70 度
    const float maxPitchRad = 0.785f; // 45 度
};
```

```cpp
// GazeComponent.cpp
#include "GazeComponent.h"
#include <cmath>

void GazeComponent::Initialize(AnimationComponent* animComp) {
    animationComponent = animComp;
    assert(animationComponent != nullptr && "Fatal: GazeComponent requires an AnimationComponent!");

    // 注册为动画评估后处理监听者
    // animationComponent->RegisterPoseModifier([this](auto& bones) { OnEvaluateAnimation(bones); });
    
    // 初始化骨骼层级节点索引 (从头至脊柱)
    spineBoneIndices = { 10, 9, 8, 7 }; 
}

void GazeComponent::SetLookAtTarget(const Vector3& targetPos, float duration, float intensity) {
    currentTargetPos = targetPos;
    gazeTimer = duration;
    targetWeight = std::clamp(intensity, 0.0f, 1.0f);
}

void GazeComponent::ClearGaze() {
    targetWeight = 0.0f;
}

void GazeComponent::UpdateGaze(float deltaTime) {
    if (gazeTimer > 0.0f) {
        gazeTimer -= deltaTime;
        if (gazeTimer <= 0.0f) {
            ClearGaze();
        }
    }
    // 权重阻尼过渡
    const float blendSpeed = 4.0f;
    gazeWeight += (targetWeight - gazeWeight) * std::clamp(deltaTime * blendSpeed, 0.0f, 1.0f);
}

void GazeComponent::OnEvaluateAnimation(std::vector<Quaternion>& boneRotations) {
    if (gazeWeight <= 1e-4f || spineBoneIndices.empty()) return;

    // 简化示例: 假定基底原点位于根节点
    // 计算局部视线矢量与目标偏转 (生产环境需转换至当前骨骼局部参考系)
    Vector3 localDir = currentTargetPos.Normalized();
    float yaw = std::atan2(localDir.x, localDir.z);
    float pitch = -std::asin(std::clamp(localDir.y, -1.0f, 1.0f));

    // 角度解剖学极限约束
    yaw = std::clamp(yaw, -maxYawRad, maxYawRad);
    pitch = std::clamp(pitch, -maxPitchRad, maxPitchRad);

    Quaternion fullHeadRot = Quaternion::AngleAxis(yaw, {0.0f, 1.0f, 0.0f}) *
                            Quaternion::AngleAxis(pitch, {1.0f, 0.0f, 0.0f});

    size_t numBones = spineBoneIndices.size();
    for (size_t i = 0; i < numBones; ++i) {
        int boneIdx = spineBoneIndices[i];
        if (boneIdx >= static_cast<int>(boneRotations.size())) continue;

        // 沿脊柱向下衰减旋转比率 (离头部越远，扭转份额越小)
        float chainRatio = 1.0f - static_cast<float>(i) / static_cast<float>(numBones);
        float effectiveWeight = gazeWeight * (chainRatio * chainRatio); // 二次衰减

        Quaternion offset = Quaternion::Slerp(Quaternion::Identity(), fullHeadRot, effectiveWeight);
        boneRotations[boneIdx] = boneRotations[boneIdx] * offset;
    }
}
```

---

### 5.2 O 型空间人际距离控制器：`ProxemicComponent.h / .cpp`

```cpp
// ProxemicComponent.h
#pragma once
#include <vector>
#include <cassert>
#include <cmath>

class LocomotionComponent;

class ProxemicComponent {
public:
    void Initialize(LocomotionComponent* locoComp);
    void SetSocialCircle(const Vector3& center, float radius, float angleRad);
    void Update(float deltaTime, const Vector3& currentAgentPos, const Vector3& currentVelocity);

    Vector3 GetDesiredVelocity() const { return desiredSteeringVelocity; }

private:
    LocomotionComponent* locomotionComponent = nullptr;
    Vector3 circleCenter;
    float circleRadius = 1.5f;
    float targetSlotAngle = 0.0f;
    Vector3 desiredSteeringVelocity;

    // 弹簧阻尼模型参数
    const float deadzoneDistance = 0.25f; // 25cm 内禁止触发位移，防止抖动
    const float maxAcceleration = 2.0f;
    const float kp = 1.8f;                // 弹性刚度
    const float kd = 2.68f;               // 阻尼系数 (2 * sqrt(1.8) ≈ 2.68 临界阻尼)
};
```

```cpp
// ProxemicComponent.cpp
#include "ProxemicComponent.h"

void ProxemicComponent::Initialize(LocomotionComponent* locoComp) {
    locomotionComponent = locoComp;
    assert(locomotionComponent != nullptr && "Fatal: ProxemicComponent requires LocomotionComponent!");
}

void ProxemicComponent::SetSocialCircle(const Vector3& center, float radius, float angleRad) {
    circleCenter = center;
    circleRadius = radius;
    targetSlotAngle = angleRad;
}

void ProxemicComponent::Update(float deltaTime, const Vector3& currentAgentPos, const Vector3& currentVelocity) {
    // 1. 根据 O-Frame 几何参数计算目标落位
    Vector3 targetPos = {
        circleCenter.x + circleRadius * std::cos(targetSlotAngle),
        circleCenter.y,
        circleCenter.z + circleRadius * std::sin(targetSlotAngle)
    };

    // 2. 空间偏差向量
    Vector3 error = targetPos - currentAgentPos;
    error.y = 0.0f; // 锁定在水平面导航
    float dist = error.Length();

    // 3. 震荡死区抑制 (Hysteresis & Deadzone)
    if (dist < deadzoneDistance) {
        desiredSteeringVelocity = {0.0f, 0.0f, 0.0f};
        return;
    }

    // 4. 计算临界阻尼导向力 (Spring-Damper Steering Force)
    Vector3 springForce = error * kp;
    Vector3 dampingForce = currentVelocity * kd;
    Vector3 netForce = springForce - dampingForce;

    // 限制最大加速度
    float forceMag = net

---

在现代高拟真度游戏世界中，NPC 的社交拟真度（Social Believability）与行为真实感（Behavioral Realism）已成为维系玩家沉浸感与“生命幻觉”（Illusion of Life）的核心要素。本章基于组件化架构（Component-Based Architecture）与事件驱动模型（Event-Driven Model），论述社交动力学系统（Social Dynamics System）的运行时调度管线（Execution Pipeline）、对象协商协议（Negotiation Protocol）、动画与移动物理级融合（Animation & Locomotion Integration），并提供生产环境下的架构设计范式。

---

## 1. 系统架构全景与实体容器模型 (System Architecture & Entity Container Model)

社交动力学系统并非孤立运行的单一单例（Singleton），而是以高度解耦的组件组合嵌入在通用实体容器（`GameObject` / `Entity`）中。各子系统之间通过**黑板（Blackboard）**共享认知状态，通过**事件总线（Event Stream / Message Bus）**处理跨实体与组件间的异步协调。

### 1.1 实体组件容器依赖拓扑

```
+-----------------------------------------------------------------------------------+
|                               GameObject Container                                |
|                                                                                   |
|  +---------------------+        +--------------------+       +-----------------+  |
|  |    SensorySystem    | -----> |     Blackboard     | <---> |   AIComponent   |  |
|  | (Perception System) |        |   (Shared State)   |       |  (BehaviorTree) |  |
|  +---------------------+        +--------------------+       +-----------------+  |
|            |                              ^                           |           |
|            | 发现交互契机                  | 读取/写入更新              | 条件满足触发|
|            v                              v                           v           |
|  +-----------------------------------------------------------------------------+  |
|  |                               SocialComponent                               |  |
|  |           (协调器：注视 Gaze / 姿态 Posture / 朝向 Orientation / 间距 Proxemics)  |  |
|  +-----------------------------------------------------------------------------+  |
|          |                           |                               |            |
|          | 跨实体协商                 | 驱动局部姿态/IK                | 输出物理力矩|
|          v                           v                               v            |
|  [Target SocialObjectComponent] [AnimationComponent]      [LocomotionComponent]   |
+-----------------------------------------------------------------------------------+
```

### 1.2 核心组件职责矩阵

| 组件名称 (Component) | 架构定位与核心职责 (Core Responsibilities) | 核心依赖 (Dependencies) |
| :--- | :--- | :--- |
| **`AIComponent`** | 驱动高层决策逻辑，运行**行为树（Behavior Tree, BT）**或**分层任务网络（HTN）**，依据黑板状态决定当前生命周期行为。 | `Blackboard`, `BehaviorTree` |
| **`Blackboard`** | 实体私有或局域共享的数据总线（Key-Value Data Store），解耦感知、决策与执行。 | 无外部强依赖 |
| **`SocialComponent`** | 社交行为核心协调器。管理注视点（Gaze）、身体姿态（Posture）、交互槽位对齐与群聚协调。 | `AIComponent`, `Blackboard`, `AnimationComponent` |
| **`SocialObjectComponent`** | 社交锚点（Smart Object）或交互发起者的仲裁组件。维护角色插槽（Slots）、协调轮流发言（Turn-Taking）及生命周期。 | `EventStream` |
| **`AnimationComponent`** | 接收姿态偏差与注视目标，执行程序化反向动力学（Procedural IK）与动画状态机混合（Blend Tree）。 | 骨骼蒙皮系统（Skeleton Rig） |
| **`LocomotionComponent`** | 接收转向行为（Steering Behaviors）输出的作用力（Force/Torque），驱动角色在**导航网格（NavMesh）**上平滑位移。 | 物理引擎 / 碰撞系统 / NavMesh |

---

## 2. 运行时协同与协议流转 (Runtime Coordination & Protocol Flow)

社交交互是一个由**空间感知（Spatial Awareness）**、**意图握手（Intent Handshake）**、**角色仲裁（Role Arbitration）**、**多模态同步（Multimodal Synchronization）**及**解散回收（Teardown）**构成的闭环状态机。

### 2.1 交互生命周期时序

```
Agent A (SocialComponent)      Blackboard A       SocialObjectComponent (Target)     Agent B (Participant)
           |                         |                           |                             |
[1. 感知]  |<-- Perception Event ----|                           |                             |
           |    (Detected Social Opportunity)                    |                             |
[2. 决策]  |---- Write Intent ------>|                           |                             |
           |                         |                           |                             |
[3. 握手]  |================ Request Participation =============>|                             |
           |                                                     |<==== Request Participation =|
           |                                                     |                             |
[4. 仲裁]  |                                                     |-- Check Quorum Satisfied -- |
           |<=============== Assign Role (Speaker) ==============|                             |
           |                                                     |======= Assign Role (Listener)
           |                                                     |                             |
[5. 同步]  |<-------------- Sync Frame (Origin, Turn-Taking) ----|                             |
           |                                                     |                             |
[6. 释放]  |<=============== Release Participant =================|                             |
           |                                                     |======= Release Participant =
```

### 2.2 阶段解析与工业级权衡

#### 阶段 1：感知与黑板写入 (Perception to Blackboard)
当角色的感知系统（视锥检测、听觉广播或**空间推理网格 Spatial Reasoning Grid**）检测到周围存在的交互契机（例如：正在交谈的群体 F-Formation、一个可供倚靠的吧台或请求对话的玩家），感知系统将该交互源对象指针及其特征标签写入 `Blackboard`。

#### 阶段 2：行为树条件触发 (Behavior Tree Evaluation)
`AIComponent` 的行为树在周期性 Tick 中评估条件节点（Decorator / Precondition）。当黑板中存在有效的社交契机数据时：
1. 对应的决策分支评估为 `true`；
2. 行为树激活社交任务节点（`Task_EngageSocialInteraction`）；
3. 行为树派发事件至该 `GameObject` 下的 `SocialComponent`。

#### 阶段 3：多方协商与请求节流 (Negotiation & Request Throttling)
`SocialComponent` 接收到决策后，向目标对象的 `SocialObjectComponent` 异步投递加入请求（`RequestParticipationEvent`）。在此阶段，必须在系统吞吐量与表现力之间做出权衡：

$$\text{Request Overhead} = \mathcal{O}(N \times M)$$

其中 $N$ 为当前活跃 Agent 数量，$M$ 为单 Agent 探测半径内的候选交互对象数量。
* **欠请求（Under-requesting）**：限制单 Agent 仅能并发申请 1 个交互。表现为角色对复杂社交环境反应迟钝、冷漠。
* **过请求（Over-requesting）**：Agent 向可视范围内的所有候选对象广播加入请求。这会导致严重的性能峰值（Spike），一旦某个交互握手成功，Agent 必须产生大量的取消请求（Revoke/Cancel Events）清空等待队列，浪费事件总线带宽与哈希表查询开销。
* **工业界平衡策略（Trade-off Rule）**：采用带优先级的**效用打分（Utility Scoring）**进行前置剪枝，单 Agent 最多维持 $K$ 个挂起请求（通常 $K \in [2, 3]$），并在距离阈值衰减时自动触发超时丢弃。

#### 阶段 4：角色仲裁与就位 (Role Assignment & Interaction Synchronization)
当 `SocialObjectComponent` 收集到满足交互最小阈值的人数（Quorum）后，交互正式确立：
1. **角色派发**：向所有入选的 Agent 派发分配通知（如指定 Agent A 为主讲人 `Speaker`，Agent B 为倾听者 `Listener`）；
2. **未就位容错**：在收到角色确认前，所有参与者均维持原本的巡逻（Patrol）或闲置（Idle）行为，杜绝状态僵死；
3. **坐标原点广播（Origin Setting）**：交互对象周期性广播当前会话的物理原点空间位置（Transform）及几何布局参数（如圆桌交谈点位、面对面注视矢量）；
4. **轮流发言控制（Turn-Taking Engine）**：基于定时器或状态机，周期性更新说话者权限，动态翻转角色姿态与注意力中心。

#### 阶段 5：会话解散与资源回收 (Teardown & Release)
当交互达成退出条件（时间结束、核心 NPC 离场、发生交火警报），`SocialObjectComponent` 广播释放事件（`ReleaseParticipantEvent`）。所有 Agent 的 `SocialComponent` 将状态同步切回至自主寻路状态，并向行为树通报节点完成（`Task Execution Status: SUCCESS / FAILURE`）。

---

## 3. 单帧主循环更新管线 (Per-Frame Main Loop Update Pipeline)

为保障多智能体行为的高内聚与确定性，主循环每帧内部的更新次序（Update Order）具有严苛的依赖关系：

```
Main Game Loop Frame (t -> t + dt)
│
├── Step 1: AIComponent Tick (Behavior Tree Execution)
│   ├── 读取感知系统更新至 Blackboard 的外部环境数据
│   ├── 遍历行为树节点，更新高层逻辑意图
│   └── 将社交意向与目标槽位写入 Blackboard
│
├── Step 2: SocialComponent Synchronous Tick
│   ├── 从 Blackboard 读取决策层输出的最新社交状态
│   ├── 向各特化执行通道分发事件 (Event Dispatching):
│   │   ├── GazeEvent -> 输出目标注视点 (Look-at Target Vector)
│   │   ├── PostureEvent -> 输出身体姿态调整偏置 (Leaning, Stance Weight)
│   │   └── ProxemicsEvent -> 计算个人空间边界与防碰撞排斥力
│   └── 跨组件输出最终执行载荷
│
└── Step 3: Physical & Kinematic Assimilation
    ├── 动画通道 (Animation Channel):
    │   └── 将 Posture / Gaze 参数写入 AnimationComponent 动画图 (IK Layer Procedural Blending)
    └── 动力学通道 (Locomotion Channel):
        └── 将转向力 (Steering Forces) 输入 LocomotionComponent，经物理积分推进位置更新
```

### 3.1 动力学与动画的双通道混合数学模型

社交动力学输出的作用力必须平滑吸收进物理与位移更新中。设角色的目标朝向与当前朝向四元数偏差为 $\Delta q$，期望行进线速度为 $\vec{v}_{\text{target}}$：

#### 1. 转向力矩融合 (Torque / Angular Assimilation)
角速度 $\vec{\omega}$ 的平滑逼近采用临界阻尼弹簧算法（Critically Damped Spring）：

$$\vec{\tau}_{\text{social}} = k_p \, \text{AngleAxis}(\Delta q) - k_d \, \vec{\omega}_{\text{current}}$$

其中 $k_p$ 为姿态恢复刚度系数，$k_d$ 为阻尼系数。

#### 2. 空间人际距离力场 (Proxemics Potential Field)
依据 Kendon 与 Argyle 的人际距离学说，角色在交互过程中维持安全距离的排斥力采用软约束势能函数计算：

$$\vec{F}_{\text{proxemics}} = \sum_{j \neq i} \frac{S_{\text{repulsive}}}{(\|\vec{p}_i - \vec{p}_j\| - d_{\text{pref}})^2 + \epsilon} \cdot \frac{\vec{p}_i - \vec{p}_j}{\|\vec{p}_i - \vec{p}_j\|}$$

其中 $d_{\text{pref}}$ 为期望交际距离，$S_{\text{repulsive}}$ 为排斥强度系数，$\epsilon$ 为防除零奇异项平滑因子。该力与寻路转向行为（Steering Behaviors）的合力一并递交至 `LocomotionComponent` 积分器。

---

## 4. 工业级 C++ 生产架构落地参考 (Production C++ Architecture Reference)

以下为符合现代高并发、组件化规范的社交动力学调度管线实现：

```cpp
#pragma once
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// -------------------------------------------------------------------------
// 基础事件定义 (Event Infrastructure)
// -------------------------------------------------------------------------
enum class SocialRole { None, Initiator, Speaker, Listener, Bystander };

struct SocialInteractionEvent {
    uint64_t interactionId;
    uint64_t targetEntityId;
    SocialRole assignedRole;
    glm::vec3 interactionOrigin;
};

// -------------------------------------------------------------------------
// 共享黑板 (Blackboard)
// -------------------------------------------------------------------------
class Blackboard {
public:
    template<typename T>
    void Set(const std::string& key, const T& val) {
        // 生产环境应使用类型擦除或固定内存池，此处为抽象范例
        mData[key] = std::make_shared<T>(val);
    }

    template<typename T>
    bool TryGet(const std::string& key, T& outVal) const {
        auto it = mData.find(key);
        if (it != mData.end()) {
            outVal = *std::static_pointer_cast<T>(it->second);
            return true;
        }
        return false;
    }
private:
    std::unordered_map<std::string, std::shared_ptr<void>> mData;
};

// -------------------------------------------------------------------------
// 社交交互对象组件 (SocialObjectComponent - 位于交互锚点或主机上)
// -------------------------------------------------------------------------
class SocialObjectComponent {
public:
    void RegisterRequest(uint64_t requesterEntityId) {
        if (mActiveParticipants.size() < mMaxParticipants) {
            mPendingRequests.push_back(requesterEntityId);
        }
    }

    void UpdateArbitration(float deltaTime) {
        if (!mIsFormed && mPendingRequests.size() >= mMinParticipants) {
            // 满足法定参与人数，建立社交交互
            mIsFormed = true;
            for (size_t i = 0; i < mPendingRequests.size(); ++i) {
                SocialRole role = (i == 0) ? SocialRole::Speaker : SocialRole::Listener;
                mActiveParticipants[mPendingRequests[i]] = role;
                NotifyParticipantAssigned(mPendingRequests[i], role);
            }
            mPendingRequests.clear();
        }
    }

    void ReleaseInteraction() {
        for (const auto& [agentId, role] : mActiveParticipants) {
            NotifyParticipantReleased(agentId);
        }
        mActiveParticipants.clear();
        mIsFormed = false;
    }

private:
    void NotifyParticipantAssigned(uint64_t agentId, SocialRole role);
    void NotifyParticipantReleased(uint64_t agentId);

    size_t mMinParticipants = 2;
    size_t mMaxParticipants = 4;
    bool mIsFormed = false;
    std::vector<uint64_t> mPendingRequests;
    std::unordered_map<uint64_t, SocialRole> mActiveParticipants;
};

// -------------------------------------------------------------------------
// Agent 社交核心组件 (SocialComponent)
// -------------------------------------------------------------------------
class SocialComponent {
public:
    void Initialize(Blackboard* blackboard) {
        mBlackboard = blackboard;
    }

    // 接收系统事件通知 (可由调试工具直接注入)
    void OnRoleAssigned(const SocialInteractionEvent& evt) {
        mCurrentRole = evt.assignedRole;
        mInteractionOrigin = evt.interactionOrigin;
        mIsInInteraction = true;
    }

    void OnInteractionReleased() {
        mCurrentRole = SocialRole::None;
        mIsInInteraction = false;
    }

    // 对应 35.5 节的主循环同步 Tick
    void Update(float deltaTime, class AnimationComponent& anim, class LocomotionComponent& locomotion) {
        if (!mIsInInteraction) {
            return;
        }

        // 1. 读取 Blackboard 最新数据
        glm::vec3 focusTarget{};
        mBlackboard->TryGet("PerceivedFocusTarget", focusTarget);

        // 2. 状态映射与计算
        glm::vec3 gazeTarget = (mCurrentRole == SocialRole::Speaker) ? mInteractionOrigin : focusTarget;
        
        // 3. 动力学通道：输出面向交互中心的导向力
        glm::vec3 steeringForce = ComputeSocialSteering(mInteractionOrigin);
        locomotion.ApplySteeringForce(steeringForce);

        // 4. 动画通道：程序化注视与姿态同化
        anim.SetLookAtTarget(gazeTarget, 1.0f /* blendWeight */);
        anim.SetPostureBias(mCurrentRole == SocialRole::Speaker ? 0.2f : -0.1f);
    }

private:
    glm::vec3 ComputeSocialSteering(const glm::vec3& targetPos);

    Blackboard* mBlackboard = nullptr;
    SocialRole mCurrentRole = SocialRole::None;
    glm::vec3 mInteractionOrigin{0.0f};
    bool mIsInInteraction = false;
};
```

---

## 5. 解耦调试与模块化渐进式落地路径 (Decoupled Debugging & Progressive Delivery)

如原著章节指出，组件化设计的终极工程价值在于**测试解耦**与**增量迭代（Incremental Evolution）**。

### 5.1 事件流注入式调试 (Event Injection Debugging)
在传统的强耦合状态机中，调试“倾听他人说话并点头”的行为必须要求场景内真实存在另一个处于“说话状态”的 NPC，测试代价极高。而在组件化架构下：
* `SocialComponent` 不直接依赖其他角色的内存实例，仅依赖事件流（Event Stream）；
* QA 或开发者可通过控制台指令或调试面板（ImGui）向当前角色的事件队列直接手动注入 `OnRoleAssigned(Speaker)` 或 `InteractionSyncFrame`；
* 单个角色在完全空白的测试场景中即可独立验证姿态混合、头部 IK 追踪、注视切换等表现力逻辑。

### 5.2 社交动力学系统的工业级演进梯度

工业界引入社交动力学系统时，应严格遵循由轻量至复杂的迭代路径：

```
Level 1: 头部注视 (Look-At System)
    │   └── 仅实现局部骨骼 IK，角色在巡逻时向玩家或重要物体转头
    v
Level 2: 空间距离与身体朝向 (Proxemics & Orientation)
    │   └── 引入排斥力场，角色停步时自动转向对话者并调整间距
    v
Level 3: 槽位式智能对象交互 (Smart Object Synchronization)
    │   └── 引入 SocialObjectComponent，实现固定点位的双人/多人对话与同步就座
    v
Level 4: 动态空间群聚学模型 (Dynamic F-Formations & Turn-Taking)
        └── 完整的无锚点动态组群、自主插话（Interruption）、动态散开与重构

---

## 参考文献 (References)

* **[Ancessi 10]** Ancessi, Laurent. Interview with Alex Champandard. *Interactive Parkour Animation*, aiGameDev.com, 2010. [Online Masterclass].
* **[Argyle 88]** Argyle, Michael. *Bodily Communication*. 2nd Edition, Taylor & Francis / Methuen & Co. Ltd, 1988, p. 363.
* **[Cassell 00]** Cassell, Justine, et al. *Embodied Conversational Agents*. MIT Press, Cambridge, MA, 2000, p. 440.
* **[Kendon 90]** Kendon, Adam. *Conducting Interaction: Patterns of Behavior in Focused Encounters* (Studies in Interactional Sociolinguistics). Cambridge University Press, 1990, p. 308.
* **[Pedica and Vilhjálmsson 08]** Pedica, Claudio, and Hannes Högni Vilhjálmsson. "Social perception and steering for online avatars." *Intelligent Virtual Agents (IVA 2008)*, Lecture Notes in Computer Science, vol. 5208, Springer, Berlin, Heidelberg, 2008, pp. 104–116.
