---
type: Reference
title: "第10章 AI-Driven Autoplay Agents for Prelaunch Game Tuning"
description: "Game AI Pro 工业级精读：AI-Driven Autoplay Agents for Prelaunch Game Tuning。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第10章 AI-Driven Autoplay Agents for Prelaunch Game Tuning

> 来源：*Game AI Pro 4 (Online Edition 2021)*, Chapter 10.  
> 原文作者 / 资源：[AI-Driven Autoplay Agents for Prelaunch Game Tuning](http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter10_AI-Driven_Autoplay_Agents_for_Prelaunch_Game_Tuning.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷4-OnlineEdition2021](README.md) ｜ [专栏首页](../README.md)

---

*(AI-Driven Autoplay Agents for Prelaunch Game Tuning)*

---

### 作者与技术背景说明
* **作者**：Igor Borovikov（Electronic Arts / EADP AI Applications 研发组）
* **出处**：《Game AI Pro 4: Agent-Based Testing and Prelaunch Tuning》（工业界权威游戏 AI 系列专著）
* **核心定位**：在游戏进入公开玩家测试（Playtests）前，利用 AI 驱动的自动游玩代理（Autoplay Agents，以下简称“代理”）对玩家养成进度（Player Progression）、游戏内经济（In-Game Economy）、战斗平衡（Combat Balance）、死锁状态与机制漏洞（Exploits and Locked States）进行全自动化定量评估与调优。

---

## 1. 概述与核心范式转换 (Introduction & Paradigm Shift)

