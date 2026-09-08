---
type: Reference
title: "第30章 Hierarchical Portfolio Search in Prismata"
description: "Game AI Pro 工业级精读：Hierarchical Portfolio Search in Prismata。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第30章 Hierarchical Portfolio Search in Prismata

> 来源：*Game AI Pro 3: Balanced Cuts of Game AI Professionals*, Chapter 30.  
> 原文作者 / 资源：[Hierarchical Portfolio Search in Prismata](http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter30_Hierarchical_Portfolio_Search_in_Prismata.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷3-GameAIPro3](README.md) ｜ [专栏首页](../README.md)

---

---

## 1. 行业背景与核心挑战 (Industrial Context & Core Challenges)

在现代在线对抗类策略游戏（Strategy Games）开发中，设计具备高鲁棒性、高竞技强度且可动态调谐的 AI 决策系统始终是工业界的前沿技术难题。以 Lunarch Studios 开发的策略游戏 *Prismata* 为代表，此类游戏兼具即时战略（Real-Time Strategy, RTS）的资源管理、建筑科技树衍生、单位协同机制，以及集换式卡牌游戏（Collectible Card Games, CCG）的组合策略与战术组合深度。

```
+-------------------------------------------------------------------------------+
|                           Prismata 核心对弈特征空间                           |
+-------------------------------------------------------------------------------+
|  1. 完美信息 (Perfect Information)        - 无战争迷雾，全图单位、资源可见    |
|  2. 零和博弈 (Zero-Sum Game)              - 收益严格对立，胜负直接关联        |
|  3. 轮流交替行动 (Alternating Move)       - 离散化回合制状态转移              |
|  4. 确定性机制 (Deterministic Mechanics)  - 无洗牌/抽牌随机性，行动结果确定   |
|  5. 共享随机池 (Shared Randomized Pool)   - 每局开局随机生成公共单位池        |
+-------------------------------------------------------------------------------+
```

### 1.1 动作空间维数灾难与传统博弈算法失效分析

在传统的博弈搜索中，极大极小算法（Minimax）、Alpha-Beta 剪枝搜索（$\alpha$-$\beta$ Pruning Search）及蒙特卡洛树搜索（Monte Carlo Tree Search, MCTS）依赖于对当前状态 $s$ 遍历展开其合法动作集 $A(s)$。

在 *Prismata* 中，单一回合内玩家可操纵数十乃至上百个离散单位，涵盖多阶段的战术行为（防御分配、技能触发、资源采集、单位采购与破防突破结算）。单个回合的复合动作（Compound Turn Action）$m$ 由一系列有序的基本微观动作（Atomic Actions）序列构成：
$$m = \langle a_1, a_2, \dots, a_k \rangle, \quad a_i \in \mathcal{A}$$

设每个游戏阶段存在平均 $K$ 种战术选择，宏观回合由 $M$ 个有序子阶段构成，而每个子阶段又包含组合爆炸级的微观指令分支，单一状态展开出的全局合法动作空间规模可迅速膨胀至 $|A(s)| \gg 10^{6} \sim 10^{15}$。

传统 MCTS 的选择策略（如 UCB1）要求对状态节点下的所有候选动作进行先验探索与访问统计更新：
$$\text{UCB1}(a) = \bar{X}_a + C \sqrt{\frac{\ln N}{N_a}}$$
当候选动作分支因子 $b = |A(s)|$ 极大时，绝大多数计算资源将被浪费在对无效、劣势或冗余动作的初次试探采样上，搜索树在时间预算（Time Budget，通常为毫秒级）内根本无法向下加深，导致树深度严重退化为 1 层的浅显探索，造成算法的彻底失效。

---

## 2. 系统设计愿景与工程目标 (Engineering Objectives & Constraints)

针对上述计算瓶颈与商业化在线运营需求，*Prismata* AI 架构确立了四项核心工程设计准则：

