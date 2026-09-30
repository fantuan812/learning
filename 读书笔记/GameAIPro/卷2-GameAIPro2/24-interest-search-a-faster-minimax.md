---
type: Reference
title: "第24章 Interest Search: A Faster Minimax"
description: "Game AI Pro 工业级精读：Interest Search: A Faster Minimax。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第24章 Interest Search: A Faster Minimax

> 来源：*Game AI Pro 2: More Collected Wisdom of Game AI Professionals*, Chapter 24.  
> 原文作者 / 资源：[Interest Search: A Faster Minimax](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter24_Interest_Search_A_Faster_Minimax.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷2-GameAIPro2](README.md) ｜ [专栏首页](../README.md)

---

---

## 24.1 引言与工业背景（Introduction & Background）

极小化极大算法（Minimax）及其派生的 $\alpha$-$\beta$ 剪枝算法数十年来一直是棋盘博弈（Board Games）与确定性完全信息动态博弈中 AI 决策模型的核心范式。然而，面对巨大的组合爆炸问题（Combinatorial Complexity），经典 Minimax 面临计算瓶颈。在围棋等分支因子过大的领域中，蒙特卡洛树搜索（Monte Carlo Tree Search, MCTS）逐渐成为首选。然而，未受引导的 MCTS 在模拟阶段（Rollout/Simulation Phase）将大量算力消耗在推演至终局的“垃圾变例（Junk Variations）”上，仅有极少比例的着法位于高价值的选择/展开树（Selection/Expansion Tree）中。

在基于经典 Minimax 架构的象棋或将棋程序中，即使结合了完善的着法排序（Move Ordering），随机冻结搜索树即可发现：**绝大多数被遍历评估的变例分支，其价值均处于恶劣到荒谬（Poor to Junk）的区间**。在某一分支中仅仅出现一步荒诞的着法，引擎便不得不为其展开一整棵昂贵的子树进行验证与反驳。

历史上，该困境导致了 Minimax 阵营在香农分类体系（Shannon Framework）下的长期对立：
1. **香农 A 型策略（Shannon Type A / Full-Width Search）**：完全宽度搜索。试图遍历所有变例，依赖 $\alpha$-$\beta$ 剪枝及空着裁剪（Null-Move Pruning）等技术进行数学安全剪枝；
2. **香农 B 型策略（Shannon Type B / Selective Search）**：前向选择性搜索。基于启发式评估对走法列表进行前向剪枝（Forward Pruning），仅搜索受限的优质候选集合。

早期博弈 AI 曾预期 B 型策略能借由较小分支因子换取更深搜索与更高质量的叶节点评估。但历史上 A 型策略长期占据绝对主导，主要根源在于传统选择性搜索的理论缺陷。**兴趣搜索（Interest Search）** 打破了这一僵局，重构了前向剪枝的数学拓扑与判定逻辑，在保持甚至提升实战棋力的同时，实现了数倍至十数倍的搜索加速。该算法在西方顶尖将棋引擎 *Shotest*（曾在日本 CSA 世界电脑将棋争霸赛中两度斩获季军）以及 *Treebeard Chess*（移动端 Elo 超过 99.5% 线上人类玩家）中得到了系统性验证。

---

## 24.2 选择性搜索的几何风险重构（Rethinking Selective Search）

传统前向选择性剪枝的核心致命伤在于**单步选择置信度的几何级衰减（Geometric Compounding Risk）**。

假设某一搜索深度（Ply）的候选走法列表中，启发式模型保留了一个子集。即便该子集包含最佳着法（Key Move）的单步置信度高达 $P = 99\%$，在展开长达 $N = 10$ 层的推演序列时，关键变例得以留存的联合概率为：

$$P_{\text{survival}} = P^N = 0.99^{10} \approx 0.9044$$

这意味着即使在单步保留率极高的情况下，经过 10 步推演后，引擎遗漏核心致胜变例的风险依然超过 $9.5\%$。而在工程实现中，若要使每一个单步列表的置信度达到 $99\%$，剪枝阈值必须极其保守，使得分支剪除带来的计算红利完全被启发式判定本身的计算开销所抵消。

传统的局部视野局限在于：试图在**单步走法列表（Individual Move List）**的微观层面上寻找割裂的分界线。而兴趣搜索从系统拓扑层面将关注点由“单步判定”转移至**“完整变例路径（Selection by Variation）”**。

```
[传统选择性搜索: 单步局部截断]
节点 (Move List) ---> 强制丢弃 Top-K 以外的走法 (单步风险几何累积!)

[兴趣搜索: 变例路径动态阻尼]
根节点 ---> 优质着法(低代价) ---> 优质着法(低代价) ---> 允许深入展开 (Deep Exploration)
      ---> 荒谬着法(高代价) ---> 耗尽兴趣预算 ---> 迅速阻尼浅层截断 (Shallow Cutoff)
```

直觉上，人类棋手在评估某一变例时，若对方或自身走出一步极度可疑或非理性的荒谬着法，并不会为该着法展开精密冗长的后续验证，而是迅速分配极少的算力给予反驳即可。**一旦某个推演变例中引入了一步劣着，系统对继续深入探索该变例的资源分配意愿（Willingness to Explore）应当产生剧烈衰减。**

---

## 24.3 兴趣搜索的核心量化模型（Quantifying the Interest Search）

### 24.3.1 兴趣度与兴趣代价的对偶反转

在工程架构中，为了控制迭代预算，算法采用对偶量化：通过计算**“兴趣代价（Interest Cost）”**来度量变例的展开阻力。
* 极高吸引力、极具战术强迫性的着法 $\rightarrow$ **兴趣代价趋近于 0**；
* 平庸、消极或荒谬的着法 $\rightarrow$ **兴趣代价趋近于极大常数 $K$**。

设单步走法的综合兴趣度度量为 $\mathcal{I}_{\text{move}}$（$\mathcal{I}_{\text{move}} > 0$），则该步着法折算的兴趣代价 $C_{\text{move}}$ 定义为：

$$C_{\text{move}} = \frac{K}{\mathcal{I}_{\text{move}}}$$

其中 $K$ 为正规化缩放常数（在定点数优化中选取以适应硬件字长，如 $K=1000$；在浮点数体系中可设为 $1.0$）。

### 24.3.2 变例迭代终止条件