### 1.1 传统游戏调优的痛点与局限
在传统游戏工程管线中，设计调优（Design Tuning）极度依赖内部开发人员的“人工试玩测试”（Manual Playtesting）。这种工作流存在严重的工业瓶颈：
* **样本规模小且带有认知偏差（Biased & Low Scale）**：早期参与测试的开发者数量极少，由于对底层机制过度熟悉，其行为模式无法代表广域真实玩家。
* **时间成本极高**：例如在大型 4X 策略游戏（如《席德·梅尔的半人马座阿尔法星》(Sid Meier's Alpha Centauri, SMAC)）或长线移动养成游戏中，单次完整通关测试需要数天甚至数周的人力投入。
* **主观定性多于客观定量**：人类玩家的反馈通常是离散、主观且模糊的，难以直接映射到底层微观数值资产（如体力消耗曲线、产出/消耗比）的精细化迭代中。

### 1.2 代理模拟：从开发补丁转向设计核心 (Agents-as-a-Starting-Point)
EA 研发团队（EADP AI Applications）提出了一种核心理念革新：**代理模拟应该作为策划人员的主要工具，以及任何游戏研发流程的初始出发点（Primary tool and starting point）**。

其本质类似于工业软件工程中的**测试驱动开发（Test-Driven Development, TDD）**，但在游戏架构中提升到了更高的抽象维度——由数值策划与系统策划驱动“变更-验证”（Change-Validate）循环。在无需依赖高精细度渲染管线、物理引擎全精度模拟或人类 UI 呈现的前提下，直接基于逻辑内核验证玩法机制。

```
                    [ 传统模式 (Retrofitting AI) ]
核心玩法代码构建 ──> 视觉/UI开发 ──> 内部人工试玩 ──> 发现系统级数值失衡 (高修复成本)

                    [ 代理驱动模式 (Agent-Centric TDD) ]
游戏数学模型建立 ──> 挂载自动游玩代理 ──> 大规模并行仿真 ──> 量化暴露漏洞 ──> 自动化参数闭环调优
```

---

## 2. 方法论与智能体策略谱系 (Methodology & Agent Taxonomy)

### 2.1 游戏内代理与外部剥削性机器人的本质区别
在计算博弈论与实际生产中，存在两类截然不同的代理交互接口：

| 维度 | 传统学术竞赛/剥削性外挂 (Exploitative Bots / Deep RL) | 游戏开发期接入的集成代理 (Instrumented Autoplay Agents) |
| :--- | :--- | :--- |
| **交互接口** | 视口像素缓冲（Render Buffer）、深度缓冲、模拟屏幕点击/手柄轴量输入 | 直接暴露的内部 RPC 协议、状态黑板（Blackboard）与马尔可夫决策过程接口 |
| **感知瓶颈** | 必须耗费海量算力先进行图像特征提取（CNN/视觉语义分割），学习难度极高 | 零感知延迟，直接访问全局底层结构化游戏状态 $S$ 及有效动作集 $\{A_i\}$ |
| **核心目的** | 追求在对抗中最大化胜率（如 AlphaGo, Deep Blue）或自动化套现/刷资源 | **量化玩法体验、探测设计漏洞、平衡资源生态、测量养成斜率** |

### 2.2 玩家画像与代理控制策略谱系 (Player Profiling via Agent Policies)
通过部署处于不同智能谱系的控制策略（Control Policies），开发者能够对发售后不同画像的玩家群体进行行为仿真与宏观推断：

1. **均匀随机代理 (Uniform Random Autoplay Agents)**：
   * **行为机理**：在任意状态 $S_t$ 下，从所有合法动作集 $A_{\text{valid}}(S_t)$ 中进行均匀离散采样：
     $$\pi_{\text{random}}(a | s) = \frac{1}{|A_{\text{valid}}(s)|}$$
   * **价值**：作为工程底线基准测试（Sanity Check）。虽然动作完全失真，但其呈现的**相对难度趋势（Relative Difficulty Trends）**与近最优代理呈现极高的相关性，能以极低成本发现代码死锁（Deadlocks）与无路可退的状态。
2. **贪心/活动驱动代理 (Greedy / Activity-Driven Agents)**：
   * **行为机理**：基于短视（Myopic）或浅层启发式（Heuristics）选取单步收益最大化的动作。
   * **模拟画像**：对应游戏中占大多数的泛用户/轻度休闲玩家（Casual Players）。可精准提供平均推进速度、常规留存瓶颈与浅层资源消耗率。
3. **最优性驱动/漏洞探测代理 (Performance-Driven / Exploitative Agents)**：
   * **行为机理**：利用强化学习（Reinforcement Learning, RL）或黑盒随机全局优化算法，在多阶段决策问题中寻找最优解。
   * **模拟画像**：硬核玩家（Min-Maxers / Hardcore Players）。专门暴露游戏机制设计缺陷（Loopholes），如不合理的数值刷取循环、派系（Faction）或作战单位（Combat Units）的不平衡强度压制。

---

## 3. 插桩游戏客户端架构设计 (Instrumented Game Client Architecture)

为了使代理能够高效驱动游戏，客户端必须进行**插桩（Instrumentation）**设计。在工程实现上，存在两种截然不同的架构选型路线。

### 3.1 客户端内嵌代理模式 (Integrated Autoplay Agents)
* **实现方式**：自动游玩逻辑与游戏业务代码共同编译在同一个客户端二进制文件（Executable）中。
* **优势**：开销小，无网络 IPC/RPC 序列化损耗；原生契合游戏内提示生成器（Hint Generator）或简单离线单机验证。
* **缺陷**：
  * **实验效率低**：代理策略逻辑的任何迭代均需要重新编译客户端，严重受制于主干分支的生产发布流（Studio Production Pipeline）。
  * **伸缩性受限（Scale Limitation）**：目标平台（如真机移动端 iOS/Android）极难横向弹性扩展千百个实例进行无头渲染（Headless）大规模压测。
  * **高阶 RL 接入困难**：Python 生态中的主流科学计算与强化学习框架（PyTorch, Ray, TF）难以直接桥接至移动端 C++/C# 运行时。
* **实战案例（Match-3 消除手游）**：开发团队以内嵌模式快速落地了动态难度调整（Dynamic Difficulty Adjustment, DDA）的关卡分级。研发中获得了一个关键洞见：**消除类游戏中不同关卡的“相对难度排序”对自动游玩的启发式算子不敏感**——即使是完全随机点击的代理所测得的关卡相对难度斜率，也与高阶近最优求解代理测出的曲线高度一致。

### 3.2 外部驱动分离架构 (External Driver Architecture)
对于高复杂度、长周期的游戏，必须采用将游戏客户端（环境模拟器）与代理决策端（驱动器）进行物理与网络解耦的方案。

#### 3.2.1 拓扑架构与数据流 (Information and Control Flow)
客户端暴露出底层的马尔可夫决策过程（Markov Decision Process, MDP）接口，外部驱动端通过轻量级网络套接字（Sockets）进行通信。

```
┌──────────────────────────────────────────────────────────────────┐
│              插桩游戏客户端 (Instrumented Game Client)               │
│                                                                  │
│  ┌─────────────────────────┐         ┌────────────────────────┐  │
│  │ 玩法逻辑与策划数值配置    │         │      游戏 MDP 接口      │  │
│  │ (Gameplay Logic &       │ ──────> │ (Game MDP Interface)   │  │
│  │  Tuning Data JSON/XML)  │         │                        │  │
│  └─────────────────────────┘         └────────────────────────┘  │
└───────────────────────────────────────────┬───────────────▲──────┘
                                            │               │
                             状态更新与合法动作集 │               │ 动作派发
                                S, {Ai}     │               │ Action Aj
                                            │               │
┌───────────────────────────────────────────▼───────────────┴──────┐
│                    外部驱动程序 (External Driver)                 │
│                                                                  │
│  ┌─────────────────────────┐         ┌────────────────────────┐  │
│  │ 学习算法与全局优化器     │ ──────> │ 控制策略 (强化学习/    │  │
│  │ (Learning & Blackbox    │         │ 启发式/效用系统)       │  │
│  │  Stochastic Opt.)       │         │ (Control Policy)       │  │
│  └─────────────────────────┘         └────────────────────────┘  │
└──────────────────┬───────────────────────────────────────────────┘
                   │ 遥测数据落盘 (Logs)
                   ▼
     ┌───────────────────────────┐
     │ 结构化数据库与报表看板     │
     │ (Agent Logs, Reports &    │
     │  Dashboards)              │
     └─────────────┬─────────────┘
                   │
                   │ 自动化参数回流 / 策划手工调优
                   ▼
     [ 驱动数值策划迭代修改 Tuning Data ]
```

#### 3.2.2 客户端轻量非破坏性插桩代码实现
插桩的核心准则在于：**非破坏性（Non-Destructive）**。必须完整保留原有的输入采集、主事件泵（Event Pump）与生命周期，并通过预编译指令支持发售版本的无残留剥离。

```cpp
void Game::update(float deltaTime) // 每帧主更新循环
{
    // 1. 常规生命周期调用：处理网络底层、原生用户触屏输入等
    ProcessNativeInput();
    ProcessPlatformEvents();
    
    // ... 正常游戏逻辑推进 ...

#ifdef AGENTS
    // 2. 外部代理 RPC 通信与动作反序列化
    // 说明：RequestAction() 不传递 *this，实现高度抽象与依赖倒置，解耦 Game 类内部依赖
    const Action& action = ExternalAgentDriver::RequestAction();
    
    switch (action.id)
    {
        case ActionId::EXECUTE_INTERACTION:
            // 直接下发至游戏低阶 API，或者入队内部事件队列 (Event Queue)
            InteractionManager::Execute(action.targetId, action.param);
            break;
            
        case ActionId::NAVIGATE_TO_ROOM:
            NavigationSystem::MoveTo(action.targetLocation);
            break;
            
        case ActionId::IDLE_PUMP:
            // 维持帧率，确保物理/动画逻辑正常流动，等待下一决策周期
            break;
            
        default:
            HandleSpecialAction(action);
            break;
    }
#endif // AGENTS

    // 3. 正常视口更新与动画状态机步进
    TickSubsystems(deltaTime);
}
```

#### 3.2.3 关键工程设计决策与权衡 (Engineering Decisions & Trade-offs)

##### A. 状态传递策略：轻量增量更新 (Delta Updates) vs 全局快照 (Full Snapshot)
* **方案权衡**：传输包含数万实体的完整状态快照（State Snapshot）开销极大。工程上普遍采用**增量状态更新（Delta Updates）**结合**初始基准同步**的方案。
* **工程陷阱**：增量同步要求外部驱动器必须自己维护一份游戏状态机镜像，如果出现丢包或逻辑偏差，外部状态会发生偏航（Drift）。
* **EA 的落地方案**：将**遥测系统（Telemetry）机制与轻量快照混合**。在传递轻量增量状态（如 `"decrease health by 1"`）的同时，周期性混入关键断言字段（Sanity State Bits）进行一致性校验。自定义通信数据包直接以 JSON 格式旁路插入外部数据库（如 PostgreSQL/ClickHouse），避免后处理流水线开销。

##### B. 合法动作空间推断 (Action Space Inference)
* **工程难题**：在任意状态 $S_t$，理论上可执行的动作集合巨大，但实际场景中多数动作在当前上下文是非法的（例如：没有足够金币购买某项装扮、角色在当前房间无法与另一个房间的家具交互）。若依赖 UI 组件的活跃态来决定合法动作，会导致 AI 强耦合于易变的 UI 层。
* **解耦方案**：
  1. **元数据驱动推导**：驱动端解析存储在游戏资源中的静态数据资产（Data Assets，如 XML/JSON Tuning Data），结合当前状态直接推导出合法的动作空间 $A_{\text{valid}}(S_t)$。
  2. **容错与惩罚闭环（Fail-Safe）**：当代理发送了当前帧非法的动作指令时，客户端绝不可崩溃（Crash），而应返回带错误码的执行失败反馈（Execution Failure Signal），外部强化学习策略将其视为一次巨大惩罚或无效动作，辅助策略加速收敛。

##### C. 语言与通信解耦 (Language Decoupling)
* 客户端使用 C++ / C# 保证游戏逻辑的高性能运行。
* 外部驱动采用 **Python** 编写，通过 TCP/IPC Sockets 进行通信。充分利用 Python 生态快速接入数值拟合算法、数据库 ORM 以及强化学习框架，平衡工程迭代效率与运行时性能。

---

## 4. 工业级实战案例：以《模拟人生移动版》(The Sims Mobile, TSM) 为例的数值调优

### 4.1 离散状态空间建模与挑战
早期版本的《模拟人生移动版》（The Sims Mobile）具有明显的类棋盘离散状态特征：
* **核心机制**：玩家通过点击屏幕进行原子交互（Atomic Interactions）。游戏推进的核心之一在于**人际关系等级提升（Relationship Tracks）**。
* **多分支路径**：提供三条平行的人际关系分支（如：友谊线 Friends、宿敌线 Rivals、浪漫线 Romantic Interest），每条分支划分为 5 个阶段（例如浪漫线的 5 个阶段：从初识到 Sweethearts、Lovers、True Loves，直至 Soulmates）。
* **事件机制（Events）**：进入下一阶段需要成功挑战对应的人际事件。事件由一系列消耗游戏内资源（体力、时间）的动作序列组成。若玩家在资源不足时盲目开启事件，会导致事件失败（Fail），造成资源和进度的大幅惩罚。因此，合理选择进入事件的时机需要一定的博弈策略。

### 4.2 核心设计问题：收益平衡与相对难度不变量
在游戏数值设计中，核心设计目标通常是：**多条路径的“单位点击收益”（Benefit per tap）或通关所需的期望点击次数是否均衡且渐进上升？**

如果某一条路径的“收益/点击”性价比过高，真实玩家群体将在发售后迅速察觉，导致极端玩家行为聚集（Skewed Player Distribution），使得其他分支的美术、动画、剧情文本等研发资产被闲置浪费。

```
[ 策划预期理想曲线 ]
阶段点击数 (难度)
   ▲                                   浪漫线 (Romantic)
   │                                  /
   │                                 /
   │                    友谊线 (Friends)
   │                   /           /
   │                  /           /
   │                 /           /
   │  阶段 1        阶段 2       阶段 3       阶段 4       阶段 5
───┴──────────────────────────────────────────────────────────► 关系进阶阶段
```

在测试中，研发团队发现并验证了**难度相对性不变量（Relative Difficulty Invariance）**：
* 尽管随机代理（Random Agent）所消耗的绝对点击次数显著高于真实人类（或最优策略），但在不同分支、不同阶段之间，**随机代理测出的相对难度比例（Relative Ratios）与高级强化学习策略测出的相对难度具有高度的一致性**。
* 这一性质使得开发团队可以在极早期直接使用启发式或近随机代理，快速扫平明显的曲线畸变。

### 4.3 效用系统策略学习与黑盒优化 (Utility-Based Policy via Black-Box Optimization)

为了获得更接近高水平人类玩家的绝对参考数值，团队采用了基于效用系统的决策代理，并利用并行云节点进行参数优化。

#### 4.3.1 效用函数数学建模
设定代理是否在状态 $s$ 触发事件决策的效用函数（Utility Function）由系统关键特征变量的线性加权或非线性组合构成：

$$U(s) = \sum_{k=1}^{K} w_k \cdot f_k(s)$$

其中：
* $f_k(s)$ 为游戏核心状态特征（例如：当前 Sims 角色当前的能量储备值百分比、当前人际关系点数余量、事件失败惩罚权重等标准化变量）。
* $w_k$ 为待学习的权重参数向量（Weight Vector） $\mathbf{w} = [w_1, w_2, \dots, w_K]^T$。
* 决策规则：当 $U(s) > \tau$（阈值）时，代理执行 `START_EVENT` 动作，否则执行资源积攒或等待动作。

#### 4.3.2 黑盒随机优化训练
由于移动游戏客户端包含复杂的游戏逻辑分支，状态转移概率矩阵难以显式写出，因此采用无导数（Derivative-Free）的黑盒随机优化算法（如并行交叉熵方法 Cross-Entropy Method, 协方差矩阵自适应演化策略 CMA-ES, 或并行遗传算法）：

$$\mathbf{w}^* = \arg\max_{\mathbf{w}} \mathbb{E}_{\tau \sim \pi_{\mathbf{w}}} \left[ \sum_{t=0}^{T} R(s_t, a_t) \right]$$

其中 $R$ 为衡量事件快速推进、高成功率与低无效点击的复合奖励函数。

```
[ 效用策略黑盒优化收敛曲线 ]
最优性能比率 (Fraction of Optimal Performance)
1.2 ┼ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - (理论最优上限 1.0)
    │
1.0 ┼                                      ┌─────────────── (Max=0.938)
    │                                     ┌┘
0.8 ┼                                    ┌┘
    │                        ┌───────────┘
0.6 ┼                       ┌┘
    │                      ┌┘
0.4 ┼        ┌─────────────┘
    │  ┌─────┘
0.2 ┼──┘
    │
0.0 ┼───────┬─────────┬─────────┬─────────┬─────────┬─────────┬───────►
    0      100       200       300       400       500       600    700 
                                                         迭代轮数 (Episodes)
```

* **收敛性**：在多个云节点上进行分布式并行执行，在仅 **700 个轮次（Episodes）**内，策略性能达到了基准最优解的 **93.8%**（见上图所示的最佳效用策略阶梯式收敛轨迹）。

### 4.4 调优发现与实战闭环

#### 4.4.1 暴露的关键数值缺陷
通过自动代理的量化反馈，团队在发售前捕获了手工测试极难精确量化的致命数值缺陷：
1. **难度过早见顶（Premature Difficulty Peak）**：
   * 在浪漫线中，第 2 阶段（Sweethearts）所需要的期望点击次数出现了异常陡峭的暴增（见下方对比表），其绝对难度甚至超过了后续阶段，并且大幅高于其他两条路线的对应阶段。
2. **渐进阶梯破损**：
   * 既定设计目标要求每条路线从阶段 1 到阶段 5 呈现光滑递增的难度梯度（如下图目标曲线所示），但实测数据呈现无序锯齿状。

#### 4.4.2 调优前后量化指标对比

| 人际关系分支 | 关系进阶阶段 | 初始设计（代理实测点击数） | 数值调优后（最终平衡点击数） | 策划既定目标难度 |
| :--- | :--- | :--- | :--- | :--- |
| **友谊线 (Friends)** | Stage 1: Friends | 8.5 | 2.0 | 5.5 (渐进起点) |
| | Stage 2: Close friends | 8.8 | 6.0 | 6.5 |
| | Stage 3: Great friends | 7.2 (异常下降) | 8.5 | 7.5 |
| | Stage 4: Best friends | 9.8 | 9.0 | 8.5 |
| | Stage 5: BFFs | 5.8 (崩塌) | 9.5 | 9.5 |
| **浪漫线 (Romantic)** | Stage 1: Romantic Interest | 4.5 | 2.5 | 5.0 |
| | Stage 2: Sweethearts | **9.6 (严重过高，难度峰值)** | **7.0 (修复回落)** | 6.5 |
| | Stage 3: Lovers | 7.8 | 8.0 | 7.5 |
| | Stage 4: True loves | 8.2 | 9.2 | 8.5 |
| | Stage 5: Soul mates | 6.7 (阶段收尾失衡) | 9.0 | 9.5 |

通过将代理采集的日志报表直接反馈给策划团队，设计人员针对事件的资源产出率、阶段点数门槛进行了重平衡。下一次迭代运行时，不仅浪漫线第 2 阶段的畸变被消除，三条不同路径在横向间的推进难度也达到了工业级平衡。

---

## 5. 生产流水线闭环：从定性分析到全自动参数回归 (Toward Automated Closed-Loop Tuning)

在《模拟人生移动版》的第一阶段实践中，调优循环依赖**“代理生成报表看板 $\rightarrow$ 策划人工修改静态参数”**的半自动流程。人工手动拟合高维非线性参数曲线仍然极具挑战。

该架构的最终演进目标是构建完全自动化的超参数回流管道：

```
                    ┌────────────────────────────┐
                    │     目标损失函数建立        │
                    │  (策划定义目标曲面 L_target) │
                    └─────────────┬──────────────┘
                                  │
                                  ▼
┌──────────────────┐  配置注入  ┌────────────────────────────┐  派发动作  ┌──────────────────┐
│  优化算法更新器   │ ───────> │    游戏数值配置资产库        │ ───────> │  无头客户端矩阵   │
│  (CMA-ES / BO)   │          │ (Tuning Data: JSON / XML)  │          │ (Headless Nodes) │
└──────────────────┘          └────────────────────────────┘          └────────┬─────────┘
         ▲                                                                     │
         │                      计算残差与梯度映射                              │ 产生模拟指标
         └─────────────────────────────────────────────────────────────────────┘
                                  Loss = || Taps - Target ||^2
```

### 自动闭环数学模型
定义目标设计曲线为向量 $\mathbf{y}^* \in \mathbb{R}^M$（例如各阶段的目标点击数向量），当前游戏配置参数空间为 $\boldsymbol{\theta} \in \Theta \subset \mathbb{R}^P$（包含体力消耗速率、事件产出倍率等数十维配置）。

仿真环境通过并行代理执行得到实测指标向量 $\bar{\mathbf{y}}(\boldsymbol{\theta})$：

$$\min_{\boldsymbol{\theta}} \mathcal{L}(\boldsymbol{\theta}) = \left\| \bar{\mathbf{y}}(\boldsymbol{\theta}) - \mathbf{y}^* \right\|_2^2 + \lambda \mathcal{R}(\boldsymbol{\theta})$$

其中 $\mathcal{R}(\boldsymbol{\theta})$ 为参数正则化约束（防止策划数值溢出不可行区间）。

自动化流水线在云端通过**贝叶斯优化（Bayesian Optimization）**或**演化策略（Evolution Strategies）**在有限预算内自动搜索最佳参数解 $\boldsymbol{\theta}^*$，将其序列化写回游戏的 JSON/XML 配置文件，从而将数周的人工试错周期缩短至数小时的机器自动化迭代。

---

## 6. 核心架构总结与落地建议

1. **解耦优先（Decoupling First）**：
   * 动作空间严禁与具体的 UI 控件、渲染视口直接绑定；动作系统与状态系统必须在底层逻辑框架中实现纯数据驱动（Data-Driven）。
2. **无需过度复杂化算法（Heuristics over Deep RL）**：
   * 在工业级游戏调优中，复杂的端到端深度强化学习并非首选。简单的效用系统（Utility Systems）、启发式规则结合均匀随机采样，足以暴露绝大部分数值缺陷与系统死锁。
3. **重视“相对指标”的鲁棒性**：
   * 随机或半随机代理的绝对数据通常不代表人类，但其相对难度和阶段趋势具有高度的不变性。在研发早期，无需等待高精度代理训练完毕，即可利用随机代理对数值设计进行快速排雷。
4. **架构前置投入**：
   * 在客户端主循环建立之初即设计基于宏隔离的插桩接口（如 `#ifdef AGENTS`），使游戏在预生产（Pre-production）阶段即可作为仿真环境运行，能规避后期游戏架构定型带来的高昂重构代价。

---

在现代商业游戏（尤其是包含长线养成与复杂 PVP 对抗的 4X MMO 或模拟经营类游戏）的研发管线中，传统的纯手工数值配置与纸面平衡验证已难以应对高维度的状态空间和复杂的玩家对抗博弈。基于自动化智能体（Agent-based Simulation）的驱动分析方法，使开发团队能够在上线前精确量化游戏机制设计中的漏洞、利用点（Exploits）与主导策略。

本文基于工业级实战经验，系统阐述如何利用强化学习、效用理论、图论拓扑分析、兰彻斯特损耗定律以及进化博弈论（纳什均衡逼近），构建高度模块化的游戏数值调优与玩法仿真体系。

---

## 1. 关系事件最优策略探索：效用驱动的轻量化强化学习

在包含资源积累与“延迟满足（Delayed Gratification）”特性的游戏循环中（例如《模拟人生：移动版》TSM 中的人际关系事件），玩家必须先投入行动点完成低收益的基础活动来积累消耗品，待资源充足后再触发高阶事件以获取峰值收益。为自动化求解该过程的最优决策，需要构建基于强化学习（Reinforcement Learning, RL）的仿真智能体。

### 1.1 状态与动作效用函数建模

尽管在限定状态空间下可采用经典的表格型学习方法（Tabular Methods），但其无法随未来设计迭代保持良好的扩展性，且其状态-动作价值函数（Action-Value Function）难以拟合人类玩家基于直觉建立的“上下文效用体系”。

我们将动作 $a$ 在状态 $s$ 下的价值近似为**上下文效用函数（Contextual Utility Function）** $U(s, a)$，由收益（Rewards）与成本（Costs）经状态加权组合而成：

$$U(s, a) = R(a) \cdot v(s) + C(a) \cdot w(s)$$

* **静态数值配置**：对每个动作 $a$，数值策划在配置表中显式定义其固有收益 $R(a)$ 与固有成本 $C(a)$（包括能量消耗、经验获取、货币流转等维度）。
* **动态状态向量**：状态 $s$ 被编码为一个特征向量，涵盖体力（Energy）、饥饿度（Hunger）等核心资源水位，以及事件指示变量（当前处于事件外部为 $0$，事件内部为 $1$）。
* **线性权重函数**：动态权重函数 $v(s)$ 与 $w(s)$ 采用线性映射：
  $$v(s) = p \cdot s, \quad w(s) = q \cdot s$$
  其中 $p$ 与 $q$ 为模型待学习的参数矩阵/向量。

### 1.2 基于 SoftMax 的概率决策规则与温度控制

智能体在决策步采用 **SoftMax 选择策略**，使得选择动作 $a_i$ 的概率正比于其效用的指数分布：

$$\sigma(a_i, s) = \frac{\exp\left(U(a_i, s) \cdot \tau\right)}{\sum_{j} \exp\left(U(a_j, s) \cdot \tau\right)}$$

其中 $\tau = \frac{1}{T}$ 为温度参数（Temperature Parameter）$T$ 的倒数：
* **高温度（$T \to \infty, \tau \to 0$）**：动作选择概率趋于均匀分布，对应于探索型或无脑随机点击的初学者策略。
* **低温度（$T \to 0, \tau \to \infty$）**：系统强烈偏好效用值最高的动作，对应于高阶玩家的贪婪决策。因此，参数 $T$ 可在工程上直接解释并充当**玩家技术熟练度（Player Skill）**的模拟调节器。

### 1.3 适应度目标函数设计与进化策略（ES）求解

优化目标为：在最少的点击交互步数（Taps）内完成所有预定事件并达成最高关系等级。针对离散回合的奖励工程（Reward Engineering），定义每轮（Episode）的复合适应度目标函数 $\mathcal{R}$：

$$\mathcal{R} = \frac{r(r + \epsilon)}{a + \epsilon} \to \max$$

* $r$：当前轮次中成功获取奖励的事件总数；
* $a$：当前轮次中尝试发起的事件总数；
* $\epsilon$：极小的正数（$\epsilon < 1$），用以规避除零异常（如智能体未尝试开启任何事件）。

该函数的设计核心在于：分母惩罚缺少事件触发尝试的行为，分子对成功完结的事件给予二次方量级的超额激励，从而迫使智能体避免无效耗时，以最快路径收敛至事件完成。

#### 优化算法决策：为何摒弃梯度下降，选用进化策略（ES）？

由于该决策问题具有高度的离散性，特别是在低温采样环境下，效用曲面存在大量“平坦露台（Flat Terraces）”，导致传统基于梯度的优化方法极易失效。针对可学习权重向量 $[p, q]$，我们引入参数空间加噪的黑盒优化算法——**自然进化策略（Evolutionary Strategies, ES）**。

```
                       [ 云端协调节点 (Master) ]
                                  │
         ┌────────────────────────┼────────────────────────┐
         ▼                        ▼                        ▼
[ 计算节点 1 (Node 1) ]   [ 计算节点 2 (Node 2) ]   [ 计算节点 N (Node N) ]
  θ + σ * ε_1              θ + σ * ε_2              θ + σ * ε_N
  运行 700 轮交互仿真       运行 700 轮交互仿真       运行 700 轮交互仿真
         │                        │                        │
         └────────────────────────┼────────────────────────┘
                                  ▼
                    [ 聚合适应度 R 并更新权重 θ ]
                    [ 退火调节温度 T 与高斯扰动径 σ ]
```

* **工程优势**：ES 具备天然的无状态并行特性，各采样分支可在无锁状态下分布式部署于多台云端节点；
* **超参数调度**：通过联合调度初始温度、高斯采样半径（Gaussian Sampler Radius）及其指数衰减速率，智能体仅需约 700 轮训练即可成功收敛至与资深平衡性策划理论推导高度一致的最优事件参与策略；
* **局限性与补足**：效用逼近法能够接近理论最优界限，但难以保证求得绝对全局最优。当需要彻底封堵游戏漏洞、挖掘极值漏洞（Exploits）时，需借助全局搜索算法（如 $A^*$ 搜索）实施进一步穷举审计。

---

## 2. 模块化智能体与 4X MMO 战斗系统评估

在 4X MMO 战略游戏中，游戏呈现强对抗性特征。游戏架构通常采用严格的客户端-服务器（C/S）分离模型，所有游戏逻辑均在服务端闭环执行，客户端仅承担表现与输入收集。

为实现低成本的大规模战斗调优，无需运行庞大的全量服务器单体，而是利用战斗模块的高内聚特性，将底层战斗逻辑打包为独立无头包装器（Standalone Combat Wrapper）。

```
           [ 传统笨重仿真方案 ]                      [ 模块化解耦仿真方案 (推荐) ]
       
    [ Headless Client (全量协议) ]                [ 独立战斗模拟器 (Combat Wrapper) ]
                 │                                                │
    (WebSocket / REST API 驱动)                               (共享服务端 C++ 内核)
                 │                                                │
    [ Game Server (全量状态机) ]                    ┌─────────────┴─────────────┐
                 │                                  ▼                           ▼
    (包含工会/建造/经济/大地图逻辑)            [ 兰彻斯特损耗验证 ]        [ 拓扑 RPS 平衡验证 ]
                 │
            [ 耗费极重 ]                            [ 高并发、极速纯内存运算 ]
```

---

## 3. 战斗核心机制分析：兰彻斯特法则与剪刀石头布（RPS）模型

### 3.1 兰彻斯特战斗损耗定律：线性与二次方陷阱

战斗系统的单位消耗本质遵循**兰彻斯特战斗定律（Lanchester's Laws）**。其核心参数为战斗能力的发挥方式，决定了古代战斗与现代战争的损耗差异：

1. **兰彻斯特线性律（Lanchester's Linear Law，古代/近战）**：单位之间只能发生一对一接敌。战斗力随部队规模呈严格线性增长：
   $$P_{\text{linear}} \propto N$$
2. **兰彻斯特二次方律（Lanchester's Square Law，现代/远程集中火力）**：所有单位在任意时刻均可向对方范围内的任意单位倾泻火力并均摊伤害。其总战斗效能与部队规模的平方成正比：
   $$P_{\text{square}} \propto N^2$$

#### 二次方定律下的反直觉平衡失真推导
假设单方存在一名攻击力为普通士兵 4 倍的精锐单位（攻击力 $D_s = 4$，生命值 $H_s = 4$），面对基础属性单位（$D_u = 1$，$H_u = 1$）。若按直觉设定，策划常误认为需要 4 名普通士兵方可与之战平。然而在二次方律作用下：

1. 设普通士兵数量为 $k$；
2. 精锐单位受到的集火伤害速率为 $k \times D_u = k$；
3. 精锐单位反击时，其伤害被分摊到 $k$ 个目标上，每个普通士兵承受的损耗率为 $\frac{D_s}{k} = \frac{4}{k}$；
4. 双方相对损耗速率比为：
   $$\text{Attrition Ratio} = \frac{\text{Damage to Elite}}{\text{Damage to Commons}} = \frac{k}{\frac{4}{k}} = \frac{k^2}{4}$$
5. 当 $k = 2$ 时，损耗比即达到 $\frac{2^2}{4} = 1$（双方同时阵亡）。

也就是说，仅需 2 名基础单位即可抹平 4 倍属性的精锐单位。这表明：**在具有非线性损耗特征的战斗体系中，数值策划的“线性直觉”会严重高估高阶单位的实战价值。** 必须通过智能体模拟统计实际损耗曲线。

---

## 4. RPS 体系的图论分析与“平衡缺陷”量化

在兵种克制设计中，石头-剪刀-布（RPS）闭环可防范单一同质化军队（Pure Strategy / Homogeneous Army）成为无解统治策略。针对不同阵营的多阶（Multi-tiered）兵种，评估克制关系需要从经济维度全面切入。

### 4.1 RPS 三维统治矩阵

为精确度量兵种优劣，智能体将战斗作为隔离的马尔可夫决策过程（MDP）子任务运行。针对阵营 $A$ 的兵种 $i$ 与阵营 $B$ 的兵种 $j$，智能体寻找能够打平对决的最小同质部队规模，从而构建三组维度为 $N \times N$ 的**统治矩阵（Dominance Matrices）**：

| 评估维度 | 矩阵元素 $M_{ij}$ 含义 | 策划与平衡意义 |
| :--- | :--- | :--- |
| **数量比 (Count Ratio)** | 打平所需单位绝对数量之比：$\frac{\text{Count}_j}{\text{Count}_i}$ | 衡量单兵面板数值的纯克制强度 |
| **人口占用比 (Unit Size Ratio)** | 打平所需的人口空间之比：$\frac{\text{Count}_j \times \text{Size}_j}{\text{Count}_i \times \text{Size}_i}$ | 受限行军容量（March Capacity）下的实战克制极限 |
| **经济造价/工时比 (Cost Ratio)** | 打平所需的资源/建造加速货币之比：$\frac{\text{Count}_j \times \text{Cost}_j}{\text{Count}_i \times \text{Cost}_i}$ | 宏观经济视角的等价代换率，避免高昂兵种因性价比崩塌沦为废案 |

### 4.2 统治二分图（Dominance Graph）与完全 RPS 图判定

我们将上述矩阵映射为一个完全有向二分图 $G = (V_A, V_B, E)$：
* **节点集合**：$V_A$ 与 $V_B$ 分别代表两敌对阵营的兵种节点；
* **有向边与权重**：若单位 $u$ 对单位 $v$ 占优，则引出一条有向边 $u \to v$，边权重 $W(u, v)$ 等于两者打平状态下的战力/规模比率；
* **环路拓扑属性**：二分图的任意闭合环路必然具有偶数长度（$2k, k \ge 2$）。一个环路即代表一个跨阵营的 RPS 克制闭环：
  $$v_1 \to v_2 \to v_3 \to \dots \to v_{2k} \to v_1$$
* **完全 RPS 图（Complete RPS Graph）**：若二分图中的**每一个节点**均隶属于至少一个简单环路（Simple Cycle），则该体系在宏观上达到完全 RPS 闭环。

在工程实现中，集成 Python 的 `NetworkX` 库并运行 **Johnson 简单环路搜索算法**，可自动化提取系统中所有独立的极小基环。

```
[阵营 A: 步兵 T1] ───(优势)───> [阵营 B: 骑兵 T2]
       ▲                               │
       │ (优势)                     (优势)
       │                               ▼
[阵营 A: 弓兵 T3] <───(优势)─── [阵营 B: 盾兵 T1]
         (构成长度为 4 的跨阵营二分闭合 RPS 环)
```

### 4.3 “RPS 平衡缺陷”的势函数数学定义与反向传播优化

若某些单位在图谱中未被任何简单环路覆盖，说明该单位存在“数值孤立”或“绝对应激碾压”现象，破坏了宏观循环。我们通过图势函数定义其**缺陷度量（Defect Metric）**：

1. **势能映射**：在统治图上定义势函数 $U(x)$。理想的闭合递增遍历环路要求势能沿有向边具有单调性；
2. **深度优先生成树（DFS Trees）**：针对待检测的非 RPS 单位节点 $n$，沿支配关系有向边分别向上（被克制链）和向下（克制链）遍历，构建以 $n$ 为根的综合统治树；
3. **缺陷计算**：对二分树中所有非树横截边对 $(a, b)$，其电荷势差定义为：
   $$P(a, b) = U(a) - U(b)$$
   该势差破坏了整条克制路径的单调传递性。
4. **单节点 RPS 缺陷值**：
   $$\text{Defect}(n) = \min_{(a, b) \in E} |P(a, b)|$$
   
该标量在几何与拓扑层面上直接反映：**仅需反转或调整代价最小的一条有向边（$a \to b$）的相对战力，即可将节点 $n$ 重新吸纳进一个全局闭合 RPS 环中。**

```
                  [ 待诊断异常单位: 节点 n ]
                             │
              ┌──────────────┴──────────────┐
       (向下: 克制链 DFS)             (向上: 被克制链 DFS)
              ▼                             ▼
         [ 子节点 a ]                  [ 父节点 b ]
              │                             │
              └─────── 违背单调性的横截边 ─────┘
                         (a, b): 势能差 P
                       P = U(a) - U(b)
                Defect(n) = min |P(a, b)|
```

由此，平衡性调整演化为一个带约束的凸/非凸参数优化问题：
$$\min_{\Theta} \sum_{n \notin \text{Cycles}} \text{Defect}(n; \Theta)$$
其中 $\Theta$ 为目标单位群的血量、攻击力、造价等属性超参数，优化器可自动化求解改动幅度最小（Most Conservative）的属性配平方案。

---

## 5. 非对称阵营与多兵种混编的纳什均衡逼近

### 5.1 非对称兵种降维对齐：战力-规模转换曲线

现实设计中不同阵营往往高度非对称（例如兽人单兵高攻高血量，精灵高移速低造价）。此时单兵对称已不复存在。

为降低搜索复杂度，工程上可将单位多维特征降维为**攻击强化倍率（Attack Strength Multiplier）**与**部队规模乘数（Army Size Multiplier）**的联合分布。

```
部队规模乘数 (Army Size Multiplier)
 1.25 │                                      / (打平点散点拟合)
 1.20 │                                    /
 1.15 │                                  /
 1.10 │                       .  . .   /  [ 线性回归平衡基线 ]
 1.05 │                  .  .   .    /
 1.00 └───────────────.────────────/───────────
     1.0             1.1          1.2         1.3
              单兵攻击强度乘数 (Attack Strength Multiplier)
```

通过大量随机混编抽样对决，统计出打平状态下的离散数据点并执行线性回归（Linear Regression），可直接拟合出不同阵营间的**规模-战力兑换率（Conversion Rate）**。例如，当敌方攻击强度高出 $20\%$ 时，本方需扩充约 $15\%$ 的部队容量基准线以维持动态平衡。

### 5.2 混编配比求解：基于进化最佳响应（Evolutionary Best Response）的纳什均衡仿真

在重复博弈环境下，任何单一纯兵种阵容（Pure Strategy）都会被对手探明后针对性反制。玩家必然倾向于生产多种兵种混编的复合军团（Mixed Strategies）。求解最优编队比例即求解该有限博弈的**混合策略纳什均衡（Nash Equilibrium）**。

由于高维多兵种对抗求解精确纳什均衡具备 **PPAD-Complete 复杂度**，直接解析求解在工程上不可行。我们采用仿生学的**演化最佳响应算法（Evolutionary Best Response）**，结合**核密度估计（Kernel Density Estimation, KDE）**进行动态采样收敛。

#### 进化算法伪代码实现（工业级标准）

```python
def approximate_nash_equilibrium(factions, entry_army_size, max_army_size, size_step):
    """
    通过渐进规模演化与高斯核密度估计(KDE)逼近双阵营混编最佳响应与纳什均衡
    """
    current_size = entry_army_size
    populations = {}

    # 初始化：针对各阵营，在限定规模下通过连续 KDE 采样器初始化混编配比种群
    for faction in factions:
        populations[faction] = initialize_kde_population(
            faction=faction, 
            army_size=current_size
        )

    # 阶段递进循环：规模由小到大，有效抑制前期组合爆炸，降低战力推演计算开销
    while current_size <= max_army_size:
        # 对抗评估内循环：阵营间完全随机配对决斗
        for army_A, army_B in generate_random_pairings(populations[factions[0]], populations[factions[1]]):
            combat_result = execute_combat_simulation(army_A, army_B)
            update_win_loss_statistics(army_A, army_B, combat_result)

        # 胜者筛选淘汰：以平缓比率剔除胜率落后的劣质混编组合（防止种群剧烈震荡）
        for faction in factions:
            populations[faction] = eliminate_defeated_armies(
                populations[faction], 
                survival_rate=0.7
            )

        # 扩充军队规模容量，准备推进至下一复杂度阶梯
        current_size += size_step

        # 种群重采样与高斯变异：基于留存的优势军团，采用高斯 KDE 拟合重采样出新规模的军团
        for faction in factions:
            # 高斯核半径带宽随规模增大自适应收敛缩小，防止子代发散偏离优良父代
            adaptive_bandwidth = calculate_annealing_bandwidth(current_size)
            
            populations[faction] = resample_population_via_kde(
                survivors=populations[faction],
                new_target_size=current_size,
                kernel="gaussian",
                bandwidth=adaptive_bandwidth,
                constraint_rounding=True  # 将战力浮点配比整量化为离散兵种实体数
            )

    return populations
```

#### 关键机制解析

```
[阶段 1: 小规模军团低维探索]
      │  组合空间小、单局判定速度快 (<1ms)，快速收敛至粗略的战力重心
      ▼
[阶段 2: 渐进缩紧的高斯 KDE 变异]
      │  以获胜部队为中心建立多维高斯核：
      │  K(x) ~ exp(-||x - x_parent||^2 / (2 * h^2))
      │  带宽 h 随步数逐步退火衰减，锁定最优比例邻域
      ▼
[阶段 3: 达到全规模高精度收敛]
      │  最终兵种构成稳定在特定比例（例如步:骑:弓 ≈ 4:3:3）
      ▼
[输出纳什均衡兵种比例] ───> [导入宏观经济循环作为 NPC/智能体建造基准]
```

* **防止种群震荡**：算法避免直接采用全量截断式选择，而是保持可控的淘汰率。激进淘汰会导致种群在不同纯兵种之间来回震荡，演化为“石头 $\to$ 布 $\to$ 剪刀 $\to$ 石头”的无限循环；
* **单局时长预警**：仿真输出的**平均战斗耗时（以原子交互步数计）**分布，能直接预测线上服务器处理战斗结算的 CPU 负载，为后端架构提供压力基准。

---

## 6. 脱离宿主客户端：游戏逻辑仿真框架的架构演进与工程瓶颈

在大规模执行策略审计与平衡调优仿真时，测试载体（Client/Host）的选型决定了运算通量与云端成本。

### 6.1 三种仿真架构的工程权衡

| 架构形态 | 运行环境 | 性能与算力开销 | 逻辑保真度 (Fidelity) | 工业界适用场景 |
| :--- | :--- | :--- | :--- | :--- |
| **插桩客户端 (Instrumented Client)** | 渲染引擎客户端（如 Unity/Unreal 打包运行） | **低**：受限于主线程循环，强制加载图形管线与骨骼资源，CPU/GPU 负载极重 | **极高**：直接复用玩家同款客户端代码 | 研发极早期、无专用管线时的冒烟测试与表现层 Bug 验证 |
| **全协议无头客户端 (Headless Client)** | 剥离渲染的无头进程，走标准网络通信（WebSocket/REST） | **中**：规避了 GPU 渲染开销，但受制于网络序列化/反序列化延迟与全服环境负载 | **高**：直接对齐服务端全量业务流 | 自动化集成测试、全链路安全反作弊扫描、大地图活动仿真 |
| **独立战斗内核包装器 (Combat Wrapper / Simulator)** | 提取纯 C++/C# 战斗计算内核，剥离网络与渲染的极简控制台 | **极高**：无网络开销，纯内存并行推进，可达千万级局数/秒吞吐 | **专属高**：只保障数值/物理战斗系统的绝对一致性 | 核心数值调优、RPS 图缺陷分析、纳什均衡进化推演 |

### 6.2 时间步长压缩引发的“时间穿透误差”

为加速仿真，开发团队通常尝试大幅提升逻辑更新步长（Tick Multiplier / Large $\Delta t$）。但该操作在基于微分与数值积分的子系统（如运动学物理、连续损耗运算、复杂导向行为）中会引入致命的离散化漂移：

```
真实物理曲线 (连续时间) :  ───────╮
                                  ╰───────────────
小步长积分 (Small dt)  :  ──*──*──*──*──*──*──*──* (高度逼近真实轨迹)
大步长积分 (Large dt)  :  ─────*───────────*────── (剧烈穿透/过冲偏差)
```

若算法隐含了“单步变化极小”的假设，增大 $\Delta t$ 会破坏碰撞检测与伤害离散判定，产生虚假的战斗输赢偏差，进而彻底误导平衡性调优方向。因此，设计优良的模块化仿真体系必须做到：**在时间轴推进上使用固定小步长（Fixed Small Ticks），但剥离一切帧率限制（Uncapped Framerate），以纯 CPU 内存计算极限加速时间流逝。**

---

## 7. 总结与落地闭环工作流

```
               [ 游戏设计配置 (数值/机制) ]
                            │
                            ▼
              [ 独立无头模块提取 (C++ 内核) ]
                            │
            ┌───────────────┴───────────────┐
            ▼                               ▼
    [ 图论拓扑分析 ]                [ 进化博弈对抗 ]
  · 构建数量/人口/造价统治矩阵      · 自适应 KDE 种群变异
  · Johnson 算法闭环搜索          · 纳什均衡配比近似 (4:3:3)
  · 势函数计算 RPS 缺陷           · 拟合战力-规模线性补偿
            │                               │
            └───────────────┬───────────────┘
                            ▼
              [ 自动化参数优化与回填 (ES) ]
                            │
                            ▼
               [ 闭环生效至正式游戏环境 ]
```

将游戏 AI 智能体由传统单体运行环境中解耦，转化为面向垂直机制（战斗、经济、资源累加事件）的轻量化模块，不仅能让计算算力专注于策略解空间的推演，更使得利用**进化策略优化势函数缺陷**与**基于博弈论收敛最佳响应**成为工业化现实。该范式将数值平衡从“被动修补漏洞”彻底转变为“设计期主动数学验证”，构成了现代高阶竞技与长线策略游戏研发的技术基石。

---

在大型现代商业游戏的研发过程中，智能体（Agent）通常不仅用于驱动非玩家角色（Non-Player Character, NPC），更逐步演变为验证游戏经济系统、探索数值边界、发现玩法漏洞（Exploits）以及评估玩家成长曲线（Progression Curves）的核心设计工具。

然而，传统的游戏客户端架构包含了图形渲染（Rendering Pipelines）、物理模拟、磁盘 I/O 及复杂的生命周期同步机制，导致在执行深层前瞻搜索（Look-Ahead Search）或大规模并行强化学习（Reinforcement Learning, RL）模拟时面临难以逾越的吞吐量瓶颈。本篇文档聚焦于从完整游戏客户端剥离核心玩法逻辑，构建独立、无图形、纯内存驱动的专用轻量化模拟器（Headless Dedicated Simulator），并基于演化策略（Evolution Strategies, ES）与经典搜索算法（如 $A^*$ 算法）实现玩法建模与自动化数值调优。

---

## 1. 客户端解耦架构：专用轻量化模拟器（Headless Dedicated Simulator）

### 1.1 完整客户端驱动的瓶颈分析

在游戏开发早期或中期，直接通过在完整客户端（Instrumented Game Client）中植入测试智能体（Test Agents）来模拟数十万小时的游戏行为，往往会遭遇严峻的系统级瓶颈：

```
+-------------------------------------------------------------+
|               常规游戏客户端 (Heavyweight Client)            |
|  +-------------------------------------------------------+  |
|  | 渲染引擎 (Render Pipeline) | 物理引擎 (Physics Engine)   |  |
|  +-------------------------------------------------------+  |
|  | 磁盘 I/O / 序列化 (Save/Load Checkpoints via Disk)      |  |
|  +-------------------------------------------------------+  |
|  | 逐帧时间离散化步进 (Explicit Delta Time Ticking: dt)      |  |
|  +-------------------------------------------------------+  |
|                             | 带来巨大开销                   |
|                             v                               |
|        状态切换极慢 / 难以并行 / 前瞻搜索吞吐量受限 (< 100 FPS)   |
+-------------------------------------------------------------+
                              vs
+-------------------------------------------------------------+
|             专用无头模拟器 (Headless Dedicated Simulator)     |
|  +-------------------------------------------------------+  |
|  | 无图形化开销 (Pure Headless / No Rendering / No Audio) |  |
|  +-------------------------------------------------------+  |
|  | 内存级状态复制/回滚 (Zero-Disk State Switching / Copy)   |  |
|  +-------------------------------------------------------+  |
|  | 离散事件驱动模拟 (Discrete Event-Based Simulation: DES)   |  |
|  +-------------------------------------------------------+  |
|                             | 极致吞吐                      |
|                             v                               |
|         单核数十万次状态转换 / 秒，支持大规模云端并行评估       |
+-------------------------------------------------------------+
```

1. **状态持久化与回溯开销（State Switching Overhead）**：
   前瞻搜索（如蒙特卡洛树搜索 MCTS、$A^*$ 搜索）需要在节点间高频进行状态克隆、转移与回滚。完整客户端的存档/读档逻辑通常依赖于磁盘序列化或深度对象树复制，极大地限制了分支探索的吞吐量。
2. **时间步长推进的低效性（Explicit Time Ticking）**：
   玩法机制（如建筑建造、资源产出）往往以秒或分钟计。完整游戏引擎通常通过显式的时间增量（$\Delta t$ 积分）推进模拟，在模拟长达数周的玩家成长周期时浪费了海量的 CPU 周期。
3. **集成与维护成本的倒挂（Code Drift & Maintenance Burden）**：
   游戏客户端代码处于高频迭代中。持续维护用于拦截、驱动和重置客户端逻辑的 Instrumentation 接口，其工程代价往往超过了独立开发一个仅包含核心规则的精简模拟器。

### 1.2 独立模拟器的工程规范

为满足大规模搜索与强化学习的计算需求，专用模拟器应遵循以下工程约束：
- **纯内存状态切换（In-Memory State Operations）**：状态表示高度结构化（如紧凑结构体扁平数组），状态转移（State Transition）与分支重置（Branch Resetting）均在内存完成，彻底消除磁盘与文件系统交互。
- **离散事件驱动模拟（Discrete-Event Simulation, DES）**：对于涉及长时间冷却、资源生产与队列等待的宏观玩法（Meta-game），跳过无事件发生的连续时间步，直接推进到下一个事件时间戳（Next Event Timestamp）。
- **逻辑模型前置（Model-First Engineering）**：在游戏客户端和服务器网络层尚未完成时，优先依据数值设计文档（GDD）与电子表格（Spreadsheets）构建玩法逻辑模型，使数值平衡验证与机制纠偏具备前置测试能力（Shift-Left Testing）。

---

## 2. 4X 策略游戏成长与经济系统建模实战（Agents for 4X Genre）

### 2.1 4X 游戏节奏与成长曲线（Progression Speed Tuning）

4X 策略游戏（eXplore, eXpand, eXploit, eXterminate）的核心设计目标是控制玩家的成长速度（Progression Speed），其关键手段为分层调节不同玩家等级（Player Level）下的资源产出速率（Generation Rate）与资源消耗需求（Spending/Cost）。

* **前期阶段（Early Game）**：要求高额即时反馈、快速升级通道，以建立核心玩法循环并维持新玩家留存；
* **后期阶段（Late Game）**：升级所需资源呈指数级（Exponentially）攀升，成长斜率平缓；
* **终局阶段（End Game）**：难度曲线必须严密收敛，防止核心沉浸玩家（Hardcore Players）因成长断崖或资源极度匮乏而过早流失。

传统数值设计通常基于电子表格（如 Excel）配置成长公式，并假设玩家采取“单位时间收益最大化”的单一完美策略。对于非硬核玩家，往往仅通过线性缩放因子（Scaled-down Multipliers）粗略预估。这种静态推导无法捕捉玩家间的资源掠夺、动态决策分歧、玩家性格特征（Player Personalities）及潜藏的数值漏洞（Exploits）。引入智能体驱动模拟能够快速探索庞大的状态空间，验证非稳态博弈下的经济稳健性。

### 2.2 建筑升级效用系统（Utility-Based Upgrade Model）

基地建筑的建造与升级构成了 4X 游戏中最核心的资源汇聚与转换节点。我们构建效用系统（Utility System）来驱动智能体的决策。

#### 2.2.1 状态与效用建模

设游戏系统内包含 $K$ 类异构资源 $r \in \mathcal{R} = \{1, 2, \dots, K\}$，存在 $M$ 种不同类型的资源产出建筑 $b \in \mathcal{B} = \{1, 2, \dots, M\}$。每个建筑类型 $b$ 具有当前等级 $L_b \in \mathbb{N}$，玩家具有当前基地等级 $L_p \in \mathbb{N}$。

由于建筑的升级依赖树与等级映射在各级别间保持结构相似性，建筑升级决策的效用可简化为**玩家等级与建筑等级之差**的函数：
$$\Delta L_b = L_p - L_b$$

对于建筑 $b$，升级所消耗的综合资源成本（Cost）折算向量为 $\mathbf{C}_b(L_b)$，其单位时间产出的资源向量增量为 $\Delta \mathbf{G}_b(L_b)$。建筑升级的最终效用值 $U(b)$ 由演化策略所优化的权重向量 $\mathbf{w}$ 及资源匮乏度加权计算：

$$U(b \mid L_p, L_b) = \sum_{k \in \mathcal{R}} w_k \cdot S_k \cdot \Delta G_{b, k}(L_b) + f_{\mathbf{\theta}}(\Delta L_b)$$

其中：
- $S_k \in [0, 1]$ 为资源 $k$ 的匮乏度因子（Scarcity Factor），由玩家当前库存总量与下一阶段需求缺口决定：
  $$S_k = \max\left(0, 1 - \frac{\text{Inventory}_k}{\text{Required}_k}\right)$$
- $f_{\mathbf{\theta}}(\Delta L_b)$ 为使用自然演化策略（Natural Evolution Strategies, NES）学习得到的非线性等级差偏置项，参数由 $\mathbf{\theta}$ 决定。

#### 2.2.2 演化策略（Evolution Strategies, ES / NES）权重优化

利用基于种群的黑盒优化方法（如 Salimans 等人提出的 NES 变体）优化效用函数的权重向量 $\mathbf{\psi} = [\mathbf{w}, \mathbf{\theta}]$：

1. **扰动采样**：在当前超参数空间采样 $N$ 组高斯扰动向量 $\mathbf{\epsilon}_i \sim \mathcal{N}(0, \mathbf{I})$：
   $$\mathbf{\psi}_i = \mathbf{\psi}_t + \sigma \mathbf{\epsilon}_i, \quad i \in \{1, \dots, N\}$$
2. **无头并行仿真**：将每组参数 $\mathbf{\psi}_i$ 实例化为效用智能体，在独立模拟器中并发推进到终局或终止状态，计算适应度函数（Fitness Function）：
   $$F_i = -\text{TimeToReachLevel}(L_{\text{target}}) - \lambda \cdot \text{ResourceWasted}$$
3. **梯度估计与更新**：
   $$\mathbf{\psi}_{t+1} = \mathbf{\psi}_t + \alpha \frac{1}{N \sigma} \sum_{i=1}^N F_i \mathbf{\epsilon}_i$$

通过演化学习，智能体自动掌握了各资源生成建筑与玩家主城等级之间的动态依赖平衡，突破了人工编写启发式规则的局限。

### 2.3 四类策略对比分析（Figure 6 实验复盘）

在独立的 4X 经济模拟器中，通过对不同策略下的玩家升级速度（到达各级所需的模拟时间）与资源库存演变进行实验，获得如下定性与定量对比结论：

```
+------------------------------------------------------------------------------------+
| 策略类型 (Strategy)    | 机制特征 (Mechanism)       | 升级速度表现   | 资源状态演变 (Resource Trajectory)  |
+------------------------------------------------------------------------------------+
| 1. 无限资源基准       | 解锁资源上限约束            | 理论极限值     | 后期资源账目急剧下降（允许负平衡），  |
|    (Unlimited)         | 设定绝对时间最短界限        | (Fastest Limit)| 表明被动产出远无法支撑后期消耗    |
+------------------------------------------------------------------------------------+
| 2. 被动产出无升级     | 仅依赖初始产出速率          | 理论最慢界限   | 极后期升级时间呈指数爆炸，难以支撑  |
|    (Passive / Naïve)   | 完全不执行发生器升级        | (Slowest Limit)| 正常通关，表明成长卡点严重        |
+------------------------------------------------------------------------------------+
| 3. 贪心策略升级       | 依据当前即时资源缺口        | 性能良好       | 较理论极限速度仅慢约 25%，展现出强 |
|    (Greedy Policy)     | 贪心升级产出最低的资源发生器| (25% Slower)   | 健性，但未考虑全局前瞻规划        |
+------------------------------------------------------------------------------------+
| 4. 有限视界 NES 效用   | 基于自然演化学习效用权重，  | 逼近理论极限   | 关键瓶颈资源 A 维持紧平衡；非必须   |
|    (Finite Horizon NES)| 设定特定终局胜利条件        | (Near Optimal) | 发生器提前停止升级，暴露数值平衡漏洞|
+------------------------------------------------------------------------------------+
```

#### 关键发现与数值调优指导：
1. **被动产出缺口暴露**：在“无限资源”的极端对比实验中，后期净平衡（Net Balance）转为负值，证明系统设计的被动基础产出与后期科技消耗严重脱节；
2. **有限视界引发的早停漏洞（Finite Horizon Early-Stop Exploit）**：在有限视界模拟中，智能体发现若达成胜利并不严格依赖特定次级资源，则最优解是**在终局前彻底停止对该资源发生器的升级注入**。该策略被智能体挖掘后，向设计团队清晰揭示了资源链条间的依赖断层，便于在产品上线前修复次级资源在终局玩法中价值贬损的问题。

---

## 3. 基于搜索的方法：以《The Sims Mobile (TSM)》为例

### 3.1 确定性全局最优求解：$A^*$ 搜索算法

在模拟《The Sims Mobile》（TSM）等生活模拟类游戏中，角色的动作由具有确定性时间与收益的活动（Activities）组成。相比于主流基于采样的强化学习算法（如 Q-Learning、软最大化马尔可夫链蒙特卡洛 Soft-Max MCMC），**$A^*$ 搜索算法**具备天然的确定性与解析优势：

```
                            [ 当前状态 S_curr ]
                                     |
               +---------------------+---------------------+
               | 动作 a1             | 动作 a2             | 动作 a3
               v                     v                     v
          [ 状态 S1 ]           [ 状态 S2 ]           [ 状态 S3 ]
     g(S1) = g + cost(a1)  g(S2) = g + cost(a2)  g(S3) = g + cost(a3)
     h(S1) = Est(S1, Goal) h(S2) = Est(S2, Goal) h(S3) = Est(S3, Goal)
               |                     |                     |
               +---------------------+---------------------+
                                     |
                                     v
                       [ 优先队列 Priority Queue ]
                 按 f(S) = g(S) + h(S) 升序排列（最小堆）
```

- **单次运行得出全局最优解**：随机迭代算法往往需要运行数千次模拟并进行统计平均以消除方差，而 $A^*$ 在满足启发函数可采纳性（Admissible Heuristic）的前提下，仅需单次搜索即可直接计算出到达指定成长状态的全局最短路径或最小成本；
- **跨玩法分支的客观基准建立**：通过对比职业线（Career）、社交线（Social）、爱好线（Hobby）等不同活动路径在全局最优策略下的成长时长，设计人员能够精确衡量不同模块的内容消耗节奏；
- **全自动漏洞检测（Automated Exploit Discovery）**：由于漏洞往往具有“反常规组合却带来极高收益”的特征，全局最优解必定会包含甚至最大化这类漏洞。因此，若 $A^*$ 求解所得的解序列偏离了正常交互直觉（如反复中断并触发特定动作），即可证明该机制存在数值失衡或漏洞。

### 3.2 动作空间退化（Action Degeneration）与优先队列膨胀

在工业级游戏实践中，将 $A^*$ 直接应用于复杂生活模拟系统会遭遇**动作空间退化**的工程难题。

#### 3.2.1 退化机理
在模拟类游戏中，设计团队为了丰富沉浸感，通常会提供大量**视觉表征不同、但机制属性完全等价**的交互选项。例如：
- 动作 $a_1$: “在沙发上看电视” $\to$ 耗时 $30\,\text{s}$，消耗能量 $5$，获得快乐度 $+10$；
- 动作 $a_2$: “听收音机” $\to$ 耗时 $30\,\text{s}$，消耗能量 $5$，获得快乐度 $+10$；
- 动作 $a_3$: “翻阅时尚杂志” $\to$ 耗时 $30\,\text{s}$，消耗能量 $5$，获得快乐度 $+10$。

这些动作对成本函数 $g(n)$ 与启发估计 $h(n)$ 的贡献完全一致，导致：
$$f(n_{a_1}) = f(n_{a_2}) = f(n_{a_3})$$

在图搜索的分支扩展中，这会生成海量机制等价但哈希状态（Hash State）略有差异的状态节点，引发优先队列（Priority Queue / Open List）容量的指数级膨胀，造成严重的内存压力与搜索效率骤降。

#### 3.2.2 解决方案与工程权衡（Trade-offs）

团队评估了两种技术路线以缓解动作退化问题：

1. **人工微扰启发函数（Hand-Tuning Heuristic with Tie-Breaking）**：
   - *实现*：向各动作注入极其微小的偏置项（Epsilon Preferences），破坏成本平局；
   - *缺陷*：偏置项的累加极易破坏启发函数的可采纳性（即 $h(n) \le h^*(n)$），导致算法丢失全局最优解的理论保证，甚至系统性地扭曲模拟结果。
2. **截断优先队列容量（Limiting Priority Queue Size / Beam Search Variant）**：
   - *实现*：设定优先队列的全局最大容量 $K_{\max}$。当队列溢出时，强制剔除 $f(n)$ 较大的低优先级节点（类光束搜索策略）；
   - *代价与收益*：虽然在形式上放弃了全局最优性的理论证明，但内存占用得以严格受控于 $O(K_{\max})$，且最终发现的近似最优策略（Near-Optimal Policy）在评估效率与策略质量上，依然比基于效用函数的演化策略学习（ES Learning）更具竞争力。

---

## 4. 模拟器核心实现与搜索算法架构

以下代码展示了基于无头设计理念实现的轻量化 4X/TSM 类状态转移模型与带容量截断的 $A^*$ 搜索算法核心框架：

```python
import heapq
from typing import List, Dict, Tuple, Optional
import copy

class GameState:
    """
    轻量化无头游戏状态表示
    仅维护核心数值与离散时间步，剔除一切显示与物理层数据
    """
    __slots__ = ('player_level', 'resources', 'building_levels', 'elapsed_time', 'action_history')

    def __init__(self, player_level: int, resources: Dict[str, float], 
                 building_levels: Dict[str, int], elapsed_time: float):
        self.player_level: int = player_level
        self.resources: Dict[str, float] = resources
        self.building_levels: Dict[str, int] = building_levels
        self.elapsed_time: float = elapsed_time
        self.action_history: List[str] = []

    def __lt__(self, other: 'GameState') -> bool:
        # 针对平局时的退化降级判断，防止队列节点比较崩溃
        return self.elapsed_time < other.elapsed_time

    def get_state_signature(self) -> Tuple:
        """生成用于封闭集合 (Closed Set) 判重的紧凑哈希元组"""
        b_levels = tuple(sorted(self.building_levels.items()))
        return (self.player_level, b_levels)

class Action:
    """抽象游戏操作定义"""
    def __init__(self, name: str, duration: float, cost: Dict[str, float], 
                 resource_gain: Dict[str, float], req_level: int):
        self.name: str = name
        self.duration: float = duration
        self.cost: Dict[str, float] = cost
        self.resource_gain: Dict[str, float] = resource_gain
        self.req_level: int = req_level

    def is_applicable(self, state: GameState) -> bool:
        if state.player_level < self.req_level:
            return False
        for r_name, r_cost in self.cost.items():
            if state.resources.get(r_name, 0.0) < r_cost:
                return False
        return True

    def apply(self, state: GameState) -> GameState:
        """事件驱动的高速内存状态推进"""
        new_resources = dict(state.resources)
        # 扣除消耗
        for r_name, r_cost in self.cost.items():
            new_resources[r_name] -= r_cost
        # 注入产出
        for r_name, r_gain in self.resource_gain.items():
            new_resources[r_name] = new_resources.get(r_name, 0.0) + r_gain

        new_building_levels = dict(state.building_levels)
        new_player_level = state.player_level
        
        # 针对升级动作的确定性状态机转换
        if self.name.startswith("upgrade_building_"):
            b_name = self.name.replace("upgrade_building_", "")
            new_building_levels[b_name] = new_building_levels.get(b_name, 0) + 1
        elif self.name == "upgrade_player_level":
            new_player_level += 1

        next_state = GameState(
            player_level=new_player_level,
            resources=new_resources,
            building_levels=new_building_levels,
            elapsed_time=state.elapsed_time + self.duration
        )
        next_state.action_history = state.action_history + [self.name]
        return next_state

class HeadlessSearchPlanner:
    """带优先队列内存截断保护的工业级 A* 规划器"""
    def __init__(self, target_player_level: int, max_queue_size: int = 100000):
        self.target_player_level: int = target_player_level
        self.max_queue_size: int = max_queue_size

    def admissible_heuristic(self, state: GameState) -> float:
        """
        可采纳启发函数：低估达到目标所需的最短理论耗时
        h(n) <= h*(n)，确保算法具备最优性保证
        """
        level_gap = max(0, self.target_player_level - state.player_level)
        # 假定每个等级的最极限提升理论周期为 15.0 时间单位（最乐观估计）
        return float(level_gap * 15.0)

    def search_optimal_progression(self, initial_state: GameState, 
                                  actions: List[Action]) -> Optional[GameState]:
        # 优先队列元素元组结构: (f_score, g_score, sequence_id, state)
        seq_id = 0
        open_list: List[Tuple[float, float, int, GameState]] = []
        
        init_h = self.admissible_heuristic(initial_state)
        heapq.heappush(open_list, (0.0 + init_h, 0.0, seq_id, initial_state))
        
        best_g_costs: Dict[Tuple, float] = {initial_state.get_state_signature(): 0.0}

        while open_list:
            f_cost, g_cost, _, curr_state = heapq.heappop(open_list)

            # 目标检测
            if curr_state.player_level >= self.target_player_level:
                return curr_state

            # 状态扩展
            for act in actions:
                if not act.is_applicable(curr_state):
                    continue

                succ_state = act.apply(curr_state)
                succ_sig = succ_state.get_state_signature()
                succ_g_cost = g_cost + act.duration

                # 剪枝与松弛 (Relaxation)
                if succ_sig in best_g_costs and succ_g_cost >= best_g_costs[succ_sig]:
                    continue

                best_g_costs[succ_sig] = succ_g_cost
                succ_h_cost = self.admissible_heuristic(succ_state)
                succ_f_cost = succ_g_cost + succ_h_cost
                seq_id += 1

                heapq.heappush(open_list, (succ_f_cost, succ_g_cost, seq_id, succ_state))

            # 优先队列容量限制与截断保护（解决动作退化引发的内存爆炸）
            if len(open_list) > self.max_queue_size:
                # 仅保留优先级最高（f_cost 最小）的前 80% 节点
                open_list = heapq.nsmallest(int(self.max_queue_size * 0.8), open_list)
                heapq.heapify(open_list)

        return None
```

---

## 5. 范式转移：以智能体为核心的设计驱动模式（Leveraging Agents as a Primary Design Tool）

长久以来，自动化智能体被视作游戏研发后期的辅助工具，主要应用于交付前的测试覆盖或性能压测。作者团队在经历了多个工业级项目后提出，应实现从“后期被动集成”到“以智能体为首要设计工具”的研发范式转型：

```
传统游戏开发流水线 (Waterfall/Reactive):
[ 策划案撰写 GDD ] -> [ 客户端/服务端全功能开发 ] -> [ 研发晚期接入 AI 智能体 ] -> (暴露数值/架构死结，修改代价极高)

自底向上智能体驱动流 (Simulation/Agent-Enabled):
[ 玩法逻辑离散模型 ]
       |
       v
[ 独立无头模拟器开发 ] <---> [ AI 智能体自动化探索 / NES / A* 闭环调优 ]
       |                                      |
       | 自动代码生成 (Code Generation)         | 验证通过的收敛参数与状态迁移矩阵
       v                                      v
[ 生产级游戏客户端 / 游戏服务器引擎实现 ] <--------+
```

### 5.1 三阶段技术演进历程
1. **集成型插桩客户端（Integrated Instrumented Client）**：在重度客户端内嵌入通信协议与智能体逻辑，初步建立测试闭环，但受制于单步模拟耗时和引擎耦合；
2. **模块化解耦逻辑（Modularized Gameplay Logic）**：剥离表现层，支持按子系统（Sub-systems）独立执行单元级平衡测试，有效验证各玩法的局部闭环；
3. **完全外部无头仿真（External Simulator Driving Design）**：将游戏核心循环彻底实现于独立模拟器中，实现计算吞吐量最大化，使大规模前瞻搜索、全局最优解提取和强化学习训练成为日常可行流程。

### 5.2 自底向上设计（Bottom-Up Agent-First Design）与代码生成
在游戏工程立项之初，设计团队与算法工程师应当协同构建包含核心游戏规则的仿真模拟框架。该框架应具备直接向最终工程代码库进行**代码生成（Code Generation）**的能力。

通过统一逻辑层的数据驱动接口，模拟器中经过严格验证的状态机、行为树节点定义与效用评估函数可以直接编译为目标语言（如 C++、C#），直接部署于游戏客户端与服务器。这彻底消除了“模拟器编写一次、客户端重写一次”的重复工程消耗，使游戏机制在方案确立阶段即达到可测试、可验证的工业级标准。

---

## 6. 核心结论与行业展望（Summary）

1. **突破电子表格的局限性**：传统电子表格仅能处理单线程、静态无博弈的最优解估算；基于智能体的仿真能够引入玩家策略分歧、性格模型，在闭环测试（Change-Test Loop）中自主发现参数漏洞；
2. **算力红利赋能先进算法**：弹性云计算集群彻底打破了算力边界。开发者不仅能够运行低成本的启发式搜索，更可常态化部署自然演化策略（NES）与大规模无约束全局搜索（如 $A^*$、MCTS），在立项早期即确立数值平衡的数学收敛基石；
3. **设计即测试（Design is Testable）**：在现代游戏工业化管线中，驱动智能体的 AI 算法正在从“表现层功能”升级为“基础生产力工具”。依托脱离客户端的轻量化仿真，AI 正在重塑从数值架构、机制探索到自动化测试的完整研发闭环。

---

## 7. 参考文献（References）

* **[Borovikov 18]** I. Borovikov, et al., “Imitation Learning via Bootstrapped Demonstrations in an Open-World Video Game.” *Workshop on Reinforcement Learning under Partial Observability, NeurIPS*, 2018.
* **[Borovikov 19a]** I. Borovikov, et al., “Towards Interactive Training of Non-Player Characters in Video Games,” *ICML Workshop on Human in the Loop Learning*, June 2019.
* **[Borovikov 19b]** I. Borovikov, et al., “From Demonstrations and Knowledge Engineering to a DNN Agent in a Modern Open-World Video Game,” *AAAI-Make*, 2019.
* **[Borovikov 20]** I. Borovikov, “Imitation Learning: Building Practical Agents to Test and Explore a First-Person Shooter,” *GDC*, 2020.
* **[Salimans 17]** T. Salimans, et al., “Evolution Strategies as a Scalable Alternative to Reinforcement Learning,” *arXiv preprint arXiv:1703.03864*, 2017.
* **[Silva 18]** Fernando De Mesentier Silva, et al., “Exploring Gameplay with AI Agents,” *AIIDE*, 2018.
* **[Skiena 90]** S. Skiena, “Coloring Bipartite Graphs.” *Implementing Discrete Mathematics: Combinatorics and Graph Theory with Mathematica*, Addison-Wesley, p. 213, 1990.
* **[SMAC 99]** *Sid Meier’s Alien Crossfire*, Electronic Arts, Official expansion pack for *Sid Meier's Alpha Centauri*, Firaxis Games, PC, 1999.
* **[Xue 17]** S. Xue, et al., “Dynamic Difficulty Adjustment for Maximized Engagement in Digital Games,” *WWW '17: Proceedings of the 26th International Conference on World Wide Web*, pp. 465–471, 2017.