1. **新玩家梯级教学适配（New Player Tutorial & Difficulty Scaling）**
   游戏机制复杂、学习曲线陡峭。系统必须支持自适应的难度分级，能原生覆盖从零基础教学（Beginner）、进阶对抗直至天梯顶尖专家（Expert / Master）的动态阻抗曲线。
2. **单人重玩战术多样性（Single Player Replay Value）**
   杜绝传统硬编码规则系统（Hardcoded Rule-Based Systems）所导致的行为模式固化（Predictable Exploits）。AI 必须在相同博弈态势下呈现出多样化（Diverse）且合乎战术逻辑的应对策略，确保重玩体验不单调。
3. **版本迭代高鲁棒性（Robustness to Live-Ops Balance Changes）**
   在线竞技网络游戏频繁经历数值平衡性热更（Game Balance Patches）、技能重构与机制调整。AI 系统必须完全脱离依赖硬编码数值权重与固定单位属性的静态启发式逻辑，具备对规则演进与数值微调的天然免维护（Zero-Maintenance）鲁棒性。
4. **直观的模块化解耦架构（Intuitive & Modular Architecture）**
   摆脱不可解释的黑盒模型（Black-Box Models）。决策管线需要具备高透明度与模块化接口，使游戏策划（Game Designers）能够直观评估战术原语的效能边界并迅速介入行为拓扑调优。

---

## 3. 分层组合搜索（Hierarchical Portfolio Search, HPS）算法架构

### 3.1 HPS 核心思想与拓扑机理

分层组合搜索（Hierarchical Portfolio Search, HPS）由 David Churchill 和 Michael Buro 提出，是针对高维连续/离散组合决策空间的一种自底向上的双层分层启发式搜索架构（Bottom-up, Two-level Hierarchical Search Architecture）。其拓扑灵感源自现代军事指挥体系的权能解耦与组合推演。

HPS 的核心机理是将原先不可解的、极度庞大的全局动作生成问题，降维分解为若干局部战术领域的“算法投资组合”（Portfolio of Sub-Algorithms）。顶层搜索器仅在此组合空间生成的候选动作集上执行推演，从而在保留高质量战术分支的同时，将有效分支因子强行压缩数个数量级。

```
+-----------------------------------------------------------------------------+
|               Top-Level Game Tree Search (高层博弈树搜索层)                 |
|             Alpha-Beta / Negamax Search / Monte Carlo Tree Search           |
+-----------------------------------------------------------------------------+
                                       ▲
                                       │ 交叉积候选动作空间
                                       │ Moves = crossProduct(m[f])
                                       │
+-----------------------------------------------------------------------------+
|                 GenerateChildren 战术候选生成与阶段交叉积层                 |
|                   m = < m_Defense, m_Ability, m_Buy, m_Breach >             |
+-----------------------------------------------------------------------------+
         ▲                      ▲                     ▲                     ▲
         │                      │                     │                     │
+----------------+     +----------------+    +----------------+    +----------------+
|  Defense 局部   |     |  Ability 局部   |    |   Buy 局部      |    |  Breach 局部    |
|   决策行动子集   |     |   决策行动子集   |    |   决策行动子集   |    |   决策行动子集   |
+----------------+     +----------------+    +----------------+    +----------------+
| pp_def_1(s)    |     | pp_ab_1(s)     |    | pp_buy_1(s)    |    | pp_brc_1(s)    |
| pp_def_2(s)    |     | pp_ab_2(s)     |    | pp_buy_2(s)    |    | pp_brc_2(s)    |
| ...            |     | ...            |    | ...            |    | ...            |
+----------------+     +----------------+    +----------------+    +----------------+
  [Defense 阶段]         [Ability 阶段]         [Buy 阶段]           [Breach 阶段]
                 Portfolio 战术子空间解耦层 (Partial Players)
```

### 3.2 抽象代数模型与基础数学定义

* **状态空间（State Space） $\mathcal{S}$**：
  状态 $s \in \mathcal{S}$ 封装全局对弈状态的完全信息，包括玩家与对手的全部资产、未受保护的防御值、充能槽位与公共可用购买池。
