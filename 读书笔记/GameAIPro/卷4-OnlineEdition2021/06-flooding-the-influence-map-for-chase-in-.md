---
type: Reference
title: "第6章 Flooding the Influence Map for Chase in Dishonored 2"
description: "Game AI Pro 工业级精读：Flooding the Influence Map for Chase in Dishonored 2。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第6章 Flooding the Influence Map for Chase in Dishonored 2

> 来源：*Game AI Pro 4 (Online Edition 2021)*, Chapter 6.  
> 原文作者 / 资源：[Flooding the Influence Map for Chase in Dishonored 2](http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter06_Flooding_the_Influence_Map_for_Chase_in_Dishonored_2.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷4-OnlineEdition2021](README.md) ｜ [专栏首页](../README.md)

---

## 1. 概述与核心技术背景

在游戏 AI 设计中，模拟非玩家角色（NPC）自然且符合生物本能的运动（Organic Movement）是塑造沉浸感的核心。神经科学家丹尼尔·沃尔珀特（Daniel Wolpert）曾提出一个假说：生物演化出大脑的唯一核心目的就是为了控制运动。在视频游戏中，NPC “如何移动” 以及 “向何处移动” 直接决定了玩具体验的上限。

实现具有说服力的 NPC 运动决策本质上依赖于**空间推理（Spatial Reasoning）**，即通过对物理世界进行建模与空间表征，推导并执行最合理的行动方案。在游戏工业界，**影响图（Influence Mapping）** 是最具代表性的空间推理技术之一。影响图的底层拓扑由离散化的网格单元（Cell）集合构成，每个单元在空间中拥有明确的坐标、体积包络以及与其相邻单元的连接拓扑（显示或隐式）。工业界对其离散化的手段多种多样，包括 2D/3D 规则网格、几何图（Graph）乃至无格网的连续点集影响表示（Infinite-Resolution Point-Based Influence）。

传统的影响图算法多用于实时战略游戏（RTS），通过将不同单位的战斗力或占有率投射到网格中，描述当前博弈双方对战场的控制权分布（Current Game State）。随着技术演进，影响图被拓展用于推导**未来博弈状态（Prospective Game State）**，即用于预测玩家或敌方在未来时间窗口内可能前往的区域或行动概率。

在《羞辱2》高度强调立体化垂直空间（Verticality）、瞬移超能力（如 Blink、Far Reach）以及复杂关卡拓扑的环境下，开发团队开发了一种轻量、稳健且反作弊感极强的空间推理算法：**基于双相种子与动力阻断的影响图泛洪算法（Dual-Phase Seeding and Barrier Influence Flooding）**。该方案成功解决了复杂拓扑下的玩家追踪难题，并在保持极低运行时开销的前提下实现了高度拟人化的追逐行为。

---

## 2. 追逐状态问题定义与工程方案演进

### 2.1 战斗状态机与追逐契机
在《羞辱2》的战斗 AI 状态机架构中，核心关注两个直接相关的状态切换：
- **交战状态（Engage State）**：NPC 对目标具有直接的视线判定（Line of Sight, LOS）或感知输入，直接实施战术攻击。
- **追逐状态（Chase State）**：NPC 因视线遮挡、烟雾、地形死角或玩家超自然位移而**失去对目标的直接感知**，必须快速判定玩家的逃窜方向并全力追赶。

```
           [直接视线 LOS 建立]
     +--------------------------------+
     |                                |
     v                                |
+------------+   失去目标感知   +-------------+   到达推断终点/未果   +------------+
|  交战状态  | --------------> |  追逐状态   | ------------------> |  搜索状态  |
|  (Engage)  |                 |   (Chase)   |                     |  (Search)  |
+------------+                 +-------------+                     +------------+
                                      |                                   ^
                                      +-----------------------------------+
                                            单步加热单元超限 / 达到最大步数
```

从玩家的交互心理学角度看，一次优秀的“追逐体验”必须满足：
1. **认知合理性**：NPC 的追踪行为建立在对目标动量的合理推断上，而非系统后门给予的全知全能（Omniscience）。
2. **适时放弃机制**：追至合理尽头若未发现玩家，应有符合人设的放弃表现（例如原地张望停顿，并伴随挫败感语音播报 Bark），随后平滑降级为低速地毯式巡视的**搜索状态（Search State）**。

### 2.2 方案演进与工业权衡（Trade-offs）