兴趣搜索完全颠覆了经典迭代加深（Iterative Deepening）中的“按深度迭代（Iterate by Depth）”模式，升级为**“按兴趣预算迭代（Iterate by Interest）”**。每一轮外层迭代设定一个全局兴趣预算阈值 $L_{\text{iteration}}$，并在每次迭代周期递增：

$$L_{\text{iteration}}^{(m)} = L_{\text{iteration}}^{(m-1)} + \Delta L$$

在树搜索递归遍历中，每一个节点评估时维护当前路径累积的净代价 $\mathcal{C}_{\text{net}}$：

$$\mathcal{C}_{\text{net}} = \mathcal{C}_{\text{path}} + \mathcal{C}_{\text{ply\_history}} + C_{\text{move}}$$

各变量定义如下：
* $\mathcal{C}_{\text{path}}$（`total_Path_Interest`）：从根节点到当前层父路径所累积的净兴趣代价；
* $\mathcal{C}_{\text{ply\_history}}$（`total_Interest_This_Ply`）：当前层（Ply）此前已探索完的所有兄弟走法所消耗的净兴趣代价累加；
* $C_{\text{move}}$（`interest_This_Move`）：当前候选着法本身的兴趣代价。

**剪枝裁决规则（Cutoff Rule）**：
若满足：

$$\mathcal{C}_{\text{net}} > L_{\text{iteration}}$$

则在该节点触发兴趣剪枝截断（Cutoff），不再对该着法展开深层递归搜索。若未越界，则将 $\mathcal{C}_{\text{net}}$ 作为新的父路径代价下发传递：

$$\mathcal{C}_{\text{path}}^{\prime} \leftarrow \mathcal{C}_{\text{net}}$$

```
                      [Root Node]
                     /           \
               Move A (Cost=5)    Move B (Cost=30)
                 Net=5              Net=30
                /     \                \
         Move A1(5)  Move A2(35)      Move B1(10)
           Net=10      Net=40           Net=40
          /      \       \                \
     A1a(10)   A1b(15)   A2a(12)         B1a(15)
     Net=20    Net=25    Net=52 (CUT!)   Net=55 (CUT!)
      ...        ...      [阈值 L=40]      [阈值 L=40]
```

---

## 24.4 走法兴趣度多维度特征分类体系（Move Classification & Plausibility Analysis）

在搜索的每一层展开前，引擎执行轻量级**合理性分析（Plausibility Analysis）**，独立于终节点静态评价（Static Terminal Evaluation）。该过程生成两个输出：
1. **合理性先验分数（Plausibility Score）**：用于衡量“着法的客观战术强度”；
2. **兴趣度增量（Interest Metric, $\mathcal{I}_{\text{move}}$）**：用于计算上述搜索代价阻尼。

两者的加权组合直接重构该层的着法排序优先级：

$$\text{SortScore} = \text{Score}_{\text{plausibility}} + \mathcal{I}_{\text{move}}$$

在 *Treebeard Chess* 中，提取了包含 17 项独立特征因子的评分体系：

### 表 24.1: Treebeard 象棋兴趣度特征映射准则表

| 序号 | 触发战术上下文 / 结构特征 | 兴趣度增量 $\Delta \mathcal{I}$ 映射规则 | 工业级工程语义解析 |
| :---: | :--- | :--- | :--- |
| 1 | 基础先验初始化（Base Prior） | $\mathcal{I}_0 = \frac{1000}{N_{\text{legal}}}$ | 反比于合法着法总数 $N_{\text{legal}}$，自动平衡分支度 |
| 2 | 处于被将军状态（In Check） | $\mathcal{I} \mathrel{+}= 1000 \times g(N_{\text{replies}})$ | 应将着法具有强迫性，深度探索解将变例 |
| 3 | 置换表命中着法（Hash Move） | $\mathcal{I} \mathrel{+}= 25$ | 历史深层搜索证实的优质候选 |
| 4 | 上一迭代根节点最佳走法（PV Move）| $\mathcal{I} \mathrel{+}= 100$ | 迭代加深主变例延续性保护 |
| 5 | 杀手走法（Killer Move） | $\mathcal{I} \mathrel{+}= 100$ | 兄弟子树中引发 Beta 截断的高价值非吃子走法 |
| 6 | 全局最佳应对响应（Global Best Reply）| $\mathcal{I} \mathrel{+}= 100$ | 针对前一步着法的全局哈希最佳对抗着法 |
| 7 | 跨层协同响应（2-Plies Best Follow-on）| $\mathcal{I} \mathrel{+}= 100$ | 延续两层前己方战术意图的最佳联动着法 |
| 8 | 兵突击高价值棋子（Pawn Push Attack） | $\mathcal{I} \mathrel{+}= f(V_{\text{target}})$ | 低成本兵种攻击高价值目标，极具战术破坏力 |
| 9 | 吃子走法价值（Capture Value） | $\mathcal{I} \mathrel{+}= f(V_{\text{captured}} - V_{\text{attacker}})$ | 遵循 MVV-LVA 启发式战术价值函数 |
| 10 | 实施将军（Give Check） | $\mathcal{I} \mathrel{+}= 100 \ (\text{若前一步亦为将军则} >100)$ | 强迫性逼着，持续性将军需深挖底线 |
| 11 | 棋子战术进攻（Attack / Fork） | $\mathcal{I} \mathrel{+}= f(V_{\text{target}}) \ (+ \text{Fork\_Bonus})$ | 制造新的威胁，尤其是捉双（Fork）战术 |
| 12 | 逃避战术受击（Move Under Attack）| $\mathcal{I} \mathrel{+}= f(V_{\text{piece}})$ | 规避战术损失的高危防守动作 |
| 13 | 消除将军源（Capture Checking Piece）| $\mathcal{I} \mathrel{+}= 100$ | 绝对防御优先级 |
| 14 | 兵种升变（Pawn Promotion） | $\mathcal{I} \mathrel{+}= 100$ | 极高局面质变事件 |
| 15 | 超出名义深度惩罚（Beyond Depth） | $\mathcal{I} \mathrel{-}= \text{DecayPenalty}$ | 针对极深层平庸走法的自然衰减衰减阻尼 |
| 16 | 牵制脱困（Evade Pin/Tie Target） | $\mathcal{I} \mathrel{+}= f(V_{\text{pin}})$ | 解除局面绝对战术限制的关键调整 |
| 17 | 最优非吃子先验（Best Non-Capture） | $\mathcal{I} \mathrel{+}= 100$ | 局面型静止步长中的结构最优点 |