* **微观动作（Atomic Action） $a \in \mathcal{A}$ 与回合宏观动作（Move） $m \in \mathcal{M}$**：
  $$m = \langle a_1, a_2, \dots, a_k \rangle, \quad a_i \in \mathcal{A}$$
  其中宏观动作 $m$ 代表玩家在单一回合内按严格顺序执行的微观动作序列。
* **全局完整玩家策略函数（Player Function） $p$**：
  $$p: \mathcal{S} \to \mathcal{M}$$
  在输入状态 $s$ 下，经过内部推演计算并直接输出全局合法回合动作 $m = p(s)$。
* **游戏确定性状态转移演算函数（Game Function） $g$**：
  $$g: \mathcal{S} \times \mathcal{P} \times \mathcal{P} \to \mathcal{S}_{\text{terminal}}$$
  接收初始输入状态 $s$ 及双方玩家的执行策略 $p_1, p_2 \in \mathcal{P}$，严格按规则向前离散推进对弈状态直至终局节点，输出终局状态 $s' = g(s, p_1, p_2)$。
* **偏序战术子阶段（Move Phases） $\mathcal{F}$**：
  回合时间轴或战术空间划分为 $K$ 个有序阶段：
  $$\mathcal{F} = \langle f_1, f_2, \dots, f_K \rangle$$
* **局部/战术偏玩家函数（Partial Player Function） $pp$**：
  $$pp_f: \mathcal{S} \to \mathcal{M}_f$$
  局部玩家 $pp_f$ 仅关注特定子阶段 $f \in \mathcal{F}$（或特定空间区域、兵种集群），并仅生成属于该受限战术范畴的局部微观动作序列 $m_f \in \mathcal{M}_f$。
* **算法组合池（Portfolio） $\mathcal{P}_{HPS}$**：
  由一系列按战术阶段（或战术类型）分组的局部偏玩家函数集合构成：
  $$\mathcal{P}_{HPS} = \bigcup_{f \in \mathcal{F}} P_f, \quad \text{其中 } P_f = \{ pp_{f,1}, pp_{f,2}, \dots, pp_{f,|P_f|} \}$$

---

## 4. 战术空间解耦与组合构建 (Tactical Decomposition & Portfolio Assembly)

组合池构建（Portfolio Creation）决定了搜索的基底质量。在 HPS 中，顶层搜索的探索空间上界完全受限于各局部策略所能覆盖的动作语义边界。

### 4.1 Prismata 阶段战术分解实例

在 *Prismata* 的落地实践中，单一回合被时序解耦为 4 个串行发生的物理逻辑阶段（Game Phases）。每个阶段分别挂载专属的局部策略偏玩家（Partial Players），如表所示：

| 战术阶段 (Tactical Phase) | 决策逻辑与战术语义 (Semantic Scope) | 局部策略实现原语 (Concrete Partial Players) |
| :--- | :--- | :--- |
| **1. 防御阶段 (Defense)** | 敌方反击结算，必须决定防御单位（Blockers）的消耗与承伤阻挡顺序 | <ul><li>`Min cost loss`（最小经济损耗阻挡）</li><li>`Save attackers`（优先保全高攻/核心输出单位）</li></ul> |
| **2. 技能阶段 (Ability)** | 激活己方存活单位的基础与充能特技，进行能量收集、抽卡或输出预热 | <ul><li>`Attack all`（倾泻所有可用攻击特技）</li><li>`Leave block`（保留关键技能充能用于护盾构筑）</li><li>`Do not attack`（完全积蓄力量、抑制即时战损）</li></ul> |
| **3. 采购阶段 (Buy)** | 消耗金币与各类专属晶体资源，从公共卡池购置新建筑或作战单位 | <ul><li>`Buy attack`（最大化采购攻击型单位）</li><li>`Buy defense`（极限加固防御壁垒，购入阻挡单位）</li><li>`Buy econ`（全力扩张经济，购置资源采集型单位）</li></ul> |
| **4. 突破阶段 (Breach)** | 穿透敌方前排掩体后，将剩余溢出伤害按最优战术顺序点杀敌军核心 | <ul><li>`Breach cost`（优先消灭敌方造价高昂的资产）</li><li>`Breach attack`（优先击杀敌方输出核心，抑制其反击动能）</li></ul> |

