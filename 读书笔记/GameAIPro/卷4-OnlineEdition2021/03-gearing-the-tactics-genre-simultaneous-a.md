---
type: Reference
title: "第3章 Gearing the Tactics Genre: Simultaneous AI Actions in Gears Tactics"
description: "Game AI Pro 工业级精读：Gearing the Tactics Genre: Simultaneous AI Actions in Gears Tactics。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第3章 Gearing the Tactics Genre: Simultaneous AI Actions in Gears Tactics

> 来源：*Game AI Pro 4 (Online Edition 2021)*, Chapter 3.  
> 原文作者 / 资源：[Gearing the Tactics Genre: Simultaneous AI Actions in Gears Tactics](http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter03_Gearing_the_Tactics_Genre_Simultaneous_AI_Actions_in_Gears_Tactics.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷4-OnlineEdition2021](README.md) ｜ [专栏首页](../README.md)

---

## 引言与设计背景 (Introduction & Motivation)

### 战术游戏类型的演进与设计挑战
回合制战术游戏（Turn-Based Tactical Games）在 20 世纪 90 年代末逐渐淡出主流大众市场，直到 2012 年 Firaxis 推出重启之作《幽浮：未知敌人》（*XCOM: Enemy Unknown*），该类型才在全球范围内迎来全面复兴。与之相对，《战争机器》（*Gears of War*, 2006）被公认为现代第三人称掩体射击（Third-Person, Cover-Based Shooter）合作游戏的奠基之作，其核心体验建立在实时的战术站位、掩体利用、小队协同以及高压下对抗涌现而出的兽族（Locust）蜂群。

《战争机器：战术小队》（*Gears Tactics*）的工程目标在于将这两种截然不同的体验进行系统化融合：**在一款快节奏、单人、无网格（Gridless）限制、具备物理弹道计算（Live Bullets）和半程序化生成地图（Semi-Procedural Generated Maps）的回合制战术游戏中，重现《战争机器》标志性的高速战斗与压迫感。**

```
                ┌──────────────────────────────────────────────┐
                │        《战争机器：战术小队》核心设计诉求          │
                └──────────────────────┬───────────────────────┘
                                       │
        ┌──────────────────────────────┴──────────────────────────────┐
        ▼                                                             ▼
┌───────────────────────────────┐             ┌───────────────────────────────┐
│     战术游戏的博弈深度         │             │    《战争机器》IP 的核心质感    │
│  • 严谨的站位与掩体利用       │             │  • 兽族兽群的高密度压迫感     │
│  • 多兵种技能与装备的协同组合 │             │  • 快节奏、高击杀反馈与爽快感 │
│  • 信息完全透明下的策略对抗   │             │  • 残暴、迅捷的近战与突脸体验 │
└───────────────────────────────┘             └───────────────────────────────┘
```

### 兵种角色映射与决策空间坍缩问题
在实时射击游戏到回合制战术游戏的移植过程中，AI 行为逻辑即便发生变化，其带给玩家的“心理感受”与兵种辨识度（Identity）必须保持一致。开发团队早期原型实现了两类基础单位：
1. **兽族兵（Drone / 镜像单位 Mirror Unit）**：持有突击步枪在掩体间进行推进与射击，其能力配置与玩家角色镜像对称。采用常规的回合制 AI 处理（玩家与 AI 单位数量接近）即可获得极佳的博弈深度。
2. **苦工（Wretch / 近战敏捷单位 Melee Unit）**：体型小、移动迅速、纯近战。

在早期原型验证中，苦工的实现暴露出严峻的设计缺陷：
* **数量过少导致决策空间（Decision Space）坍缩**：若苦工数量与兽族兵相同，玩家总能在远距离将它们全部击杀。此时玩家的最优解恒定为“一旦进入视线立即射杀”，决策空间退化为单一解。苦工几乎无法进入肉搏射程，失去战术威胁。
* **数量提升引发回合拖沓（Dull Turn Problem）**：当以原作中蜂群般的规模生成苦工时，玩家虽然获得了应对压迫感与大量击杀的正向反馈，但在传统回合制模式下，每个苦工必须被依次选中、计算最优行动、串行执行位移与攻击。由于苦工初始距离玩家极远且移动方向一致，玩家在观察完第一个单位移动后即可获知所有信息，随后被迫经历漫长且枯燥的单位串行动画播放过程。

---

## 战术清晰度与核心原则 (Tactical Clarity)

### 串行与全并行执行的博弈陷阱
为了解决回合耗时过长的问题，团队尝试让所有 AI 单位在回合开始时**同时（Simultaneously）执行所有行动**。这种极端的并发执行虽然极大缩短了回合等待时间并带来了动作大片的视觉冲击，却催生了毁灭性的游戏性缺陷：
* **信息过载与遗漏**：多个关键战术事件在视野各处同时发生，玩家无法捕捉重要的战场变化（如敌方走位、突进和技能施放）。
* **物理弹道逻辑穿帮**：因缺乏时序约束，出现了友军在前方移动而遮挡后方射击弹道、甚至被后方开火的友方子弹命中的荒诞情景。
* **表现退化为“无脑”**：在博弈论与认知心理学层面，**无论 AI 内部规划逻辑多么严密，如果玩家未能清晰观测、理解其行动的过程与因果影响，该行为在玩家眼中就会被视作随机甚至愚蠢的垃圾表现。重要信息不仅需要被呈现，更需要被突出强调（Highlighted）。**

### 传统群体算法在自由移动战术游戏中的局限
开发团队评估了基础群集算法（Flocking Behaviors）、领队跟随算法（Follow-the-Leader）以及动态编队拆分技术（Dynamic Squad Splitting）。在《战争机器：战术小队》的环境下，这些传统方案均宣告失效：
* **无网格自由移动（Gridless Free Movement）**：系统不存在离散格子的物理约束，空间推理与寻路更为连续。
* **极度悬殊的地形尺度**：关卡包含从极开阔的室外广场到狭窄复杂的室内通道。传统小队（Squad）强行绑定目标会导致单位无法拆分；而在狭小空间内，由于目标玩家周围的物理站位空间（Engagement Slots）有限，小队若无法解耦自主规划，突进单位将无法有效包围玩家，整体威胁度骤降。
* **异构单位混编限制**：传统小队倾向于同质单位同步，无法有效支持不同体型、血量与职责的异构单位混合协同。