---

## 24.5 搜索奇偶性反常阻断与博弈对称性解耦（Search Parity & Horizon Decoupling）

### 24.5.1 奇偶破坏佯谬（The Parity Sabotage Paradox）

在将兴趣代价下发模型引入标准极小化极大算法时，会暴露出一个理论缺陷：**着法破坏防御现象（Horizon Avoidance via Dull Moves）**。

* **思维实验**：
  假设在极小化极大搜索中，白方选择走法 $M_1$，经过 3 步常规推演后将进入黑方的必输局面（对白方极端有利）。此时轮到黑方走棋。如果黑方选择正常、合乎逻辑的强抗辩走法 $M_2$（兴趣度高、代价低），则路径净代价累加缓慢，搜索将直击黑方的劣势结局，导致走法 $M_1$ 的 Minimax 倒退值极高；
  反之，如果黑方选择一步极其荒唐、平庸的走法 $M_{\text{dull}}$（兴趣代价极大），该走法瞬间耗尽了剩余的全局兴趣预算，在到达终局灾难之前**强制触发兴趣剪枝（Cutoff）**。叶节点静态评价因地平线效应（Horizon Effect）尚未捕获潜在灾难，返回一个平庸分值。

结果表明：**一方可以通过故意推演恶劣招法来伪造“剪枝”，从而在算法层面掩盖另一方的实质性杀招。**

### 24.5.2 双奇偶独立代价追踪机制（Dual-Parity Cost Tracking）

为了消除上述漏洞，兴趣搜索解耦了单一的路径累加器，在递归调用栈中**按着棋方奇偶性（Parity）独立追踪各自的兴趣代价**：

$$\mathbf{C}_{\text{net}} = \big\langle \mathcal{C}_{\text{PlayerA}}, \mathcal{C}_{\text{PlayerB}} \big\rangle$$

```
                         [Ply n : Player A 走棋]
                       /                         \
         Move A_good (Cost_A = 2)           Move A_bad (Cost_A = 30)
         C_A = C_A + 2                      C_A = C_A + 30
               |                                  |
         [Ply n+1 : Player B 走棋]          [Ply n+1 : Player B 走棋]
         (Player B 试图走平庸步欺骗)        (B 无法通过自身走平庸步截断 A 的预算)
         Move B_dull (Cost_B = 35)          Move B_normal (Cost_B = 5)
         C_B = C_B + 35                     C_B = C_B + 5
               |                                  |
     检查: C_B > Limit ?                检查: C_A > Limit ?
     若 B 耗尽预算: 截断并归还估值,             A 产生截断，但最后一手落子权归 B,
     但此时最后一手由 A 决定!                  B 握有终局优势!
```

* **数学解耦规则**：
  1. 当白方落子时，仅增加 $\mathcal{C}_{\text{White}}$，黑方代价 $\mathcal{C}_{\text{Black}}$ 保持恒定；
  2. 当黑方落子时，仅增加 $\mathcal{C}_{\text{Black}}$，白方代价 $\mathcal{C}_{\text{White}}$ 保持恒定；
  3. 若白方耗尽其自身的兴趣预算（$\mathcal{C}_{\text{White}} > L_{\text{iteration}}$）而引发强制剪枝截断，搜索终止时的**最后一手落子权（Last Move Advantage）归属于黑方**；
  4. 任何一方均无法通过在己方回合选择平庸着法来阻断对方已建立的深层威胁展开，彻底封堵了利用“地平线掩耳盗铃”操纵剪枝的系统漏洞。

---

## 24.6 日本将棋（Shogi）的极端组合复杂度适配

### 24.6.1 将棋的组合爆炸特征

相比国际象棋，日本将棋（Shogi）的搜索复杂度呈指数级剧增：
* **有效分支因子**：国际象棋平均分支因子约为 35，而将棋全盘平均合法走法约为 80，终盘阶段因大量打入规则可飙升至 **200 以上**；
* **打入机制（Drop Rule）**：捕获的敌方棋子成为己方手驹，可以在 $9 \times 9$ 棋盘的几乎任意空位重新放置（打入）。这一规则导致：
  1. **无法通过兑子简化局面（No Swapoff Simplification）**：棋子永远在棋盘与手驹之间循环，无法缩减分支树；
  2. **同质化着法泛滥（Homogeneous Move Flooding）**：在某一复杂局面下，仅单单一枚“飞车（Rook）”的合法打入点可能瞬间达到 40 处，其静态合理性先验极为相似。

### 24.6.2 扩展合理性分析模型（48 维特征系统）

*Shotest* 将将棋合理性评估扩充至 48 个独立特征维度，其中最具辨识度的准则包括：
1. **动态增益安全性（Dynamic Square Safety）**：目标格点当前受击次数相比根节点状态明显净减少；
2. **潜在机动合法性解锁（Newly Legal Moves）**：因前面数步交换而全新解锁的走法（根节点时不合法）；
3. **空间几何拓扑索引（Positional Array Indexing）**：针对将棋特殊棋盘阵型（如矢仓、穴熊）的理想渗透格点数组；
4. **强迫解杀上下文奖励（Anti-Mate Contextual Credits）**：精准防御敌方杀将威胁（詰み / Threatened Mates）的紧迫着法；
5. **负合理性惩罚（Negative Plausibility）**：在同层前序分支搜索中，所有在“最终最佳 $\alpha$ 产生之前”被探索且未引发任何截断的无效试错动作，施加惩罚以压低排位。

---

## 24.7 动态相似度衰减机制（Dynamic Move Suppression via Similarity Decay）

为了彻底解决 40 处飞车打入导致置换表及 $\alpha$-$\beta$ 顶层队列被雷同变例淹没（Flooding）的工程瓶颈，兴趣搜索引入了**基于先验相似度的动态走法重排（Dynamic Move Re-ordering）**。

### 24.7.1 算法拓扑流程

在当前节点探索某一候选着法 $M_k$ 后，搜索并不直接以静态队列遍历 $M_{k+1}$，而是立即基于 $M_k$ 的物理与战术特征，对当前层**剩余所有未搜索着法（Unsearched Moves）执行相似度碰撞检查与惩罚**。