在项目推进中，团队先后尝试并评估了三种追逐目标点生成方案：

| 技术方案 | 工作机制 | 优点 | 致命缺陷 / 淘汰原因 |
| :--- | :--- | :--- | :--- |
| **面包屑寻路追踪<br>(Breadcrumbing)** | 逃跑玩家沿路径隐式生成点阵轨迹，NPC 依次向各点导航并消耗节点。 | 极易实现，寻路逻辑简单，NPC 路径完全贴合玩家实际路线。 | **作弊感强烈（Omniscient AI）**：玩家即使使用闪现等技能绕到隐蔽死角，NPC 依然能沿绝对精准的路线跟随，严重破坏潜行游戏的拟真度与公平性。 |
| **航位推测法<br>(Dead Reckoning)** | 提取玩家丢失前的位置与朝向速度向量，向**导航网格（NavMesh）**边界进行线性外插值，结合 NavMesh 射线检测投射终点。 | 计算开销极低（几近于零），无需复杂的空间数据结构辅助。 | **缺乏拓扑适应性**：在直角拐角、悬崖或复杂掩体处，线性射线极易直接截断于前方障碍墙体，导致 NPC 过早放弃追逐，在有明显连通走廊的情况下停止追赶。 |
| **影响图泛洪算法<br>(Influence Flooding)** | 基于稀疏 3D 空间矩阵，结合最后已知动量注入单向阻断屏障，执行受限的热传导泛洪计算加权质心。 | **完全遵循关卡拓扑结构**；自动适配走廊与开阔空间；开销可控；具备拟人化的动量保持特性。 | 实现复杂度高于前两套方案，需要额外的空间标量网格支撑。 |

#### 航位推测法的拓扑失效模型
```
               [实体墙体 Wall]
      +-------------------------------+
      |                               |
      |          玩家实际逃跑预期      |
      |                ^              |
      |                | (转弯逃离)   |
      |        +-------+              |
      |        |                      |
      |        * 丢失点 (Last Known)  |
      |       /                       |
      |      X 线性外插终点 (射线撞墙) |
      |     /  (导致 NPC 放弃追逐)    |
      |    ^                          |
      |    |                          |
      |   NPC                         |
```
*图解：航位推测法在直角走廊转角直接撞击前向墙壁，AI 判定前方不可通行而异常放弃，表现为严重的“人工智障”。*

---

## 3. 算法核心架构与数学实现

### 3.1 空间载体：动态空间管理器（Dynamic Space Manager, DSM）
《羞辱2》放弃了耗费连续内存的密集三维数组，构建了基于稀疏 3D 矩阵的**动态空间管理器（DSM）**。DSM 直接基于关卡的 NavMesh 离散化生成，其底层由一系列通过一维紧凑数组（Flat Arrays）存储的离散单元构成，使用空间哈希坐标作为键（Key）建立轻量字典进行索引映射，兼顾了空间连续性检测与低内存占用。

#### 单元（Cell）数据结构定义
```cpp
struct InfluenceCell
{
    Vector3 m_position;                 // 空间绝对坐标 (3D/2D)
    FixedArray<InfluenceCell*, 8> m_neighbors; // 拓扑相邻单元指针引用
    int32_t m_temperature;             // 单元“温度”标量：表示玩家存在于该处的概率权重
};
```

---

### 3.2 阶段 A：影响图双相种子注入（Dual-Phase Seeding）
当 NPC 与玩家视线中断（LOS Lost）的瞬间，触发追逐初始化，向 DSM 注入两组互斥的种子：

```
                [玩家丢失时朝向: 右上]
                     / 
                    / 
                   /  
         -1 | -1 | -1 | . | . 
        ----+----+----+---+---
         -1 | -1 |  9 | . | . 
        ----+----+----+---+---  <-- 点划线为动量正交超平面
         -1 | -1 | -1 | . | .       (Barrier Seed = -1)
        ----+----+----+---+---
          . |  . |  . | . | . 
   (背向半平面: 屏障填充)  (前向空间: 准备热传导)
```

#### 1. 热源生成（Heat Generation）
将玩家最后已知位置映射到的网格单元设为初始热源单元 $C_{origin}$，赋予初始热量常量值 $H$：
$$T(C_{origin}) = H \quad (H \in \mathbb{N}^+, \text{工程默认值 } H = 20)$$