### 4.2 组合交叉积机制与分支因子对比

在特定状态 $s$ 下，系统并行触发每个子阶段 $f \in \mathcal{F}$ 内的所有偏玩家函数 $pp \in P_f$，收集局部动作集 $m[f]$：
$$m[f] = \left\{ pp(s) \mid pp \in P_f \right\}$$

全局组合动作空间由所有阶段局部动作的笛卡尔积（Cartesian Product）构成：
$$\mathcal{M}_{\text{turn}}(s) = \prod_{f \in \mathcal{F}} m[f] = m[\text{Defense}] \times m[\text{Ability}] \times m[\text{Buy}] \times m[\text{Breach}]$$

复合动作拼接操作将四元组映射为串行复合序列：
$$m = m_{\text{Defense}} \circ m_{\text{Ability}} \circ m_{\text{Buy}} \circ m_{\text{Breach}}$$

#### 分支因子压缩分析 (Branching Factor Analysis)

* **全排列搜索空间**：假设每个阶段的合法基础动作微观组合均值分别为 $N_{\text{def}} = 10^3, N_{\text{ab}} = 10^4, N_{\text{buy}} = 10^3, N_{\text{brc}} = 10^2$。全局组合空间为：
  $$|A(s)| \approx 10^3 \times 10^4 \times 10^3 \times 10^2 = 10^{12}$$
* **HPS 降维搜索空间**：采用上述组合池配置，阶段策略数量分别为 $|P_{\text{def}}| = 2, |P_{\text{ab}}| = 3, |P_{\text{buy}}| = 3, |P_{\text{brc}}| = 2$。顶层候选宏观动作总数被压缩至：
  $$|\mathcal{M}_{\text{turn}}(s)| = 2 \times 3 \times 3 \times 2 = 36$$

高层搜索算法仅需在 36 个由领域战术原语蒸馏出的高价值候选动作上展开推演，使得两层甚至多层深度的前向博弈搜索（Lookahead Search）在数十毫秒内收敛成为可能。

---

## 5. 对称模拟推演状态评估 (Symmetric Game Playout Evaluation)

在有限搜索预算下，博弈树展开至深度阈值时必须依赖叶子节点启发式评估函数（Leaf Node Evaluation Function）。

### 5.1 传统静态估值函数的缺陷

在早期的抽象博弈（如国际象棋）中，静态评估函数主要依赖手工构造的线性加权加法模型：
$$V_{\text{static}}(s) = \sum_{i} w_i \cdot \phi_i(s)$$
其中 $\phi_i(s)$ 表示局面特征（如兵种基础分值：Pawn=1, Queen=9），$w_i$ 为经验权重。然而在 *Prismata* 等复杂的现代即时策略衍生游戏中，静态权重评估极易失效：
1. 单位价值随博弈时相（Game Phase Dynamics）发生剧烈非线性形变（例如：前期纯经济单位价值远高于防守单位，而绝杀回合经济单位价值归零）；
2. 忽略了多单位联动下的组合协同放大效应（Synergy Effects）；
3. 易受“地平线效应（Horizon Effect）”蒙蔽，无法识别延迟生效的毁灭性组合技。

### 5.2 对称模拟推演（Symmetric Game Playout）原理

为克服静态估值的缺陷，HPS 引入了对称模拟推演机制（Symmetric Game Playout）。