```
[当前层待搜走法集合: M_1, M_2, M_3 ... M_n]
                      |
                      v
      取出当前最优着法 M_curr 执行推演搜索
                      |
                      v
      [相似度碰撞引擎 (Similarity Checks)]
      对所有剩余未搜走法 M_rem 逐一匹配 12 项特征:
      - 是否同为打入 (Drop Move)?
      - 是否同棋子类型 (Piece Type)?
      - 是否同一起点/目标格 (From/To Square)?
      - 是否具有相同战术属性 (Dull / Check / Fork)?
                      |
                      v
      计算动态衰减惩罚:
      Score(M_rem)    -= Penalty_Score(Similarity)
      Interest(M_rem) -= Penalty_Interest(Similarity)
                      |
                      v
      重新排序剩余走法队列 (Re-sort Remaining Moves)
```

### 24.7.2 动态相似度 12 项判定矩阵

1. **无战术组件的平庸步（Dull Moves without Tactics）**：若上一步为平庸步，剩余平庸步大幅降权；
2. **规避威胁的腾退步（Vacate Moves avoiding Threats）**：如逃避吃子威胁的退让；
3. **定点格腾退步（Vacates per Specific Square）**：针对特定受损格点的同构腾退；
4. **预期受损的不安全走法（Unsafe Moves Resulting in Loss）**；
5. **同兵种移动（Same Piece Type Movement）**；
6. **逼杀威胁步（Mate Threats）**；
7. **将军步（Checks）**；
8. **同源或同宿格移动（Matching From-Square or To-Square）**；
9. **打入动作（Drop Moves）**：同类手驹的不同点位打入受到抑制；
10. **垫子/阻挡步（Blocking Moves）**；
11. **制造新威胁的进攻步（Attacking Moves Creating New Threats）**；
12. **抽将/闪击步（Discovered Attacks）**。

通过该机制，一旦引擎尝试了一次“飞车打入防守”且该分支未形成突破，算法将迅速削弱其余 39 个类似飞车打入的评分与兴趣度，将算力立刻转移至“马步跳出”、“金将阻挡”或“玉将规避”等异质结构战术中。

---

## 24.8 剪枝拓扑集成与生产环境基准评测（Benchmarks & Performance Topology）

兴趣搜索并非孤立运行，而是作为骨干框架与经典前向剪枝技术构筑成多阶段流水线。在 *Shotest* 的工业拓扑中，涵盖以下多重防护体系：
1. **剃刀剪枝（Razoring）**：在叶节点附近，若静态评估值加安全裕度仍远低于 $\beta$，则放弃搜索直接返回值；
2. **Gamma 剪枝（Gamma Pruning）**：通过静态或极浅层评估直接比对 $\alpha$ 边界，对绝对无法拉升下界的节点进行截断；
3. **Alpha 失败剪枝（Alpha Fail Pruning）**：若在当前节点连续尝试了多个先验着法仍完全无法刷新当前 $\alpha$，则强行终止后续走法展开。

### 24.8.1 实测对比数据集

以下实验数据基于真实对局（每组 500 局以上对局样本）测定：

#### 表 24.2: 国际象棋（Treebeard Chess）兴趣搜索基准评测

| 实验组编号 | Player A (Interest Search) | Player B (Vanilla Minimax) | 消耗时间对比 (Time Ratio) | 净胜率 (Win%) | 工业工程结论与配置分析 |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **Case 1** | **开启 (ON)** | 关闭 (OFF) | **20.0× 极速 (Faster)** | 41.0% | 在等同深度配置下，算力开销缩减 95%，因前向剪枝遗漏关键步导致胜率略有下滑 |
| **Case 2** | **开启 (ON)** | 关闭 (OFF) | **5.0× 提速 (Faster)** | **50.0%** | **【战力等价点】**：通过增加迭代轮次补偿战力，在**完全等价棋力**下实现净吞吐加速 **5 倍** |
| **Case 3** | **开启 (ON)** | 关闭 (OFF) | 等同用时 (1.0× Time) | **72.0%** | **【资源平价点】**：在相同思考耗时下，深层推演优势形成降维打击，胜率跃升至 72% |

#### 表 24.3: 日本将棋（Shotest Shogi）兴趣搜索基准评测

| 实验组编号 | Player A (Interest Search) | Player B (Vanilla Minimax) | 消耗时间对比 (Time Ratio) | 净胜率 (Win%) | 工业工程结论与配置分析 |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **Case 1** | **开启 (ON)** | 关闭 (OFF) | **119.0× 极速 (Faster)** | 8.3% | 极端激进剪枝；分支因子极大导致大量边缘变例被直接截断 |
| **Case 2** | **开启 (ON)** | 关闭 (OFF) | **14.0× 提速 (Faster)** | **50.0%** | **【战力等价点】**：重平衡迭代次数后，维持**相同竞技实力**下获得 **14 倍** 综合加速 |
| **Case 3** | **开启 (ON)** | 关闭 (OFF) | 等同用时 (1.0× Time) | **90.5%** | **【资源平价点】**：在限定时钟内，兴趣搜索摧毁未优化对手，达成 90.5% 的压倒性统治率 |

---

## 24.9 生产级核心算法逻辑实现（C++17 工业级实现）

以下代码还原了具备双奇偶独立代价追踪（Dual-Parity Cost Tracking）、动态相似度惩罚、以及兴趣边界裁决的搜索实现：