#### 2. 动量屏障生成（Barrier Generation）
为防止 NPC 做出“违反人类直觉的 180 度反向折返”，算法利用玩家最后已知的行进方向单位向量 $\vec{D}_{player}$ 构建一个局部几何分离超平面。

设空间中 $C_{origin}$ 的坐标为 $\mathbf{P}_0$，任意相邻单元 $C_k$ 坐标为 $\mathbf{P}_k$，其方向向量为 $\vec{V}_k = \mathbf{P}_k - \mathbf{P}_0$。所有满足处于**背向半平面（Opposite Half-Plane）**的邻近单元均被强行置为阻断状态：
$$\text{若 } \vec{V}_k \cdot \vec{D}_{player} < 0 \implies T(C_k) = -1$$
值为 $-1$ 的单元被标记为永久阻断屏障，阻断热量反向倒灌。

---

### 3.3 阶段 B：双相传播机制（Dual-Phase Propagation）
影响图在离散步数推进中，交替执行**热传导**与**屏障推移**两个子阶段：

#### 1. 热传导阶段（Heat Propagation）
- **外扩加热**：遍历上一轮迭代被加热的单元集合 $S_{warm}^{t-1}$。对于其每个相邻未访问单元 $C_{next}$，若满足：
  $$T^t(C_{next}) \notin \{-1\} \land C_{next} \text{ 本轮之前未被激活}$$
  则将其激活，赋予当前轮次的固定热量值：
  $$T^t(C_{next}) = H$$
- **历史降温（Cooling Down）**：在上一轮次已经处于加热状态且本轮未被重新赋予热值的单元，发生自然衰减。热量标量逐轮递减 1，直到降至 0：
  $$T^t(C) = \max\left(0, T^{t-1}(C) - 1\right)$$
  该机制在空间中形成波浪状向前扩散的“热力波”（Heat Wave）。

#### 2. 屏障传播阶段（Barrier Propagation）
为了防止热浪在狭窄走廊或开阔地绕过初始屏障边缘向玩家丢失点后方折返扩散，屏障单元同样具有传播性。
- 遍历上一阶段新增的屏障单元集合 $S_{barrier}^{t-1}$。
- 每个屏障单元在其自身的局部坐标系下，沿着与 $\vec{D}_{player}$ 逆向的半平面方向，将其邻接的非热源单元传染为新的屏障单元（设定为 $-1$）。
- 这种前向驱赶机制确保了影响扩散始终保持强大的**前向动量偏置（Momentum Bias）**。

```
传播步进示例流程：

[第 0 步: 种子设定]        [第 1 步: 热外扩与屏障推移]       [第 N 步: 波前外延与尾部冷却]
 .  .  .  .  .              .  .  .  9  9                  .  .  9  9  9
-1 -1  9  .  .   ----->    -1 -1  8  9  9      ----->      0  1  7  8  9
-1 -1 -1  .  .             -1 -1 -1  .  .                  0  0  0  1  1
```

---

### 3.4 算法循环终止条件（Stopping Conditions）
无约束的泛洪会导致巨大的计算峰值。算法在满足以下任一条件时立刻强行截断传播：

1. **边界耗尽**：拓扑边界内无任何可供加热的新单元（$\Delta S_{heated} = \emptyset$）。
2. **空间开阔度自适应截断（Wide Area Clamp）**：单步迭代中新加热的单元总数超过阈值 $M_H$：
   $$\left| S_{new\_heated}^t \right| > M_H$$
   *设计意图：当热量波涌入开阔广场或大厅时，单步激活单元数会发生指数级爆炸。此时截断可直接模拟“开阔区域线索丢失，目标去向发散，停止鲁莽追赶”的人类直觉。*
3. **最大步数限制（Max Steps Clamp）**：迭代步数达到绝对上限 $M_S$：
   $$\text{CurrentStep} \ge M_S$$
   *设计意图：限制追逐物理范围距离的硬性指标。*

---

### 3.5 追逐终点确定：加权质心推导（Centroid Resolution）

当泛洪算法终止时，追逐终点 $\mathbf{P}_{target}$ 通过对整个地图中留存的所有正温度单元（即 $T(C_i) > 0$ 的活跃单元）进行**温度加权平均质心（Weighted Centroid）**计算得出：

$$\mathbf{P}_{centroid} = \frac{\sum\limits_{i=1}^{N} T(C_i) \cdot \mathbf{P}(C_i)}{\sum\limits_{i=1}^{N} T(C_i)}$$