### 战术清晰度三大定律 (The Three Rules of Tactical Clarity)
基于对“高速节奏、高敌方密度、无网格、物理弹道、半程序化地图”目标的收敛，团队确立了战术清晰度的三大铁律：

| 规则编号 | 规则名称 | 工业级实现原理与设计意图 |
| :--- | :--- | :--- |
| **规则 1** | **禁止直接攻击多目标并发**<br>*(Never attack more than one player unit with direct attacks simultaneously)* | 玩家的视觉焦点在同一时刻只能锁定一处遭受直接伤害的区域。若两个玩家角色在不同屏幕区域同时挨打，玩家无法评估伤害源和战损严重程度。直接攻击必须在时序上串行化或聚焦于单点。 |
| **规则 2** | **最小化单单位行动碎裂度**<br>*(Try to split the actions of a single unit as little as possible)* | 避免单一单位在回合中执行“移动一段 $\to$ 等待其他单位 $\to$ 再移动一段 $\to$ 开火”的割裂时序。单个单位的规划动作链应尽量保持连贯，防止玩家认知产生断层。 |
| **规则 3** | **剧烈局势变更动作独占播放**<br>*(Highlight actions that apply drastic changes by playing them exclusively)* | 针对对战场拓扑产生颠覆性影响的动作（如将玩家击退脱离掩体的 Push Back 技能、高爆手雷投掷、处决等），系统强制挂起其他并发行为，使用专属镜头（Cinematic Focus）和独占时间片进行特写广播。 |

---

## 分层 AI 系统架构 (Layered AI Architecture)

《战争机器：战术小队》采用管道化的分层规划架构（Layered Planning Architecture），高层级的输出作为低层级的输入，逐层精化并推演战场计划，最终通过组合动作分析器打包生成符合战术清晰度约束的并发执行指令。

```
                     ┌─────────────────────────────┐
                     │   关卡脚本层 Level Scripting  │
                     └──────────────┬──────────────┘
                                    │ (输入/传递原始计划)
                                    ▼
                     ┌─────────────────────────────┐
                     │   目标规划层 Goal Planning   │
                     └──────────────┬──────────────┘
                                    │ (更新虚拟世界状态)
                                    ▼
                     ┌─────────────────────────────┐
                     │   单位规划层 Unit Planning   │
                     └──────────────┬──────────────┘
                                    │ (生成全量回合计划)
                                    ▼
                     ┌─────────────────────────────┐
                     │ 组合动作分析器 Combo Analyzer │
                     └──────────────┬──────────────┘
                                    │ (插入同步标记与包聚合)
                                    ▼
                     ┌─────────────────────────────┐
                     │   计划执行层 Plan Execution  │
                     └─────────────────────────────┘
```

### 虚拟世界状态 (The WorldState)
在全回合预规划（Pre-planning）模式下，如果后续规划的单位无法感知先行动单位已经预定的世界变更，就会引发致命的时空冲突：
* **空间抢占冲突（Spatial Conflict）**：单位 1 规划移动到点 $P$ 寻找掩体，而单位 2 在规划时尚未感知点 $P$ 已被占用，同样规划移动至 $P$，导致最终物理碰撞阻挡或重叠（见图 2a）。
* **视线遮挡冲突（Line-of-Sight Conflict）**：单位 1 规划在掩体内射击玩家目标，单位 2 随后规划移动路线，其路径恰好横切穿过单位 1 与目标之间的射击弹道，导致开火瞬间友军误伤或弹道受阻（见图 2b）。

```
        图 2a：空间位置抢占冲突                     图 2b：射击弹道与视线遮挡冲突
             单位 1       单位 2                        单位 1 (射手)
                \         /                                │
                 \       /                                 ▼ (原定射击弹道)
                  ▼     ▼                             ┌─────────┐
               ┌───────────┐                          │ 单位 2  │ ──► (横切路线)
               │ 同一掩体点 │                          └─────────┘
               └───────────┘                               │
                                                           ▼
                                                      ┌─────────┐
                                                      │  玩家   │
                                                      └─────────┘
```

#### WorldState 运行机制
`WorldState` 是脱离当前物理游戏世界（Game World）的一份抽象轻量级内存副本。
1. **沙盒式前向推演**：AI 单位所规划的每一项技能（Ability），均先在 `WorldState` 中执行虚拟模拟，而非直接修改游戏物理世界。
2. **状态全量涵盖**：涵盖所有单位的预测生命值（Health）、预测空间坐标（Transform / Loc）、掩体占位标记（Cover Reservation）、状态效果（Status Effects）以及弹道空间走廊（Ballistic Corridor）。
3. **基于节点的技能架构（Node-Based Ability System）**：技能系统被全面解耦为节点序列。每个技能节点天然具备两套执行通道：
   * **模拟通道（Simulation Channel）**：将该节点产生的影响写入 `WorldState`。策划配置新技能时无需编写专门的 AI 预测代码，天然获得跨回合前向推演支持。
   * **物理执行通道（Execution Channel）**：底层渲染与物理世界真正的逻辑执行。
4. **决策提交与反作弊时序**：**AI 仅能在做出确定行动决策后，才将预测结果写入 `WorldState`。** 绝对不允许 AI 在决策评估阶段为了“寻找命中概率最高的结果”而逆向读取随机数生成器（RNG）的模拟状态，否则会导致敌人“算准了这一枪会空从而拒绝开枪”等作弊感极强的破坏体验行为。

---

## 计划生成层体系 (Plan Creation Layers)

### 关卡脚本层 (Level Scripting)
* **设计意图**：允许关卡策划（Level Scripters）完全接管或部分限制特定 AI 单位在一个或多个回合内的行为，主要用于新手引导（Tutorial）、Boss 战特定阶段或剧情演出。
* **无缝集成机制**：脚本层直接注入预设的技能节点到单位规划队列中，底层同样触发 `WorldState` 的模拟推演。对于系统后续层级和外部玩家而言，脚本接管的单位与自主思考的 AI 单位在架构语义上完全一致，维持了游戏逻辑表现的恒常性。