```cpp
#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>

constexpr int32_t INFINITY_SCORE = 30000;
constexpr int32_t INTEREST_CONSTANT_K = 1000;
constexpr int32_t MAX_SIMILARITY_PENALTY_SCORE = 150;
constexpr int32_t MAX_SIMILARITY_PENALTY_INTEREST = 200;

enum Player : uint8_t {
    PLAYER_WHITE = 0,
    PLAYER_BLACK = 1
};

struct Move {
    uint8_t fromSq;
    uint8_t toSq;
    uint8_t pieceType;
    uint8_t isDrop;
    uint8_t isCheck;
    uint8_t isCapture;
    uint8_t isMateThreat;
    int32_t plausibilityScore;
    int32_t interestMetric; // Move interest (I_move)
};

struct SearchParityInterest {
    int32_t whiteCost;
    int32_t blackCost;

    [[nodiscard]] inline int32_t GetCost(Player p) const {
        return (p == PLAYER_WHITE) ? whiteCost : blackCost;
    }

    inline void AddCost(Player p, int32_t cost) {
        if (p == PLAYER_WHITE) {
            whiteCost += cost;
        } else {
            blackCost += cost;
        }
    }
};

class InterestSearchEngine {
public:
    int32_t IterativeDeepeningRoot(const GameState& rootState, int32_t maxInterestThreshold, int32_t interestStep) {
        int32_t bestMoveFound = -1;
        int32_t currentThreshold = interestStep;

        // Iterate by Interest completely replacing Iterate by Depth
        while (currentThreshold <= maxInterestThreshold) {
            SearchParityInterest initialCost{0, 0};
            int32_t iterationScore = SearchMinimax(
                rootState, 
                currentThreshold, 
                -INFINITY_SCORE, 
                INFINITY_SCORE, 
                initialCost, 
                0, 
                rootState.GetActivePlayer()
            );
            
            if (IsTimeExpired()) {
                break;
            }
            currentThreshold += interestStep;
        }
        return bestMoveFound;
    }

private:
    int32_t SearchMinimax(
        const GameState& state, 
        int32_t interestLimit, 
        int32_t alpha, 
        int32_t beta, 
        SearchParityInterest currentPathCost, 
        int32_t ply, 
        Player activePlayer
    ) {
        if (state.IsTerminal()) {
            return state.EvaluateTerminalScore();
        }

        // 1. 生成并计算合理性与初始兴趣度分析
        std::vector<Move> moveList = state.GenerateLegalMoves();
        if (moveList.empty()) {
            return state.IsInCheck(activePlayer) ? (-INFINITY_SCORE + ply) : 0;
        }
        EvaluatePlausibilityAndInterest(state, moveList, ply);

        int32_t totalInterestThisPly = 0;
        int32_t bestScore = -INFINITY_SCORE;
        Player opponent = (activePlayer == PLAYER_WHITE) ? PLAYER_BLACK : PLAYER_WHITE;

        for (size_t i = 0; i < moveList.size(); ++i) {
            // 按当前分数即时选择最高优先级的着法 (Move Ordering)
            SelectBestMoveToFront(moveList, i);
            Move& move = moveList[i];

            // 2. 将兴趣度度量转换为消耗代价: Cost = K / I_move
            int32_t moveInterestCost = INTEREST_CONSTANT_K / std::max(1, move.interestMetric);

            // 3. 奇偶性解耦预算校验: 仅针对当前行棋方计算累积净代价
            int32_t playerNetCost = currentPathCost.GetCost(activePlayer) 
                                  + totalInterestThisPly 
                                  + moveInterestCost;

            // 兴趣剪枝判定
            if (playerNetCost > interestLimit) {
                // 阻尼截断: 该变例耗尽当前玩家兴趣预算，直接执行截断 (Cutoff)
                break; 
            }

            // 4. 下发路径代价结构体更新
            SearchParityInterest nextPathCost = currentPathCost;
            nextPathCost.AddCost(activePlayer, totalInterestThisPly + moveInterestCost);

            // 5. 状态推进与递归推演
            GameState nextState = state.MakeMove(move);
            int32_t score = -SearchMinimax(
                nextState, 
                interestLimit, 
                -beta, 
                -alpha, 
                nextPathCost, 
                ply + 1, 
                opponent
            );

            totalInterestThisPly += moveInterestCost;

            if (score > bestScore) {
                bestScore = score;
            }
            if (score > alpha) {
                alpha = score;
            }
            if (alpha >= beta) {
                // Alpha-Beta 失败截断
                break;
            }

            // 6. 动态相似度衰减计算: 惩罚剩余队列中与当前 move 相似的走法
            ApplySimilarityDecay(move, moveList, i + 1);
        }

        return (bestScore == -INFINITY_SCORE) ? state.EvaluateTerminalScore() : bestScore;
    }

    void ApplySimilarityDecay(const Move& executedMove, std::vector<Move>& moves, size_t startIndex) {
        for (size_t j = startIndex; j < moves.size(); ++j) {
            Move& target = moves[j];
            int32_t penaltyScore = 0;
            int32_t penaltyInterest = 0;

            // 判定 1: 打入相似性惩罚 (Drop Match)
            if (executedMove.isDrop && target.isDrop) {
                penaltyScore += 40;
                penaltyInterest += 50;
            }

            // 判定 2: 同兵种行为惩罚 (Piece Type Match)
            if (executedMove.pieceType == target.pieceType) {
                penaltyScore += 30;
                penaltyInterest += 40;
            }

            // 判定 3: 目标格点相同 (To Square Match)
            if (executedMove.toSq == target.toSq) {
                penaltyScore += 50;
                penaltyInterest += 60;
            }

            // 判定 4: 战术属性同构 (Tactical Dull/Checks)
            if (!executedMove.isCheck && !executedMove.isCapture && 
                !target.isCheck && !target.isCapture) {
                penaltyScore += 20;
                penaltyInterest += 30;
            }

            // 应用衰减与安全下限控制
            target.plausibilityScore -= std::min(penaltyScore, MAX_SIMILARITY_PENALTY_SCORE);
            target.interestMetric = std::max(1, target.interestMetric - std::min(penaltyInterest, MAX_SIMILARITY_PENALTY_INTEREST));
        }
    }

    void SelectBestMoveToFront(std::vector<Move>& moves, size_t currentIndex) {
        size_t bestIdx = currentIndex;
        int32_t bestCompositeScore = moves[currentIndex].plausibilityScore + moves[currentIndex].interestMetric;

        for (size_t i = currentIndex + 1; i < moves.size(); ++i) {
            int32_t composite = moves[i].plausibilityScore + moves[i].interestMetric;
            if (composite > bestCompositeScore) {
                bestCompositeScore = composite;
                bestIdx = i;
            }
        }
        if (bestIdx != currentIndex) {
            std::

---

---

## 1. 绪论与决策搜索范式演化

在现代对抗性博弈与高复杂度游戏人工智能决策系统中，极大极小搜索（Minimax Search）与蒙特卡洛树搜索（Monte Carlo Tree Search, MCTS）构成了空间推理与状态决策的两大基石。然而，在面对具有极高分支因子（Branching Factor）与复杂组合爆炸（Combinatorial Explosion）的环境（如日本将棋 Shogi、国际象棋 Chess 以及各类回合制/即时战术游戏）时，传统算法均暴露出显著的结构性缺陷：

1. **传统 Minimax 的极端脆弱性与“伪截断”问题**：在穷举或均匀加深的搜索路径中，系统不可避免地会遍历到大量偏离人类博弈常理的“怪异盘面/走法”（Odd-ball Positions/Moves）。局势评估函数（Evaluation Function）通常在设计与权重拟合时严重偏向“合理盘面（Reasonable Positions）”，当其被迫为分布之外的极端离群荒谬状态计算分值时，常常出现评估失效，产生反常的虚高评分，从而错误触发 Alpha-Beta 剪枝体系中的 $\beta$ 截断（$\beta$-Cutoff），导致整条关键变例被过早剪除。
2. **评估函数对比域负担过重**：常规启发式估值不仅需要在合理走法集合内给出平滑精准的相对优劣排序，还被迫承担将海量荒谬盘面（Absurd Positions）与合理盘面进行交叉量化对比的职责。这种横跨大跨度无效状态空间的比较，极大稀释了评估函数对微妙战术差异的分辨率。
3. **MCTS 的统计不确定性与模拟开销**：MCTS 通过蒙特卡洛模拟（Monte Carlo Rollouts）收敛最佳路径概率，但其仅能提供渐进概率最优性，无法像 Minimax 那样给出具有严格逻辑证明链条的确定性最佳着法（Near-proven Best Plays）。此外，若在随机或弱启发式模拟阶段耗费过多算力去遍历极低质量走法，模拟效率将严重衰退。

**兴趣搜索（Interest Search）**应运而生。它突破了传统以“整齐几何深度”（Discrete Depth Iteration）推进搜索的死板约束，引入名为**“兴趣度”（Interest）**的连续单一价值货币体系。算法通过对分支进行动态合理性分析（Plausibility Analysis），将计算算力严格聚焦于高概率变例树上，使得树的展开形态自然逼近 MCTS 探索树的非对称稀疏拓扑，同时完整继承了极值推导的战术严谨性。

---

## 2. 兴趣搜索数学模型与理论推导

### 2.1 兴趣度配额衰减与路径展开模型

兴趣搜索的核心机理是将传统的整型深度计数器 $d \in \mathbb{N}^+$ 泛化为一个能量/配额衰减系统。设搜索根节点被赋予初始兴趣资本（Interest Capital）$I_{\text{root}} \in \mathbb{R}^+$。

对于任意博弈状态 $s$，其可用合法走法集合为 $A(s)$。对每个候选走法 $a \in A(s)$，通过领域相关的合理性分析器（Plausibility Analyzer）映射其兴趣消耗量（Interest Cost）或分配折扣因子：

$$c(s, a) \in \mathbb{R}^+, \quad \text{其中 } c(s, a) = f_{\text{cost}}(\text{Plausibility}(s, a))$$

沿搜索路径 $\pi = (a_1, a_2, \dots, a_k)$，第 $k$ 层节点的剩余兴趣量递推定义为：

$$I(s_k) = I(s_{k-1}) - c(s_{k-1}, a_k)$$

终止评估条件（Leaf Termination）为：

$$\text{IsTerminal}(s_k) \iff I(s_k) \le 0 \quad \lor \quad \text{IsGameOver}(s_k)$$

当 $I(s_k) \le 0$ 时，触发叶子节点静态估值函数 $\text{Evaluate}(s_k)$。

```
     [Root: I = 100]
       /        \
