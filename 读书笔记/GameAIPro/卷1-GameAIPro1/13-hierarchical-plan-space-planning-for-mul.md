---
type: Reference
title: "第13章 Hierarchical Plan-Space Planning for Multi-Unit Combat Maneuvers"
description: "Game AI Pro 工业级精读：Hierarchical Plan-Space Planning for Multi-Unit Combat Maneuvers。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第13章 Hierarchical Plan-Space Planning for Multi-Unit Combat Maneuvers

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 13.  
> 原文作者 / 资源：[Hierarchical Plan-Space Planning for Multi-Unit Combat Maneuvers](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter13_Hierarchical_Plan-Space_Planning_for_Multi-unit_Combat_Maneuvers.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

*(Hierarchical Plan-Space Planning for Multi-unit Combat Maneuvers)*

---

### 目录 (Contents)
- 13.1 导言 (Introduction)
- 13.2 多单位协同规划的挑战 (Planning for Multiple Units)
- 13.3 计划空间分层规划的核心要素 (Hierarchical Planning in Plan-Space: The Ingredients)
- 13.4 规划器主循环：计划空间的 $A^*$ 搜索 (Planner Main Loop: An $A^*$ Search through Plan-Space)
- 13.5 任务网络架构 (A Plan of Tasks)
- 13.6 规划器分解方法 (Planner Methods)
- 13.7 计划空间数学模型与代价评估 (Plan-Space) *(待续)*
- 13.8 搜索效率优化工程 (Making Planning More Efficient) *(待续)*
- 13.9 总结 (Conclusion) *(待续)*
- 13.10 未来展望 (Future Work) *(待续)*

---

## 13.1 导言 (Introduction)

在现代战斗模拟系统与兵棋推演游戏（Combat Simulators and War Games）中，构建高质量的作战计划是决定胜负的核心。一个战术合理、协同周密的方案，不仅能够让 AI 表现为令人信服且具备高度智商的战场对手，还能充当玩家可信赖的副指挥官（Assistant Commander）。

在复杂的战场环境与动态博弈中，多单位（Multi-unit）协同达成联合战役目标（Joint Objective）的难度呈指数级增长。本章深入剖析了一种**分层计划空间规划器（Hierarchical Plan-Space Planner）**的架构与工程实现：
1. 分析传统单单位规划器在面对多单位并发协同场景时的根本缺陷与组合爆炸难题；
2. 建立分层计划空间搜索的核心理论与拓扑模型；
3. 将该理论具象化落地于多兵种合成部队的战斗机动（Combat Maneuvers）规划之中。

---

## 13.2 多单位协同规划的挑战 (Planning for Multiple Units)

### 13.2.1 单单位与多单位规划的本质区别

为多兵种集群规划与为单单位规划存在三大本质差异：

1. **高并发性（High Concurrency）**：规划不仅需要覆盖全部参战单位，且各单位通常处于同一时间线上并行执行动作（Concurrent Actions）。
2. **复杂的时空协同交互（Spatio-temporal Interaction）**：单位间需高度协同才能完成全局目标。规划必须精确决策：**谁（Who）**在**何时（When）**、**何地（Where）**与**谁（Whom）**进行交互。
3. **战术意图的可解释性与传达（Communication of the Plan）**：单单位的原子动作无需向指挥官过度解释。然而多单位作战方案必须能够生成战术简报（Briefing），清晰表达兵力分组、支援关系、各组战术角色及总体作战构想（Overall Concept）。

```
                                  [ 战役目标: Objective Z ]
                                             ▲
                                             │
                   ┌─────────────────────────┴─────────────────────────┐
                   │                                                   │
          [ 北桥突击支队: A, C 机械化排 ]                     [ 南桥突击支队: D, E 步兵排 ]
                   ▲                                                   ▲
                   │                                                   │
      [ 战术机动: B 运输排机动输送 ]                                   │
                   │                                                   │
                   └─────────────────────────┬─────────────────────────┘
                                             │
                       [ 支援火力梯队: H, J 炮兵连 / W 武装直升机 ]
                                (实施烟幕遮蔽与待命近距空中支援)
```

> **战场实战简报示例（Briefing to Player）**：
> “我军计划合围并肃清目标点 Z。A、B、C、D、E 排完成战前集结并实施双路协同夹击，战后于目标点 Z 重新集结。B 排负责将 A、C 排摩托化机动输送到集结地域；随后 A、C 排沿北侧桥梁突击，D、E 排沿南侧桥梁突击。H、J 炮兵连实施炮火掩护，沿两座桥梁释放烟幕屏障遮蔽敌军视野；W 武装直升机编队空中待命，随时提供近距离火力支援。”

---

### 13.2.2 状态空间搜索的组合爆炸陷阱 (State-Space Search Collapse)

工业界主流的单单位规划技术（如目标导向型动作规划 **GOAP [Orkin 06]**）及经典分层任务网络（**HTN [Ghallab et al. 04, Humphreys 13]**）主要在**状态空间（State-Space）**中自前向后或自后向前遍历世界状态节点（World States）。

当直接使用状态空间规划器处理多单位并发系统时，搜索空间将发生灾难性的指数爆炸：

设单个单位在规划每一步拥有 $b$ 个可选动作（分支因子 Branching Factor），规划深度（步数）为 $d$：
- **单单位状态空间**：
  $$S_{\text{single}} = b^d$$
  若 $b = 4$，$d = 5$，则：
  $$S_{\text{single}} = 4^5 = 1{,}024 \text{ 个状态}$$
  此规模可在数毫秒内穷举或启发式遍历。

- **$N$ 个单位并行执行状态空间**：
  若存在 $N$ 个单位同时行动，每个时间步所有单位动作的联合组合数激增为 $b^N$。则 $d$ 步后的联合搜索状态空间为：
  $$S_{\text{multi}} = (b^N)^d = b^{N \cdot d}$$
  当编队包含 $N = 6$ 个单位时：
  $$S_{\text{multi}} = (4^6)^5 = 4^{30} \approx 1.15 \times 10^{18} \text{ 种状态组合}$$

面对超十亿亿级（$10^{18}$）的状态组合，即便是搭载严苛裁剪机制的 GOAP 和标准 HTN 规划器也无法在游戏帧预算内收敛。

---

## 13.3 计划空间分层规划的核心要素 (Hierarchical Planning in Plan-Space: The Ingredients)

为了彻底突破状态空间搜索维度灾难的限制，必须将搜索空间转移至**计划空间（Plan-Space）**。

### 13.3.1 状态空间搜索与计划空间搜索的拓扑对比

- **状态空间搜索（State-Space Search）**：
  搜索图的节点为**世界状态（World State）**，边为导致状态跃迁的**原子动作（Action）**。每次扩展均沿着时间线性步进，强迫规划器在完全未确定宏观战略前，过早陷入所有并发单位的微观底层时序排列。
- **计划空间搜索（Plan-Space Search）**：
  搜索图的节点为**计划本体（Plan）**（包括完全计划与未完成的部分计划 Partial Plan），边为**规划分解操作（Plan Refinements）**。规划器像人类战役指挥官一样，先确定抽象目标任务，再自顶向下逐步细化分解为子任务，直到完全落地为单兵或单小队的执行原子。

```
【状态空间搜索 (Searching in State-Space)】
（按时间步长向前推进联合状态，维度极高）

  [初始状态] ──Action A/J/R──> [状态 1] ──Action B/K/S──> [状态 2] ──...> [目标状态]
  Unit A:    Action A                 Action B                 ...
  Unit B:    Action J                 Action K                 ...
  Unit C:    Action R                 Action S                 ...

─────────────────────────────────────────────────────────────────────────────

【计划空间搜索 (Searching in Plan-Space)】
（自顶向下任务抽象展开，延迟绑定具体动作）

                             ┌─────────────────┐
                             │  目标任务 (Goal) │
                             └────────┬────────┘
                                      │ 分解
               ┌──────────────────────┼──────────────────────┐
               ▼                      ▼                      ▼
        ┌─────────────┐        ┌─────────────┐        ┌─────────────┐
        │ Sub Task 1  ├───────>│ Sub Task 2  ├───────>│ Sub Task 3  │
        └──────┬──────┘        └──────┬──────┘        └──────┬──────┘
               │                      │                      │
        ┌──────┴──────┐               ▼                      ▼
        ▼             ▼             [ ? ]             ┌─────────────┐
  ┌───────────┐ ┌───────────┐                         │  Action D   │ (Unit A)
  │ Task 1.1  │ │ Task 1.2  │                         ├─────────────┤
  └─────┬─────┘ └─────┬─────┘                         │  Action Q   │ (Unit B)
        │             │                               ├─────────────┤
        ▼             ▼                               │  Action V   │ (Unit C)
  ┌───────────┐ ┌───────────┐                         └─────────────┘
  │ Action A  │ │ Action B  │ (Unit A)
  ├───────────┤ ├───────────┤
  │ Action J  │ │   [ ? ]   │ (Unit B)
  ├───────────┤ ├───────────┤
  │ Action R  │ │   [ ? ]   │ (Unit C)
  └───────────┘ └───────────┘
```

### 13.3.2 计划空间三大战略优势

1. **高阶决策优先（High-Level Reasoning First）**：
   规划器首先评估“由谁阻击、由谁包抄、何处实施火力准备”等任务级交互，无需在初期穷举每个单位在每一米的转向与开火动作。
2. **非线性细化自由度（Least-Commitment / Arbitrary Order Refinement）**：
   摆脱了传统规划器沿时间轴严格单向推演的枷锁，能够优先细化战役中最为核心、受限最严的关键任务（如主攻通道上的渡桥突击），次要任务（如侧翼隐蔽）可延后绑定。
3. **显式协同与同步拓扑（Explicit Coordination & Synchronization）**：
   协同直接建模为由多个单位联合认领的任务；时序同步直接表达为任务节点间的依赖弧（Precedence Edges，即后续任务必须等待前置任务的所有子动作全部结束方可执行）。

### 13.3.3 系统的四大核心构成构件

1. **规划器主循环（Planner Main Loop）**：驱动整个计划空间寻优的 $A^*$ 状态机；
2. **任务网络与动作定义（Tasks and Actions）**：表达层级化目标图与原子执行行为的数据结构；
3. **分解方法库（Planner Methods）**：封装领域知识、具备将抽象复合任务转化为具体子拓扑能力的算子；
4. **计划空间拓扑与代价评估（Plan-Space Management & Heuristics）**：管理并评价候选部分计划有效性与代价值的搜索空间。

---

## 13.4 规划器主循环：计划空间的 $A^*$ 搜索 (Planner Main Loop: An $A^*$ Search through Plan-Space)

规划器主循环将计划生成问题形式化为在**以未完成计划为节点构成的图空间**中的 $A^*$ 启发式搜索。

- **初始节点**：仅包含单一顶层抽象任务（如战役级任务 `Mission`）的根计划。
- **目标节点**：所有叶子节点均已分解为原子动作、所有输入输出已完全绑定（Grounded）且无时序冲突的“完全计划”（Complete Plan）。

```
                                ┌───────────────┐
                                │   Open List   │
                                └───────┬───────┘
                                        │
                         获取当前代价最低的候选计划 Current
                                        │
                                        ▼
                            /───────────────────────\
                           <  Current 是否为完全计划?  > ─── 是 ───> [ 规划成功，返回最终执行图 ]
                            \───────────────────────/
                                        │ 否
                                        ▼
                            /───────────────────────\
                           <   Current 是否为空指针?   > ─── 是 ───> [ 规划失败，无解退出 ]
                            \───────────────────────/
                                        │ 否
                                        ▼
                             加入 Closed List 闭表
                                        │
                                        ▼
                          选取当前待细化任务 t (Pick Task)
                                        │
                                        ▼
                        查询匹配方法库 Methods M that apply to t
                                        │
                                        ▼
                      对每个方法 m，生成可选分支 alternatives
                                        │
                        ┌───────────────┴───────────────┐
                        │   遍历每一个解法分支 a         │
                        │   1. 深拷贝克隆新计划: Plan'   │
                        │   2. 调用 m.plan(Plan', t, a) │
                        │   3. 计算 Plan' 代价 (g + h)  │
                        │   4. 将 Plan' 压入 Open List  │
                        └───────────────┬───────────────┘
                                        │
                                        └─────── 循环下一轮迭代
```

### 规划器主循环伪代码实现

```python
def plan_space_search():
    open_list = PriorityQueue()
    closed_list = Set()
    
    # 构造包含战役级根任务的初始部分计划
    root_plan = Plan(root_task=MissionTask())
    root_plan.cost = evaluate_heuristic_cost(root_plan)
    open_list.push(root_plan, root_plan.cost)
    
    while not open_list.empty():
        current = open_list.pop_min()
        
        # 退出条件 1: 找到完全落地的有效规划方案
        if current is not None and current.is_complete():
            return current
            
        # 退出条件 2: 搜索空间耗尽
        if current is None:
            break
            
        closed_list.add(current)
        
        # 挑选出最亟待细化的抽象任务节点
        t = current.pick_task_to_detail()
        
        # 匹配能够细化任务 t 的所有规划算子方法
        applicable_methods = get_methods_matching(t)
        for m in applicable_methods:
            # 获取该算子在此战术上下文下的具体派生方案集
            alternatives = m.generate(current, t)
            
            for a in alternatives:
                # 必须深拷贝当前计划以探索分支空间
                new_plan = current.clone()
                
                # 执行应用分解：向 new_plan 中插入子任务并建立依赖
                m.refine(new_plan, t, a)
                
                # 计算新部分计划的代价评估值：f(p) = g(p) + h(p)
                new_plan.cost = compute_plan_cost(new_plan)
                
                # 重新加入优先队列
                open_list.push(new_plan, new_plan.cost)
                
    return FAILURE
```

---

## 13.5 任务网络架构 (A Plan of Tasks)

### 13.5.1 军事领域层级范围划分 (Task Scope Hierarchy)

为了适应多兵种作战指挥的条令习惯，系统构建了一套自顶向下的六级任务范围体系：

| 层次划分 (Scope) | 抽象级别解析 | 典型战术任务示例 (Task Examples) |
| :--- | :--- | :--- |
| **战役级 (Mission)** | 全局战略部署与根本目标 | `Mission` |
| **战略目标 (Objective)** | 特定战场区域的占领、清扫或坚守 | `Clear` (清扫), `Occupy` (占领), `Defend` (防御) |
| **编队梯队 (Team)** | 多兵种大集群的战役阶段机动与协同动作 | `Move` (机动), `Form Up` (整队集结), `Attack` (进攻), `Air Land` (机降), `Counter-Attack` (反击), `Para Drop` (伞降) |
| **战术协同 (Tactic)** | 针对具体突击方案的合成战术支持 | `Formation Ground Attack` (队形地面突击), `Planned Fire Support` (预设火力准备), `Smoke Screen` (烟幕遮蔽) |
| **联合子单位 (Units)** | 少数单位间的紧密互动行为 | `Transported Move` (载具输送机动), `Defend Sector` (扇区协同防御) |
| **单兵/单装 (Unit)** | 无法再分的原子物理执行动作 (Primitive Tasks) | `Defend`, `Attack`, `Hide`, `Move`, `Wait`, `Air Ingress` (空中切入), `Air Egress` (脱离), `Mount` (登车), `Dismount` (下车), `Load` (装载), `Unload` (卸载), `Ride` (乘车机动), `Para Jump` (跳伞), `Fire Artillery Mission` (炮兵射击) |

### 13.5.2 复合任务与原子任务 (Compound vs. Primitive Tasks)

1. **原子任务（Primitive Task）**：
   位于层次网络叶节点，直接映射为单个物理单位的执行系统指令（如行为树、状态机或导向行为）。原子任务无法继续分解。
2. **复合任务（Compound Task）**：
   位于非叶节点，负责将战略资源（单位、弹药、火力窗口）分配给各个子目标，并在其派生的子任务之间建立时空协同与约束弧。

### 13.5.3 任务执行时间跨度建模 (Temporal Duration Model)

任务在时间轴上表现为闭区间 $[T_{\text{start}}, T_{\text{end}}]$，其持续时间 $D = T_{\text{end}} - T_{\text{start}}$ 的计算逻辑取决于任务类型：

- **对于原子任务 $T_p$**：由底层寻路系统、运动学动力学模型或固定射击动作耗时直接确定：
  $$D(T_p) = \text{Duration}_{\text{physical}}(T_p)$$
- **对于已细化的复合任务 $T_c$**：动态包络其所有子任务网络的时间跨度：
  $$D(T_c) = \max_{t_i \in \text{Children}(T_c)} \big( T_{\text{end}}(t_i) \big) - \min_{t_j \in \text{Children}(T_c)} \big( T_{\text{start}}(t_j) \big)$$
- **对于尚未细化的复合任务 $T_{\text{unrefined}}$**：采用领域启发式函数评估估算时间 $\hat{D}(T)$，保证 $A^*$ 启发函数的下界预估一致性。

### 13.5.4 偏序时序依赖关系 (Precedence Relations)

任务以**有向无环图（DAG）**形式组织：
- 若任务 $T_1$ 前置于 $T_2$（记作 $T_1 \prec T_2$），则意味着 $T_2$ 的绝对开始时间受限于 $T_1$ 的结束时间：
  $$T_{\text{start}}(T_2) \ge T_{\text{end}}(T_1)$$
- **层级时序传播定理**：若复合任务 $T_A \prec T_B$，则对于 $T_A$ 递归派生的所有原子叶节点 $\forall t_a \in \text{Leaves}(T_A)$，以及 $T_B$ 的所有原子叶节点 $\forall t_b \in \text{Leaves}(T_B)$，恒满足：
  $$t_a \prec t_b \implies T_{\text{start}}(t_b) \ge T_{\text{end}}(t_a)$$
  *工业实践：这一性质保证了多单位梯队集结动作在宏观上的严格同步。*

---

## 13.6 任务数据流：参数绑定与输入输出拓扑 (Task Inputs and Outputs)

任务具有参数化接口。任务输入输出的声明式连接是解决规划中**空间推理（Spatial Reasoning）**与**资源分配**解耦的核心手段。

```
【父子任务间的数据绑定与时空值回传拓扑】

            ┌────────────────────────────────────────────────────────┐
            │ Team Formation Attack - AC (复合父任务)                 │
            │                                                        │
            │ Inputs:                                                │
            │   objective: Z                                         │
            │   start state: [ A@(5,1), C@(7,3) ]                    │
            │   target state: [ A@(2,4), C@(3,6) ]                   │
            │                                                        │
            │ Outputs:                                               │
            │   end state: [ A@(1,5), C@(2,6) ] ◄──┐                 │
            └──────────────┬───────────────────────┼─────────────────┘
                           │                       │
                分解派生   │                       │ 回传终态数据
                (Refines)  ▼                       │ (Data Propagation)
            ┌───────────────────────────┐          │
            │ Unit Attack - A (原子子任务)│          │
            │ Inputs:                   │          │
            │   start state: A@(5,1)    │          │
            │   target state: A@(2,4)   │          │
            │ Outputs:                  │          │
            │   end state: A@(1,5) ─────┼──────────┤
            └───────────────────────────┘          │
                                                   │
            ┌───────────────────────────┐          │
            │ Unit Attack - C (原子子任务)│          │
            │ Inputs:                   │          │
            │   start state: C@(7,3)    │          │
            │   target state: C@(3,6)   │          │
            │ Outputs:                  │          │
            │   end state: C@(2,6) ─────┴──────────┘
            └───────────────────────────┘
```

### 13.6.1 任务输入输出的数据定义规范

```ruby
# 原子任务：载具运兵装载
class LoadTask < Task
  is_primitive
  has_scope :unit
  
  # 输入：包含输送车单位标识、初始空间坐标及当前装载状态
  has_input :start_state,  :type => :unit
  # 输入：装载目标达成状态（指定步兵单位完成装载后装甲车的状态）
  has_input :target_state, :type => :unit
  # 输入：待登车的乘员单位引用
  has_input :passenger,    :type => :unit
  
  def compute_expected_costs(context)
    return 15.0
  end
end

# 复合任务：从集结地域发起编队地面协同进攻
class AttackAfterFormUpTeamTask < Task
  has_scope :team
  
  # 外部高层输入：参战集群的单位数组及其初始状态
  has_input :start_state,        :type => :units
  # 外部高层输入：战役目标区域地理实体
  has_input :objective,          :type => :objective
  # 外部高层输入：进攻主轴方向通道（Avenue of Approach）
  has_input :avenue_of_approach, :type => :avenue_of_approach
  
  # 任务对外输出：回传具体占领的目标区域
  has_output :objective_area,    :type => :area
  # 任务对外输出：突击前选取的展开集结地（Form-up Position）
  has_output :assembly_area,     :type => :area
  # 任务对外输出：各单位完成集结瞬间的联合状态向量
  has_output :assembled_state,   :type => :units
  # 任务对外输出：完成目标突击后，各单位的最终分布状态向量
  has_output :end_state,         :type => :units
  
  def compute_expected_costs(context)
    # 基于时空测算与掩体效用计算启发式代价值
    ...
  end
end
```

### 13.6.2 数据流传递机制：接地任务与未接地任务 (Grounding Mechanics)

- **接地任务（Grounded Tasks）**：
  若某任务的所有必要 `has_input` 槽位均已填充有效实体引用或数值，该任务即为**完全接地**。规划方法只有在任务处于接地状态时，才能进行空间几何投影与寻路分析。
- **未接地任务（Ungrounded Tasks）**：
  在父任务刚刚被声明创建时，其输入可能处于悬空状态（如其初始位置依赖于另一支行军纵队何时到达），允许其槽位挂载数据绑定引用（Data-flow References）。
- **参数向上与向后传导（Value Propagation）**：
  如图 13.6 所示，`TeamFormationAttack` 创建时，其全局目标状态为 $A@(2,4)$ 与 $C@(3,6)$。但在微观战术细化中，底层 `UnitAttack` 结合导航网格（NavMesh）与战场掩体空间推理（Spatial Reasoning），计算出实际射击与掩护终点为森林边缘掩体点：$A@(1,5)$ 与 $C@(2,6)$。这一精确坐标立刻向上传播并填入父任务的 `end_state` 数组槽位中，进而驱动后续后续任务（如 `Regroup`）以该位置作为其输入起点。

---

## 13.7 规划器分解方法 (Planner Methods)

规划器方法（Methods）是具有领域知识的转换引擎。其生命周期分为两个阶段：
1. **可行性生成阶段（`generate`）**：分析输入参数与世界态，生成所有在几何学与战术上成立的候选参数分支；
2. **细化应用阶段（`refine`）**：克隆原计划，将复合任务替换或拆解为具有精密因果和时序拓扑的子图。

```
【复合输送机动任务的子任务编排时序图 (TransportedMove Execution Timeline)】

  时间轴 (Time) ─────────────────────────────────────────────────────────────►

  Infantry A:  [Move to Open]──►[Mount A-C]─────────────────────────────►[Dismount A-C]──►[Move to Form-up]
                                    ▲                                         ▲
                                    │                                         │
  Infantry B:  ─────[Move to Open]──┼──────►[Mount B-C]─────────────────►[Dismount B-C]──►[Move to Form-up]
                                    │           ▲                             ▲
                                    │           │                             │
  Truck C:     [Move to Pos A]──►[Load C-A]─►[Move Pos B]─►[Load C-B]─►[Move Area X]──►[Unload C-A-B]──►[Regroup]
```

### 13.7.1 战术输送案例：TransportedMove 的方法细化推演

以卡车运输排 $C$ 运载两支身处密林边缘的步兵排 $A$ 与 $B$ 前往集结区 $X$ 为例（图 13.7），`TransportedMoveMethod` 需进行四阶段战术推理与图扩展：

1. **上车点几何空间决策（Pick-up Sites Selection）**：
   - 卡车运输排 $C$ 属于轮式/重型车辆，其 NavMesh 多边形穿透限制了它无法驶入森林覆盖区。
   - 规划方法结合空间查询，在林地外缘计算两处既满足 $C$ 的通行能力、又使步兵 $A$ 和 $B$ 的徒步行军代价最小化的接驳点（Pick-up Points）。
2. **搭乘时序博弈（Pickup Sequencing & Routing）**：
   - 比较先接 $A$ 后接 $B$（$C \to A \to B \to X$）与先接 $B$ 后接 $A$（$C \to B \to A \to X$）的全局时间代价。
   - 调用 A* 路径预查询，因 $A$ 的当前驻地离 $C$ 更近，系统收敛于路径更短的序列决策。
3. **下车点与终点选择（Drop-off Site & Final Deployment）**：
   - 在集结地域 $X$ 边缘寻找满足全体步兵快速脱离车辆的开阔空地作为下车点（Drop-off Site）。
   - 为 $A$、$B$、$C$ 分配在 $X$ 战术防区内的最终防御站位。
4. **生成依赖因果图（Causal Precedence Construction）**：
   方法生成 13 个子任务并严密交织其时序弧（如图 13.7 所示）：
   - $A$ 必须先步行移动至开阔地（`Move [A]`）；
   - $C$ 机动至 $A$ 上车点后，步兵 $A$ 登车（`Mount [A,C]`）与卡车装载（`Load [C,A]`）形成双向时序硬同步；
   - 随后 $C$ 机动至点 $B$，与步兵 $B$ 执行相同的同步登载；
   - 车辆向 $X$ 实施远程越野行军（`Ride [A,C]`、`Ride [B,C]` 与 $C$ 的机动动作并行执行）；
   - 到达 $X$ 后，执行单点广播式同步下车（`Unload [C,A,B]` 与两单位的 `Dismount` 强同步）；
   - 最终各单位展开机动占领阵地。

通过此机制，多单位在时空维度的复杂协同行为（步车协同战术机动）被严谨拆解为带有显式时序依赖的有向子图，彻底杜绝了状态空间搜索中极其容易出现的“步兵未登车卡车提前开走”或“卡车驶入不可通行区域”的荒谬失效解。

---

## 13.8 关键架构特性对比总结 (Architectural Comparison)

| 核心评估维度 | 传统状态空间规划 (GOAP / 传统前向 HTN) | 工业级分层计划空间规划 (Hierarchical Plan-Space Planning) |
| :--- | :--- | :--- |
| **状态搜索空间规模** | 面对 $N$ 个并行单位发生超指数爆炸：$(b^N)^d$ | 极大压缩，以战术分解替代全排列搜索：$\mathcal{O}(M \times K)$ |
| **时间维度组织形式** | 严格单向线性步进（从前向后或从后向前） | **偏序时序网络（Partial-Order Temporal Network）**，支持非线性解耦细化 |
| **多单位协同建模** | 只能依靠隐式状态变量或复杂的黑板协调器驱动 | **显式建模**：协同任务拥有多实体插槽，时序因果弧硬约束同步 |
| **空间推理与几何绑定** | 难以在粗粒度早期搜索中进行昂贵的环境几何查询 | **延迟绑定（Least Commitment）**：底层几何与掩体查询仅在细化原子任务时按需触发 |
| **战术意图解释性** | 极低：仅为一串扁平无序的并发原子动作流水 | **极高**：具备完整的任务树谱系，天然映射为军用级作战简报（Briefing） |

---

---

## 1. 方法分解体系与作战范围职责（Method Decomposition Architecture & Scope Responsibilities）

在分层规划空间规划（Hierarchical Plan-Space Planning, 简称 HPSP）中，复合任务（Compound Tasks）必须被逐步解构为可在物理引擎或游戏执行层直接调度的元任务（Primitive Unit Tasks）。方法（Methods）承担着逻辑分解的核心职责，不仅定义任务层级衍生规则，更在时序与数据流上绑定各个作战单位之间的依赖关系。

### 1.1 作战范围（Scope）与规划方法职责分工

为了全面覆盖大规模联合战术机动领域，规划器依据军事指挥层级，自顶向下镜像映射（Mirror）出清晰的分解机制。通常情况下，方法会将任务分解至下一层级，偶尔跨越至下两层级。

| 范围（Scope） | 规划方法职责（Planner Method Responsibility） | 典型业务场景与产出 |
| :--- | :--- | :--- |
| **任务级（Mission）** | 编排作战目标序列，分配作战单位至各个目标。 | 划分主要攻防战区，将机步连、装甲排分配给对应目标。 |
| **目标级（Objective）** | 规划团队活动，编配主攻战术单位与支援单位至各分队。 | 组织由坦克、步战车与步兵班构成的混合机动特遣队。 |
| **团队级（Team）** | 以协同编队执行任务，依据角色分工指派具体职责。 | 展开攻击阵型、协调正面突击与侧翼掩护。 |
| **战术级（Tactic）** | 同步多个异构作战单位之间的战术协同动作。 | 步兵搭载、装甲协同推进、火力压制时机对齐。 |
| **跨单位级（Units）** | 编排具备互补能力的单位协同交互。 | 输送载具与乘车步兵之间的集结、装载与卸载同步。 |
| **单单位级（Unit）** | 确定个体执行的元任务最终状态（End-State）。 | 单车的路线沿途寻路、单兵掩体射击姿态确定。 |

```
                       [ Mission Level ]
                    Arrange & Allocate Units
                               |
                               v
                      [ Objective Level ]
                   Team Activities & Support
                               |
                               v
                        [ Team Level ]
                    Roles & Formation Attack
                               |
                               v
                       [ Tactic Level ]
                    Tactical Synchronization
                               |
                               v
                       [ Units Level ]
                  Cross-Unit Interaction (e.g., Transport)
                               |
                               v
                        [ Unit Level ]
                   Primitive End-State Tasks
```

### 1.2 协同动作时序同步机制：以 TransportedMove 为例

在复杂机动中，复合动作往往牵涉异构单位之间的强时序约束。以机械化步兵搭载卡车行军（`TransportedMove`）为例：两个步兵班（$A, B$）由运输卡车排（$C$）机动运送至前线卸载点 $X$。

1. **先决依赖建立（Predecessor Binding）**：
   - 载具 $C$ 对步兵 $A$ 执行装载任务 $T_{\text{load}}(C, A)$ 的开始，严格依赖于 $C$ 到达上车点 $P_A$ 与步兵 $A$ 到达集结区 $P_A$ 两者同时满足。即：
     $$\text{StartTime}(T_{\text{load}}(C, A)) \ge \max\left( \text{EndTime}(T_{\text{move}}(C, P_A)),\, \text{EndTime}(T_{\text{move}}(A, P_A)) \right)$$
   - 步兵下车任务 $T_{\text{unload}}$ 的开始，强依赖于载具排 $C$ 到达下客区 $X$ 的元任务结束时间。
2. **多方案穷举与分支决策**：
   - 在细粒度空间中，`TransportedMove` 拥有充分的局部领域语义，能够直接估算“先接载 $A$ 再接载 $B$”与“先接载 $B$ 再接载 $A$”的时间成本并就地择优。
   - 当局部逻辑难以裁决时，方法不应进行次优硬编码，而是向规划器主循环暴露多重候选分支（Alternatives），交由规划主循环克隆父计划（Parent Plan）并衍生多分支进入搜索空间。

### 1.3 战术教条（Tactical Doctrine）的解耦与配置

对于同一宏观任务，系统可通过注册多个互斥或备选的方法来实现战术教条的动态切换：
- **阵地防御（Static Defense）**：分解出使可用排各自占据目标周围防御阵地的子任务。
- **弹性防御（Active/Elastic Defense）**：分解为步兵排依托预设工事阻击、装甲排部署于纵深后方以备战术反击。
- **工业级配置权衡**：通过根据关卡剧本或战区指挥官性格配置开启/关闭特定方法库，可以在无需修改核心规划引擎的前提下直接调整 AI 的宏观战役战术教条。

### 1.4 分解失败与规划局部死胡同（Failure Handling & Local Dead Ends）

当方法无法满足任务前提条件或环境强约束时，分解将返回失败且不产生派生计划：
- **前置资源枯竭**：火炮单位在之前的计划中已打光全部炮弹，后续炮击任务分解失败。
- **拓扑几何冲突**：三支机械化排被要求以宽阵型突击，但前进路线上存在仅能容纳单车通行的狭窄桥梁，导致阵型突击方法失效。
- **规划空间回溯机制**：某个方法的失败仅代表当前局部分支的死胡同（Local Dead End）。由于搜索基于 $A^*$ 驱动，算法将自动回退并在开放列表（Open List）中转向评估其他潜在可行的分支空间。

---

## 2. 规划空间搜索与时间代价值计算（Plan-Space Search & Cost Evaluation）

传统分层任务网络（Hierarchical Task Networks, HTN）通常在状态空间（State-Space）中以深度优先搜索（DFS）加回溯的方式运作。而本架构将整个规划过程构建为**规划空间搜索（Plan-Space Search）**，其搜索图中的每一个节点均为一个（部分完整的）计划 $\mathcal{P}$，边则代表通过方法对某一复合任务进行细化展开（Refinement）的操作。

### 2.1 规划空间与开放列表（Open List）驱动

规划器维护一个以计划综合代价为优先级的开放列表 $\text{OpenList}$：

```
                              [ Plan 1.1 ]
                               Cost: 90.0
                                    |
                    +---------------+---------------+
                    | (Refine ClearObjective)       |
                    v                               v
             [ Plan 1.1.1 ]                  [ Plan 1.1.2 ]
               Cost: 95.0                     Cost: 125.0
                    |                               |
       +------------+------------+                  | (Retained in OpenList)
       | (Refine Move[ABC])      |                  v
       v                         v
[ Plan 1.1.1.1 ]          [ Plan 1.1.1.2 ]
  Cost: 104.0               Cost: 119.0
```

每一次迭代，规划器始终从 $\text{OpenList}$ 中弹出当前全局代价最低的计划节点进行细化，确保最先收敛到完全具体化元任务的解为全局最优或次优解。

### 2.2 任务网络时间代价递归计算公式

在战术机动领域，**执行持续时间（Plan Duration）**是衡量计划质量的最优代价指标（更早占领阵地、更快集结兵力发起突击具有决定性战术优势）。

计划的持续时间基于关键路径法（Critical Path Method, CPM）进行自底向上的严格时序推进与递归收敛：

#### 复合任务/元任务开始与结束时间计算
设计划包含任务集 $\mathcal{T}$。对于任一任务 $T_i \in \mathcal{T}$，其前序任务集合为 $\text{Pred}(T_i)$：

$$\text{StartTime}(T_i) = \begin{cases} 0, & \text{若 } \text{Pred}(T_i) = \emptyset \\ \displaystyle\max_{T_p \in \text{Pred}(T_i)} \left( \text{EndTime}(T_p) \right), & \text{若 } \text{Pred}(T_i) \neq \emptyset \end{cases}$$

$$\text{EndTime}(T_i) = \text{StartTime}(T_i) + \text{Duration}(T_i)$$

#### 复合任务的持续时间归纳
对于尚未细化的复合任务 $T_{\text{compound}}$，其 $\text{Duration}(T_{\text{compound}})$ 由其特定的启发式估算函数提供；对于已被细化为子任务图 $\mathcal{C}(T_{\text{compound}})$ 的任务，其持续时间由子任务图完全决定：

$$\text{EndTime}(T_{\text{compound}}) = \max_{T_c \in \mathcal{C}(T_{\text{compound}})} \left( \text{EndTime}(T_c) \right)$$

$$\text{Duration}(T_{\text{compound}}) = \text{EndTime}(T_{\text{compound}}) - \text{StartTime}(T_{\text{compound}})$$

#### 全局计划代价
全局根任务为 $T_{\text{root}}$，则计划 $\mathcal{P}$ 的总代价 $f(\mathcal{P})$ 定义为：

$$f(\mathcal{P}) = \text{EndTime}(T_{\text{root}}) - \text{StartTime}(T_{\text{root}})$$

在工程实现中，每当计划被细化更新时，算法从根任务开始，拓扑遍历所有前序任务时间均已确定的子节点，递归推进时间戳。对于参数输入未变的复合任务估算值，采用哈希缓存策略避免重复计算。

### 2.3 复合任务持续时间启发式估算（Heuristic Duration Estimation）

为保证 $A^*$ 搜索在规划空间中的**可采纳性（Admissibility）**与**最优性**，复合任务的持续时间估计必须满足不高于实际执行时间的准则（Admissible Heuristic，即不高估真实代价）：

$$h(T_{\text{compound}}) \le h^*(T_{\text{compound}})$$

以机动运输复合任务 $\text{TransportedMove}(A, B, C \to X)$ 为例，其中载具 $C$ 运送单位 $A$ 和 $B$ 前往目的地 $X$：

```
[ C ] --------> [ A ] --------> [ B ] --------> [ X (Objective) ]
  (Move top-spd)  (Load A)       (Load B)       (Unload A & B)
```

其底层低估启发式代价公式为：

$$\hat{D} = t_{\text{path}}(C \to A) + t_{\text{path}}(A \to B) + t_{\text{path}}(B \to X) + 2 \cdot t_{\text{load}} + 1 \cdot t_{\text{unload}}$$

其中：
- 路径时间 $t_{\text{path}}(P_i \to P_j) = \frac{\text{Distance}(P_i, P_j)}{V_{\max}(C)}$，直接采用载具最高理论平地巡航速度 $V_{\max}(C)$ 结合理想几何距离计算，忽略地表起伏阻力与转弯减速，严格保证其代价低于真实物理行军时间。
- 拾取次序通过几何就近判定：若 $\text{Dist}(C, A) < \text{Dist}(C, B)$ 则评估 $C \to A \to B$，反之评估 $C \to B \to A$；或在多路径估算中直接取两者的极小值 $\min(\hat{D}_{A\to B}, \hat{D}_{B\to A})$。

### 2.4 战术偏差控制：代价人为膨胀与通缩策略（Cost Inflation & Deflation）

在生产实践中，除了纯物理时间，还可以通过操纵时间代价来引导规划器的战术偏好：

1. **高危动作代价膨胀（Cost Inflation for Risky Actions）**：
   - 战术目标：避免使用薄皮无装甲卡车（Soft-skinned Vehicles）在前线热区运送步兵。
   - 算法干预：人为提升卡车进入危险区域的机动时间虚拟系数：
     $$\text{Duration}_{\text{cost}}(T_{\text{truck\_move}}) = \alpha \cdot \text{Duration}_{\text{actual}}(T_{\text{truck\_move}}), \quad (\alpha \gg 1)$$
   - 规划效应：若环境中同时存在重装甲运兵车（APC），尽管其自身极速偏低，但因无惩罚系数，规划器经 $A^*$ 代价比较后会自动优先选择装甲输送方案。
2. **无关次要动作代价通缩（Cost Deflation for Irrelevant Post-actions）**：
   - 战术目标：武装直升机将空降突击队投送至着陆场（LZ）后，直升机返回基地的撤离航程可能极为漫长。
   - 算法干预：返航任务的时间差异不应掩盖地面步兵突击方案的优劣。系统将返航任务强制设定为一个极小的固定常数 $\epsilon$：
     $$\text{Duration}_{\text{cost}}(T_{\text{helo\_return}}) = C_{\text{fixed}} \approx \epsilon$$
   - 规划效应：消除冗余返航路径对攻坚战术选择的干扰，确保搜索焦点始终汇聚于关键作战机动。

---

## 3. 抑制组合爆炸的工程优化策略（Combinatorial Optimization Techniques）

在涉及十几个步兵排、装甲部队、炮兵阵地与武直机群的协同推演中，分支因子（Branching Factor）会随着单位数量增加呈指数级激增（即经典的组合爆炸问题）。本架构通过三重工业级手段将搜索空间严密约束在可控区间。

### 3.1 搜索范式切换：$A^*$ 规划空间搜索 vs. 传统 HTN 深度优先回溯

传统 HTN 规划器大多采用深度优先搜索结合局部回溯机制（Depth-First Backtracking）。一旦在深层叶子节点发生战术不满足，必须回溯大量先前做出的微观决策，极易陷入组合泥潭。

规划空间 $A^*$ 搜索则将所有候选（不完整）计划置于全局优先级队列中。系统始终展开当前代价最优的部分计划，直接从宏观评估不同战术路径的潜力，从而大幅剪除无望的搜索空间。

### 3.2 顶层优先调度律（Highest Scope First Principle）

当一个中间计划被取出细化时，其中通常存在多个已经**参数完全实例化（Grounded）**的复合任务。主循环绝不随机挑选，而是严格遵循**高层级范围优先（Highest Scope First）**策略调度展开。

```
[ Task Hierarchy Scope ]
----------------------------------------------------------------------
Scope: Mission     |  [ Mission [ABC] ]
Scope: Objective   |       |--> [ Clear Objective [ABC] ]
Scope: Team        |             |--> [ Form Up [ABC] ] (Scope: Team, Grounded) -> PICKED FIRST!
Scope: Units       |             |--> [ Move [ABC] ]
                   |                    |--> [ Transported Move [BC] ] (Scope: Units, Grounded)
Scope: Unit        |                    |--> [ Move [A] ] (Scope: Unit, Grounded)
----------------------------------------------------------------------
Decision: Task "Form Up [ABC]" has higher scope than "Transported Move [BC]", 
          thus "Form Up" is selected for next refinement iteration.
```

#### 战术工程学机理
- 高层级决策（例如装甲连以何种战斗队形在何处完成集结）决定了整个作战方案的可行性底线（Feasibility）与成本基准。
- 若过早陷入底层细节（例如步兵在运兵卡车内的具体座位排布、单车路径拐弯微操），一旦后续发现目标集结地空间狭窄无法容纳多排展开协同突击，所有底层的微观计算将全部作废。
- **高层优先策略确保系统以最少的规划迭代步数迅速锚定宏观可行架构。**

### 3.3 中间突破规划范式（Middle-Out Planning）

传统的正向规划（Forward Planning）从初始状态逐步推演至目标状态；反向规划（Reverse/Goal-Directed Planning）则从胜利条件向后逆推。

军事战术制定普遍采用**混合正反向思维**：参谋往往首先锁定全盘作战的核心瓶颈步骤（Critical Step）——例如空中突击空降夺占咽喉要地或向核心阵地发起主攻，随后分别向“任务完成”正向规划，向“任务发起”逆向倒推。此机制被称为**中间突破规划（Middle-Out Planning）**。

#### 数据流图重组（Dataflow Graph Rewiring）
通过将任务的输入/输出（I/O）参数依赖关系进行拓扑倒置与移位，规划器能够强行确保关键攻坚任务率先成为唯一完全实例化的任务（Grounded Task）。

```
+-----------------------------------------------------------------------------------------+
|                                Clear Objective (Compound)                               |
| i: start_state                                                            end_state :o  |
| i: objective                                                                            |
| i: threat_intel                                                                         |
+-----------------------------------------------------------------------------------------+
       |                                       |                                    |
       | (Supplies target_state)               | (Supplies Objective & Intel)       | (Supplies start_state)
       v                                       v                                    v
+-------------------------------+   +------------------------------------+   +--------------+
|             Move              |   |       AttackAfterFormUp            |   |   Regroup    |
| i: start_state   end_state :o |   | i: start_state        end_state :o |   | i: start_st. |
| i: target_state               |   | i: objective     objective_area :o |   | end_state :o |
+-------------------------------+   | i: approach        form_up_area :o |   +--------------+
       ^                            |                formed_up_state :o  |          ^
       |                            +------------------------------------+          |
       | (Supplies arrival_state)                      |                            |
       |                                               | (Supplies form_up_area &   |
+------------------------------------+                 |  formed_up_state)          |
|              FormUp                |                 |                            |
| i: form_up_area       end_state :o |<----------------+                            |
| i: target_state   arrival_state :o |----------------------------------------------+
+------------------------------------+
```

#### 中间突破时序展开流程
1. **任务分解结构**：复合任务 `ClearObjective` 被分解为四个按物理时序执行的任务：
   $$\text{Move} \longrightarrow \text{FormUp} \longrightarrow \text{AttackAfterFormUp} \longrightarrow \text{Regroup}$$
2. **依赖反向注入**：
   - 规划器不先细化 `Move`，而是将战场初始情报输入给主攻任务 `AttackAfterFormUp`。
   - `AttackAfterFormUp` 成为**第一个**全部输入均已具备（Grounded）的待细化任务。
3. **关键决策外溢**：
   - 方法细化 `AttackAfterFormUp`，根据客观敌情确定主攻轴线、阵型展开线，并输出所需的集结区域（`form_up_area`）与集结达成状态（`formed_up_state`），以及攻占后的终末状态。
4. **前后向双向闭合**：
   - `form_up_area` 输出激活了前置任务 `FormUp` 的输入；
   - `FormUp` 细化后确定各单位切入集结区的入口航向与到达要求（`arrival_state`），进而反哺激活起始任务 `Move` 的目标状态输入（`target_state`）；
   - 同时，主攻结束状态直接激活收尾任务 `Regroup`。
5. **战术架构收益**：优先确立不可妥协的核心战术支点，使非关键的前移机动与后续清剿完全围绕核心交战进行收敛，极大地削减了漫无目的的机动推演组合。

---

## 4. 工业级实践全景、性能基准与执行监控（Production Real-World Metrics & Execution Monitoring）

本分层规划空间规划体系并非停留于实验室概念阶段，而是已经在商业级大型军事模拟与战术任务生成器（如 *PlannedAssault* 针对《ARMA》/《ARMA 2》环境）中完成严苛的工业化验证。

### 4.1 生产落地实测性能指标（PlannedAssault Engine）

在单线程环境下运行的大规模联合战术规划实测表现如下：

- **运行硬件**：Intel Core 2 Quad Q8400 (单线程执行)。
- **运行环境**：JRuby on Java Virtual Machine (JVM)。
- **规划空间规模**：$4\text{ km} \times 3\text{ km}$ 复杂高精地形。
- **参战部队编制**：超过 12 个合成兵种分队（包括机械化步兵排、主战坦克连排、武装直升机空中编队、远程火炮阵地等）。
- **生成总耗时**：约 $10 \sim 30$ 秒。
  - **CPU 性能热点剖析**：**超 $85\%$ 的 CPU 时间消耗于高精地形分析（Terrain Analysis）与多单位三维全局寻路（Pathfinding）**，纯规划主循环展开与状态空间推演耗时极低。
- **收敛效率**：绝大多数实战攻击方案在**少于 200 次规划器主循环迭代（Main Loop Iterations）**内即达成完全实例化收敛。

### 4.2 规划空间数据结构在执行监控（Execution Monitoring）中的实战价值

与传统状态空间规划器输出单一线性动作序列不同，规划空间规划产出的是一个保留了全生命周期元数据的**层次化偏序时序因果网络（Hierarchical Temporal/Causal Network）**。这为实时动态博弈中的智能体执行监控与动态修复带来了关键架构支撑：

```
[ Mission Plan Execution Monitor ]
-----------------------------------------------------------------------------------------
Task Hierarchy:
[ Clear Objective ]
    |-- [ Move ] (Completed, took +5s)
    |-- [ Form Up ] (In Progress, ALERT: Unit B Delay +12s)
            |
            v
   [ Impact Analysis ]:
   - Max Allowed Slack Duration: 15.0s
   - Status: Absorbed (12s < 15s). Downstream "AttackAfterFormUp" unaffected.
   - If Delay > 15s: Trigger Sub-Plan Repair for "FormUp" node only.
-----------------------------------------------------------------------------------------
```

1. **人类可读战术简报自动生成（Tactical Briefing Generation）**：
   - 计划树中完整保留了最高指令目标、战术小队编组逻辑及动作先决条件。
   - 系统可无损导出具备自然语言结构的战术简报，例如：“一排与二排向集结地 Alpha 机动以掩护三排主突”，极大提升了调试效率与游戏叙事体验。
2. **精准战术影响面分析（Causal Impact Analysis）**：
   - 当战场发生动态扰动（例如三号步战车因被摧毁或地形受阻导致集结延迟 $\Delta t$）时，系统利用因果边向下遍历受影响的后序任务 $\text{Succ}(T_i)$。
   - 能够精准计算该扰动是否超出下游攻坚任务的时差裕量（Slack Time），而无需盲目推翻全盘方案。
3. **局部最小化计划修复（Local Plan Repair）**：
   - 监控系统可明确锁定受损的局部子任务节点。
   - 在维持宏观战役结构不变的前提下，通过继承原计划的边界条件（如输入输出状态和最大允许持续时间），针对发生故障的子树快速重运行特定方法展开局部替换，实现毫秒级反应与稳健执行。