```
叶子节点状态 s (Depth = 0 或 Terminal)
        │
        ▼
初始化对弈双方为同一轻量化确定性策略: e (Playout Player)
        │
        ▼
执行对局推演模拟: s' = Game(s, e, e)
  ┌────────────────────────────────────────────────────────┐
  │ 状态 s: 双方均按照预设策略 e 执行动作，离散推进游戏   │
  │   Turn t+1 : Player 1 执行 m = e(s_t)                  │
  │   Turn t+2 : Player 2 执行 m = e(s_{t+1})              │
  │   ... 直至触发胜负判定或达到最大步数约束 (Terminal)     │
  └────────────────────────────────────────────────────────┘
        │
        ▼
提取终局绝对胜负状态并回传评估分值: s'.eval()
```

#### 数学机理与单调性保证

令 $e \in \mathcal{P}$ 为一个轻量级、确定性的规则策略（Deterministic Rule-Based Policy）。定义对称评估算子：
$$V_{\text{playout}}(s) = \text{Utility}\Big( g\big(s, e, e\big) \Big)$$
* **博弈一致性推论**：即使该公共模拟策略 $e$ 并非最优策略（Suboptimal Policy），当对弈双方对称施加相同的决策行为时，若状态 $s$ 包含内生性战略优势（如经济引擎更健壮、破防先手权更近），则此优势在对称推演的展开过程中具有极高的单调传递概率。
* **抗平衡扰动特性**：此评估机制完全依赖底层物理规则自发结算，摆脱了任何人工指定的局部特征打分项。在游戏数值与卡牌属性大幅迭代后，只要规则引擎本身被更新，对称推演结果自动调整，实现了全生命周期的免标注泛化。

---

## 6. 核心算法实现规范 (Algorithmic Implementation Blueprint)

以下为采用 Negamax 作为顶层驱动器的分层组合搜索（HPS）的完整工程伪代码实现，完全覆盖状态递归展开、组合子节点交叉积生成以及对称终局推演估值。

```python
# ==============================================================================
# 算法清单 30.1: 基于 Negamax 架构的分层组合搜索 (Hierarchical Portfolio Search)
# ==============================================================================

procedure HPS(State s, Portfolio p)
    # 顶层入口: 启动极小化极大 (Negamax) 组合空间搜索
    return NegaMax(s, p, maxDepth)
end procedure


procedure GenerateChildren(State s, Portfolio p)
    # 输入: 当前对弈状态 s, 分阶段局部组合池 p
    # 输出: 组合衍生出的全部后继合法状态集合 children[]
    
    m[] = empty set  # 存储各阶段局部动作的映射表
    
    # 遍历当前游戏状态包含的所有有序移动阶段 f (Defense, Ability, Buy, Breach)
    for all move phases f in s:
        m[f] = empty set
        
        # 并行/遍历执行当前阶段挂载的局部战术策略 (Partial Players)
        for PartialPlayers pp in p[f]:
            # pp(s) 仅计算并生成属于阶段 f 的子动作序列
            m[f].add(pp(s))
        end for
    end for
    
    # 计算全部战术阶段动作集合的笛卡尔交叉积 (Cross Product)
    # 每个组合构件为一个完整回合宏观动作: m_composite = <a_def, a_ab, a_buy, a_brc>
    moves[] = crossProduct(m[f] : move phase f)
    
    # 将生成的组合宏观动作序列原子化应用到当前状态，获得各分支后继状态
    return ApplyMovesToState(moves, s)
end procedure


procedure NegaMax(State s, Portfolio p, Depth d)
    # 输入: 当前节点状态 s, 组合池 p, 递归深度配额 d
    # 输出: 当前状态下的最优极值博弈分值
    
    # 递归基判定: 达到最大搜索深度或遭遇终局节点
    if (d == 0) or s.isTerminal():
        # 实例化轻量化确定性规则推演策略 (Playout Player)
        Player e = playout player for evaluation
        
        # 启动对称推演演算引擎，双方均使用策略 e 进行终局对抗
        # 并返回推进到终局状态后的客观胜负判定值 (如: +1.0 胜利, -1.0 失败, 0.0 平局)
        return Game(s, e, e).eval()
    end if
    
    # 通过组合池阶段交叉积，受限生成子节点分支
    children[] = GenerateChildren(s, p)
    bestVal = -infty
    
    # 极大化极小子节点分值检索
    for all c in children:
        # 符号取反机制消除 Min/Max 条件判断分支
        val = -NegaMax(c, p, d - 1)
        bestVal = max(bestVal, val)
    end for
    
    return bestVal
end procedure
```