c=20  /          \  c=80 (Unreasonable Move)
     v            v
  [Node A]     [Node B]
  (I = 80)     (I = 20)
   /    \         |
  /      \        v (c=20)
 ...     ...   [Leaf / Eval] (I = 0)
 (Deep Sparse   (Pruned/Terminated Early)
  Expansion)
```

### 2.2 搜索树拓扑特性比较

通过动态消耗 $c(s, a)$，兴趣搜索在搜索空间中重构了树的几何形态。不同搜索范式的拓扑特征对比矩阵如下：

| 评估维度 | 传统 Alpha-Beta Minimax | 蒙特卡洛树搜索 (MCTS) | 兴趣搜索 (Interest Search) |
| :--- | :--- | :--- | :--- |
| **驱动机制** | 固定离散几何深度递增迭代 | UCB1 多臂老虎机与模拟统计反向传播 | 基于合理性分析的单一“兴趣度货币”消耗 |
| **拓扑形态** | 对称且稠密（Bushy Tree），深度受限 | 沿高收益分支高度非对称延伸（Deep Sparse） | 沿合理走法线高度非对称深入，离群分支浅层阻断 |
| **离群走法暴露度** | **极高**（遍历荒谬走法，易导致 $\beta$ 伪截断） | **中-高**（随机模拟阶段接触大量低质量走法） | **极低**（仅在严格限制下接触少量边界走法） |
| **算力步进约束** | 刚性几何级数增长 $\mathcal{O}(b^d)$，不可动态微调 | 平滑连续，可随时打断模拟停止 | 连续/微小增量可调，支持根据局势动态控制步长 |
| **产出置信度** | 依赖固定深度的确定性极值证明 | 概率渐进最优（无严格证伪保障） | 近似严格证明（Near-proven Best Plays） |

---

## 3. 核心机制解构：规避 $\beta$ 伪截断与估值解耦

### 3.1 消除 $\beta$ 伪截断机理分析

在传统 Alpha-Beta 剪枝中：

$$\alpha < \beta$$

若极小化层（MIN 节点）的子节点评估值产生异常极大值 $v \ge \beta$，则立即触发剪枝：

$$\text{if } v \ge \beta \text{ then return } \beta \quad (\beta\text{-cutoff})$$

如果该走法 $a_{\text{odd}}$ 属于离群怪异走法（Odd-ball move），该盘面 $s' = \text{Apply}(s, a_{\text{odd}})$ 超出了静态评估函数的有效特征定义域，导致评估模型输出了未经标定的极高分 $v_{\text{err}} \gg \text{TrueValue}(s')$。此时触发的 $\beta$ 截断是一次**伪截断（False Cutoff）**，它直接屏蔽了后续合理防御着法的推导，造成战术失误。

```
                    [MAX Node]
                   /          \
                  /            \
        [MIN Node (β=10)]      ...
          /            \
         / (Odd-ball)   \ (Sound Move)
  [Position X]       [Position Y]
  Eval = 9999        (Unreached due to cutoff!)
  (Beta Cutoff!)