### 目标规划层 (Goal Planning)
战术游戏要求 AI 在回合开始时一次性提交一系列连贯的动作决策，并在随后的执行中接受玩家的全程审视。AI 必须处理宏观维度的攻防任务：
* 宏观攻防：夺取并守卫占领点、解救/看守折磨舱（Torture Pods）中的囚犯。
* 全局战术时序协同：例如增益单位（Buffer）必须先为突击单位施加增益 Buff，突击单位才能执行高威力射击。

#### 模糊逻辑分配系统 (Fuzzy Logic Assignment System)
系统引入基于模糊逻辑（Fuzzy Logic）的全局目标黑板。目标数据实例（Goal Data Instance）根据战场实体动态实例化。

##### 数据契约规范：以处决倒地濒死（DBNO）目标为例
下表展示了一个典型处决倒地（Down-But-Not-Out, DBNO）目标的元数据配置规范：

| 属性字段 (Field) | 配置参数与逻辑表达 | 逻辑说明 |
| :--- | :--- | :--- |
| **目标 (Target)** | `DBNO Enemy` | 场景中每个处于倒地濒死状态的玩家单位均实例化一个目标对象 |
| **销毁条件 (Destruction)** | `Target Invalid` | 目标彻底死亡、被救援复活或物理实体销毁 |
| **激活条件 (Activation)** | $\text{CanBePerceived}(\text{Target}) == \text{True}$ | 该目标处于 AI 阵营的全局感知/视线范围内 |
| **钝化条件 (Deactivation)** | $\neg(\text{CanBePerceived}(\text{Target}))$ | 失去目标视线或感知丢失 |
| **静态优先级范围 (Priority)** | $[60.0,\, 69.0] \times f(\text{DistanceToFriendly})$ | 模糊插值：目标距离 AI 友军越近，基础优先级越高 |
| **容量限制 (Max Score / Units)**| $1.0 \;/\; 1$ | 互斥性目标：同一时刻只允许 1 个 AI 单位认领该处决任务 |
| **候选适配权重 (Assignable Units)**| • **苦工 (Wretch)**: $1.0 \times g(\text{Dist})$<br>• **兽族兵 (Drone)**: $0.9 \times g(\text{Dist})$ | 苦工属于近战专长单位，具有更高的基础认领权重 |
| **子目标集 (SubGoals)** | $\emptyset$ (Empty) | 无嵌套子任务 |

##### 目标迫切度 (Insistence) 数学评估模型
目标并非静态争抢，其运行期优先级（Insistence，即迫切度）由模糊隶属度函数实时动态调制。设某一防御或交互目标 $G$，其基础优先级区间为 $[P_{\min}, P_{\max}]$，则当前实例的迫切度 $I(G)$ 综合空间距离、血量威胁及宏观态势动态计算：

$$I(G) = P_{\min} + (P_{\max} - P_{\min}) \cdot \mu_{\text{Context}}(G)$$

其中，环境上下文隶属度 $\mu_{\text{Context}}(G) \in [0, 1]$ 由多项子因子加权聚合而成：

$$\mu_{\text{Context}}(G) = w_d \cdot f_{\text{dist}}(d) + w_h \cdot f_{\text{hp}}(HP) + w_o \cdot f_{\text{obj}}(O)$$

各分项因子定义如下：
* **距离衰减因子 $f_{\text{dist}}(d)$**：
  $$f_{\text{dist}}(d) = \text{clamp}\left(1.0 - \frac{d - d_{\min}}{d_{\max} - d_{\min}},\, 0.0,\, 1.0\right)$$
  其中 $d$ 为目标距最近候选单位的连续几何欧氏距离。
* **血量状态因子 $f_{\text{hp}}(HP)$**：
  $$f_{\text{hp}}(HP) = 1.0 - \frac{HP_{\text{current}}}{HP_{\text{max}}}$$
  对于保护友军目标，友方残血程度越高，迫切度提升越显著。
* **占领与阵营权值因子 $f_{\text{obj}}(O)$**：
  用于衡量目标点当前被友方占据的单位数量。若占领点内已有多个友军防守，则该目标的防守紧迫度急剧下降；若完全未被占领，则紧迫度提升至极值。

##### 动态重排与回溯管线
系统遍历按 $I(G)$ 降序排列的目标队列，将最优候选单位以 Job 形式派发。若单位的规划子行为树执行失败，系统将触发回滚机制（Rollback），恢复 `WorldState` 并将该目标交由下一顺位单位仲裁。

```
[全局目标黑板]
  │  1. 实例化全部场景目标 (Goal Instances)
  │  2. 计算模糊迫切度 Insistence: I(G)
  ▼
[目标队列 (按 I(G) 降序排序)]
  │
  ├─► 弹出当前最高优先级目标 G*
  │     │
  │     ├─► 检索候选单位集合 {U}，计算适配分数 Score(U, G*)
  │     │   选取最优单位 U*
  │     │
  │     ├─► 向单位 U* 派发 Job 并传入目标上下文
  │     │   U* 调用对应子行为树 (Sub-BT)
  │     │
  │     ├── [规划成功] ──► 标记目标 G* 满足 ──► 更新 WorldState ──► 重新评估各目标迫切度并重排
  │     │
  │     └── [规划失败] ──► 触发回滚 (Rollback WorldState) ──► 将 G* 尝试分配给次优单位或降低优先级
  │
  └─► 重复迭代，直至全部单位耗尽 AP 或所有目标均分配完毕
```

#### 目标驱动的单位子行为树规划 (Sub-Behavior Tree Planning)
当单位被分配到具体的任务类型（如 `ExecuteEnemy`）时，会调用对应的专用子行为树进行微观推演。

```
                    ┌──────────────────────────────────────────────┐
                    │      Selector: 规划执行 DBNO 敌方目标         │
                    └──────────────────────┬───────────────────────┘
                                           │
                                           ▼
                    ┌──────────────────────────────────────────────┐
                    │                Sequence 节点                 │
                    │  [条件: AssignedToGoalType(ExecuteEnemy)]    │
                    │  [条件: NotMovedThisTurn]                    │
                    │  [条件: NotAttackedThisTurn]                 │
                    └──────────────────────┬───────────────────────┘
                                           │
        ┌──────────────────────────────────┼──────────────────────────────────┐
        ▼                                  ▼                                  ▼
┌─────────────────────────┐    ┌─────────────────────────┐    ┌─────────────────────────┐
│     Find Location       │    │      Queue Ability      │    │      Queue Ability      │
│ 寻找至目标的有效处决站位  │───►│       Move 技能         │───►│       Execution 技能    │
│ (写入 Blackboard 变量)  │    │ (写入 WorldState 沙盒)  │    │ (写入 WorldState 沙盒)  │
└─────────────────────────┘    └─────────────────────────┘    └─────────────────────────┘
                                                                              │
                                                                              ▼
                                                               ┌─────────────────────────┐
                                                               │    End Goal Planning    │
                                                               │  规划成功，向上返回 Success │
                                                               └─────────────────────────┘
```

