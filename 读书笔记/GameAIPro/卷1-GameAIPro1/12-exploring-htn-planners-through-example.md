---
type: Reference
title: "第12章 Exploring HTN Planners through Example"
description: "Game AI Pro 工业级精读：Exploring HTN Planners through Example。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第12章 Exploring HTN Planners through Example

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 12.  
> 原文作者 / 资源：[Exploring HTN Planners through Example](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter12_Exploring_HTN_Planners_through_Example.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

**——基于《Game AI Pro 1》第12章（Troy Humphreys 著）深度工程解析**

---

## 12.1 引言与行为选择架构选型（Introduction & Behavior Selection）

在现代 AAA 级游戏人工智能架构中，非玩家角色（Non-Player Character, NPC）的核心议题是**行为选择（Behavior Selection）**。工业界在不同历史周期演进出多种解决方案，包括有限状态机（Finite-State Machines, FSM）、行为树（Behavior Trees, BT）、效用系统（Utility Systems）、神经网络（Neural Networks）以及各类规划器（Planners）。

在《变形金刚：塞伯坦的陨落》（*Transformers: Fall of Cybertron*, High Moon Studios）的工业实践中，AI 团队由传统的**目标导向型行动规划（Goal-Oriented Action Planning, GOAP）**转型为**分层任务网络（Hierarchical Task Networks, HTN）**规划器。

### 12.1.1 核心对比：HTN、行为树与 GOAP

```
+------------------+------------------------------------+------------------------------------+------------------------------------+
| 评估维度         | 行为树 (Behavior Trees)            | GOAP 规划器                        | HTN 规划器 (Total-Order Forward)   |
+------------------+------------------------------------+------------------------------------+------------------------------------+
| 规划机制         | 反应式执行，无前向推演模拟         | 基于 A*/Dijkstra 的状态空间反向/前向| 基于任务前向递归分解 (Decomposition)|
|                  | (Reactive Execution)               | 状态空间搜索 (State Space Search)  | 与回溯 (Backtracking)              |
+------------------+------------------------------------+------------------------------------+------------------------------------+
| 时间复杂度       | $O(N)$ 遍历，极快                  | 指数级状态展开，依赖启发式排序     | 深度优先搜索，剪枝迅速，无排序开销 |
+------------------+------------------------------------+------------------------------------+------------------------------------+
| 前瞻能力         | 无原生未来推演（难以评估动作链后续 | 原生支持（在规划期模拟状态变化）   | 强前瞻性（可在规划期模拟工作世界状态|
| (Look-ahead)     | 副作用）                           |                                    | 变更，并基于未来状态继续分支）     |
+------------------+------------------------------------+------------------------------------+------------------------------------+
| 模块化与表达力   | 高度模块化，控制流直观             | 扁平化 Action 池，长序列难以精准控 | 极高（高层意图向底层原子动作分层透 |
|                  |                                    | 制动作节奏与特定剧本风格           | 传，兼具模块化与序列语义严密性）   |
+------------------+------------------------------------+------------------------------------+------------------------------------+
```

HTN 区别于其他规划器的本质在于：**它将待求解的问题表示为一个极高维度的抽象任务（High-Level Task），在规划阶段通过前向分解（Forward Decomposition）递归地将其拆解为细粒度的子任务序列，直至完全转化为由原子动作组成的线性任务链（即 Plan）。** 

行为树虽然同样具有分层模块化和执行速度极快的特性，但无法对动作的“未来副作用”进行推演；而基于状态空间搜索的 GOAP 系统在长规划步长下往往伴随巨大的启发式堆排序（Priority Queue Ordering）开销。HTN 采用全序前向分解（Total-Order Forward Decomposition），在结合领域知识（Domain Knowledge）的前提下，能够大幅剪枝搜索空间，在保持极高行为表达力（Expressiveness）的同时，实现媲美行为树的执行性能。

---

## 12.2 HTN 的基本构建块（Building Blocks of HTN）

HTN 系统主要由四大核心部件协作构成：
1. **世界状态（World State）**：当前问题空间的紧凑数字化抽象表达。
2. **感知系统（Sensors）**：负责将复杂世界环境实体映射、量化并写入世界状态。
3. **领域结构（HTN Domain）**：包含复合任务（Compound Tasks）与基元任务（Primitive Tasks）的层次网络。
4. **规划求解器（Planner）与计划执行器（Plan Runner）**：规划器生成可执行的基元任务序列，执行器负责物理驱动并在运行时校验。

```
                     +---------------------------------------------+
                     |                 Sensors                     |
                     |  (Vision, Audio, Health, Range Sensors)     |
                     +----------------------+----------------------+
                                            | Time-sliced Update
                                            v
+-----------------------+           +---------------+
|      HTN Domain       |           |  World State  |
|  (Compound Tasks &    |<--------->|  (Property    |
|   Primitive Tasks)    |           |   Vector)     |
+-----------+-----------+           +-------+-------+
            |                               |
            | Decomposes into               | Working Copy
            v                               v
+---------------------------------------------------+
|                     Planner                       |
|   (Total-Order Forward Decomposition Planner)     |
+---------------------------+-----------------------+
                            | Outputs Plan (Task Sequence)
                            v
+---------------------------------------------------+
|                   Plan Runner                     |
|  [Task 0] -> [Task 1] -> [Current Task] -> ...    |
+---------------------------------------------------+
```

### 12.2.1 世界状态（The World State）

工业级游戏 AI 不直接在规划树中传递实体指针或物理世界对象（如直接查询 `Actor->GetActorLocation()`），因为物理查询与场景遍历无法承受规划过程中的虚拟回溯与多状态模拟。

HTN 引入**世界状态（World State）**作为规划器的推演沙盒。世界状态被设计为一个强类型或离散化的紧凑属性向量（Property Vector），由枚举进行数组下标索引。其核心指导思想为：**仅记录 AI 进行逻辑裁决（Reasoning）所必需的最精简抽象特征，彻底剥离与规划解算无关的具体数值。**

```cpp
// 世界状态属性索引定义
enum EHtnWorldStateProperties
{
    WsEnemyRange,   // 敌方相对距离区间
    WsHealth,       // 自身生命状态
    WsIsTired,      // 疲劳状态标志位
    WsLocation,     // 当前位置锚点 (LocRef)
    WsHasTreeTrunk, // 是否持有主武器树干
    WsBored,        // 无聊度计数器
    WsMaxProperties
};

// 敌方距离抽象枚举（禁止在世界状态中直接存放 float 物理标量）
enum EEnemyRange : uint8_t
{
    MeleeRange,     // 近战接敌范围
    ViewRange,      // 视线可察觉范围
    OutOfRange      // 规划范围之外
};

// 工业紧凑数组实现（在内存中连续存储，支持极速 memcpy 复制以供规划器模拟）
using FWorldState = std::vector<uint8_t>;

FWorldState CurrentWorldState(WsMaxProperties, 0);
EEnemyRange currentRange = static_cast<EEnemyRange>(CurrentWorldState[WsEnemyRange]);
CurrentWorldState[WsEnemyRange] = MeleeRange;
```

### 12.2.2 传感器系统（Sensors）

世界状态的生命周期由两股力量协同驱动：
1. **内部推演与成功任务的写入**：AI 自身规划执行的动作产生的影响。
2. **外部环境的不可控变更**：玩家位移、第三方友军干扰、不可抗力物理破坏等。

为了反映物理世界的动态变化，AI 架构配备了一个**分时切片传感器系统（Time-Sliced Sensor System）**。传感器包含：
- **视觉传感器（Vision Sensor）**：提取视锥范围内的敌人，将物理坐标抽象为 `WsEnemyRange` 状态并写入世界状态。
- **听觉传感器（Hearing Sensor）**：将声源刺激转换为特定警觉度属性。
- **距离/状态传感器（Range/Health Sensors）**：以低频切片机制监控血量与全局寻路拓扑。

传感器系统扮演着**“物理世界到抽象向量的编码转换层（Encoder Layer）”**，使 HTN 规划器解耦于底层的物理引擎。

### 12.2.3 基元任务（Primitive Tasks）

基元任务是 NPC 最终执行的原子操作单元，它由三部分构成：
1. **前置条件（Preconditions）**：必须在执行环境（或工作世界状态）中完全满足的逻辑表达式集合。
2. **算子（Operator）**：任务绑定的具体执行逻辑（如寻路导航、播放蒙太奇动画、触发射线检测等）。
3. **副作用（Effects）**：该任务假设执行成功后，将对世界状态造成的确定性变更。

> **工程提示（Best Practice）**：在基元任务级别添加前置条件并非 HTN 形式化定义的绝对要求，但在工业工程实践中强烈推荐。基元任务自洽的前置条件检查，能避免在复合任务（Compound Tasks）高层反复编写重复冗余的防御性条件校验，消除逻辑发散带来的潜在 Bug。

```cpp
Primitive Task [SprintToEnemy]
    Preconditions: 
        WsHasEnemy == true
    Operator: 
        NavigateTo(EnemyLoc, Speed_Fast)
    Effects: 
        WsLocation = EnemyLoc, 
        WsIsTired = true

Primitive Task [WalkToNextBridge]
    Operator: 
        NavigateTo(BridgeLoc, Speed_Slow)
    Effects: 
        WsLocation = BridgeLoc, 
        WsBored += 1
```

**算子（Operator）与基元任务（Primitive Task）的区别**：
算子是纯粹的通用动作承载器（如通用导航 `NavigateTo`）。基元任务则是带领域语义的包装器，它定义了算子在特定 AI 上下文中的语义前置与结果状态。例如 `SprintToEnemy` 与 `WalkToNextBridge` 共享底层的 `NavigateTo` 算子，但前者以进入疲劳（`WsIsTired = true`）为代价换取高速逼近目标，后者则以漫步方式改变巡逻状态并累积无聊值。

### 12.2.4 复合任务（Compound Tasks）

复合任务代表 AI 的高级战略意图或宏观目标。**复合任务本身不包含任何运行时直接驱动的算子（Operator），它纯粹是一个容纳多种“实现途径（Methods）”的逻辑容器。**

复合任务结构解析：
- 每个复合任务包含 $M$ 个方法（Method 0, Method 1, ...），各方法按**从高到低**的优先级声明。
- 每个方法内部包含**前置条件（Conditions）**以及**子任务集（Subtasks）**。
- 子任务集可包含基元任务，也可继续递归嵌套其他复合任务。

```cpp
Compound Task [AttackEnemy]
    // Method 0: 最高优先级近战攻击
    Method 0 [WsHasTreeTrunk == true]
        Subtasks: [NavigateTo(EnemyLoc), DoTrunkSlam()]
        
    // Method 1: 次优先级远程投石反制
    Method 1 [WsHasTreeTrunk == false]
        Subtasks: [LiftBoulderFromGround(), ThrowBoulderAt(EnemyLoc)]
```

---

## 12.3 构建 HTN 领域（Putting Together an HTN Domain）

以树干重击者（Trunk Thumper）——一个挥舞树干巡逻于吊桥之间的巨型巨魔 NPC 为例。构建其根复合任务 `BeTrunkThumper` 及其底层动作展开逻辑：

```cpp
// ==================== 复合任务定义 ====================
Compound Task [BeTrunkThumper]
    // 战斗方法：可见敌人时触发
    Method 0 [WsCanSeeEnemy == true]
        Subtasks: [NavigateToEnemy(), DoTrunkSlam()]
        
    // 巡逻巡视方法：默认 Fallback（无条件触发）
    Method 1 [true]
        Subtasks: [ChooseBridgeToCheck(), NavigateToBridge(), CheckBridge()]

// ==================== 基元任务定义 ====================
Primitive Task [DoTrunkSlam]
    Operator: AnimatedAttackOperator(TrunkSlamAnimName)

Primitive Task [NavigateToEnemy]
    Operator: NavigateToOperator(EnemyLocRef)
    Effects:  WsLocation = EnemyLocRef

Primitive Task [ChooseBridgeToCheck]
    Operator: ChooseBridgeToCheckOperator

Primitive Task [NavigateToBridge]
    Operator: NavigateToOperator(NextBridgeLocRef)
    Effects:  WsLocation = NextBridgeLocRef

Primitive Task [CheckBridge]
    Operator: CheckBridgeOperator(SearchAnimName)
```

在这个领域中，根复合任务确立了清晰的决策优先级：发现目标立即突进砸地；否则按“选择桥梁 $\to$ 移动至桥梁 $\to$ 侦察警戒”的闭环推进巡逻。

---

## 12.4 规划寻解核心算法（Finding a Plan）

规划器是 HTN 的核心运转引擎。规划过程本质上是一个**基于前向推演的确定性深度优先搜索（Depth-First Search, DFS）**。

### 12.4.1 重规划触发条件（Re-plan Triggers）

规划器不会在每一帧都重新解算全部网络，而是仅在满足以下三项确定性条件之一时启动寻解：
1. **当前计划终止或失败（Plan Finished or Failed）**：所有动作执行完毕，或某个算子报告物理执行故障。
2. **计划缺失（No Plan Active）**：AI 初始化或刚遭遇外部逻辑强行打断。
3. **感知导致世界状态突变（World State Invalidation via Sensors）**：传感器捕捉到关键外部事实变迁，致使当前计划的前提失效。

### 12.4.2 求解推演流程与算法伪代码

初始化规划时，规划器执行两大操作：
1. 深拷贝当前的实体世界状态，建立一份**工作世界状态（Working World State, $WWS$）**。
2. 将根复合任务推入待处理任务栈（`TasksToProcess`）。

规划器逐次弹出栈顶任务，根据类型执行前向分解或前置校验：
- **遇到复合任务**：依次评估各方法的条件是否满足工作世界状态 $WWS$。命中首个匹配方法后，记录分解历史（压入 `DecompHistory`），并将该方法的子任务集逆序或批量压入待处理任务栈顶。
- **遇到基元任务**：校验其前置条件是否满足 $WWS$。满足则将其加入最终计划列表（`FinalPlan`），并**立即将其副作用（Effects）应用到工作世界状态 $WWS$ 中**。
- **遭遇失败分支**：若某复合任务的所有方法均失效，或某基元任务前置条件不满足，立即触发状态回溯机制（Backtracking）。

```cpp
// HTN 核心前向深度优先求解算法
FWorldState WorkingWS = CurrentWorldState;
TStack<FTask> TasksToProcess;
TList<FPrimitiveTask> FinalPlan;
TStack<FDecompState> DecompHistory;

TasksToProcess.Push(RootTask);

while (!TasksToProcess.IsEmpty())
{
    FTask CurrentTask = TasksToProcess.Pop();

    if (CurrentTask.Type == ETaskType::CompoundTask)
    {
        // 查找当前 WorkingWS 下首个条件成立的方法
        FMethod* SatisfiedMethod = CurrentTask.FindSatisfiedMethod(WorkingWS);
        if (SatisfiedMethod != nullptr)
        {
            // 备份规划器上下文以备回溯
            RecordDecompositionOfTask(CurrentTask, FinalPlan, DecompHistory, WorkingWS);
            
            // 将子任务依次置入处理栈顶
            TasksToProcess.InsertTop(SatisfiedMethod->SubTasks);
        }
        else
        {
            // 当前复合任务分解穷尽且失败，回溯至上一有效决策节点
            if (!RestoreToLastDecomposedTask(TasksToProcess, FinalPlan, DecompHistory, WorkingWS))
            {
                // 回溯栈已空，全领域无解
                return EPlanResult::FailedNoPlanFound;
            }
        }
    }
    else // 基元任务处理分支
    {
        if (PrimitiveConditionMet(CurrentTask, WorkingWS))
        {
            // 虚拟执行：将预期副作用施加于推演世界状态上
            WorkingWS.ApplyEffects(CurrentTask.Effects);
            FinalPlan.PushBack(CurrentTask);
        }
        else
        {
            // 基元任务前置条件在模拟推演中破损，强行触发回溯
            if (!RestoreToLastDecomposedTask(TasksToProcess, FinalPlan, DecompHistory, WorkingWS))
            {
                return EPlanResult::FailedNoPlanFound;
            }
        }
    }
}

return EPlanResult::Success; // FinalPlan 中即为最终生成的基元动作序列
```

### 12.4.3 回溯机制与性能优势（State Rollback & Pruning Efficiency）

在 `RecordDecompositionOfTask` 中，规划器必须将当前的任务处理栈 `TasksToProcess`、最终计划列表 `FinalPlan`、被尝试的方法索引及当刻的 `WorkingWS` 打包成快照（Snapshot）置入 `DecompHistory`。一旦触发 `RestoreToLastDecomposedTask`，规划器弹出历史快照，还原现场，并指示该复合任务跳过失败的方法，尝试下一优先级的 Method 分支。

**为什么工业界 HTN 规划效率显著优于 GOAP？**
1. **强剪枝率**：复合任务的方法以领域知识为引导，在搜索树浅层即可直接剔除无关的庞大子树。
2. **零启发式开销**：传统状态搜索算法（A\*、Dijkstra）依赖优先队列维护 Open 表，其堆插入与重平衡带来大量内存与计算耗时（$O(\log N)$）。HTN 采用严格前向全序回溯（Total-Order DFS），其数据结构为简单的连续线性内存栈，缓存局部性极高。

---

## 12.5 计划的物理执行与运行时监控（Running the Plan）

生成任务序列后，控制权移交至计划执行器（Plan Runner）。执行过程并非盲目的线性调用，而是一套严密受控的状态推进闭环：

1. **算子驱动与真实世界状态同步**：
   执行器按序激发基元任务内的底层算子（如驱动寻路代理追踪目标）。当且仅当算子报告物理层执行成功时，执行器将该基元任务的副作用（Effects）真正施加到全局的 `CurrentWorldState`。

2. **异常熔断与动态重规划（Re-plan Execution）**：
   - **算子物理失败**：例如目标在突进过程中跳出导航网格（NavMesh），底层算子返回失败代码。执行器立即终止当前执行链，宣告计划崩溃，迫使 AI 规划器介入重规划。
   - **前置条件动态失效**：执行器维护一个同步监听机制，周期性校验后续基元任务的前置条件在实时世界状态下是否依然有效。一旦外部扰动导致前提被破坏，执行器主动熔断当前动作并触发重规划，保证角色的行为自洽。

---

## 12.6 复杂领域规划分解执行全景（Tracing Decomposition Walkthrough）

为深入剖析 HTN 分解过程中栈与回溯的具体工作机理，进一步扩充树干重击者（Trunk Thumper）的模型。我们将 `DoTrunkSlam` 提炼为复合任务，并提供“双重砸地（Double Slam）”与“跳跃砸地（Jump Slam）”两种备选形态：

```
[BeTrunkThumper] (Compound)
  |-- Method 0 (Combat): [NavigateToEnemy, DoTrunkSlam]
  \-- Method 1 (Patrol): [ChooseBridgeToCheck, NavigateToBridge, CheckBridge]

[DoTrunkSlam] (Compound)
  |-- Method 0 (Heavy): [WindupHeavy, SlamGround]
  \-- Method 1 (Quick): [QuickSlam]
```

### 规划栈演进追踪矩阵（Trace Matrix）

下表完整追踪了规划器从初始状态逐步分解至生成最终完整线性计划的迭代全生命周期：

| 步骤 (Step) | 操作行为 (Planner Action) | 处理任务 (CurrentTask) | 任务处理栈状态 (`TasksToProcess`) | 生成计划队列 (`FinalPlan`) | 推演工作状态 (`WorkingWS`) 变迁 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **0. Init** | 初始化推演栈 | - | `[BeTrunkThumper]` | `[]` | 复制真实世界状态快照 |
| **1. Decomp**| 分解根任务 (选择 Method 0) | `BeTrunkThumper` | `[NavigateToEnemy, DoTrunkSlam]` | `[]` | 记录状态至 `DecompHistory` |
| **2. Eval**  | 提取基元任务，条件成立 | `NavigateToEnemy` | `[DoTrunkSlam]` | `[NavigateToEnemy]` | 应用副作用：`WsLocation = EnemyLoc` |
| **3. Decomp**| 分解子复合任务 (选择 Method 0)| `DoTrunkSlam` | `[WindupHeavy, SlamGround]` | `[NavigateToEnemy]` | 记录状态至 `DecompHistory` |
| **4. Eval**  | 提取基元任务，条件成立 | `WindupHeavy` | `[SlamGround]` | `[NavigateToEnemy, WindupHeavy]` | 应用蓄力状态变化 |
| **5. Eval**  | 提取基元任务，条件成立 | `SlamGround` | `[]` (栈空) | `[NavigateToEnemy, WindupHeavy, SlamGround]` | 应用落地砸击副作用，标记攻击硬直 |
| **6. Finish**| 栈完全清空，求解收敛 | - | `[]` | **最终执行计划交付 Plan Runner** | 规划成功，耗时极低 |

---

## 12.7 关键架构决策与工业工程落地总结

1. **完全解耦逻辑空间与物理实现**：
   通过传感器将连续物理事实（坐标矢量、浮点距离）量化压缩为离散的世界状态枚举；通过算子将物理动作与抽象基元任务解耦。这使得规划算法可以在轻量级内存沙盒中快速运行，不受游戏场景复杂度影响。

2. **以层次分解消除排序开销**：
   HTN 在宏观上依赖设计师赋予的领域结构实现前向剪枝，在微观上通过线性全序遍历消除了复杂的动态启发式评分计算，兼具确定性行为控制与顶尖的执行性能。

3. **规划模拟与运行时双重保障**：
   规划期利用工作世界状态模拟前向动作的副作用，确保生成的计划在时序逻辑上自洽；运行时通过计划执行器监控算子反馈与环境扰动，提供毫秒级的中断响应与重规划保障。

---

在工业级游戏 AI 决策架构中，分层任务网络（Hierarchical Task Network, 简称 HTN）规划器以其强大的前向推理能力（Forward Reasoning）、确定性的任务分解结构以及优异的执行效率，成为现代 3A 动作与射击游戏（如《变形金刚：塞伯坦的陨落》、《地平线：零之曙光》等）的核心 AI 架构。本技术文档深入剖析 HTN 核心分解推导、递归模式、预期效果、优先级抢占机制以及并发多任务协同机制的工程实践。

---

## 1. 核心分解推导与工作世界状态（Decomposition & Working World State）

HTN 的核心执行机制由两部分组成：**规划期（Planning Time）** 与 **执行期（Execution / Runtime）**。

在规划期间，规划器从根复合任务（Root Compound Task）出发，依次遍历可选的方法（Method）。当某个方法的先决条件（Preconditions）在当前上下文得到满足时，该方法下的子任务序列将被压入分解任务列表（Decomposition Tasks List）中。

```
                    +--------------------------------+
                    | BeTrunkThumper (Compound Task) |
                    +--------------------------------+
                                   |
              +--------------------+--------------------+
              | Method 0                                | Method 1
       [WsCanSeeEnemy == true]                       [Default: true]
              |                                         |
     +--------+--------+                       +--------+--------+
     | NavToEnemy()    |                       | ChooseBridge()  |
     | DoTrunkSlam()   |                       | NavToBridge()   |
     | Recover()       |                       | CheckBridge()   |
     +-----------------+                       +-----------------+
              |
      (Decompose DoTrunkSlam)
              |
      +-------+-------+
      |               |
   Method 0        Method 1
    [Taunt,         [Tell,
   SuperSlam]       Slam]
```

### 1.1 规划器迭代轨迹（Planner Trace）

下表展示了从根任务开始，规划器在各迭代周期中如何逐步处理复合任务，并将基元任务（Primitive Task）转移至最终规划序列（Final Plan）：

| 迭代步数 (Step) | 最终规划序列（Final Plan） | 待处理任务队列（Tasks To Process） | 关键操作与世界状态演进（Action & World State Effects） |
| :--- | :--- | :--- | :--- |
| **0** | `[]` | `[BeTrunkThumper]` | 遍历 `BeTrunkThumper`，评估条件命中 `Method 0`。 |
| **1** | `[]` | `[NavToEnemy, DoTrunkSlam, Recover]` | 展开复合任务为子任务序列。 |
| **2** | `[NavToEnemy]` | `[DoTrunkSlam, Recover]` | `NavToEnemy` 为基元任务，移入 Final Plan，并向 Working World State 应用其效果。 |
| **3** | `[NavToEnemy]` | `[Tell, Slam, Recover]` | `DoTrunkSlam` 展开为其 `Method 1`（`Tell`, `Slam`）。 |
| **4** | `[NavToEnemy, Tell]` | `[Slam, Recover]` | `Tell` 弹出并追加至 Final Plan。 |
| **5** | `[NavToEnemy, Tell, Slam]` | `[Recover]` | `Slam` 弹出并追加至 Final Plan。 |
| **6** | `[NavToEnemy, Tell, Slam, Recover]`| `[]` | `Recover` 弹出，待处理任务队列为空，规划构建成功。 |

### 1.2 规划验证（Plan Validation）与工作世界状态的作用

基元任务必须将其修改的效果（Effects）实时应用到**工作世界状态（Working World State）**中。
* **原因**：后续任务的先决条件判断完全依赖前置任务执行后所产生的工作状态副本。
* **意义**：这一机制使 HTN 具备了前向推演与预判能力。若省略效果写入，HTN 仅能感知当前物理帧世界，退化为无记忆深度的反应式状态机（Reactive FSM）。

---

## 2. 利用递归提升领域表达力（Recursive Task Decomposition）

在战斗关卡设计中，常有资源耗尽（如武器损坏、耐久归零）后需要重新检索并获取资源、再返回攻击状态的复合需求。HTN 通过任务的**尾递归/结构递归（Recursive Decomposition）**能够优雅解决此问题。

### 2.1 递归领域结构定义

```
Compound Task [BeTrunkThumper]
    Method [ WsCanSeeEnemy == true ]
        Subtasks [ AttackEnemy() ]
    Method [ true ]
        Subtasks [ ChooseBridgeToCheck(), NavigateToBridge(), CheckBridge() ]

Compound Task [AttackEnemy]
    Method [ WsTrunkHealth > 0 ]
        Subtasks [ NavigateToEnemy(), DoTrunkSlam() ]
    Method [ true ]
        Subtasks [ FindTrunk(), NavigateToTrunk(), UprootTrunk(), AttackEnemy() ]

Primitive Task [DoTrunkSlam]
    Operator [ DoTrunkSlamOperator ]
    Effects  [ WsTrunkHealth += -1 ]

Primitive Task [UprootTrunk]
    Operator [ UprootTrunkOperator ]
    Effects  [ WsTrunkHealth = 3 ]

Primitive Task [NavigateToTrunk]
    Operator [ NavigateToOperator(FoundTrunk) ]
    Effects  [ WsLocation = FoundTrunk ]
```

### 2.2 递归图解与死循环规避

当树干耐久度 `WsTrunkHealth == 0` 时，复合任务 `AttackEnemy` 的第一方法不满足，掉入第二备选方法。

```
AttackEnemy (WsTrunkHealth == 0)
    |
    +---> FindTrunk()
    +---> NavigateToTrunk()
    +---> UprootTrunk() --------> [Effects: WsTrunkHealth = 3]
    +---> AttackEnemy() (Recursive Call)
              |
              +---> (WsTrunkHealth == 3 > 0) -> NavigateToEnemy(), DoTrunkSlam()
```

* **死循环致命风险**：若 `UprootTrunk` 未在 Working World State 中将 `WsTrunkHealth` 重置为大于 0 的数值，递归调用的 `AttackEnemy` 将永远选择备选分支，引发**规划器栈溢出或无限死循环（Infinite Planning Loop）**。
* **架构保障**：所有递归展开链路中，必须存在**基准情形转移（Base Case Transition）**，即前置任务的效果必须打破递归触发的先决条件。

---

## 3. 非任务控制的世界状态演进：预期效果（Expected Effects）

在动态游戏场景中，许多关键世界状态并不由 AI 的基元动作算子直接改写，而是由底层物理系统、碰撞引擎或感知组件（Sensory System，如视线检测 Line-Of-Sight）在异步运行时进行计算。

### 3.1 预估断言失效问题

当 Boss 丢失玩家视线并试图移动至玩家最后出现点，并在重获视线时播放特定咆哮动作：

```
Primitive Task [NavToLastEnemyLoc]
    Operator [ NavigateToOperator(LastEnemyLocation) ]
    Effects  [ WsLocation = LastEnemyLocation ]

Primitive Task [RegainLOSRoar]
    Preconditions [ WsCanSeeEnemy == true ]
    Operator      [ RegainLOSRoar() ]
```

**问题分析**：在规划期，当规划器尝试将 `RegainLOSRoar` 加入规划序列时，检测到 `WsCanSeeEnemy == false`。由于 `NavToLastEnemyLoc` 的普通效果仅包含位置变更，视线由视觉传感器决定，因此规划器在此处**先决条件校验失败，规划立即夭折**。

### 3.2 预期效果（Expected Effects）机制

引入 **Expected Effects** 语义：仅在**规划推演期（Planning Time）**和**重规划校验期（Plan Validation）**应用于 Working World State，而在真实物理执行期不产生直接的硬件状态写入，留由外部感知系统在执行期进行真实验证。

```
Primitive Task [NavToLastEnemyLoc]
    Operator        [ NavigateToOperator(LastEnemyLocation) ]
    Effects         [ WsLocation = LastEnemyLocation ]
    ExpectedEffects [ WsCanSeeEnemy = true ]
```

* **工业实战价值**：此特性在《变形金刚：塞伯坦的陨落》（Transformers: Fall of Cybertron）等大型项目中被高频应用。它赋予规划器在没有确定性物理执行结果介入前，**基于“行动预估（Reason about the future）”跨越感知断点**，规划出长周期的复杂行为链。

---

## 4. 运行时高优先级抢占机制（Handling Higher Priority Plans）

当世界状态突发改变（如玩家进入视野、受到突发伤害），AI 必须具备中断当前低优先级动作、抢占执行更高优先级动作的能力。但粗暴的重规划会导致严重的“动作抽搐”与逻辑冲突。

### 4.1 典型缺陷案例剖析

```
Compound Task [AttackEnemy]
    Method [ WsTrunkHealth > 0, AttackedRecently == false, CanNavigateToEnemy == true ]
        Subtasks [ NavigateToEnemy(), DoTrunkSlam(), RecoveryRoar() ]
    Method [ WsTrunkHealth == 0 ]
        Subtasks [ FindTrunk(), NavigateToTrunk(), UprootTrunk(), AttackEnemy() ]
    Method [ true ]
        Subtasks [ PickupBoulder(), ThrowBoulder() ]

Primitive Task [DoTrunkSlam]
    Operator [ DoTrunkSlamOperator ]
    Effects  [ WsTrunkHealth += -1, AttackedRecently = true ]

Primitive Task [RecoveryRoar]
    Operator [ PlayAnimation(TrunkSlamRecoverAnim) ]
```

* **Bug 场景**：在执行 `RecoveryRoar` 期间，环境触发重规划。规划器重新评估 `AttackEnemy`，由于 `DoTrunkSlam` 已经将 `AttackedRecently` 置为 `true`，导致第一高优先级 Method 不满足，向下穿透（Fall-through）匹配到了 `PickupBoulder()`，**正在播放的恢复咆哮动画被强制打断，直接变为扔石头**。此逻辑破坏了设计上留给玩家的攻击后摇硬直窗口。

---

### 4.2 决策优化权衡：图搜索成本对比方法遍历记录（MTR）

为解决计划优先级裁决问题，工业界通常在两种技术路线中抉择：

| 评估维度 | 路线 A：基于开销的图搜索（A* / Dijkstra Cost-based） | 路线 B：方法遍历记录（Method Traversal Record, MTR） |
| :--- | :--- | :--- |
| **工作原理** | 为每个基元任务或方法绑定浮点 Cost，规划为最小代价路径。 | 将展开所选的方法索引序列化为一个整型向量/记录，利用字典序比较优先级。 |
| **性能开销** | 高。需要维护开放集（Open Set）优先级队列并频繁排序，拖慢帧率。 | 极低。仅涉及整数数组的按位/分量顺序比对，运算时间复杂度为 $\mathcal{O}(D)$（$D$ 为任务树深度）。 |
| **调优难度** | 极高。Cost 权重微调极易引起隐蔽的逻辑死锁或全局规划漂移。 | 极低。直接基于设计人员在编辑器中对 Method 排布的自然顺序（In-order priority）。 |
| **工程推荐度** | 适用于全局战略级长距离调度。 | **3A 动作系统、战斗 NPC 行为规划的绝对首选。** |

---

### 4.3 方法遍历记录（Method Traversal Record, MTR）技术实战

MTR 是用于记录生成当前规划序列时，沿树形结构分解所遍历的所有复合任务的**方法索引号（Method Index）**组合。

```
              Trunk Thumper Domain
        +-------------------------------+
        | BeTrunkThumper                |
        | [0] Method 0 (Nav & Slam)     |
        | [1] Method 1 (Bridge Patrol)  |
        +-------------------------------+
                       |
        +--------------+----------------+
        |                               |
  (If Method 0)                   (If Method 1)
        |                               |
+-------+-------+                  Plan: [ChooseBridge, NavToBridge, CheckBridge]
| DoTrunkSlam   |                  MTR : [ 1 ] (Low Priority)
| [0] Method 0  |
| [1] Method 1  |
+-------+-------+
        |
  +-----+-----+
  |           |
Method 0    Method 1
[Taunt,     [Tell,
SuperSlam]  Slam]
  |           |
Plan:       Plan:
[Nav,Taunt, [Nav,Tell,
Super,Recv]  Slam,Recv]
MTR: [0, 0] MTR: [0, 1]
(Highest)   (Medium)
```

#### MTR 优先级判定数学模型
设当前运行中计划的遍历记录为向量 $\mathbf{M}_{curr} = [c_1, c_2, \dots, c_n]$，新提议计划的记录为 $\mathbf{M}_{new} = [p_1, p_2, \dots, p_m]$。对于分层决策结构，索引数值越小代表优先级越高（例如 index 0 代表高优先级特技，index 1 代表常规巡逻）。

两者的相对优先级由其**最长公共前缀之后的首个歧异项**决定：
$$k = \min \{ i \mid c_i \neq p_i \}$$
抢占生效的充要条件为：
$$p_k < c_k \quad (k \le \min(n, m))$$

#### MTR 在工程中的双重实现策略
1. **计划生成后比对（Post-Planning Comparison）**：
   规划器无视当前状态正常生成新规划，若 $\text{CompareMTR}(\mathbf{M}_{new}, \mathbf{M}_{curr}) > 0$，则执行打断抢占；否则丢弃。
2. **搜索时分支剪枝（Search-Time Branch Culling）**：
   在规划器下潜展开复合任务时，若发现当前选取的 Method 索引在对应层级劣于 $\mathbf{M}_{curr}$ 记录的索引，则**直接对整棵子树执行剪枝（Prune）**，大幅缩减无效推演耗时。

---

### 4.4 状态反馈诱发重规划震荡及其终极消除

**现象隐患**：若系统配置了“世界状态变化即触发重规划”，基元任务完成后主动写入 Effects 会立刻触发重规划，引发高优先级计划误抢占后续后摇动作。

```
+-----------------------------------------------------------------------------------+
| 方案缺陷演进流程                                                                  |
+-----------------------------------------------------------------------------------+
| 1. DoTrunkSlam 运行结束 -> 成功设置 WsPowerUp += 1                                |
| 2. 状态改变触发 Re-plan                                                           |
| 3. 若 WsPowerUp == 3，复合任务判定高优先级 Method 0 有效                          |
| 4. 规划器强行中止原计划中的 DoRecovery，直接衔接大招 Whirlwind                     |
| 5. 现象：AI 越过动画硬直直接打出不可阻挡的 Combo，玩家体验崩坏                    |
+-----------------------------------------------------------------------------------+
```

#### 终极解法：使用显式世界状态守护动作完整性（Explicit Guard State）

在数据领域模型中，将“为何需要 Recovery”转化为显式的世界状态（如疲劳度、失衡度）。

```
Compound Task [AttackEnemy]
    Method [ WsPowerUp == 3 ]
        Subtasks [ DoWhirlwindTrunkAttack(), DoRecovery() ]
    Method [ WsEnemyRange > MeleeRange ]
        Subtasks [ DoTrunkSlam(), DoRecovery() ]

Primitive Task [DoTrunkSlam]
    Operator [ AnimatedAttackOperator(TrunkSlamAnimName) ]
    Effects  [ WsPowerUp += 1, WsIsTired = true ]

Primitive Task [DoWhirlwindTrunkAttack]
    Preconditions [ WsIsTired == false ]   // 关键守卫：疲劳时无法施放连招
    Operator      [ DoWhirlwindTrunkAttack() ]
    Effects       [ WsPowerUp = 0 ]

Primitive Task [DoRecover]
    Operator [ PlayAnimation(TrunkSlamRecoveryAnim) ]
    Effects  [ WsIsTired = false ]         // 恢复动作完成，解除疲劳状态
```

通过引入 `WsIsTired` 约束：
1. 即使 `DoTrunkSlam` 结束后触发重规划，由于 `WsIsTired == true`，更高优先级的 Whirlwind 仍无法满足先决条件；
2. 保护了 `DoRecovery` 的必经执行路径；
3. 保留了领域原子任务的解耦性与模块复用性（Modularity），支持后续自由组合三连击、耐力消耗等复杂机制。

---

## 5. 并发行为管理架构（Managing Simultaneous Behaviors）

游戏实体常需要“一心多用”，例如下半身移动巡逻、上半身举盾防御，或边射击边后撤。在 HTN 范式中，管理并发行为主要有三种架构模式：

```
       模式 1：算子合并                  模式 2：双规划器解耦                 模式 3：后台化移动算子
 (Operator Combination)              (Dual-Planner System)           (Background Path Following)

+-----------------------+      +-------------+ +-------------+       +-------------------------+
|     MoveAndAim()      |      | Upper Body  | | Lower Body  |       | HTN: [Navigate]         |
|                       |      |   Planner   | |   Planner   |       |       (Returns Success) |
| (High Complexity,     |      +-------------+ +-------------+       +------------+------------+
|  Zero Reusability)    |             \               /                           | (Path Following in Bkg)
+-----------------------+              \             /               +------------v------------+
                                   +------------------+              | HTN Plans Face Shielding|
                                   | Shared Blackboard|              | while Navigating == true|
                                   +------------------+              +-------------------------+
```

### 5.1 架构路线对比

#### 模式 1：算子合并（Combine Multiple Operators）
* **实现**：编写统一的复合执行器（如 `NavigateAndShieldOperator`）。
* **缺陷**：组合爆炸，破坏原有算子复用性，维护成本剧增。

#### 模式 2：分身体双域双规划器架构（Dual-Domain Dual-Planner）
* **实现**：划分为上半身（Upper Body）与下半身（Lower Body）两个独立的 HTN 域，各自运行独立的 Planner。
* **同步手段**：通过共享黑板（Blackboard）写入状态同步信息。例如下半身执行任务时在黑板置位 `Navigating = true`，上半身以此为前置条件规划 `GuardFaceWithArm()`。

```
Compound Task [BeTrunkThumperUpper]
    Method [ WsHasEnemy == true, WsEnemyRange <= MeleeRange ]
        Subtasks [ DoTrunkSlam() ]
    Method [ Navigating == true, HitByRangedAttack == true ]
        Subtasks [ GuardFaceWithArm() ]
    Method [ true ]
        Subtasks [ Idle() ]

Compound Task [BeTrunkThumperLower]
    Method [ WsHasEnemy == true, WsEnemyRange > MeleeRange ]
        Subtasks [ NavigateToEnemy(), BeTrunkThumperLower() ]
    Method [ true ]
        Subtasks [ Idle() ]
```
* **缺陷**：
  1. CPU 开销成倍上升（每个 NPC 需要驱动多个 Planner 实例）；
  2. 两个独立的图状态机可能产生严重的异步竞态（Race Condition）与调试灾难。

#### 模式 3：后台化瞬时导航算子（Backgrounding Path Following）——工业界最佳实践
* **核心机制**：传统寻路基元任务在到达目标点后才返回 `Success`；本方案中，`NavigateToEnemy` 算子仅负责向寻路组件提交目标路标，**并立即向规划器报告执行成功（Complete Immediately）**。真实的位置推演与寻路循迹委托给底层寻路引擎在后台执行。
* **规划流**：规划器从长生命周期的等待中解放，在寻路未终止期间，系统世界状态维持 `Navigating == true` 以及 `DistanceToDestination` 动态更新，使得规划器可以在单规划域下流畅规划并发防御、上半身举盾、视角观察等任务。

```
Compound Task [BeTrunkThumper]
    Method [ WsHasEnemy == true, WsEnemyRange <= MeleeRange ]
        Subtasks [ DoTrunkSlam() ]

    Method [ WsHasEnemy == true, WsEnemyRange > MeleeRange ]
        Subtasks [ NavigateToEnemy() ]  // 发起循迹立即返回成功

    Method [ Navigating == true, HitByRangedAttack == true ]
        Subtasks [ GuardFaceWithArm() ] // 移动时受到远程攻击，无缝并发防御

    Method [ true ]
        Subtasks [ Idle() ]
```

该模式在保证**单规划器确定性、高性能、无并发死锁**的同时，达成复杂的全身/局部并发协调行为。

---

在现代 AAA 级游戏 AI 架构设计中，决策规划器（Planner）的实时性与算力开销往往是决定架构成败的关键瓶颈。分层任务网络（Hierarchical Task Networks, HTN）不仅具备强大的行为抽象与模块化能力，更因其前向分解的计算特性，为规划器的高性能优化——特别是**部分规划（Partial Planning）**——提供了坚实的理论支撑。

---

## 1. 架构回顾：从双领域并发到单领域状态解耦

在复杂的 NPC（如近战巨怪 Trunk Thumper）行为设计中，针对耗时性长程动作（如导航、寻路）的处理存在两种典型的架构模式：

```
模式 A：双领域并发架构 (Dual Domain Approach)
┌─────────────────────────┐          ┌─────────────────────────┐
│     Combat Planner      │          │   Navigation Planner    │
│ (高层决策: 攻击/防御等)  │◄────────►│ (低层决策: 寻路/避障等) │
└─────────────────────────┘ 同步状态 └─────────────────────────┘
                                       (WsNavigating == true)

模式 B：单领域状态解耦架构 (Single Domain with Background Execution)
┌──────────────────────────────────────────────────────────────┐
│                    Single HTN Domain                         │
│                                                              │
│  [NavigateToEnemy]  ───(Effects: Navigating = true)───►      │
│  [GuardFaceWithArm]                                          │
│  [Idle]                                                      │
│                                                              │
│  * 路径跟随行为在后台执行，世界状态直接反映其执行进度        │
└──────────────────────────────────────────────────────────────┘
```

### 1.1 单领域状态映射定义
在单领域设计中，图元任务（Primitive Task）直接将底层运动控制系统的执行状态映射至规划器的世界状态中：

```text
Primitive Task [GuardFaceWithArm]
    Operator [GuardFaceWithArmOperator]

Primitive Task [NavigateToEnemy]
    Operator [NavigateToOperator(Enemy)]
    Effects  [Navigating = true]

Primitive Task [Idle]
    Operator [IdleOperator]
```

* **架构权衡（Trade-offs）**：
  * **双领域架构**：需要两个规划实例独立运行，通过共享世界状态属性（如 $WsNavigating$）维系同步机制，极易因异步时序差引入不可控的竞态条件（Race Conditions）。
  * **单领域解耦架构**：消除了对双规划器并发运行的依赖，仅运行单一规划器，将寻路与长程位移状态内嵌于统一的世界状态模型中，大幅降低了系统维护与状态同步的复杂度。

---

## 2. 利用部分规划（Partial Planning）加速 HTN

当 NPC 的行为领域（Domain）膨胀为一个大型网络，即使对规划算法本身（如内存池化、位掩码世界状态匹配）完成了深度底层优化，每帧或每次规划开销仍可能消耗数毫秒计算预算。此时必须从规划算法的搜索机制层面实施空间剪枝与截断。

### 2.1 规划搜索范式对比：HTN vs. GOAP / STRIPS

| 维度 | HTN 规划器（前向分解 / 搜索） | GOAP / STRIPS 变体（后向回归搜索） |
| :--- | :--- | :--- |
| **搜索方向** | **从当前世界状态前向展开** ($S_0 \to S_{target}$) | **从期望目标状态后向搜索** ($G \to S_0$) |
| **计划完整性要求** | **支持部分展开**（Partial Planning，仅规划前 $k$ 步） | **必须全量展开**（必须搜通到初始状态以确定第 1 步） |
| **剪枝能力** | 复合任务的 Method 先验条件实现指数级分枝剪枝 | 依赖启发式函数（$A^*$ / $IDA^*$），在大状态空间易爆炸 |
| **动态适应性** | 极高：对易变环境仅需做出短期决策 | 较低：耗时规划的完整长序列极易因动态事件失效中途作废 |

在基于目标驱动的规划器（GOAP/STRIPS）中，由于算法采用从目标向起点的逆向搜索（Backward Search），系统在未搜索到当前世界状态（Current World State）前，根本无法获知执行序列的第一步动作是什么。

而 HTN 采用**前向分解（Forward Decomposition）**，规划器始终立足于当前世界状态 $S_{current}$，在时间轴上向未来推演。这赋予了规划器只规划前瞻若干步骤、延迟后续决策的天然能力。

---

## 3. 部分规划的工程实现模式

### 3.1 领域手动拆分模式（Domain-Authoring Split）

通过拆分高阶复合任务的 Method，使其只生成当前紧迫任务，而非生成跨越较长时间的完整序列。

#### 原始全量规划领域（Full Plan Domain）
```text
Compound Task [BeTrunkThumper]
    Method [WsCanSeeEnemy == true]
        Subtasks [NavigateToEnemy(), DoTrunkSlam()]

Primitive Task [DoTrunkSlam]
    Operator [DoTrunkSlamOperator]

Compound Task [NavigateToEnemy]
    Method [...]
        Subtasks [...]
```
* **缺陷**：由于 $NavigateToEnemy$ 是耗时操作，过早规划 $DoTrunkSlam$ 毫无意义——在导航执行的数秒内，敌人的位置、血量、甚至 NPC 自身的存活状态极可能发生巨变，导致后半段计划完全作废。

#### 手动拆分后的部分规划领域（Partial Plan Domain）
通过引入世界状态空间约束，将全量 Method 分离为按时空优先级排序的独立 Method：

```text
Compound Task [BeTrunkThumper]
    Method [WsCanSeeEnemy == true, WsEnemyRange > MeleeRange]
        Subtasks [NavigateToEnemy()]

    Method [WsCanSeeEnemy == true]
        Subtasks [DoTrunkSlam()]

Primitive Task [DoTrunkSlam]
    Operator [DoTrunkSlamOperator]

Compound Task [NavigateToEnemy]
    Method [...]
        Subtasks [...]
```

* **执行流转逻辑**：
  1. 当敌人远在攻击范围之外时，命中高优先级的 Method 1，规划器仅生成 $[NavigateToEnemy()]$ 这个部分计划。
  2. 实体执行导航移动，世界状态中的 $WsEnemyRange$ 逐步衰减。
  3. 当逼近到 $MeleeRange$ 以内时，触发规划器重新规划（Replan）。
  4. 此时由于不满足 $WsEnemyRange > MeleeRange$，跳过 Method 1，命中 Method 2，生成攻击动作 $[DoTrunkSlam()]$。
* **设计前置条件**：该拆分方式**必须依赖能够有效区分各切片阶段的世界状态（World State Property）**（如 $WsEnemyRange$ 与阈值 $MeleeRange$）。

```
        ┌──────────────────────────────────────┐
        │  Method 1: WsEnemyRange > MeleeRange │
   ┌───►│  Plan: [NavigateToEnemy()]           │
   │    └──────────────────┬───────────────────┘
   │                       │ 执行位移, 改变物理位置
   │                       ▼
   │    ┌──────────────────────────────────────┐
   │    │  Method 2: WsCanSeeEnemy == true     │
   └───┤  Plan: [DoTrunkSlam()]                │
        └──────────────────────────────────────┘
```

---

### 3.2 自动化部分规划（Automated Partial Planning）与其缺陷

为了避免领域设计者（策划/AI 程序员）手动拆解成百上千个复合任务，一种看似优雅的技术方案是在引擎底层实现基于时间阈值的**自动化规划截断**。

#### 机制设计：时间预算截断
向每个图元任务赋予其预估耗时 $t(Task_i)$，规划器在维护分解队列时记录累积推演时间：
$$T_{planned} = \sum_{i=1}^{k} t(PrimitiveTask_i)$$
一旦 $T_{planned} \ge T_{threshold}$，规划器立刻停止向更深层次继续分解，截取当前生成的序列作为部分计划输出。

#### 隐蔽 Bug 案例分析：破坏长程前置条件校验（Precondition Validation Breakdown）
考虑以下包含资源约束的领域结构：

```text
Compound Task [BeTrunkThumper]
    Method [WsCanSeeEnemy == true]
        Subtasks [NavigateToEnemy(), DoTrunkSlam()]

Primitive Task [DoTrunkSlam]
    Preconditions [WsStamina > 0]
    Operator [DoTrunkSlamOperator]

Compound Task [NavigateToEnemy]
    Method [...]
        Subtasks [...]
```

当运行自动化时间截断时，系统将遭遇致命的逻辑漏洞：
1. 规划器分解完 $NavigateToEnemy()$ 时，累积时间 $T_{planned}$ 突破阈值，规划强行截断并交付执行。
2. 此时世界状态中 $WsStamina = 0$。
3. 实体开始走向敌人；但到达后，因体力不足，根本无法执行 $DoTrunkSlam()$。
4. **根因**：全量规划原本拥有前向推演验证整个行为链条有效性的能力（若无法攻击，在规划期就会放弃导航而选择其他备选策略）；**自动化部分截断抹杀了规划器校验后续图元任务合法性的能力**，进而产生微妙且极难复现的幽灵 Bug（Subtle Bugs）。

---

### 3.3 部分计划的续接挑战（Continuation Mechanisms）

当部分计划的第一阶段执行完毕后，系统面临如何安全续接后续行为的关键问题。

#### 方案一：从根节点重规划（Replan from Root）
* **流程**：任务队列清空后，重新将根复合任务推入规划器推演。
* **缺陷**：要求领域作者必须编写高优先级的分支 Method（例如判定是否已经处于近战距离），否则 NPC 会反复重新导航。然而，**如果需要手动在领域中添加这些防御性状态与方法分支，自动化部分规划便失去了其省去人工干预的初衷**。

#### 方案二：未处理任务列表持久化（Recording the Unprocessed List）
* **流程**：当时间截断发生时，规划器将当前尚未遍历与展开的“待处理任务列表”（Unprocessed Task List / Open List）序列化并挂载到黑板（Blackboard）中。
* **续接原理**：在下一轮规划时，不再以单个 Root Task 初始化，而是将上次保留的待处理列表作为种子任务队列注入规划器。
* **不可逆风险（Irreversible Execution）**：在复杂动作博弈中，系统无法回滚已经交付到世界中执行的第一段物理表现。如果在第二段展开时发生 Method 全部失败（Decomposition Failure），系统由于已发生不可逆的物理状态变更，将直接处于无合法应对策略的严重悬空异常（Zombie State）。

---

## 4. 工业界落地经验与权衡：《变形金刚：塞伯坦的陨落》最佳实践

High Moon Studios 在开发 AAA 级大作《变形金刚：塞伯坦的陨落》（*Transformers: Fall of Cybertron*）的过程中，提炼出了极具实战指导意义的工程哲学：

```
                    工业级 HTN 规划架构权衡矩阵
  ┌─────────────────────────────────────────────────────────────┐
  │ 方案 A: 自动化黑盒时间截断规划                               │
  │   - 优势: 领域编写者无需考虑任务拆分                        │
  │   - 劣势: 破坏前瞻验证、破坏状态一致性、极易引入幽灵 Bug   │
  │                                                             │
  │ 方案 B: 领域原生内建部分规划 (Domain-Built Partial Plans)   │
  │   - 优势: 逻辑严密可控、确定性强、天然避开状态死锁          │
  │   - 劣势: 需要在策划层对状态划分建立清晰认知                │
  │                                                             │
  │              ★ 最终工业界架构选型: 方案 B                   │
  └─────────────────────────────────────────────────────────────┘
```

1. **全面放弃自动化隐式截断**：由于自动化部分规划破坏长程前置条件验证的风险极高，团队全面转向**直接在领域内设计部分规划（Built the partial plans directly into the domains）**。
2. **长程行为天然切片**：在无须进行严苛全量计划验证的场景下，将长耗时导航与即时战斗决策在 Method 层解耦，把导航行为本身作为一种天然的部分规划单元。
3. **前瞻推演的不可替代性**：HTN 的核心价值在于**对未来的推演与推理（Reason about the future）**，这是有限状态机（FSM）或基础行为树（Behavior Tree）无法比拟的表现力源泉。以规范的领域逻辑驾驭前向搜索，才能在毫秒级预算约束下构建出高度拟真、严谨且高鲁棒性的工业级 NPC AI。

---

## 5. 核心参考文献

* **[Erol et al. 94]** K. Erol, D. Nau, and J. Henler, *"HTN planning: Complexity and expressivity."* AAAI-94 Proceedings, 1994.
* **[Erol et al. 95]** K. Erol, J. Henler, and D. Nau, *"Semantics for Hierarchical Task-Network Planning."* Technical report TR 95-9. The Institute for Systems Research, 1995.
* **[Ghallab et al. 04]** M. Ghallab, D. Nau, and P. Traverso, *Automated Planning*. San Francisco, CA: Elsevier, 2004, pp. 229–259.
* **[HighMoon 10]** *Transformers: War for Cybertron*, High Moon Studios/Activision Publishing, 2010.
* **[HighMoon 12]** *Transformers: Fall of Cybertron*, High Moon Studios/Activision Publishing, 2012.
* **[Orkin 04]** Jeff Orkin, *"Applying goal-oriented action planning to games."* In *AI Game Programming Wisdom 2*, edited by Steve Rabin. Hingham, MA: Charles River Media, 2004, pp. 217–227.