```

兴趣搜索从根源上消除了伪截断的诱因：
1. **输入域过滤**：离群走法被赋予极高的兴趣成本 $c(s, a_{\text{odd}}) \ge I(s)$，导致搜索树无法在离群节点之后展开复杂链条，绝大多数荒谬走法在产生时即被阻断或只允许浅层验证。
2. **评估函数解耦**：评估函数 $\text{Evaluate}(s)$ 无需在高方差的“全空间（包含荒诞盘面）”与“合理空间”之间做权衡折中，仅需在“高度合理的局部空间（A narrow set of related reasonable positions）”内给出高质量的单调排序。

---

## 4. 连续货币驱动的平滑迭代控制（Continuous Budget Stepping）

### 4.1 几何步进（Geometric Stepping）的瓶颈

标准迭代加深 Minimax（Iterative Deepening Search, IDS）依赖离散深度 $d \in \{1, 2, 3, \dots, D\}$。每次迭代时间消耗呈指数级跃升：

$$T(d) \approx \mathcal{O}(b_{\text{eff}}^d)$$

其中 $b_{\text{eff}}$ 为有效分支因子。当 $d$ 较大时，$T(d+1) - T(d)$ 跨度极大，系统极难在硬性时间限制（Hard Time Budget）下精准切分算力，极易因超时而被迫废弃未完成的迭代层。

### 4.2 兴趣搜索的连续可调步进模型

兴趣搜索将加深过程转化为**兴趣配额步进**。第 $m$ 次迭代的初始预算为 $I_m$：

$$I_{m+1} = I_m + \Delta I_m$$

其中 $\Delta I_m$ 是一个连续可控的变量，不再受限于整型深度跃迁：

1. **动态局势微调**：若在当前走法分支遭遇战术震荡（如局面陷入复杂的进攻与换子判断），系统可设置极小的增量 $\Delta I_{\text{small}}$，以平滑展开深度线，精细锁定战术路径（Pin down the line of play）。
2. **平缓局势跳跃**：当局势清晰明朗时，可增大 $\Delta I$，实现快速前向探索。
3. **形态可重构性**：通过调整兴趣分配规则，系统可实时在**浅而密拓扑（Shallow Bushy Tree）**与**深而疏拓扑（Deep Sparse Tree）**之间平滑切换。

```
[Iterative Expansion Control]

  Minimax (Rigid):
  Depth 1 ----> Depth 2 ------------> Depth 3 ---------------------------------> Depth 4 (Explosion!)

  Interest Search (Fluid):
  I = 20 -> I = 35 -> I = 50 -> I = 60 -> I = 70 -> I = 85 -> I = 100 ... (Arbitrary Steps)
```

---

## 5. 工业级系统架构设计与 C++ 实现

以下设计还原工业级战术对弈系统的兴趣搜索核心架构，包含连续兴趣预算消耗机制、Alpha-Beta 极值推导以及可插拔的合理性分析器。

### 5.1 核心数据结构与类接口定义

```cpp
#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <limits>
#include <memory>

// 分值与兴趣度常量定义
constexpr int32_t SCORE_INFINITY = 1000000;
constexpr int32_t SCORE_MATE     = 900000;
constexpr float   EPSILON_INTEREST = 0.001f;

// 游戏走法抽象表示
struct Move {
    uint16_t from    : 6;
    uint16_t to      : 6;
    uint16_t flags   : 4;
    
    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && flags == other.flags;
    }
};

// 状态局面抽象接口
class GameState {
public:
    virtual ~GameState() = default;
    virtual void GenerateLegalMoves(std::vector<Move>& outMoves) const = 0;
    virtual void MakeMove(const Move& move) = 0;
    virtual void UndoMove(const Move& move) = 0;
    virtual bool IsGameOver() const = 0;
    virtual int32_t Evaluate() const = 0;
};

// 合理性分析器：负责计算走法与盘面的合理性并输出“兴趣消耗”
class IPlausibilityAnalyzer {
public:
    virtual ~IPlausibilityAnalyzer() = default;
    
    /**
     * @brief 计算给定走法在当前局面的兴趣成本
     * @param state 当前局面引用
     * @param move 候选走法
     * @return 消耗的兴趣值。合理/强力着法成本低，荒谬走法成本极高
     */
    virtual float ComputeInterestCost(const GameState& state, const Move& move) = 0;
};
```

### 5.2 兴趣驱动的 Alpha-Beta 搜索引擎实现

```cpp
class InterestSearchEngine {
public:
    InterestSearchEngine(std::shared_ptr<IPlausibilityAnalyzer> analyzer)
        : m_analyzer(std::move(analyzer)) {}

    struct SearchResult {
        Move bestMove{};
        int32_t bestScore = -SCORE_INFINITY;
        uint64_t nodesVisited = 0;
    };