* **失败回滚规范**：若 `Find Location` 由于地形阻挡、AP 不足无法寻得有效路径，或后续 `Queue Ability` 出现逻辑冲突导致任意节点返回 `FAILURE`，整棵子树立即中断。此时系统执行**反向补偿（Undo Simulation）**，撤销此前该树内部的 `Queue Ability` 节点在 `WorldState` 中写入的全部状态污染，并向全局目标规划器报告 `FAILED`。

### 单位协同与破掩体机制 (Unit Collaboration)
系统通过目标规划层自然实现了跨单位的战术配合：
* **破掩体协同机制（Push-Out-of-Cover Combo）**：掩体内部的玩家单位使得常规远程命中率（Hit Chance）大幅降低。此时全局层生成一个隐藏的 `Push-Out-of-Cover` 高优先级目标。拥有击退、破掩体技能（如手雷投掷、冲撞）的单位会优先于其他单位进行决策。当破掩体单位将该动作成功预排进 `WorldState` 后，受击玩家在沙盒中的掩体掩护状态被强制剥除，暴露在开阔地带。随后进行决策的普通射击单位（如兽族步兵）读取最新的 `WorldState` 时，将计算出对该目标极高的命中期望，从而顺理成章地将火力全部倾泻到该目标身上。
* **互斥性全局协同（Exclusive Objective Mutual Exclusion）**：若友方单位倒地，需要有人前往实施急救（Revive）。如果缺乏顶层目标系统仲裁，临近的多个 AI 单位在独立思考时均会倾向于前往救援，导致大量单位浪费 AP 移向同一位置。通过设置目标容量 $1.0 / 1$，确保仅有综合代价最低的单位会进入救援管线，其余单位则正常执行掩护或压制。

### 单位自主规划层 (Unit Planning)
在宏观目标规划完成后，若单位仍有未耗尽的行动点数（Action Points, AP），或者该单位因战场态势未能认领任何全局目标，系统将启动单位自主规划层。每个兵种绑定有其本能欲望驱动的默认行为树（Default Behavior Tree），针对剩余 AP 执行自适应站位寻找、就地警戒（Overwatch）或寻找更高阶的掩体。

---

## 组合动作分析器与执行体系 (Combo Moves & Execution)

### 计划的原子抽象与元组定义
当所有层级的规划完成后，敌方回合的最终行为表现被形式化为一个包含了时序因果的有序指令序列。系统采用三个核心元素对其进行抽象：

```
                              ┌────────────────────────┐
                              │  回合总计划 (The Plan)  │
                              └───────────┬────────────┘
                                          │
        ┌─────────────────────────────────┼─────────────────────────────────┐
        ▼                                 ▼                                 ▼
┌──────────────────┐             ┌──────────────────┐             ┌──────────────────┐
│      技能元组     │             │     同步标记     │             │     组合动作     │
│     (Ability)    │             │      (Sync)      │             │     (Combo)      │
│  (Unit, Act, D)  │             │  并发阻断与栅栏   │             │ 跨单位动作逻辑聚合 │
└──────────────────┘             └──────────────────┘             └──────────────────┘
```

1. **技能元组 (Ability)**：定义为三元组：
   $$\text{Ability} = \langle u,\, a,\, \mathcal{D} \rangle$$
   其中 $u \in \mathcal{U}$ 代表执行单位（Who）；$a \in \mathcal{A}$ 代表技能原语（What）；$\mathcal{D}$ 为运行期上下文数据（How，包含目标单位引用、寻路路点序列、朝向角等）。
2. **同步标记 (Sync)**：一种作为并发栅栏（Barrier）的时序标记。在两个相邻 `Sync` 标记之间的所有原子指令，在物理执行层将被**同时（Simultaneously）**下发给涉及的全部单位；每个涉事单位在并发流内部按自身序列依序执行自身动作。
3. **组合动作 (Combo)**：由多个跨单位技能元素与同步标记打包而成的复合高阶指令块。

### 工业级执行实例剖析
考虑图 4 所示的典型战场情境：AI 方部署了单位 $A$、$B$、$C$（位于掩体后方），玩家方部署了单位 $X$、$Y$。

```
                     [AI: C]
                       │ 5 (移动)
                       ▼
    [AI: A]        [掩体]        [AI: B]
     │ 1 (移动)                   │ 3 (移动)
     ▼                           ▼
   [掩体]                      [掩体]
     │                           │
   2 ┆ (射击)                  4 ┆ (射击)
     ▼                           ▼
 [玩家: X]                   [玩家: Y]
     ▲
   6 ┆ (射击)
     │
    (C 射击)
```

规划层最初生成的未压缩串行指令流如下：
$$(A,\, \text{move}); (A,\, \text{shoot},\, X); (\text{Sync}); (B,\, \text{move}); (B,\, \text{shoot},\, Y); (\text{Sync}); (C,\, \text{move}); (C,\, \text{shoot},\, X); (\text{Sync});$$

#### 组合动作分析器（Combo Move Analyzer）的重排与并发优化
若直接串行播放上述计划，回合时长将极为冗长。组合动作分析器依据**战术清晰度三原则**进行依赖图扫描与时序重排（Dependency Graph Scheduling）：