#### 质心在物理拓扑上的投影矫正
由于加权平均属于纯代数计算，在“U型弯道”或“L型深廊”拓扑下，算出的质心几何点可能会漂移到不可行走的墙体内部或虚空中（几何凹多边形的外露质心）。

```
        +-------------------------+
        |   走廊通道 (温区)       |
        | [9][9][8][7]            |
        +------+     +------------+
               |     |
  不可通行实体 |  X  | <-- 几何质心漂移至墙体内！
  (Wall)       |     |
        +------+     +------------+
        | [9][9][8][7]            |
        |   走廊通道 (温区)       |
        +-------------------------+
                     |
                     v
  【修正策略】：执行欧氏距离最近邻查询，
  将追逐点吸附至离质心最近的合法温区单元格中心。
```

工程判定机制如下：
$$\mathbf{P}_{target} = \begin{cases} 
\mathbf{P}_{centroid}, & \text{若 } \mathbf{P}_{centroid} \text{ 位于合法活跃温区单元内} \\
\arg\min\limits_{\mathbf{P}(C_i)} \|\mathbf{P}(C_i) - \mathbf{P}_{centroid}\|, & \text{若质心位于网格外（选择最近邻合法活跃单元）}
\end{cases}$$

此数学模型确保了追逐目的地始终落在一个空间连通、物理可达的导航网格节点上。

---

## 4. 核心执行算法伪代码

```python
def calculate_chase_target(player_last_pos, player_last_dir, dsm_grid, H=20, M_H=25, M_S=30):
    """
    基于 DSM 影响图泛洪的高级追逐目标点生成算法
    :param player_last_pos: 玩家最后可见世界坐标 Vector3
    :param player_last_dir: 玩家最后已知运动朝向 (Normalized Vector3)
    :param dsm_grid: 动态空间管理器稀疏网格
    :return: Vector3 追逐目标世界坐标
    """
    origin_cell = dsm_grid.world_to_cell(player_last_pos)
    if not origin_cell:
        return player_last_pos

    # 1. 种子初始化阶段
    origin_cell.temperature = H
    active_heat_cells = [origin_cell]
    active_barrier_cells = []

    # 初始化反向半平面阻断屏障
    for neighbor in origin_cell.get_neighbors():
        v = (neighbor.position - origin_cell.position).normalized()
        if v.dot(player_last_dir) < 0.0:
            neighbor.temperature = -1
            active_barrier_cells.append(neighbor)

    steps = 0
    all_warm_cells = {origin_cell}

    # 2. 泛洪传播循环
    while steps < M_S and active_heat_cells:
        new_heat_cells = []
        new_barrier_cells = []

        # (a) 热量外扩
        for cell in active_heat_cells:
            for neighbor in cell.get_neighbors():
                if neighbor.temperature == 0:  # 仅感染未被占用且非阻断的单元
                    neighbor.temperature = H
                    new_heat_cells.append(neighbor)
                    all_warm_cells.add(neighbor)

        # 空间开阔度自适应截断判断
        if len(new_heat_cells) > M_H:
            break

        # (b) 阻断屏障单向扩散 (维持动量)
        for b_cell in active_barrier_cells:
            for neighbor in b_cell.get_neighbors():
                if neighbor.temperature == 0:
                    # 在屏障局部空间继续沿反向半平面推进
                    v = (neighbor.position - b_cell.position).normalized()
                    if v.dot(player_last_dir) <= 0.0:
                        neighbor.temperature = -1
                        new_barrier_cells.append(neighbor)

        # (c) 历史热量衰减自然冷却
        for cell in list(all_warm_cells):
            if cell not in new_heat_cells:
                cell.temperature -= 1
                if cell.temperature <= 0:
                    cell.temperature = 0
                    all_warm_cells.remove(cell)

        active_heat_cells = new_heat_cells
        active_barrier_cells = new_barrier_cells
        steps += 1

    # 3. 加权质心推导
    if not all_warm_cells:
        return origin_cell.position

    total_weight = 0.0
    weighted_sum = Vector3(0, 0, 0)
    for cell in all_warm_cells:
        weighted_sum += cell.position * cell.temperature
        total_weight += cell.temperature

    centroid = weighted_sum / total_weight

    # 4. 拓扑可达性校验与投射
    if dsm_grid.is_point_inside_cells(centroid, all_warm_cells):
        return centroid
    else:
        # 获取与浮点质心欧氏距离最近的活跃单元
        best_cell = min(all_warm_cells, key=lambda c: (c.position - centroid).length_squared())
        return best_cell.position
```