---

## 7. 工业级难度梯度构建与实验验证 (Difficulty Tiers & Empirical Evaluation)

### 7.1 模块化分级调谐机制

得益于 HPS 的分层解耦拓扑，调整 AI 难度无需重构底层决策流，仅需通过两个自由度进行正交调优：
1. **替换/限制组合池中的局部策略（Portfolio Tailoring）**；
2. **调节顶层博弈搜索器的算法选型与资源配额（Search Budgeting）**。

*Prismata* 工业级难度配置矩阵如下：

* **Master Bot（天梯宗师级）**：
  组合池装载全部 12 个经过调优的 Partial Players，顶层使用带 UCB1 选择策略的 MCTS 进行推演，硬性限制思考时限为 3000 ms，在高决策强度与玩家等待容忍度之间取得最优平衡。
* **Expert Bot（专家级）**：
  沿用 Master Bot 相同的 12 个 Partial Players 组合池，顶层降级使用 2-ply Alpha-Beta（Negamax）剪枝搜索，单步决策时延严格压制在 100 ms 内。
* **Medium Bot（中级）**：
  取消高层前向博弈搜索（0-ply），直接从 Master Bot 组合池的阶段交叉积动作集中执行等概率均匀随机抽样（Random Selection）。
* **Easy Bot（初级）**：
  继承 Medium Bot 的执行框架，但强行削弱防御阶段（Defense）与采购阶段（Buy）的 Partial Players，剥离针对防御壁垒的最优规划逻辑。
* **Pacifist Bot（沙袋人偶级）**：
  继承 Medium Bot 框架，但从其组合池中永久移除进攻型 Partial Player（从不发动实质性伤害打击，仅供纯新手感知基础机制）。
* **Random Bot（基底对照级）**：
  完全脱离组合池约束，每一步均在全量合法动作集中完全随机抛掷微观动作。

### 7.2 10,000 局单循环赛矩阵对决基准

为验证 HPS 在不同难度配置下的离散单调性与竞技梯度分离度，系统在各级别 Bot 之间执行了 10,000 局高并发单循环锦标赛（Round Robin Tournament）。

对局胜率公式统一定义为：
$$\text{Score} = \text{Win\%} + \frac{\text{Draw\%}}{2}$$

测试矩阵如下表所示（行级别 Bot 对抗列级别 Bot）：

| 对阵配置 (Row vs Col) | UCT100 | AB100 | Expert | Medium | Easy | Random | 综合胜率均值 (AVG) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **UCT100** (MCTS-100ms) | — | 52.1 | 67.3 | 96.4 | 99.7 | 99.9 | **83.1%** |
| **AB100** ($\alpha$-$\beta$-100ms) | 47.9 | — | 68.0 | 94.7 | 99.5 | 99.9 | **82.0%** |
| **Expert** (2-ply $\alpha$-$\beta$) | 32.7 | 32.0 | — | 90.7 | 98.9 | 99.8 | **70.8%** |
| **Medium** (Random from Portfolio) | 3.6 | 5.3 | 9.3 | — | 85.9 | 97.4 | **40.3%** |
| **Easy** (Weakened Defense Portfolio)| 0.3 | 0.5 | 1.1 | 14.1 | — | 86.3 | **20.5%** |
| **Random** (Pure Random Actions) | 0.1 | 0.1 | 0.2 | 2.6 | 13.7 | — | **3.3%** |