1. **位移阶段并发聚合**：由于单位 $A$、$B$、$C$ 的掩体移动路径在几何空间与弹道上相互隔离，移动阶段不会对玩家形成直接攻击过载，满足规则 2。因此三者的移动指令可并入同一并发窗口。
2. **直接攻击冲突消解（依据规则 1）**：
   * 单位 $A$ 和单位 $C$ 均选定玩家 $X$ 作为直接射击目标；
   * 单位 $B$ 选定玩家 $Y$ 作为射击目标。
   * **约束**：绝对禁止同时攻击不同玩家单位。因此针对 $X$ 的攻击集与针对 $Y$ 的攻击集**严禁**置于同一同步区间。
   * **协同聚合**：单位 $A$ 与单位 $C$ 针对同一目标 $X$ 的射击，可在镜头聚焦于玩家 $X$ 的前提下形成协同射击动作（Focus Fire Combo），从而在保证战术清晰度的同时极大加快执行节奏。

---

## 架构权衡与工程复盘 (Trade-offs & Engineering Review)

在构建工业级战术 AI 并发架构时，系统在确定性、表现力与算力开销之间做出了关键权衡：

```
                ┌──────────────────────────────────────────────┐
                │          核心权衡: 预规划推演 vs 反应式架构     │
                └──────────────────────┬───────────────────────┘
                                       │
        ┌──────────────────────────────┴──────────────────────────────┐
        ▼                                                             ▼
┌───────────────────────────────┐             ┌───────────────────────────────┐
│     预规划推演 (Gears 方案)    │             │      反应式单步决策 (传统方案) │
│  • 依赖 WorldState 深度模拟   │             │  • 走一步看一步，行为割裂     │
│  • 全局视角的跨单位连招聚合   │             │  • 串行执行缓慢，回合极其冗长 │
│  • 架构复杂度极高，需回滚机制 │             │  • 实现简单，无需维护镜像沙盒 │
└───────────────────────────────┘             └───────────────────────────────┘
```

1. **虚拟推演与物理世界完全隔离的代价**：
   * *优势*：通过 `WorldState` 的全量沙盒前向模拟，AI 能在规划期精准识别未来时刻的空间碰撞、掩体抢占以及弹道交叉阻挡，为跨单位、跨阶段的并发组合动作（Combo Moves）提供了先决条件。
   * *代价*：系统架构复杂度大幅上升。所有参与 AI 规划的技能节点必须强制实现 `Simulate(WorldState)` 与 `Execute(GameWorld)` 两个版本。一旦底层物理属性、伤害计算公式发生迭代，必须同步维护模拟层，否则将导致推演漂移（Simulation Drift）。
2. **战术清晰度对 AI 理论最优解的剪枝**：
   * *优势*：三大战术清晰度原则以牺牲“AI 理论战术自由度”为代价，换取了顶级的“玩家可读性”与“动作电影质感”。AI 的协同攻击被显式地通过分镜头、语音广播和时序对齐呈现出来，极大地提升了玩家在对抗高智商蜂群时的心理博弈满足感。
   * *代价*：为了迎合单点攻击原则与镜头调度，系统有时必须拆分原本可以完全并行的攻击动作，在某些极端场景下依然会受到串行执行时间片的约束。
3. **模糊逻辑分配与行为树的解耦优势**：
   * 采用全局模糊逻辑进行任务仲裁（Macro），将战略意图与微观单位行为树（Micro）解耦。全局层仅负责宏观调配与算力分发，单位自身行为树保留了其战术兵种的微观实现自由度。当规划在微观层受阻时，干净利落的沙盒回滚（Rollback）机制保证了整体规划管道的健壮性与可维护性。

---

在现代 AAA 级回合制战术游戏（Turn-Based Tactics, TBT）中，敌人回合（Enemy Turn）的呈现往往面临两个相互冲突的核心挑战：
1. **战术清晰度（Tactical Clarity）**：玩家必须能清晰解析敌方的战术意图、攻击目标及战场状态变化；
2. **节奏与沉浸感（Pacing & Immersion）**：当战场存在大量敌方单位时，若完全采取严格的单体串行决策与执行，回合耗时将成倍膨胀，导致玩家处于漫长的等待垃圾时间（Downtime）。

以《战争机器：战术小队》（*Gears Tactics*）的工业级实践为蓝本，本架构通过在规划后阶段引入**组合行动分析器（Combo Move Analyzer）**，重排并合并离散计划元素，辅以严格的**战术清晰度三原则**与**状态回滚打断机制（Interruption Handling）**，实现了行动并发度与战术可读性之间的工业级平衡。极限场景下，该架构将敌方回合时长缩短至传统串行模式的 $\frac{1}{4.5}$（提速达 4.5 倍）。

---

## 1. 战术清晰度原则（Rules of Tactical Clarity）

为了防止多单位并发行动导致的视觉信息过载，动作合并与编排必须遵循严苛的认知与感知约束：

| 原则编号 | 原则定义 | 核心工程推论与约束 |
| :--- | :--- | :--- |
| **第一原则**<br>*(First Rule)* | **绝不同时向超过一个玩家单位发动直接攻击**<br>*(Never attack more than one player unit with direct attacks simultaneously)* | 仅允许针对同一目标玩家单位的直接攻击动作并发。流弹（Missed shots/Stray bullets）或范围伤害（Splash damage）导致的次生伤害豁免于此，但主集火目标必须收敛于单一视场内。 |
| **第二原则**<br>*(Second Rule)* | **尽可能减少单个单位行动的拆分**<br>*(Try to split the actions of a single unit as little as possible)* | 单一单位的 `(Move, Shoot)` 链路应在时间轴上保持局部连续性。拆分单位动作会导致玩家认知断裂；除非因多单位拓扑依赖环（Cyclic Dependencies）无法解开，否则禁止跨步插入无关单位动作。 |
| **第三原则**<br>*(Third Rule)* | **高影响剧烈动作必须独占执行**<br>*(Highlight actions that apply drastic changes by playing them exclusively)* | 击退（Push-back）、高阶增益（Specific Buffs）、处决（Execution）等大幅改变战场拓扑与单位存活性状态的技能，必须在其前后强插同步屏障（Sync Points）独占播放。 |

---

## 2. 组合行动分析器（Combo Move Analyzer）

组合行动分析器是一个在基础规划（Planning Phase）完成后介入的后处理层（Post-Planning Layer）。它通过分析各单位生成的元操作计划，依据空间拓扑、依赖关系与摄像机取景限制，将离散的线性命令图重构为包含并发组的执行树。