    /**
     * @brief 执行带兴趣度预算的加深搜索
     * @param rootState 根节点局面
     * @param startInterest 初始兴趣配额
     * @param stepSize 迭代递增步长
     * @param maxInterest 最大兴趣阈值
     */
    SearchResult SearchIterative(GameState& rootState, float startInterest, float stepSize, float maxInterest) {
        SearchResult finalResult;
        float currentBudget = startInterest;

        while (currentBudget <= maxInterest) {
            SearchResult iterResult;
            iterResult.bestScore = SearchAlphaBeta(rootState, 
                                                   currentBudget, 
                                                   -SCORE_INFINITY, 
                                                   SCORE_INFINITY, 
                                                   true, 
                                                   iterResult.bestMove, 
                                                   iterResult.nodesVisited);

            finalResult = iterResult;
            
            // 工业界实践：如果已经找到绝对杀法，提前终止步进
            if (finalResult.bestScore >= SCORE_MATE || finalResult.bestScore <= -SCORE_MATE) {
                break;
            }

            // 动态调节下一次步进幅度
            currentBudget += stepSize;
        }

        return finalResult;
    }

private:
    int32_t SearchAlphaBeta(GameState& state, 
                            float remainingInterest, 
                            int32_t alpha, 
                            int32_t beta, 
                            bool isRoot, 
                            Move& bestMoveAtNode, 
                            uint64_t& nodesVisited) 
    {
        nodesVisited++;

        // 终止条件判断：兴趣耗尽或达到胜负终局
        if (remainingInterest <= EPSILON_INTEREST || state.IsGameOver()) {
            return state.Evaluate();
        }

        std::vector<Move> legalMoves;
        state.GenerateLegalMoves(legalMoves);

        if (legalMoves.empty()) {
            return state.IsGameOver() ? -SCORE_MATE : 0; // 杀棋或逼和
        }

        // 走法合理性评分结构
        struct ScoredMove {
            Move move;
            float cost;
        };

        std::vector<ScoredMove> scoredMoves;
        scoredMoves.reserve(legalMoves.size());

        for (const auto& move : legalMoves) {
            float cost = m_analyzer->ComputeInterestCost(state, move);
            scoredMoves.push_back({move, cost});
        }

        // 根据兴趣成本正序排序（成本越低越合理，越优先展开以加速剪枝）
        std::sort(scoredMoves.begin(), scoredMoves.end(), [](const ScoredMove& a, const ScoredMove& b) {
            return a.cost < b.cost;
        });

        int32_t localBestScore = -SCORE_INFINITY;
        Move localBestMove = scoredMoves[0].move;

        for (const auto& sm : scoredMoves) {
            float nextInterest = remainingInterest - sm.cost;

            state.MakeMove(sm.move);
            Move dummyMove;
            int32_t score = -SearchAlphaBeta(state, 
                                             nextInterest, 
                                             -beta, 
                                             -alpha, 
                                             false, 
                                             dummyMove, 
                                             nodesVisited);
            state.UndoMove(sm.move);

            if (score > localBestScore) {
                localBestScore = score;
                localBestMove = sm.move;
            }

            if (score > alpha) {
                alpha = score;
            }

            // 核心 Alpha-Beta 剪枝触发
            if (alpha >= beta) {
                break; // Beta Cutoff
            }
        }

        bestMoveAtNode = localBestMove;
        return localBestScore;
    }

private:
    std::shared_ptr<IPlausibilityAnalyzer> m_analyzer;
};
```

---

## 6. 与 MCTS 的交叉融合范式：Simulation Phase 优化

兴趣搜索不仅能独立运作于 Minimax 体系，亦可为现代蒙特卡洛树搜索（MCTS）带来重大算力效率跃迁。

### 6.1 MCTS 模拟阶段算力浪费问题

标准 MCTS 流程包含四步：
1. **选择（Selection）**：根据 UCT/PUCT 算法下沉遍历已有树结构；
2. **扩展（Expansion）**：向外展开新候选叶子节点；
3. **模拟/随机游走（Simulation / Rollout）**：采用随机策略或轻量启发式走子直至终局；
4. **反向传播（Backpropagation）**：将胜负收益回溯更新所有祖先节点。

在第 3 阶段（Simulation Phase），若模拟走法空间完全随机，Agent 会在极其荒谬的低质量走法变例中耗费数以万计的时钟周期，导致反向传播收集到的胜率评估带有巨大的环境方差（Noise Variance）。

```
        MCTS Node
           |
      [Selection]
           |
      [Expansion]
           |
     [Simulation] <--- 传统方式：高随机度，遍历大量极端低质走法
           |          --- 引入兴趣驱动：使用兴趣度过滤着法，阻断荒谬变例
    [Backpropagation]

### 6.2 兴趣驱动的高效模拟（Interest-Guided Simulation）

在 MCTS 模拟阶段中嵌入兴趣衰减约束：
1. **快速走法筛选**：在快速走法生成中，利用合理性分析器计算 $c(s, a)$，将采样分布由均匀分布重权分配为基于兴趣成本的玻尔兹曼分布或直接轮盘赌选择：
   $$P(a \mid s) \propto \exp\left(-\frac{c(s, a)}{\tau}\right)$$
2. **模拟早停机制**：若某条模拟路径在极短时间内累积了过高的兴趣成本（即连续出现荒诞弱步），表明该局势已滑向无效数据域，模拟进程立即调用轻量级静态估值并终止展开，无须走满全局终局。此举大幅缩减每次 Rollout 的 CPU 周期，使 MCTS 在单位时间内获取的有效高质量模拟次数成倍增加。

---

## 7. 工业实践验证与设计总结

### 7.1 实践验证与历史战绩

1. **Shotest 将棋引擎**：
   - 实践结果：在日本世界电脑将棋锦标赛（World Computer Shogi Championships）中两度荣膺**世界第 3 名**。
   - 数据对比：根据作者工程实证，若剥离兴趣搜索机制，仅依靠传统 Minimax 及同级评估函数，引擎战力将大幅下挫，排名难以进入前 20。这直接证明了非均匀合理性分配对超大分支因子棋类的决定性赋能。
2. **Treebeard Chess 与 Microsoft MSN Chess**：
   - 实践结果：作为 Android 平台上装机量极其庞大的国际象棋引擎以及微软 MSN Chess 的底层算法，具备极高拟人化（Human-like）博弈风格。
   - 机理分析：引擎产出高拟人化着法的原因，正是其内部逻辑自动过滤了反人类常理的低劣变例，搜索注意力高度聚敛于人类棋手所关注的精妙战术通道，彻底规避了传统深搜程序频繁出现的“怪诞但冷冰冰的极端无意义走子”。

### 7.2 核心设计准则提炼

1. **统一货币原则（Single Currency Principle）**：使用单一标量“兴趣（Interest）”取代死板的整型计数（深度、扩展步长、静态剪枝阈值），使整套搜索树的展开具备流体般的流动性与动态收敛特性。
2. **估值与搜索对称保护**：通过合理性分析主动构筑屏障，让静态评估函数免于直面极端异常空间，确保评估结果的单调性与稳定性。
3. **连续可调控的资源配给**：摒弃 $\mathcal{O}(b^d)$ 带来的跨代计算量断层，采用连续递增配额，赋予决策系统在严苛工业级 Frame-budget（帧算力预算）下的精密时间控制能力。