---

## 5. 关卡拓扑响应特征分析

该算法在工业级复杂场景中表现出优秀的拟人化决策特质，能够自发对拓扑结构做出极具策略性的响应：

### 5.1 对称 T 型路口决策 vs. 非对称走廊偏置（Dead End Bias）
- **对称场景**：面对 T 字形路口，左右两侧具有相同的拓扑连通深度。泛洪扩散在两侧产生完全对称的温度分布，计算出的质心精确落于路口中心。NPC 奔跑到路口中心时，因无法进一步判定走向而立即停止追逐，宣告放弃并转入原地警戒，随后无缝降级至低速巡查。
- **非对称场景**：若左侧为死胡同（仅需数步即填满），右侧为通往深处的长走廊。死胡同单元迅速撞墙并停止扩展，其单元迅速进入冷却衰减期（热值衰减为 $0$）；而长走廊方向每一轮都在不断生成新的满额温度单元（$T=H$）。质心计算因此被极大地拉向深长走廊方向，使 NPC 作出“玩家一定逃向更深通道”的高智商推断。

```
【非对称 T 型路口热力分布】：
+---------+--------------------+----------------------------------+
| 死胡同  | [1] [2] [3]        | 路口中心   [8] [9] [9] [9] [9]   | 长走廊深处
| (快速熄灭)  (热值衰减)       |            =======> 质心强烈右偏 | (持续处于高热度状态)
+---------+--------------------+----------------------------------+
```

### 5.2 走廊与开阔空间自适应（Corridor vs. Open Space）
- **狭窄走廊**：每一步仅能激活极少量的单元（$\le 2\sim 3$ 个），永远无法触发 $M_H$ 阈值。热浪在上限步数 $M_S$ 耗尽前会一直沿着走廊深处奔涌，迫使 NPC 全力穿过整个通道去封堵玩家。
- **开阔大厅**：热浪一旦从走廊涌入广场，传播波前在几何级数上以辐射状爆发，新激增的单元数量瞬间击穿 $M_H$（如 $>25$）。泛洪立刻提前终止，质心直接截断在广场入口不远处。这与人类直觉完全吻合：当追踪者冲进一个到处都是掩体的大型广场时，目标逃匿可能性的熵急剧增加，盲目狂奔毫无意义，理应减速展开地毯式搜索。

---

## 6. 核心调参指南

算法的全部宏观追逐形态收敛于极少数控制变量，各参数的物理意义与生产调优建议如下：

| 参数常量 | 引擎底层作用机制 | 调大该参数的影响 | 调小该参数的影响 | 《羞辱2》产线预设值 |
| :--- | :--- | :--- | :--- | :--- |
| **初始热值<br>Hot Value ($H$)** | 控制死胡同区域单元的降温冷却半衰期。 | 死胡同单元存活时间更长，权重消散缓慢，削弱系统向长走廊偏置的敏锐度。 | 死胡同残存热量极快衰减为 0，质心以极高倾向偏向深远通道。 | **20** |
| **单步加热上限<br>Max Heated ($M_H$)** | 充当空间拓扑的**开阔度检测断路器**。 | NPC 对开阔广场的容忍度更高，会更加激进地冲入大厅中央。 | NPC 极易在进入微小型开阔区时便刹车放弃追逐，行为偏保守。 | **25** |
| **最大步数<br>Max Steps ($M_S$)** | 决定单次追逐在连通通道中能够覆盖的最大物理曼哈顿距离。作为**难度阶梯**的核心动态控制杠杆。 | 允许 NPC 狂奔更长的距离，玩家需要逃得更远才能脱战。 | 缩短 NPC 追逐的最大航程，为玩家提供更宽松的逃生窗口。 | **简单 (L1): 20<br>普通 (L2): 30<br>困难 (L3): 40** |

---

## 7. 工业落地局限性与避坑指南（Caveats & Edge Cases）

在《羞辱2》的实际开发与迭代中，该算法呈现出以下已知局限性及处理经验：