```
原始顺序计划:
[ (A, move) -> (A, shoot, X) ] -> [ (B, move) -> (B, shoot, Y) ] -> [ (C, move) -> (C, shoot, X) ]
                                            │
                                组合行动分析器 (Combo Move Analyzer)
                                            ▼
拓扑重排与依赖归并:
  1. 目标 X 攻击合并 (A 与 C 同步集火)
  2. 空间占位解除 (B 必须先让位，C 方可进入掩体)
  3. 战术清晰度收敛 (B 的行为不拆分)
                                            │
                                            ▼
最终编排执行流:
  [ (B, move) -> (B, shoot, Y) ] ──> (Sync) ──> Combo[ (A, shoot, X) || (C, move -> C, shoot, X) ] ──> (Sync)
```

### 2.1 协同集火攻击（Simultaneous Attacking）
分析器遍历全局行动计划，依据**目标单位实例（Target Instance）**与**摄像机取景框边界（Camera Framing Limits）**构建目标分桶（Buckets）：
* **前置依赖提取（Pre-abilities）**：若单位 $C$ 射击前需位移至特定射击位点，位移操作作为该攻击行动的前置技能（Pre-ability），一并吸收入组合内；
* **时间一致性与视线判定（LoS Validity）**：规划基于虚拟世界状态（Virtual WorldState）生成。若某事件（如击退位移）改变了目标空间坐标，所有在该事件前完成规划的攻击行动必须在该事件执行前完成释放，以确保视线（Line of Sight, LoS）有效性；
* **保序约束（Order Invariant）**：重排算法可以任意平移执行时间点，但严格禁止改变单一单位内部的子动作执行先后顺序。

### 2.2 空间占位与跨单位拓扑依赖案例

设单位 $A$ 计划射击目标 $X$；单位 $B$ 计划从掩体 $P_1$ 移动至 $P_2$ 并射击目标 $Y$；单位 $C$ 计划移动至 $P_1$（$B$ 腾出的位置）并射击目标 $X$。

```
       [C]
        │ 4 (Move)
        ▼
   ┌───[B]───┐ (掩体 P1)
   │    │ 2  │
   │    ▼    │
   │   [P2]  │ (位移终点)
   │    │ 3  │
   │    ▼    │
   │   [Y]   │ (玩家目标 Y)
   │         │
  [A]        │
   │ 1       │ 5
   ▼         ▼
  [     X     ] (玩家目标 X)
```

1. **初始无冲突线性计划**：
   $$\text{Plan}_0 = \langle (A, \text{shoot}, X); \text{Sync}; (B, \text{move}); (B, \text{shoot}, Y); \text{Sync}; (C, \text{move}); (C, \text{shoot}, X); \text{Sync} \rangle$$
2. **单纯合并集火目标 $X$（引发运行时碰撞冲突）**：
   $$\text{Plan}_1 = \langle \text{Combo}[(A, \text{shoot}, X); (C, \text{move}); (C, \text{shoot}, X); \text{Sync}]; (B, \text{move}); (B, \text{shoot}, Y); \text{Sync} \rangle$$
   *冲突根因*：$C$ 尝试移动至 $P_1$，但在真实执行世界（Real World）中，$B$ 尚未移出 $P_1$（规划阶段虽然 $B$ 在状态机中已离开，但时序重排导致其物理世界未让位）。
3. **前置跨单位位移依赖（解决物理冲突，违背第二原则）**：
   $$\text{Plan}_2 = \langle (B, \text{move}); \text{Combo}[(A, \text{shoot}, X); (C, \text{move}); (C, \text{shoot}, X); \text{Sync}]; (B, \text{shoot}, Y); \text{Sync} \rangle$$
   *问题*：单位 $B$ 的动作链被拆散为两段，中间夹杂了 $A$ 和 $C$ 的协同攻击，极大损害玩家战术感知。
4. **满足清晰度原则的最优调度计划**：
   依据第二原则，将单位 $B$ 保持原子性提前完整释放：
   $$\text{Plan}_{\text{final}} = \langle (B, \text{move}); (B, \text{shoot}, Y); \text{Sync}; \text{Combo}[(A, \text{shoot}, X); (C, \text{move}); (C, \text{shoot}, X); \text{Sync}] \rangle$$

### 2.3 协同位移（Simultaneous Movement）
与定点攻击不同，多单位向同一战术区域靠拢时，摄像机无需包络各单位的起始点（Starting Locations），仅需通过平滑拉远（Camera Zoom Out）框定**目标终点区域（Final Destinations）**：
* 玩家依靠位移矢量轨迹即可推断攻击动向；
* 多单位以各自速度（Pace）向终点收敛，直至最后一个单位进入位置判定 Combo 结束；
* 营造出敌军从四面八方实施合围（Surrounded）的高压战场氛围。

### 2.4 润色修饰行动（Flavor Actions）
在组合行动生成后，编排器会注入零游戏逻辑成本（Zero Gameplay Cost）的表现性行为（Flavor Actions），例如：
* 小队指挥官在全队协同冲锋前举枪指向目标并触发“Move”语音；
* 协同射击前敌人的短促战术手势。此类行动纯粹为了强化环境生命力（Living World Perception）。

---

## 3. 计划执行层与中断回滚（Execution & Interruptions）

### 3.1 计划分发流水线（Execution Pipeline）
执行层维护一个同步推进时钟：
1. 从当前计划序列中拉取直至下一 `Sync` 节点前的所有动作元素；
2. 将动作派发至对应单位的执行代理；
3. 为避免机械化的完全同频动作，系统在各单位起手时注入**微小的随机延迟（Small Random Delays）**；
4. 各单位完成动作后向协调器回传 `Complete` 信号；
5. 收集齐当前批次所有完成信号后，越过 `Sync` 屏障进入下一节。

```
[执行调度器] ──(抓取至Sync屏障)──> [动作分发]
                                       │
            ┌──────────────────────────┴──────────────────────────┐
            ▼ (随机延迟 δ_1)                                      ▼ (随机延迟 δ_2)
     [单位 A 执行流水线]                                   [单位 B 执行流水线]
            │                                                     │
     (Action Completed)                                    (Action Completed)
            │                                                     │
            └──────────────────────────┬──────────────────────────┘
                                       ▼ (收集汇报)
                              [通过 Sync 屏障，进入下一批次]
```

### 3.2 运行时中断机制（Interruptions）
由于战术系统存在不确定性交互（如玩家单位的**监视射击（Overwatch）**反应），敌方在回合执行期间的真实物理状态随时可能偏离规划预期。

