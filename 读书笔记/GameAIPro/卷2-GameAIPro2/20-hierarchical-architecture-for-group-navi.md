---
type: Reference
title: "第20章 Hierarchical Architecture for Group Navigation Behaviors"
description: "Game AI Pro 工业级精读：Hierarchical Architecture for Group Navigation Behaviors。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第20章 Hierarchical Architecture for Group Navigation Behaviors

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 20.  
> 原文作者 / 资源：[Hierarchical Architecture for Group Navigation Behaviors](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter20_Hierarchical_Architecture_for_Group_Navigation_Behaviors.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

*(Hierarchical Architecture for Group Navigation Behaviors)*

---

## 1. 概述与群组行为分类学 (Introduction & Group Taxonomy)

在现代 3D 游戏引擎与工业级虚拟环境仿真中，单体自主智能体（Autonomous Agents）的底层导航技术（包括基于导航网格 NavMesh 的路径生成、路径跟随及基于局部几何的避障算法）已高度工业化。然而，当智能体需要呈现逼真的群体协作（如战术小队推进、游人随行导览、社交人群散步）时，传统单体独立导航模型极易引发群体脱节、队形溃散、穿插死锁及过拟合避障震荡。

群组导航系统必须协调**宏观层面的群体凝聚与编队目标**与**微观层面的个体运动约束与避障反应**。在仿真计算与游戏 AI 文献中，群体导航模型被系统性地划分为三大类拓扑范式：

```
                             群组导航分类学 (Group Taxonomy)
                                            │
        ┌───────────────────────────────────┼──────────────────────────────────┐
        ▼                                   ▼                                  ▼
   群集模型 (Flocks)                严谨阵型模型 (Formations)           小型社交群组 (Social Groups)
  ─────────────────────           ─────────────────────────          ────────────────────────────
  · 自底向上 (Bottom-Up)           · 自顶向下 (Top-Down)              · 社交动态平衡 (Social-Dynamic)
  · 局部三原则局部作用               · 绝对/相对槽位严格约束             · 随环境通畅度与密度自适应形变
  · 无中心化/无严苛空间拓扑         · 视场/射界朝向绝对分配             · 并排(Abreast) ➔ V形 ➔ 单列纵队
```

### 1.1 群集模型 (Flocks)
* **拓扑机理**：由 Craig Reynolds 开创的分布式人工生命模型，核心在于自底向上（Bottom-Up）涌现。群集没有中心化的控制节点，每个成员在局部邻域感知范围内独立执行三项基础导向行为（Steering Behaviors）：
  1. **分离 (Separation)**：避免与邻近个体拥挤碰撞；
  2. **队列 (Alignment)**：匹配邻近个体的平均朝向与速度矢量；
  3. **凝聚 (Cohesion)**：向邻近个体的质心（Barycenter）靠拢。
* **运动特征**：个体以大致相仿的速度行进，无固定的几何相对位置，朝向存在局部分散性，整体形态呈非晶态（Amorphous）。群集逻辑在宏观大机群（如鸟群、鱼群）中表现优异，但在需要严苛作战阵位或紧密空间通过性的场景中无法维持秩序。

### 1.2 严谨阵型模型 (Formations)
* **拓扑机理**：采用强约束的自顶向下（Top-Down）规则系统。编队具有精确的几何槽位分配（Slot Assignments），每个个体被绑定到编队坐标系下的相对空间偏移。
* **战术特征**：
  * **射界与视场绑定 (Fields of Fire and Sight Allocation)**：个体方向与阵型行进矢量解耦，通常强制赋予特定的朝向偏移角以实现 $360^\circ$ 警戒覆盖；
  * **角色槽位匹配 (Role-Based Slot Mapping)**：重装/近战步兵部署于前排迎敌，远程射手/脆弱法师配置在阵型后方（如经典 RTS 游戏《帝国时代》的编队设计）。

### 1.3 小型社交群组 (Social Groups)
* **拓扑机理**：现实城市场景与写实开放世界中最为普遍的人群形态。实证研究（Peters 09, Moussaïd 10）表明，真实城市人流中超过 $70\%$ 为群组而非单体个体，且成员数量绝大多数限制在 $2\sim 4$ 人（超过 4 人的紧密社交群组在运动状态下极罕见）。
* **形态学与流变机理 (Morphology & Reconfiguration)**：
  社交群组的几何构型受两大相悖目标驱动：**维持视觉/听觉对话交流（社交凝聚度）**与**侧向空间通行净空（环境几何约束）**。系统根据人流密度与通道宽度在三种基态构型间动态平滑过渡：

```
【低密度 / 宽敞空间】              【中密度 / 侧向空间受限】            【高密度 / 极度狭窄】
    并排配置 (Abreast)                前向 V 型配置 (V-like)             单列纵队 (Lane)
  
      [1]   [2]   [3]                     [2]                          [1]
       ▲     ▲     ▲                     ▲                           ▲
       │     │     │                    / \                          │
                                      /   \                         [2]
   · 成员横向并排分布               [1]     [3]                       ▲
   · 最大化社交对视交流             ▲       ▲                         │
   · 迎面截面宽度最大               · 阵型向前弯折成 V 字形           [3]
                                   · 降低前向投影截面宽度             ▲
                                   · 保持眼神对视与倾听几何           · 侧向肩膀紧挨，退化为纵列
                                                                     · 截面最小化，对向人流阻力最低
```

* **分裂与合并机制 (Splitting & Merging)**：
  当遭遇不可通行障碍（如柱体、行道树或反向对冲强人流）时，编队临时解构断开，个体降级为局部避障绕行；一旦穿越瓶颈区，个体会通过重新寻路与编队恢复行为自动汇聚融合。

---

## 2. 导航管线架构与更新循环 (Navigation Pipeline Architecture)

为解决高反应度动画物理系统与高开销路径规划系统之间的帧率失配，本架构采用标准分层解耦的导航管线。

### 2.1 智能体更新循环 (Character Update Loop)

整个系统由四个核心逻辑层级顺序解耦驱动，各层级间通过强类型指令上下文通信：

```
 ┌─────────────────────────────────────────────────────────┐
 │                   决策系统 (Decision)                   │
 └────────────────────────────┬────────────────────────────┘
                              │ 目标选择 (Target Selection)
                              ▼
 ┌─────────────────────────────────────────────────────────┐
 │                  寻路系统 (Pathfinding)                 │
 └────────────────────────────┬────────────────────────────┘
                              │ 导航指令 (Navigation Orders, e.g. Waypoints Path)
                              ▼
 ┌─────────────────────────────────────────────────────────┐
 │               导航行为层 (Navigation Behavior)          │
 └────────────────────────────┬────────────────────────────┘
                              │ 运动控制指令 (Movement Orders, e.g. Desired Velocity)
                              ▼
 ┌─────────────────────────────────────────────────────────┐
 │                  运动学驱动 (Locomotion)                │
 └─────────────────────────────────────────────────────────┘
```

各阶段职责与降频执行策略：
* **决策系统 (Decision)**：基于行为树（Behavior Trees）、分层任务网络（HTN）或效用系统（Utility Systems）运行，确定宏观目标点或战略行为。更新频率极低（$1\sim 5\text{ Hz}$）；
* **寻路系统 (Pathfinding)**：在全局多边形导航网格（NavMesh）上执行图搜索，生成多边形通道并拉平得到折线路径（Waypoints）。通常为异步任务或事件触发；
* **导航行为层 (Navigation Behavior)**：根据路径数据与即时环境感知（动态邻居、临时阻挡）计算输出期望速度矢量 $\mathbf{v}_{\text{desired}}$。执行频率中等（$10\sim 30\text{ Hz}$）；
* **运动学驱动 (Locomotion)**：由物理引擎（PhysX/Havok）与角色动画状态机驱动，解析期望速度并执行转向平滑、动画根骨骼运动（Root Motion）融合与刚体碰撞推挤。以主物理帧率高频运行（$60\text{ Hz}$）。

### 2.2 导航管线流水线模型 (Pipeline Modification Pattern)

复杂的导航运动并非由单一逻辑一蹴而就，而是由一串“导航行为过滤器”组成的顺序管线（Navigation Pipeline）计算生成。管线内的每一个组件都以前序组件输出的期望移动指令为输入，结合特定上下文进行叠加校正，并向后传递：

```
       导航指令
   (Navigation Orders)
          │
          ▼
   ┌──────────────┐     $\mathbf{v}_{\text{path}}$      ┌──────────────┐     $\mathbf{v}_{\text{noisy}}$     ┌─────────────────────┐    $\mathbf{v}_{\text{final}}$
   │ 路径跟随行为 │ ───────────────────► │   负伤扰动   │ ───────────────────► │  局部动态避障行为   │ ──────────────────►
   │ PathFollow   │                      │ WoundedNoise │                      │ Collision Avoidance │
   └──────────────┘                      └──────────────┘                      └─────────────────────┘
                                                                               (具有运动指令最终仲裁权)
```

1. **基底行为（如 Path Following）**：输入多边形走廊与路径点，输出引导智能体沿路径行进的基底速度 $\mathbf{v}_{\text{path}}$；
2. **修饰行为（如 Wounded Modulate）**：引入非线性步态扰动函数 $f_{\text{noise}}(t)$，叠加物理抖动或方向偏折，生成受损步态速度矢量 $\mathbf{v}_{\text{noisy}} = \mathbf{v}_{\text{path}} + \boldsymbol{\epsilon}(t)$；
3. **安全仲裁行为（如 Collision Avoidance）**：基于 ORCA（Optimal Reciprocal Collision Avoidance）或力导向势场算法，扫描即时局部障碍几何与运动邻居，强制调整运动速度以确保时空无碰撞。**处于管线末端的行为具备最终决策仲裁权（Last Word）**。

该管线设计使得群体协同逻辑可以被无缝封装为管线中的一个独立行为节点，既可单独作用于群组级代理，亦可植入底层个体管线中。

---

## 3. 群组-成员拓扑模型对比 (Group to Members Relationship Model)

实现多智能体协同移动时，必须在“领队机制”与“虚拟代理机制”两种设计范式之间权衡选择。

```
范式 A: 领队-跟随者模型 (Leader-Follower)          范式 B: 虚拟群组代理模型 (Virtual Group Entity)
─────────────────────────────────────────────      ─────────────────────────────────────────────────
               [ Leader (实体) ]                                    [ Virtual Group Entity ]
             (自身承载物理与视觉)                                      (纯逻辑实体，无物理/视觉)
               /            \                                         /          │          \
              ▼              ▼                                       ▼           ▼           ▼
       Follower A       Follower B                             Entity A       Entity B    Entity C
      (追随实体前驱)   (追随实体前驱)                         (同构对等个体) (同构对等个体) (同构对等个体)
```

### 3.1 领队-跟随者模型深度解构 (Leader-Follower Architecture)

* **拓扑机制**：在群组中指定某一个特定物理个体充当 Leader，负责调用完整的导航管线；其余物理个体作为 Follower，注册指向 Leader 的引用，依赖 Leader 的实时位姿推导自身的局部期望速度。
* **架构弊端分析**：
  1. **状态不对等与决策异质性**：Leader 必须承载群组整体的空间几何跨度（Bulk）与全体跟随者的机动限制，使得 Leader 的寻路/避障状态机与普通个体产生硬性代码分叉，丧失多态一致性；
  2. **领队属性与群体属性混淆**：Leader 自身的物理位置、朝向与其所代表的群组质心及编队朝向产生强耦合。若 Leader 遭遇局部减速或闪避，将直接引发全体跟随者的连锁振荡；
  3. **拓扑脆弱性 (Single Point of Failure)**：领队发生物理阻挡、坠落或死亡时，需要代价高昂的领队选举机制（Leader Election）重建拓扑体系。

### 3.2 虚拟群组代理模型 (Virtual Group Entity Architecture)

* **拓扑机制**：将群组的“锚点（Anchor）”从具体个体中剥离，在内存中实例化一个不可见、无物理碰撞胶囊体、无渲染模型的纯逻辑实体（Virtual Group Entity）。
* **核心优势**：
  1. **个体同构性 (Uniformity of Individuals)**：所有成员个体在逻辑与接口层面完全平等，仅需订阅虚拟代理输出的编队引导场；
  2. **职责分离 (Separation of Concerns)**：群组代理专门负责宏观路径规划、大尺度几何通道研判与队形拓扑管理；成员个体专注于局部微观避障与动画呈现；
  3. **递归可扩展性 (Recursive Composability)**：天然支持嵌套定义。群组实体可包含若干子群组实体，形成树状群落结构（如：军团 $\rightarrow$ 大队 $\rightarrow$ 班组 $\rightarrow$ 士兵）。

---

## 4. 层次化实体组合架构 (Hierarchical Entity Architecture)

通过引入设计模式中的**组合模式 (Composite Pattern)**，将物理个体与逻辑群组在抽象基类层面统一为 `NavigationEntity`，构建起严谨的面向对象树状层次拓扑。

### 4.1 组合实体类层次拓扑 (Composite Entity Hierarchy)

```
                       ┌───────────────────────────────┐
                       │       NavigationEntity        │ (Abstract Base)
                       ├───────────────────────────────┤
                       │ - m_Position: Vector3         │
                       │ - m_Velocity: Vector3         │
                       │ - m_Orientation: Quaternion   │
                       │ - m_MaxSpeed: float           │
                       │ - m_MaxAngularSpeed: float    │
                       │ - m_Parent: GroupEntity*      │
                       ├───────────────────────────────┤
                       │ + Update(dt: float) = 0       │
                       │ + GetBoundingVolume() = 0     │
                       └───────────────┬───────────────┘
                                       │
                    ┌──────────────────┴──────────────────┐
                    │                                     │
     ┌──────────────┴──────────────┐       ┌──────────────┴──────────────┐
     │      IndividualEntity       │       │         GroupEntity         │
     ├─────────────────────────────┤       ├─────────────────────────────┤
     │ - m_PhysicsColliderRef      │       │ - m_Children: List<Entity*> │
     │ - m_LocomotionEngine        │       │ - m_FormationPattern        │
     ├─────────────────────────────┤       ├─────────────────────────────┤
     │ + Update(dt: float) override│       │ + Update(dt: float) override│
     │ + ProcessPipeline()         │       │ + AddChild(child: Entity*)  │
     └─────────────────────────────┘       │ + RecomputeBulkProperties() │
                                           └─────────────────────────────┘
```

### 4.2 群组宏观动态物理属性的数学推导与聚合算法

虚拟群组实体并不具备原生物理引擎驱动的刚体组件，其所有状态变量必须在每个逻辑 Tick 内依据其子实体集合 $\mathcal{M} = \{e_1, e_2, \dots, e_N\}$ 进行精确的数学映射与动力学约束聚合：

#### 1. 群组参考位置 (Barycentric Position)
群组的中心位置定义为其激活成员空间位置的加权质心（在等权重设定下为算术平均值）：
$$\mathbf{P}_{\text{group}} = \frac{1}{N}\sum_{i=1}^{N}\mathbf{p}_i$$

#### 2. 群组空间跨度与包围体积 (Bulk Calculation)
为支持群组级粗粒度空间裁剪与 NavMesh 几何通道校验，群组根据场景复杂度采用以下两种模型之一表征自身几何尺寸：
* **包围球模型 (Bounding Sphere)**：
  $$R_{\text{group}} = \max_{i \in \mathcal{M}} \left( \|\mathbf{p}_i - \mathbf{P}_{\text{group}}\| + r_i \right)$$
  其中 $r_i$ 为个体物理碰撞半径。
* **有向包围盒模型 (Oriented Bounding Box, OBB)**：
  在主行进方向坐标基底 $(\mathbf{u}_{\text{forward}}, \mathbf{u}_{\text{right}}, \mathbf{u}_{\text{up}})$ 下投影极值：
  $$L_{\text{half}} = \max_{i \in \mathcal{M}} \left| (\mathbf{p}_i - \mathbf{P}_{\text{group}}) \cdot \mathbf{u}_{\text{forward}} \right| + d_{\text{margin}}$$
  $$W_{\text{half}} = \max_{i \in \mathcal{M}} \left| (\mathbf{p}_i - \mathbf{P}_{\text{group}}) \cdot \mathbf{u}_{\text{right}} \right| + d_{\text{margin}}$$

#### 3. 群组瞬时取向 (Orientation)
群组成员朝向离散且高度随机，无法直接求取平均角。工业界采用**运动学对齐模型**：
* 当群组整体线速度显著时，直接将其前向基底锚定在群组线性速度矢量的单位切线上：
  $$\mathbf{d}_{\text{group}} = \begin{cases} \dfrac{\mathbf{V}_{\text{group}}}{\|\mathbf{V}_{\text{group}}\|}, & \|\mathbf{V}_{\text{group}}\| > \epsilon_{\text{threshold}} \\ \mathbf{d}_{\text{group}}^{(t-\Delta t)}, & \text{otherwise} \end{cases}$$
* 在编队旋转时，由独立的旋转行为（Rotational Navigation Behavior）通过角速度积分平滑推进。

#### 4. 最大线速度与角速度硬约束 (Kinematic Limits Constraints)
为杜绝群组脱节，群组的最高能力上限必须严格受限于群体内机能最弱的个体：
* **极限线速度**：群组最高平移速度不能突破成员最高速度的下确界：
  $$V_{\max}^{\text{group}} = \min_{i \in \mathcal{M}} \left( v_{\max}^{(i)} \right)$$
* **极限角速度 (Maximum Angular Speed)**：
  当群组绕瞬时曲率中心以角速度 $\omega$ 执行旋转时，处于编队最外侧边缘的个体将经历最高的线速度线速度分量 $v_{\text{outer}} = \mathbf{V}_{\text{group}} + \omega \cdot R_{\text{group}}$。为确保最外侧成员不发生严重掉队，群组角速度必须满足以下不等式：
  $$\omega_{\max}^{\text{group}} \le \frac{\min_{i \in \mathcal{M}}\left( v_{\max}^{(i)} \right) - \|\mathbf{V}_{\text{group}}\|}{R_{\text{group}}}$$
  式中 $R_{\text{group}}$ 为编队半宽或最大径向距离。

---

## 5. 多线程拓扑与状态只读设计 (Multithreading & Concurrency Topologies)

在追求极高算力吞吐的 3A 级大型游戏引擎中，导航系统若存在频繁的锁竞争或跨实体随机状态写入，将导致不可接受的并发性能下降。

### 5.1 数据并行更新协议 (Read-Only Double Buffering Pattern)

本架构将模拟帧强制划分为完全解耦的两阶段：**拓扑组织阶段 (Topology Management Phase)** 与 **并行感知决策阶段 (Parallel Execution Phase)**。

```
时间轴 (Time) ──►
═══════════════════════════════════════════════════════════════════════════════════════════════
[ 主线程: 串行组织阶段 ]
  · 变更群组层级结构 (Split / Merge)
  · 写入当前全局黑板 (Blackboard)
  · 提交帧状态双缓冲交换：State(T-1) ◄── [Freeze as Read-Only]
───────────────────────────────────────────────────────────────────────────────────────────────
[ Worker 工作线程池: 并行计算阶段 ]
  · Entity A Navigation Update ───► 读取 State(T-1) (无锁并发) ───► 输出 Local Command Buffer
  · Entity B Navigation Update ───► 读取 State(T-1) (无锁并发) ───► 输出 Local Command Buffer
  · Virtual Group Entity Update ──► 读取 State(T-1) (无锁并发) ───► 输出 Group Command Buffer
───────────────────────────────────────────────────────────────────────────────────────────────
[ 主线程: 串行合流阶段 ]
  · 收集全部 Command Buffers 统一应用到位移及动画状态机
═══════════════════════════════════════════════════════════════════════════════════════════════
```

在工作线程并行计算导航管线时，任何智能体 $e$ 的逻辑更新仅准许检索上一帧处于冻结状态的只读数据镜像：
1. **Self State**：$S_e^{(t-1)}$（个体自身上一帧的时空位姿与速度）；
2. **Parent / Children State**：$S_{\text{parent}}^{(t-1)}$ 与 $\{S_{\text{child}}^{(t-1)}\}$（由虚拟群组分配的编队槽位与质心参数）；
3. **Spatial Neighbors**：$\mathcal{N}_e^{(t-1)}$（通过空间哈希网格或 KD-Tree 查询到的邻居个体位姿，用于局部避障）。

任何导航行为**严禁在管线内部动态修改群组的从属拓扑关系（如分裂、合并、跨群转移）**。所有的拓扑变更必须作为外部控制输入，由高阶 AI 层（如战术决策树、分层任务网络）在并行帧之前完成静态调度。

### 5.2 深度层次结构引入的线性传递延迟 (Latency in Deep Hierarchies)

由于每个实体在更新时依赖其父级节点在上一模拟帧计算出的指令（双缓冲机制确保线程安全），层次结构的加深会引入固定的时间延迟（Time Delay）。

* **延迟模型**：对于深度为 $D$ 的实体树（叶子个体在第 $D$ 层，根节点虚拟群在第 $0$ 层），根节点的转向或速度变更传递至叶子节点需要经过严格的延迟采样：
  $$\Delta t_{\text{latency}} = D \times \Delta t_{\text{tick}}$$
* **工程评估**：在大多数游戏实践中，群组层次很少超过两层（最高仅为：连队 $\rightarrow$ 小队 $\rightarrow$ 士兵，$D=2$）。在 $30\text{ Hz}$ 逻辑帧率下，该延迟仅约为 $66\text{ ms}$，这与生物从神经反应到肌肉收缩的真实生理延迟高度吻合，不仅不会引起视觉瑕疵，反而能为大编队行进赋予自然有机的次级摆动与波动感（Wave Effect）。

---

## 6. 群组宏观寻路与可变形通道规划 (Group-Level Pathfinding)

将群组所有成员的寻路请求合并因子化为单一的高层图搜索（Factorized Path Planning），是优化全局性能的核心手段。然而，群组与单体智能体最大的寻路差异在于：**单体的物理轮廓（Bulk）是静态刚性的硬约束，而群组的物理轮廓是具有高度流变弹性（Reconfigurable Elasticity）的软约束**。

```
                                    群组通道选择代价权衡模型
  
                                  ┌────────────────────────┐
                                  │      起始点 Start      │
                                  └───────────┬────────────┘
                                              │
                     ┌────────────────────────┴────────────────────────┐
                     ▼                                                 ▼
      【分支 A：宽阔通路 (Wide Path)】                 【分支 B：狭窄瓶颈 (Narrow Bottleneck)】
       · 路径长度：$L_{\text{wide}}$ (长路径)          · 路径长度：$L_{\text{narrow}}$ (短路径)
       · 维持基准宽幅编队 (No Form-Change)            · 必须压缩变形为一字纵队 (Reconfiguration)
       · 通行惩罚：$C_{\text{morph}} = 0$              · 通行惩罚：$C_{\text{morph}} \gg 0$
                     │                                                 │
                     └────────────────────────┬────────────────────────┘
                                              ▼
                             Cost Evaluation: 选取综合代价最小值
```

### 6.1 通道选择成本函数推导 (Cost Function Formulation)

在基于 $A^*$ 的多边形网格搜索中，定义边 $e_{u \to v}$ 的遍历代价值为 $f(u, v)$。当评估虚拟群组实体通行时，算法根据通路多边形所能提供的最大内切圆半径（或走廊宽度 $W_{\text{portal}}$）动态引入非线性惩罚因子：

$$f(u, v) = \text{Distance}(u, v) \times \mu_{\text{surface}} + C_{\text{morph}}(W_{\text{portal}}, W_{\text{formation}})$$

其中变形容许代价函数 $C_{\text{morph}}$ 定义为：
$$C_{\text{morph}} = \begin{cases} 
0, & \text{if } W_{\text{portal}} \ge W_{\text{formation}} \quad (\text{无需形变}) \\ 
\alpha \cdot \left( \dfrac{W_{\text{formation}} - W_{\text{portal}}}{W_{\text{formation}}} \right)^\beta + K_{\text{penalty}}, & \text{if } W_{\min} \le W_{\portal} < W_{\text{formation}} \quad (\text{允许收缩通过}) \\ 
+\infty, & \text{if } W_{\text{portal}} < W_{\min} \quad (\text{超出极限，绝对阻断}) 
\end{cases}$$

* $W_{\text{formation}}$：群组在正常社交/战术行进状态下的标准横向几何跨度；
* $W_{\min}$：群组成员退化为单列纵队时的物理最小宽度极限（单体通行宽度阈值）；
* $\alpha, \beta, K_{\text{penalty}}$：工程调谐权重，确保算法能够在**绕道宽阔长路径**与**挤压队形穿过狭窄短路径**之间取得优雅的自然权衡（Trade-Off）。

---

## 7. 工业级 C++ 核心架构与代码实现

以下展示工业级群组导航系统的核心组件设计。代码严格遵从虚拟群组代理拓扑与只读双缓冲并行规范。

```cpp
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cassert>

// 基础三维数学结构
struct Vector3 {
    float x{0.0f}, y{0.0f}, z{0.0f};

    Vector3 operator+(const Vector3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    Vector3 operator-(const Vector3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    Vector3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    Vector3& operator+=(const Vector3& rhs) { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
    
    [[nodiscard]] float LengthSq() const { return x * x + y * y + z * z; }
    [[nodiscard]] float Length() const { return std::sqrt(LengthSq()); }
    
    [[nodiscard]] Vector3 Normalized() const {
        float len = Length();
        return (len > 1e-5f) ? Vector3{x / len, y / len, z / len} : Vector3{0.0f, 0.0f, 0.0f};
    }
};

// 实体核心快照 (双缓冲只读数据，保证多线程安全)
struct EntitySnapshot {
    Vector3 position;
    Vector3 velocity;
    Vector3 forward;
    float maxSpeed{0.0f};
    float boundingRadius{0.0f};
};

class GroupEntity;

// ============================================================================
// 导航实体基类 (Composite Base Class)
// ============================================================================
class NavigationEntity {
public:
    virtual ~NavigationEntity() = default;

    // 获取上一帧只读快照 (并发安全入口)
    [[nodiscard]] const EntitySnapshot& GetReadSnapshot() const { return m_readSnapshot; }

    // 提交双缓冲交换
    void FlipDoubleBuffer() {
        m_readSnapshot = m_writeSnapshot;
    }

    [[nodiscard]] GroupEntity* GetParentGroup() const { return m_parentGroup; }
    void SetParentGroup(GroupEntity* parent) { m_parentGroup = parent; }

    virtual void ParallelNavigationUpdate(float dt) = 0;
    virtual void CommitMovement(float dt) = 0;

protected:
    EntitySnapshot m_readSnapshot;   // 供并行线程读取的上一帧只读数据
    EntitySnapshot m_writeSnapshot;  // 本帧计算暂存数据
    GroupEntity*   m_parentGroup{nullptr};
};

// ============================================================================
// 虚拟群组代理实体 (Virtual Group Entity)
// ============================================================================
class GroupEntity : public NavigationEntity {
public:
    void AddMember(std::shared_ptr<NavigationEntity> member) {
        assert(member != nullptr);
        member->SetParentGroup(this);
        m_members.push_back(member);
    }

    void RemoveMember(const std::shared_ptr<NavigationEntity>& member) {
        auto it = std::remove(m_members.begin(), m_members.end(), member);
        if (it != m_members.end()) {
            (*it)->SetParentGroup(nullptr);
            m_members.erase(it, m_members.end());
        }
    }

    [[nodiscard]] const std::vector<std::shared_ptr<NavigationEntity>>& GetMembers() const {
        return m_members;
    }

    // 依据子集状态重新聚合质心、包围球及运动限制
    void RecomputeBulkAndKinematics() {
        if (m_members.empty()) {
            m_writeSnapshot.position = {0, 0, 0};
            m_writeSnapshot.boundingRadius = 0.0f;
            m_writeSnapshot.maxSpeed = 0.0f;
            return;
        }

        // 1. 计算加权质心 (Barycenter)
        Vector3 centerSum{0.0f, 0.0f, 0.0f};
        float minSpeed = std::numeric_limits<float>::max();

        for (const auto& member : m_members) {
            const auto& snap = member->GetReadSnapshot();
            centerSum += snap.position;
            minSpeed = std::min(minSpeed, snap.maxSpeed);
        }

        m_writeSnapshot.position = centerSum * (1.0f / static_cast<float>(m_members.size()));
        m_writeSnapshot.maxSpeed = minSpeed; // 确保编队不超越最慢个体

        // 2. 计算宏观包围球 (Bulk Radius)
        float maxDistSq = 0.0f;
        for (const auto& member : m_members) {
            const auto& snap = member->GetReadSnapshot();
            float distSq = (snap.position - m_writeSnapshot.position).LengthSq();
            maxDistSq = std::max(maxDistSq, distSq);
        }
        m_writeSnapshot.boundingRadius = std::sqrt(maxDistSq) + 0.5f; // 加上个体基础边界外延
    }

    void ParallelNavigationUpdate(float dt) override {
        // 群组宏观寻路与编队行进管线
        // 计算群组质心期望位移矢量 (High-Level Path Following)
        Vector3 steeringDirection = ComputeMacroPathSteering();
        
        m_writeSnapshot.velocity = steeringDirection * m_writeSnapshot.maxSpeed;
        if (m_writeSnapshot.velocity.LengthSq() > 1e-4f) {
            m_writeSnapshot.forward = m_writeSnapshot.velocity.Normalized();
        }
    }

    void CommitMovement(float dt) override {
        // 虚拟群组仅更新逻辑位置
        m_writeSnapshot.position += m_writeSnapshot.velocity * dt;
    }

private:
    Vector3 ComputeMacroPathSteering() {
        // 示例：宏观路径引导矢量（实际应用中对接全局路径点走廊）
        return m_readSnapshot.forward.LengthSq() > 0.1f ? m_readSnapshot.forward : Vector3{0.0f, 0.0f, 1.0f};
    }

    std::vector<std::shared_ptr<NavigationEntity>> m_members;
};

// ============================================================================
// 具象单体智能体实体 (Individual Physical Entity)
// ============================================================================
class IndividualEntity : public NavigationEntity {
public:
    IndividualEntity(Vector3 initialPos, float maxSpd, float radius) {
        m_readSnapshot.position = initialPos;
        m_readSnapshot.maxSpeed = maxSpd;
        m_readSnapshot.boundingRadius = radius;
        m_readSnapshot.forward = {0.0f, 0.0f, 1.0f};
        m_writeSnapshot = m_readSnapshot;
    }

    void SetFormationOffset(const Vector3& offset) { m_formationOffset = offset; }

    void ParallelNavigationUpdate(float dt) override {
        // 1. 基底编队导向行为 (Formation Steer)
        Vector3 desiredVelocity{0.0f, 0.0f, 0.0f};
        if (m_parentGroup) {
            const auto& groupSnap = m_parentGroup->GetReadSnapshot();
            // 在群组局部坐标系下映射槽位世界坐标
            Vector3 targetSlot = groupSnap.position + (groupSnap.forward * m_formationOffset.z) 
                                 + (Vector3{groupSnap.forward.z, 0.0f, -groupSnap.forward.x} * m_formationOffset.x);
            
            Vector3 toSlot = targetSlot - m_readSnapshot.position;
            desiredVelocity = toSlot.Normalized() * m_readSnapshot.maxSpeed;
        }

        // 2. 局部微观避障仲裁 (Local Collision Avoidance Filter)
        // 模拟管线链式校准：CollisionAvoidanceBehavior.Filter(desiredVelocity)
        Vector3 safeVelocity = ResolveLocalCollisionAvoidance(desiredVelocity);

        m_writeSnapshot.velocity = safeVelocity;
        if (m_writeSnapshot.velocity.LengthSq() > 1e-4f) {
            m_writeSnapshot.forward = m_writeSnapshot.velocity.Normalized();
        }
    }

    void CommitMovement(float dt) override {
        //

---

在现代游戏工业界与大规模人群仿真（Crowd Simulation）引擎中，如何在维持高拟真度个体智能（Individual Autonomy）的同时，实现高度可控、自然收敛的群体协调（Group Coordination），是群体 AI 架构设计的核心挑战。本架构方案解构自工业级标准（如 Golaem SDK 及开源 Recast/Detour 导航栈扩展），提出了一套将高层群体宏观决策与低层个体导向行为（Steering Behaviors）严格解耦的分层协同控制管线。

---

## 1. 涌现式群体结构（Emergent Group Structure）

涌现式群体通过严格去中心化（Decentralized）的局部交互法则运行。个体仅依赖局部感知（Local Perception）做出导航决策，而无需显式中央编排器（Central Choreographer）。

### 1.1 Boids 衍生模型与社交力扩展

经典 Reynolds 蜂拥模型依赖三项核心基础导向力：
1. **分离力（Separation）**：避免与近邻个体物理碰撞。
2. **队列对齐力（Alignment）**：与局部群体平均行进速度与朝向同步。
3. **凝聚力（Cohesion）**：向局部群体质心（Anchor/Centroid）靠拢。

其加速度合力模型表述为：

$$\vec{F}_{\text{flock}} = w_s \vec{F}_{\text{sep}} + w_a \vec{F}_{\text{align}} + w_c \vec{F}_{\text{coh}}$$

工业界在群体仿真中对该模型引入了感知与社交调制扩展：
* **视场通信力（Field of View Constraints）**：为了模拟小型社交群体（Social Groups，如结伴同行的行人），模型引入视场保持力 $\vec{F}_{\text{fov}}$，使个体在行进过程中始终将同伴维持在注视视角范围内，以便进行社交交谈 $[$Moussaïd 10$]$。
* **社交引力调制（Social Attractivity Modulation）**：依据个体间的人际关系矩阵 $\mathbf{A} \in \mathbb{R}^{n \times n}$ 动态调整吸引与排斥因子的权重，使得关系紧密者间隙缩小，陌生人或低好感度成员间距扩大 $[$Qiu 10$]$。

### 1.2 晶体附着点局部编队（“Local” Formations via Attachment Sites）

受分子晶体结构（Molecular Crystals）启发，去中心化编队系统通过“附着点方法（Attachment Site Method）”实现。
* 每个个体在其局部坐标系下向外定义多个预置附着插槽（Attachment Sites），描述近邻允许存在的相对偏移量。
* 个体在移动过程中，在感知邻域内搜索距离最近且处于空闲（Available）状态的附着点，并计算导向力向其靠拢。
* **架构特性**：系统可以线性扩展至任意成员规模，无需全局几何计算。但由于其纯局部法则，系统无法显式收敛到全局硬性指定的几何外轮廓（Overall Shape），主要适用于社交聚落等松散形态 $[$Balch 00$]$。

### 1.3 分层架构下的虚拟领队实现（Implementing an Emergent Group）

传统蜂拥算法若需沿指定路径行进，通常引入一个显式的物理实体作为“领队（Leader）”，领队沿路径引导，其余个体追随该物理领队。这种模式极易导致物理死锁和队形拉扯。

本分层架构引入了**虚拟群体实体（Virtual Group Entity）**作为高层决策载体，其系统拓扑如下：

```
+-------------------------------------------------------------------+
|                        Group Entity                               |
|  +---------------------------+     +---------------------------+  |
|  |       Path Following      |     |    Collision Avoidance    |  |
|  +---------------------------+     +---------------------------+  |
+-------------------------------------------------------------------+
                  |                                   |
                  | Group Position                    | Group Velocity
                  v                                   v
+-------------------------------------------------------------------+
|                      Individual Entity                            |
|  +---------------------------+     +---------------------------+  |
|  |          Flocking         |     |          Wounded          |  |
|  +---------------------------+     +---------------------------+  |
+-------------------------------------------------------------------+
```

#### 双向更新调度时序（Update Loop Tick）

在每个模拟帧（Tick）内，更新流展开如下（两阶段执行解耦，无严格先后死锁）：
1. **群体实体高阶规划**：
   $$\vec{P}_{\text{group}}(t) = \frac{1}{N} \sum_{i=1}^N \vec{P}_i(t), \quad \vec{V}_{\text{group\_current}}(t) = \frac{1}{N} \sum_{i=1}^N \vec{V}_i(t)$$
   根据全局导航网格路径（NavMesh Path）计算群体期望新速度 $\vec{V}_{\text{group\_target}}(t)$。
2. **个体实体动力学更新**：
   群体成员不再直接寻找物理领队，而是将虚拟群体实体的 $\vec{P}_{\text{group}}$ 作为凝聚力靶点，$\vec{V}_{\text{group}}$ 作为对齐力输入，同时计算个体间的局部排斥力：
   $$\vec{V}_i(t + \Delta t) = \vec{V}_i(t) + \left( w_c (\vec{P}_{\text{group}} - \vec{P}_i) + w_a \vec{V}_{\text{group}} + w_s \sum_{j \neq i} \text{Repulse}(\vec{P}_i, \vec{P}_j) \right) \Delta t$$

---

## 2. 编排式编队控制（Choreographed Formations）

当业务场景对队形几何精度存在硬性约束（如军阵行进、仪仗队列）时，必须由中央编排器执行自顶向下的控制。系统由三阶段构成：编队设计、插槽分配、编队跟随。

```
+-------------------+      +-------------------+      +---------------------+
| 1. Formation      | ---> | 2. Slot           | ---> | 3. Formation        |
|    Design         |      |    Assignment     |      |    Following        |
| (Slots & Roles)   |      | (Spatial Sorting) |      | (Extrapolation/RVO) |
+-------------------+      +-------------------+      +---------------------+
```

### 2.1 编队设计（Formation Design）

编队定义为群体成员在 2D 空间中的相对几何拓扑：
* 每个插槽（Slot）包含相对于群体局部坐标系原点（Group Local Origin）的 2D 偏移位置 $\vec{S}_k \in \mathbb{R}^2$。
* 附加属性：局部朝向（Orientation）及角色约束（Role，例如重装步兵、弓箭手、平民）。
* 插槽数量 $M$ 与实体数量 $N$ 的平衡机制：当 $M \neq N$ 时，采用动态裁剪或基于拓扑模板动态补全插槽 $[$Silveira 08$]$。

### 2.2 插槽分配算法（Slots Assignment）

插槽分配旨在建立实体集合与插槽集合之间的双射映射 $f: \mathcal{E} \to \mathcal{S}$。

```
[贪心最近分配 (Greedy Nearest)]
实体 A ---> 插槽 2  (轨迹交叉!)
实体 B ---> 插槽 1  (导致个体绕行、产生严重拥堵)

[空间同序排序投影 (Spatial Sorting & Matching - Mars 14)]
实体空间轴投影: [E_1, E_2, E_3]  (按投影位置升序)
插槽空间轴投影: [S_1, S_2, S_3]  (按投影位置升序)
映射关系: E_i -> S_i (拓扑单调匹配，完全规避路径交叉)
```

#### 算法权衡与选型
* **贪心最近原则（Greedy Nearest）**：极易产生路径互相交叉（Path Crossing），导致内部拥塞与绕圈死锁。
* **全局最优排列（Brute-Force Permutations）**：计算组合爆炸，时间复杂度为 $\mathcal{O}(N!)$。在实时工业引擎中无法落地（即便使用 Kuhn-Munkres 匈牙利算法，复杂度亦达到 $\mathcal{O}(N^3)$）。
* **空间同序排序投影算法（Spatial Sorting Method $[$Mars 14$]$）**：
  在不指定强类型角色约束的场景下，将所有插槽按主空间轴（例如群体的横向侧向量 Lateral Axis）进行几何排序，同时将所有实体沿相同投影轴进行排序。
  $$f(E_{(i)}) = S_{(i)}, \quad \forall i \in [1, N]$$
  算法复杂度仅取决于排序步骤 $\mathcal{O}(N \log N)$，有效杜绝了个体行进线交叉现象。

---

### 2.3 编队跟随模式（Formation Following）

#### 模式 A：“盲目跟随”与时间前瞻外推（“Blind” Formation Following）
个体完全剥离自主导向逻辑，严格吸附于绝对插槽变换矩阵下：

$$\vec{P}_{i}^{\text{world}} = \mathbf{T}_{\text{group}} \cdot \vec{S}_i$$

为防止动力学突变并允许外部运动系统（Locomotion/IK）平滑过渡，需引入**插槽位置前瞻外推（Slot Position Extrapolation）** $[$Karamouzas 10, Schuerman 10$]$：

$$\vec{P}_{i,\text{target}}(t) = \mathbf{T}_{\text{group}}(t) \cdot \vec{S}_i + \vec{V}_{\text{group}}(t) \cdot \tau$$

* **避免运动冲击（Motion Jolts）**：外推时间视界 $\tau$ 必须严格大于单帧物理时长（$\tau > \Delta t_{\text{frame}}$），使目标点始终处于单步更新不可达状态，从而消除频繁减速刹车震颤。当群体停止时，$\vec{V}_{\text{group}} = \vec{0}$，目标点平滑归位。
* **内聚松紧度控制（Cohesion Tuning）**：
  * 调小 $\tau$：实现刚性紧致队形（Tight Formation）。
  * 调大 $\tau$：实现松散柔性队形（Loose Formation）。

#### 模式 B：自主编队跟随（Autonomous Formation Following）

在真实行人及狭小走廊中，成员不可盲目硬套几何插槽，需允许其临时脱离编队以绕过障碍物。其混合架构拓扑如下图所示：

```
+-----------------------------------------------------------------------------------+
|                                   Group Entity                                    |
|  +----------------------+  +----------------------+  +-------------------------+  |
|  |    Path Following    |  |   Slots Assignment   |  |   Collision Avoidance   |  |
|  +----------------------+  +----------------------+  +-------------------------+  |
+-----------------------------------------------------------------------------------+
                                         |
                                         | Target Slot Transformations
                                         v
+-----------------------------------------------------------------------------------+
|                                 Individual Entity                                 |
|  +----------------------+  +----------------------+  +-------------------------+  |
|  |  Formation Following |  |   Personality Noise  |  |   Collision Avoidance   |  |
|  +----------------------+  +----------------------+  +-------------------------+  |
+-----------------------------------------------------------------------------------+
```

##### 动力学混合公式
个体最终指令速度综合了跟随槽位向量、次级子目标引导、个性化速度噪声扰动以及动态避障力：

$$\vec{V}_i^{\text{des}} = \vec{V}_{\text{formation}}(\vec{P}_i, \vec{P}_{i,\text{target}}) + \vec{V}_{\text{subgoal}} + \vec{\eta}_i(t)$$

* $\vec{V}_{\text{subgoal}}$：个体次级目标（如路边商店橱窗吸引力）。
* $\vec{\eta}_i(t)$：基于伪随机噪声（如 Perlin Noise）的速度偏移，赋予个体异质化特征。

##### 协同避障退化抑制
互惠速度障碍物（Reciprocal Velocity Obstacles, RVO）$[van den Berg 08]$ 的标准实现会在个体间强制设定全局安全缓冲半径 $r_{\text{safe}}$，该半径通常大于编队插槽内部间距，从而造成系统性冲突，导致编队在无外界障碍时被自身避障系统撕裂。
* **分级邻域过滤机制**：将近邻划分为同编队成员集合 $\mathcal{N}_{\text{group}}$ 与外界干扰集合 $\mathcal{N}_{\text{world}}$。
* **紧急碰撞剪裁**：对内部成员只检测碰撞时间（Time to Collision, TTC）极度紧迫的接触（$TTC < t_{\text{imminent}}$），否则压制个体间 RVO 排斥。

---

## 3. 群体级避障与形貌自适应（Group Collision Avoidance）

传统单体避障将物理实体视为刚体，而**群体的空间占用体积（Bulk）是非刚性、可重塑的（Not a Hard Constraint）**。

```
[群体行进遭遇对向行人]
刚性包围体投影:    [   Group Area   ]   vs   (Pedestrian)  -->  强制大幅转向绕行
形貌自适应重构:    [Group Lane Formation]    (Pedestrian)  -->  横向压缩、纵向伸展，平滑对穿
```

### 3.1 速度修正（Velocity Correction）
* **盘状包围体局限**：将群体投影为大直径圆盘时，会严重过度高估群体的实际物理足迹（Footprint Overestimation）$[Schuerman 10]$。
* **有向包围盒模型（Oriented Bounding Box, OBB）**：根据群体实体的瞬时行进方向与成员分布主轴构建 OBB，将 RVO 采样几何推导至多边形速度障碍空间 $[Karamouzas 04, Peters 09]$。

### 3.2 编队形貌自适应算法（Formation Adaptation Algorithm）

形貌自适应算法（$[Karamouzas 10]$）利用速度与形貌的联合状态采样，实现群体在遭遇对向人流或狭窄瓶颈时的自动重构：

```
+---------------------------------------------------------------------------+
|                          1. 预设基准编队库                                 |
|            { F_1 (并排走廊), F_2 (单列纵队), F_3 (V形阵列), ... }          |
+---------------------------------------------------------------------------+
                                      |
                                      v
+---------------------------------------------------------------------------+
|                        2. 空间几何插值 (动态插值)                         |
|    从当前编队 F_current 插值生成 K 个候选编队 C_k (例如: 5个基准 x 3次插值)   |
+---------------------------------------------------------------------------+
                                      |
                                      v
+---------------------------------------------------------------------------+
|                        3. 多边形 RVO 碰撞与运动学评估                      |
|          对每个候选形貌 C_k 计算最优可行速度 V*_k 以及碰撞时间 TTC_k       |
+---------------------------------------------------------------------------+
                                      |
                                      v
+---------------------------------------------------------------------------+
|                        4. 多目标代价函数 (Cost Evaluation)                |
|       J(C_k) = α ||V*_k - V_des|| + β (1 / TTC_k) + γ Dist(C_k, F_pref)   |
+---------------------------------------------------------------------------+
                                      |
                                      v
+---------------------------------------------------------------------------+
|                        5. 选取代价最低形态作为当前控制基准                |
|                              C* = argmin J(C_k)                           |
+---------------------------------------------------------------------------+
```

#### 代价函数数学定义
针对每一个候选状态 $C_k$，计算加权效用代价值：

$$\mathcal{J}(C_k) = w_v \|\vec{V}_k^* - \vec{V}_{\text{desired}}\| + w_t \cdot g(\text{TTC}_k) + w_f \cdot \mathcal{D}(C_k, F_{\text{pref}})$$

其中：
* $\vec{V}_k^*$：候选形貌 $C_k$ 下，经多边形 RVO 过滤后允许的最大行进速度。
* $\text{TTC}_k$：在该候选形貌下的时间碰壁界限。若无碰撞则 $g(\text{TTC}_k) \to 0$；若碰撞紧迫则呈反比爆发激增：
  $$g(\text{TTC}) = \frac{1}{\text{TTC} + \epsilon}$$
* $\mathcal{D}(C_k, F_{\text{pref}})$：候选形貌与高层期望编队（如散步社交默认形貌）的弗罗贝尼乌斯范数或几何形变距离。
* 系统通常保留 5 种基准形貌，每种插值 3 个中间过渡态，单帧只需评估 $5 \times 3 = 15$ 个候选集，兼顾实时性与空间灵活性。

---

## 4. 工业级 C++ 核心架构实现规范

以下代码基于现代 C++ 规范，对虚拟群体协调器（Choreographer）、空间投影插槽分配器及外推导航更新机制进行完整封装。

```cpp
#include <vector>
#include <array>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>
#include <memory>

// 基础空间数学定义
struct Vector2D {
    float x{0.0f};
    float y{0.0f};

    Vector2D operator+(const Vector2D& rhs) const { return {x + rhs.x, y + rhs.y}; }
    Vector2D operator-(const Vector2D& rhs) const { return {x - rhs.x, y - rhs.y}; }
    Vector2D operator*(float scalar) const { return {x * scalar, y * scalar}; }
    Vector2D& operator+=(const Vector2D& rhs) { x += rhs.x; y += rhs.y; return *this; }
    
    [[nodiscard]] float Dot(const Vector2D& rhs) const { return x * rhs.x + y * rhs.y; }
    [[nodiscard]] float MagnitudeSq() const { return x * x + y * y; }
    [[nodiscard]] float Magnitude() const { return std::sqrt(MagnitudeSq()); }
    
    [[nodiscard]] Vector2D Normalized() const {
        float mag = Magnitude();
        return (mag > 1e-5f) ? Vector2D{x / mag, y / mag} : Vector2D{0.0f, 0.0f};
    }
};

// 编队插槽定义
struct FormationSlot {
    int id{-1};
    Vector2D localOffset; // 相对于群体局部坐标系原点的偏移
    float orientationOffset{0.0f};
    int requiredRole{0};
};

// 导航实体抽象
struct NavigationEntity {
    int id{-1};
    Vector2D position;
    Vector2D velocity;
    int role{0};
    int assignedSlotId{-1};
};

// 虚拟群体编排器 (Virtual Group Choreographer)
class VirtualGroupEntity {
public:
    explicit VirtualGroupEntity(int id) : groupId_(id) {}

    void SetPreferredPath(const std::vector<Vector2D>& waypoints) {
        pathWaypoints_ = waypoints;
        currentWaypointIdx_ = 0;
    }

    void SetFormationSlots(const std::vector<FormationSlot>& slots) {
        slots_ = slots;
    }

    void RegisterMember(const std::shared_ptr<NavigationEntity>& entity) {
        members_.push_back(entity);
    }

    // 核心 Tick 更新：执行群体宏观计算与分层更新调度
    void Update(float deltaTime, float extrapolationHorizon) {
        if (members_.empty()) return;

        // 1. 群体高阶质心与平均速度聚合
        Vector2D centroid{0.0f, 0.0f};
        Vector2D meanVelocity{0.0f, 0.0f};
        for (const auto& member : members_) {
            centroid += member->position;
            meanVelocity += member->velocity;
        }
        groupPosition_ = centroid * (1.0f / static_cast<float>(members_.size()));
        groupVelocity_ = meanVelocity * (1.0f / static_cast<float>(members_.size()));

        // 2. 宏观寻路与编排速度推导
        ComputeGroupDesiredVelocity();

        // 3. 执行空间单调排序插槽分配 (Mars 14 Method)
        AssignSlotsSpatialSorting();

        // 4. 下发外推目标点并驱动成员运动学更新
        DispatchMemberUpdates(deltaTime, extrapolationHorizon);
    }

    [[nodiscard]] Vector2D GetGroupPosition() const { return groupPosition_; }
    [[nodiscard]] Vector2D GetGroupVelocity() const { return groupVelocity_; }

private:
    void ComputeGroupDesiredVelocity() {
        if (currentWaypointIdx_ >= pathWaypoints_.size()) {
            groupDesiredVelocity_ = {0.0f, 0.0f};
            return;
        }

        Vector2D targetPoint = pathWaypoints_[currentWaypointIdx_];
        Vector2D toTarget = targetPoint - groupPosition_;
        float distSq = toTarget.MagnitudeSq();

        constexpr float kWaypointArrivalRadiusSq = 4.0f; // 到达阈值判定
        if (distSq < kWaypointArrivalRadiusSq) {
            currentWaypointIdx_++;
            if (currentWaypointIdx_ >= pathWaypoints_.size()) {
                groupDesiredVelocity_ = {0.0f, 0.0f};
                return;
            }
            toTarget = pathWaypoints_[currentWaypointIdx_] - groupPosition_;
        }

        constexpr float kDefaultGroupCruiseSpeed = 1.34f; // 典型行人巡航速度 1.34m/s
        groupDesiredVelocity_ = toTarget.Normalized() * kDefaultGroupCruiseSpeed;
    }

    // 基于空间投影排序的非碰撞插槽分配
    void AssignSlotsSpatialSorting() {
        size_t n = std::min(members_.size(), slots_.size());
        if (n == 0) return;

        // 确定群体前进侧向基底向量 (Lateral Vector)
        Vector2D forward = (groupDesiredVelocity_.MagnitudeSq() > 1e-4f) 
                           ? groupDesiredVelocity_.Normalized() 
                           : Vector2D{1.0f, 0.0f};
        Vector2D lateral{-forward.y, forward.x};

        // 创建带投影标量与原始索引的临时容器
        struct ProjectionEntry {
            size_t index;
            float projectionValue;
        };

        std::vector<ProjectionEntry> memberProjections(members_.size());
        for (size_t i = 0; i < members_.size(); ++i) {
            memberProjections[i] = { i, (members_[i]->position - groupPosition_).Dot(lateral) };
        }

        std::vector<ProjectionEntry> slotProjections(slots_.size());
        for (size_t j = 0; j < slots_.size(); ++j) {
            slotProjections[j] = { j, slots_[j].localOffset.Dot(lateral) };
        }

        // 沿侧向轴执行单调排序
        std::sort(memberProjections.begin(), memberProjections.end(), 
            [](const ProjectionEntry& a, const ProjectionEntry& b) {
                return a.projectionValue < b.projectionValue;
            });

        std::sort(slotProjections.begin(), slotProjections.end(), 
            [](const ProjectionEntry& a, const ProjectionEntry& b) {
                return a.projectionValue < b.projectionValue;
            });

        // 建立拓扑单调映射：第 i 个投影实体分配至第 i 个投影插槽
        for (size_t k = 0; k < n; ++k) {
            size_t memIdx = memberProjections[k].index;
            size_t slotIdx = slotProjections[k].index;
            members_[memIdx]->assignedSlotId = slots_[slotIdx].id;
        }
    }

    void DispatchMemberUpdates(float deltaTime, float tau) {
        for (auto& member : members_) {
            if (member->assignedSlotId < 0) continue;

            // 定位对应的插槽
            auto it = std::find_if(slots_.begin(), slots_.end(), 
                [id = member->assignedSlotId](const FormationSlot& s) { return s.id == id; });
            if (it == slots_.end()) continue;

            // 计算世界坐标系下的插槽基准位置
            Vector2D slotWorldTarget = groupPosition_ + it->localOffset;

            // 引入时间前瞻外推，消除抖动：P_target_extrapolated = P_slot + V_group * tau
            Vector2D extrapolatedTarget = slotWorldTarget + groupDesiredVelocity_ * tau;

            // 个体转向动力学输出 (P-Controller 模拟速度跟随阶段)
            Vector2D steerError = extrapolatedTarget - member->position;
            constexpr float kSteerGain = 1.2f;
            Vector2D candidateVelocity = steerError * (kSteerGain / std::max(tau, deltaTime));

            // 限速阶段
            constexpr float kMaxIndividualSpeed = 2.0f;
            if (candidateVelocity.MagnitudeSq() > (kMaxIndividualSpeed * kMaxIndividualSpeed)) {
                candidateVelocity = candidateVelocity.Normalized() * kMaxIndividualSpeed;
            }

            // 积分更新
            member->velocity = candidateVelocity;
            member->position += member->velocity * deltaTime;
        }
    }

private:
    int groupId_{-1};
    Vector2D groupPosition_{0.0f, 0.0f};
    Vector2D groupVelocity_{0.0f, 0.0f};
    Vector2D groupDesiredVelocity_{0.0f, 0.0f};

    std::vector<Vector2D> pathWaypoints_;
    size_t currentWaypointIdx_{0};

    std::vector<FormationSlot> slots_;
    std::vector<std::shared_ptr<NavigationEntity>> members_;
};

---

## 5. 编排器设计模式与工业应用延展（Choreographer Pattern Extensions）

本分层群体架构的核心思想在于**将群体协作决策外化（Externalizing Collaborative Decision Making）至虚拟实体**。该模式可推广至以下关键工业应用场景：

| 业务场景 | 虚拟编排器（Choreographer）职责 | 下层个体代理（Individual Agent）反应 |
| :--- | :--- | :--- |
| **窄道与狭门通行管理 (Traffic Bottlenecks)** | 建立几何门禁令牌桶（Token Bucket），动态为到达个体分发穿过优先级与时隙序列。 | 维持局部避碰，未获 Token 实体执行排队阻尼；获 Token 实体恢复正常速度通关。 |
| **群组社交交互 (Group Discussions)** | 动态根据交谈人数计算 $N$ 边形会话闭环，投影面向中心点（Face-To-Center）朝向。 | 触发闲置姿态层（Idle Layers），向槽位平滑就位，头部追踪注视说话者。 |
| **多兵种战术同步 (Tactical Synchronization)** | 计算冲锋对齐基准线，以最慢单位标定编队巡航速度；动态广播射击视线包络。 | 执行局部射击姿态抑制，防止友军误伤（Friendly Fire），同步扣动扳机。 |
| **战斗节奏控制 (Combat Pacing)** | 聚合战场敌我距离与压力值，执行进攻槽位挂载限制（Slot Tokens），防止围攻失衡。 | 根据分配到的 Attack Slot 执行攻击树，未获得槽位者进入外围迂回或格挡姿态。 |

### 架构演进方向
该分层导航体系不仅成功应用于影视级群集与工业人群模拟中间件（如 **Golaem SDK**），而且是 **Recast/Detour** 等开源网格底层向高层多代理架构升级的核心拓扑演进范式。通过分离宏观拓扑约束与微观个体自主权，系统在确保编队宏观收敛性的同时，赋予个体微观避碰、子目标探索与个性化动作表现的自由度。