### 7.1 环境刺激响应与频繁重置陷阱
- **动态刺激打断**：泛洪算法给出的是一段确定性的位移目标。若追逐途中有外界更高优先级的听觉/视觉感知刺激注入（如玩家在侧方开枪、投掷手榴弹引爆），状态机应立即重置（Reset）并以新刺激来源为输入重新运行算法。
- **死循环追逐风险**：若环境刺激持续不断，理论上会导致 NPC 永远处于“重置-重算-未跑完-又重置”的震荡状态。工程建议：为单次追逐状态设置**有限重置计数器**或冷却定时器（Cooldown Timer），超出限制后强制锁定目标或强制转入搜索。

### 7.2 屏障单向性导致的侧向盲区（Side Exits Cut-off）
为了保证动量并防止倒退，反向半平面屏障将追逐起点后方的整个半球空间完全切断。

```
              [原本可能藏匿的侧向出口]
                     [Exit] 
                        ^
                        | 
     [屏障 Barrier]     | (无法泛洪至此处)
     -1  -1  -1  -1     | 
    --------------------+----------
      .   .   9   9   9   .   .   .
             (前向热浪) ===>
```
*现象：如图所示，若在玩家丢失点斜后方仅数米处存在一个侧向隐蔽门，阻断屏障会直接阻止热浪涌入该门，导致 NPC 永远不会推断玩家闪身进入该出口的可能。*

- **工程权衡结论**：团队曾尝试通过设置屏障最大距离包络、收缩阻断角度至钝角区间等方式进行妥协，但在各种复杂变异关卡拓扑中，任何边界放宽方案都会重新引入更致命的“180 度反向调头”漏洞。由于《羞辱2》是 3D 纵深关卡，垂直空间的多向连通性在很大程度上稀释了平面阻断带来的误判，因此团队最终维持了绝对半平面阻断的简洁设计，以代码确定性换取鲁棒性。

### 7.3 时间切片泛洪引发的运动动画颠簸（Locomotion Erraticism）
- **演进陷阱**：团队曾构想将泛洪算法以时间切片的方式铺展在多帧内运行（按玩家逃跑速度逐帧传播），以实现“追逐目标沿路径动态延伸”的效果。
- **实测缺陷**：波前在拓扑分叉处的跳跃直接导致 NPC 底层寻路组件频繁重规划（Repathing），NPC 角色在动画上表现出频繁、突兀且不合常理的急转弯，严重破坏骨骼动画根骨骼位移（Root Motion）的连贯性。
- **落地方案**：抛弃多帧渐进拟合，将整个算法在**状态触发时利用单帧计算完毕**（或分摊在极少帧内以固定预算执行），给移动系统提供一个绝对稳定且不可频繁变更的单一世界坐标目标点。

---

## 8. 扩展演化方向与架构总结

### 8.1 扩展演化方向
1. **多重战术效用融合**：当前热量传播仅考虑拓扑连通距离。可结合经典战术图分析技术，将掩体适宜度（Cover Score）、视线阴影度（Visibility Shadows）、光照暗度以及高价值资源点作为加权乘子，即：
   $$T_{final}(C) = T_{flood}(C) \cdot W_{cover}(C) \cdot W_{shadow}(C)$$
   实现 NPC 预判逃犯“最可能蹲伏在暗处掩体”的微观推断。
2. **转角顿挫感知平滑**：由于质心受大空间权重吸引，质心有时会落在转角后方的死角内，偶尔会导致 NPC 在视线即将看到转角的一瞬间突然因步数耗尽而停滞。可在追逐判定末端增加针对视野拐角的视线预测射线（Corner Peek Raycast），使转角处未果的停顿更加合理。

### 8.2 架构设计启示
《羞辱2》的追逐算法为现代游戏 AI 架构提供了一个典范级思路：**优秀的系统设计不追求复杂的全知推演，而追求对空间信息的轻量表征与行为层面的“善意欺骗”。**

通过 DSM 稀疏网格架构、双相前向阻断机制以及加权质心推导，算法在 PC、PS3 与 Xbox One 的受限硬件预算下，以近乎不可见的性能损耗，在纵横交错的立体关卡中实现了极其鲁棒且极富动态博弈乐趣的 NPC 行为。正是这种遵循拓扑与人类直觉的推导机制，使潜行玩家能够主动预测 NPC 的思考路径，从而衍生出“在必经走廊预设电击陷阱并隐入侧室”的非脚本化、自发涌现式（Emergent Gameplay）高光游戏体验。