```
【时序推进中的状态偏离】
单位 A: 规划 -> [移动至掩体] -> [射击玩家 P] (预期击倒 DBNO)
单位 B: 规划 -> [前进至玩家 P 旁] -> [执行处决能力]
               ▲
               │ (玩家 Overwatch 拦截触发)
               └── 玩家向 A 开火 -> A 被击倒/击杀/硬直
                   结果: 玩家 P 未倒地，且掩体环境发生变更
                   引发: Interruption 信号抛出 -> 全局规划流水线强制重置
```

* **中断代价**：在无网格自由移动（Free Movement）和实时弹道（Live Bullet System）环境下，由于无法对空间采样点进行穷举预计算（Precomputation），必须重新投射大量射线（Ray casts）。中断处理的运行时开销极高，必须严格限制其触发频次；
* **黑板状态回滚（Blackboard Modifications Inversion）**：
  为避免规划期与执行期数据污染，行为树（Behavior Trees）严禁直接写死持久化黑板（Blackboard）。系统使用**黑板变更记录单（Blackboard Modifications）**伴随计划元操作生成：
  $$BB_{\text{runtime}} = BB_{\text{initial}} + \sum_{k=1}^{n} \Delta BB_k$$
  当第 $m$ 个动作在执行期被打断时，执行引擎倒序应用逆向操作：
  $$BB_{\text{recovered}} = BB_{\text{current}} - \Delta BB_m$$
  *示例*：若单位每回合被限制攻击 2 次，入队时 `AttacksQueued++`；一旦中途被打断且该射击未实际释放，立即执行 `AttacksQueued--`，确保重规划时状态机数据严格自洽。

---

## 4. 并发规划、执行重叠与等待感知优化

在《战争机器：战术小队》的研发阶段，开发团队曾面临严重的玩家体验悖论：单纯追求更快的总执行时间，反而导致玩家感知到了“更慢且乏味”的敌方回合。

### 4.1 认知工程三项核心实验发现
1. **初始思考时间并不线性减少总时间**：在后台流水线化规划模式下，前期让 AI 思考越久，并不能显著降低总耗时，因为**后续单位的规划完全可以与当前单位的动作表现重叠执行（Overlapped Execution）**；
2. **玩家对协同并发表现呈现强烈的正向情绪**；
3. **停滞感知（Player Downtime Perception）非均匀分布**：
   * **回合起始阶段**：玩家心理上接受 AI 处于“思考部署期”，适度的停顿（数秒）不会引起负面情绪，且游戏开场横幅（Banner）与音效可自然遮蔽该等待；
   * **回合运行中途**：一旦进入执行流，任何超过 1-2 秒的完全静态断点（无任何动画播放）都会被玩家在主观上放大数倍，被定义为“节奏崩溃”或“AI 卡死”。

```
[回合启动] ──(Banner与音效播放，掩盖初始思考)──> [首批计划产出]
                                                       │
         ┌─────────────────────────────────────────────┴────────────────────────────────────────┐
         ▼                                                                                      ▼
[前台渲染/物理执行: 单位 1..k 并发执行]                                           [后台并行规划: 单位 k+1..n 投线与空间计算]
         │                                                                                      │
         └─────────────────────────────────────────────┬────────────────────────────────────────┘
                                                       ▼
                                      [平滑过渡至下一行动，消灭中途停顿]
```

### 4.2 工业级调度启发式规则集
基于上述发现，系统确立了以下生产规则：
* **击退动作立发约束**：一旦当前规划图生成了击退（Push-back）动作，立即截断并交付执行层，因为击退前后的计划必须严格隔离，不可并发；
* **非对称超时阈值（Asymmetric Timeouts）**：
  $$\tau_{\text{initial}} \gg \tau_{\text{interruption}}$$
  回合启动时的超时容忍窗口 $\tau_{\text{initial}}$ 设定得相对宽裕（容许 AI 梳理全局拓扑）；而中途打断后的重规划超时窗口 $\tau_{\text{interruption}}$ 被压缩至极短，优先保障视觉连贯性，防止产生中途卡顿感。

---

## 5. 架构级调试、可观测性与性能遥测

### 5.1 假死（Enemy Turn Stuck）的病理学解构
“敌方回合假死”是战术 AI 研发中最容易爆发的高频缺陷。该现象本质是**症状而非病因**。实际工程中的典型病因分布包括：
1. 技能状态机（Ability State Machine）无法正常发出终止事件（Non-terminating abilities）；
2. 异常碰撞体或破坏组件每帧触发导航网格（NavMesh）全量脏刷新；
3. 空间推理或射线投射陷入死循环；
4. 协同动作依赖图存在循环拓扑等待（Cyclic Dependency Locks）。

即使出现概率仅为 $\frac{1}{100}$ 的极低频竞态 Bug，在数十万商业玩家规模下也会被放大为灾难性的死局体验。必须建立深度的离线审计与回放工具链。

### 5.2 基于 Unreal Engine 4 的可观测性流水线
团队深度定制了 UE4 的 **Visual Logger** 基础设施，强制要求所有内部测试与自动化用例的 Bug 报告必须绑定 `.vlog` 二进制轨迹日志。

```
[UE4 运行环境] ──> [Visual Logger 记录核心元数据]
                    ├── 全局随机数种子 (Random Seeds)
                    ├── 环境查询系统结果 (EQS Items & Scores)
                    ├── 行为树遍历追踪 (BT Execution Paths)
                    ├── 目标分配决议 (Goal Assignments)
                    └── 技能生命周期事件 (Ability Start / Interrupt / End)
```

通过时间轴（Timeline）前后拖拽，架构师能够实现确定性（Deterministic-like）的状态倒带，复盘卡死瞬间各单位的世界感知与内部决策分支。

### 5.3 遥测与离线分析指标系统（Telemetry & Offline Analysis）
在面对海量环境查询系统（Environment Query System, EQS）分片计算时，开发环境（Development Builds）的绝对性能耗时具有极强的误导性。团队建立了基于大盘遥测的离线性能追踪架构：