*(注：Pacifist Bot 因设计上禁止进攻，无法达成胜利条件，故在基准矩阵中予以剔除。)*

#### 数据解构结论
1. **梯度单调递减**：从 UCT100 的 83.1% 线性收敛至 Random 的 3.3%，难度层级完全契合系统预设的认知梯队，无任何倒挂现象。
2. **搜索层效能增益**：对比无深层搜索的 Medium Bot（40.3%）与搭载 2-ply 搜索的 Expert Bot（70.8%），证明在组合池剪枝基础上的高层推演带来了超过 30% 的净胜率跃迁。
3. **算法平替性验证**：在同为 100 ms 计算预算下，UCT 仅以 52.1% 对 47.9% 的极微弱优势领先 Alpha-Beta，证明在组合池将动作空间高度规整后，高层搜索器的结构依赖性显著降低。

---

## 8. 真实生产环境实测与架构演进 (Production Ladder Deployment)

### 8.1 天梯盲测系统集成 (Stealth Ranked Ladder Deployment)

为消除由于玩家“对抗 AI 时的投机倾向”（Bot Exploitation）而引发的数据偏置，开发组构建了专用的匿名对战客户端，将 Master Bot 隐匿注入官方排位赛天梯（Ranked Ladder）。

```
+─────────────────────────────────────────────────────────────+
|               Prismata 天梯隐匿对决管线架构                 |
+─────────────────────────────────────────────────────────────+
                            │
                            ▼
    [匿名排位匹配队列] ──> 获取真实人类对手 (Tier 1 ~ Tier 10)
                            │
                            ▼
    [拟人化操作注入层] ──> 施加随机化人机点击时延扰动 (Anti-Detection)
                            │
                            ▼
    [Master Bot 核心决策] ──> MCTS + 12 战术算子 (Think Time = 3000ms)
                            │
                            ▼
    [对局结束自动结算] ──> 记录积分、天梯段位爬升、重新入队
```

### 8.2 竞技段位表现

* **天梯分级体系**：*Prismata* 天梯采用阶梯晋级机制，由 Tier 1 起步逐步跨越至 Tier 10。进入 Tier 10 后开启类似国际象棋的 Elo 积分结算。
* **盲测战绩**：Master Bot 在 48 小时内持续运行，完成了超过 200 局高强度真人天梯对抗，顺利冲上 **Tier 6 伴随 48% 晋级点数**，并在该高竞技分段稳定横盘长达数小时。

```
Prismata 人类天梯玩家段位分布 (Ranked Ladder Tier Distribution)
% 玩家
35% | [***] (Tier 1: 34%)
30% |
25% |       [***] (Tier 2: 22%)
20% |
15% |
10% |             [*] (Tier 3: 7%) [*] (Tier 4: 7.5%) [*] (Tier 5: 6.8%)
 5% |                                                       | (Bot: Tier 6.48)
 0% +-------------------------------------------------------▼-----------
      T1    T2    T3             T4    T5             T6    T7    T8    T9    T10
                                                      ===================
                                                      Master Bot 稳居人类前 25%
```

该表现直接将 HPS 系统的实战竞技强度推演至**全服排位人类玩家的前 25%（Top 25%）**。

### 8.3 工业迭代与生产演进经验

在基准实测完成后，工程团队持续对战术层执行了定向针对性重构，主要集中于：
1. **防御与突破阶段的精细化规则重构**：修复了极端阻挡拓扑下的过量损耗 Bug，重写了点杀判定树；
2. **推演评估器（Playout Player）策略升级**：优化对称模拟过程中的科技树偏好；
3. **进攻逻辑除虫**：根除了特定状态下由于候选动作组合不当导致的空放战力（Attack Blunder）。

根据工程演进估算，升级后的 Master Bot 竞技实力已稳固跨入 **Tier 8 门槛**，超越该游戏**前 85% ~ 90% 的人类竞技玩家**，验证了分层组合搜索（HPS）在大规模离散博弈系统中的实用价值。