```
                    ┌─────────────────────────┐
                    │ 全局遥测采集器 (Telemetry) │
                    └────────────┬────────────┘
                                 │
     ┌───────────────────────────┼───────────────────────────┐
     ▼                           ▼                           ▼
[规划耗时比矩阵]             [目标分配健康度]              [感知停滞追踪]
 t_goal (单个目标耗时)        N_fail / N_success          t_downtime (玩家零输入停滞)
 t_unit (单体规划耗时)        (判定是否出现规则振荡)        N_interrupts (单回合打断总数)
```

* **指标相对化对比策略**：忽略非发布版本的绝对帧耗时，专注于**相对比值异常**。
  * *判定规则 1*：若特定单位 $U_i$ 在地图 $M$ 上的规划时间满足：
    $$\frac{t(U_i)}{\frac{1}{n}\sum_{k=1}^{n} t(U_k)} > 3.0$$
    直接对该单位的行为树 EQS 空间采样点与射线穿透逻辑进行下钻重构；
  * *判定规则 2*：若某目标的指派失败率（Failed Goal Assignments）突增，表明目标选择规则（Goal Selection Rules）存在逻辑断崖，导致下游规划层发起了大量必定失败的无效计算尝试，需在决策层顶端提前剔除。

---

## 6. 核心架构伪代码实现：后处理组合分析器

以下为组合行动分析器的核心重排算法，展示了从线性初始计划到满足三项清晰度原则的高并发执行计划的完整推导过程：

```python
from typing import List, Dict, Set, Optional
from dataclasses import dataclass

@dataclass
class PlanElement:
    unit_id: str
    action_type: str  # 'MOVE', 'SHOOT', 'PUSHBACK', 'BUFF'
    target_id: Optional[str]
    pre_abilities: List['PlanElement']
    affects_gamestate_drastically: bool = False

@dataclass
class ComboMove:
    elements: List[PlanElement]

ExecutionSequence = List[object]  # 元素可为 PlanElement, ComboMove 或 "SYNC"

class ComboMoveAnalyzer:
    def __init__(self, camera_frustum_validator):
        self.camera = camera_frustum_validator

    def build_optimized_plan(self, initial_plan: List[PlanElement]) -> ExecutionSequence:
        """
        组合行动后处理主管道：依据清晰度三原则进行依赖排序与并发合并
        """
        final_sequence: ExecutionSequence = []
        pending_elements = list(initial_plan)

        while pending_elements:
            current = pending_elements.pop(0)

            # 第三原则：剧烈改变游戏状态的技能（如击退）必须绝对独占执行
            if current.affects_gamestate_drastically:
                final_sequence.append(current)
                final_sequence.append("SYNC")
                continue

            # 针对攻击行动，尝试构建协同集火分桶 (Simultaneous Attacking)
            if current.action_type == 'SHOOT':
                target = current.target_id
                attack_combo = [current]
                consumed_indices = []

                # 第一原则：绝不同时向多个玩家单位发动直接攻击 -> 仅抓取相同 target_id
                for idx, candidate in enumerate(pending_elements):
                    if (candidate.action_type == 'SHOOT' and 
                        candidate.target_id == target and 
                        self.camera.can_frame_simultaneously(attack_combo + [candidate])):
                        
                        # 检查前置依赖是否存在空间死锁冲突
                        if not self._has_spatial_dependency_conflict(candidate, pending_elements):
                            attack_combo.append(candidate)
                            consumed_indices.append(idx)

                # 倒序安全移除已被吸收到 Combo 中的元素
                for idx in reversed(consumed_indices):
                    pending_elements.pop(idx)

                # 第二原则：减少单个单位行动的拆分
                # 处理各个攻击元素内部的位移前置技能 (Pre-abilities)
                resolved_combo_elements = []
                for attack_elem in attack_combo:
                    for pre in attack_elem.pre_abilities:
                        # 尽量紧邻当前攻击挂载，若存在跨单位位移让路，提升至 Combo 前执行
                        if self._is_clearing_path_for_others(pre, initial_plan):
                            final_sequence.append(pre)
                            final_sequence.append("SYNC")
                        else:
                            resolved_combo_elements.append(pre)
                    resolved_combo_elements.append(attack_elem)

                if len(resolved_combo_elements) > 1:
                    final_sequence.append(ComboMove(elements=resolved_combo_elements))
                else:
                    final_sequence.extend(resolved_combo_elements)

                final_sequence.append("SYNC")
            else:
                final_sequence.append(current)

        return self._collapse_redundant_syncs(final_sequence)

    def _has_spatial_dependency_conflict(self, candidate: PlanElement, queue: List[PlanElement]) -> bool:
        """检测若提前执行 candidate，其前置位移是否会与现有队列中物理未让位的单位发生冲突"""
        # 工程判定：遍历当前计划中尚未执行的物理占位，若目标位置存在重叠则判定冲突
        return False

    def _is_clearing_path_for_others(self, pre_action: PlanElement, full_plan: List[PlanElement]) -> bool:
        """判定前置动作是否属于让出掩体给其他单位使用的强时序依赖"""
        return False

    def _collapse_redundant_syncs(self, sequence: ExecutionSequence) -> ExecutionSequence:
        """清理相邻的多余同步屏障"""
        sanitized = []
        last_elem = None
        for elem in sequence:
            if elem == "SYNC" and last_elem == "SYNC":
                continue
            sanitized.append(elem)
            last_elem = elem
        return sanitized
```

---

## 7. 结语与架构启示

在复杂的战术 AI 系统工程中，**解耦规划（Reasoning）与执行（Choreography）**是解决回合制游戏迟滞感的核心范式：
1. **WorldState 必须是全局系统的一等公民**：虚拟世界状态机不仅服务于行为树寻路，还必须精准模拟未来动作执行所引起的物理遮挡、空间占有与视线变化。若底层状态模拟失准，重排器生成的组合行动将在物理世界频繁引发穿模或执行死锁；
2. **感知真实度高于算法纯粹度**：让所有敌人机械化并发开火虽然能最大化压缩回合耗时，但彻底摧毁了游戏的信息可读性。战术清晰度三原则是维系“动作紧凑感”与“战术博弈乐趣”的决定性防线；
3. **分步推进胜过单体思考**：流水线式规划使得执行与后台射线投射并行推进，配合针对开场与中途场景的非对称等待容忍度控制，成功将复杂的全局计算成本优雅地隐藏在视听语言之后。
