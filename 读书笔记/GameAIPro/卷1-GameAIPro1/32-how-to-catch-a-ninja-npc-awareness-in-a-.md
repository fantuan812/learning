---
type: Reference
title: "第32章 How to Catch a Ninja: NPC Awareness in a 2D Stealth Platformer"
description: "Game AI Pro 工业级精读：How to Catch a Ninja: NPC Awareness in a 2D Stealth Platformer。系统解析核心AI机制、设计考量、数学模型与工业级落地方案。"
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

# 第32章 How to Catch a Ninja: NPC Awareness in a 2D Stealth Platformer

> 来源：*Game AI Pro 1: Collected Wisdom of Game AI Professionals*, Chapter 32.  
> 原文作者 / 资源：[How to Catch a Ninja: NPC Awareness in a 2D Stealth Platformer](http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter32_How_to_Catch_a_Ninja_NPC_Awareness_in_a_2D_Stealth_Platformer.pdf)（全书官方免费开放获取 PDF 视觉多模态端到端重构）。  
> 核心定位：本章由 Gemini 3.8 Flash 基于 Game AI Pro 权威原版 PDF 视觉多模态精准重构，系统呈现现代商业游戏 AI 决策逻辑、代码实现与工程权衡。  
> 专栏导航：[卷1-GameAIPro1](README.md) ｜ [专栏首页](../README.md)

---

> **作者**：Brook Miles (Klei Entertainment)  
> **重构与生产实战解析**：涵盖《忍者印记》（Mark of the Ninja）核心感知管线、数据驱动的兴趣源系统（Interest System）、反向驱动检测模型、轻量级群体协同算法（Implicit Group Roles）以及状态竞争仲裁逻辑。

---

## 1. 概述与架构背景 (Introduction & Architectural Evolution)

在传统清版动作游戏（如 Klei 的前作 *Shank* 与 *Shank 2*）中，AI 架构主要围绕**可攻击目标（Attackable Targets）**展开。此类动作游戏中，敌人的决策循环极度紧凑且单一：轮询视野内的玩家实体、锁定目标、进入近身或远程作战状态，直至实体死亡。

```
[传统动作游戏 (Shank)]
NPC 轮询感知 ---> 锁定玩家目标 (Target) ---> 纯战斗循环 (Combat Loop)
```

转向 2D 潜行动作平台游戏《忍者印记》（*Mark of the Ninja*）时，这种纯战斗架构暴露出致命缺陷。潜行玩法的核心在于信息差、环境操控与猫鼠博弈：
- **多层级警戒状态（Multi-level Alertness）**：守卫必须具备从“松懈巡逻（Patrol）”、“心生怀疑（Suspicious）”、“局部搜查（Investigating）”到“全面交火（High Alert/Combat）”的平滑过渡。
- **环境因果推断（Environmental Reasoning）**：守卫需对玩家制造的声响、破损的灯具、被破坏的配电箱、倒下的同伴等非生命体事件产生合理的因果反馈。
- **击溃抱团机制（Cluster Splitting）**：系统必须支持玩家通过声东击西诱导敌军分散，而非让聚集的敌人如同蜂群般对单一微弱刺激协同蜂拥而上。

为解决上述挑战，Klei 放弃了单纯依赖 NPC 局部感知轮询的传统模式，重构并实现了一套**数据驱动的“兴趣系统（Interest System）”**。

---

## 2. 核心范式区分：目标（Targets）与兴趣（Interests）

在感知系统底层，系统将实体在空间中需要关注并产生反应的对象严格划分为两类独立抽象：

| 维度 | 目标 (Target) | 兴趣 (Interest) |
| :--- | :--- | :--- |
| **语义定义** | 必须被压制或消灭的敌对实体（如玩家） | 引发注意力转移的静态物理事件或环境异常 |
| **空间特性** | 动态运动实体，需高频执行轨迹追踪与预测 | 空间固定点（2D Point），通常在生命周期内静止 |
| **警戒级别** | 强制进入最高警戒（High Alert） | 触发低/中度警戒，引导走向搜查（Investigation） |
| **认知专注度** | 独占 NPC 大脑注意力，忽略大部分环境次级刺激 | 允许被更高优先级或同优先级的最新刺激动态打断 |
| **数量级与类型** | 极少（1~2 个玩家实体） | 庞杂多样（游戏上线时包含约 60 种不同类型） |

```
                     +----------------------------------------+
                     |         感知刺激输入 (Stimulus)        |
                     +----------------------------------------+
                                         |
                     +-------------------+--------------------+
                     |                                        |
              [敌对目标判定]                            [环境异常判定]
                     v                                        v
        +-------------------------+              +-------------------------+
        |     目标 (Target)       |              |    兴趣 (Interest)      |
        +-------------------------+              +-------------------------+
        | • 动态追踪              |              | • 静态空间点            |
        | • 强制最高戒备          |              | • 引导搜查行为          |
        | • 抑制次级感知          |              | • 允许抢占与动态仲裁    |
        | • 状态：交火/死斗       |              | • 约 60 种差异化数据源  |
        +-------------------------+              +-------------------------+
```

---

## 3. 感知管线与多模态检测几何 (Senses: Sight and Sound)

AI 的被动感知底层归结为两大基本物理通道：**视觉（Sight）** 与 **听觉（Sound）**。

```
                                  [感知系统空间检测]
                                          |
                +-------------------------+-------------------------+
                |                                                   |
                v                                                   v
        【视觉判定 (Sight)】                                 【听觉判定 (Sound)】
  • 多形态视锥体相交测试 (Vision Cone)                 • 拓扑三角网格 A* 寻路 (Sound Mesh)
  • 光照环境检测 (Dynamic Lighting)                    • 障碍物与拐角阻尼衰减计算
  • 实体间射线遮挡检测 (Raycast Line-of-Sight)         • 最大听觉半径 (Sound Radius) 剪枝
```

### 3.1 视觉判定流水线 (Sight Pipeline)

视觉判定包含三重层叠检测，任一环节失败均判定为不可视：

$$V_{\text{visible}} = \text{TestCone}(\mathbf{x}_{\text{agent}}, \mathbf{x}_{\text{source}}) \land \text{TestIllumination}(\mathbf{x}_{\text{source}}) \land \text{TestLOS}(\mathbf{x}_{\text{agent}}, \mathbf{x}_{\text{source}})$$

1. **视锥几何测试（Vision Cones Testing）**：
   - 标准视锥由眼睛基准偏移（Offset）、朝向向量（Direction Vector）、视场半角（Half-Angle $\theta$）及最大可视距离（$R_{\max}$）定义。
   - 特殊传感器支持其他几何形变：单射线（Single Ray，用于缝隙窥视）、全方位圆形（Circle，近身感知）、矩形或梯形区域（Trapezoid，守卫特定下沉通道）。
2. **光照条件检测（Illumination Test）**：
   - 检查兴趣源所依附的物体是否处于光源照射下（In Light）。
   - **例外处理**：若该 NPC 拥有夜视属性标志（`Night Vision Flag`），则绕过光照判定，可在完全黑暗中探测物体。
3. **视线阻挡射线投射（Line of Sight Raycast）**：
   - 自 NPC 眼睛锚点向目标点投射 2D 物理射线，判断是否被静态几何形体或门体碰撞遮挡。

### 3.2 基于三角网格寻路的真实听觉管线 (Sound Mesh & Acoustic Pathfinding)

与直接计算欧氏距离（Euclidean Distance）的粗糙实现不同，《忍者印记》的声波传播基于**物理连通路径**进行几何仿真。

- **离线数据烘焙**：
  - 关卡编辑器在导出阶段，利用 Jonathan Shewchuk 开发的开源网格剖分工具 **Triangle**，对关卡内部所有的**自由开放可行走/空气空间（Empty Space）**执行受约束的 Delaunay 三角化（Constrained Delaunay Triangulation），生成一张高质量的低多边形**“声音网格（Sound Mesh）”**。（同时会对实体碰撞体剖分生成反向网格，用于地图渲染）。
- **运行时声波 A\* 寻路**：
  - 当声音事件爆发时，首先通过半径 $R_{\text{sound}}$ 筛选包围盒内候选 NPC。
  - 声波不仅是球形广播，必须通过 Sound Mesh 执行从声音源点 $\mathbf{x}_{\text{sound}}$ 到守卫头部空间点 $\mathbf{x}_{\text{head}}$ 的拓扑 A\* 寻路（A\* Pathfinding）。
  - **路径阻断性**：若两点之间存在密闭墙体阻隔（拓扑不可达），则守卫即使处于几何半径内，其路径代偿值为无穷大，判定为绝对无法听见（见下图 Guard B 场景）。
  - **音频声学滤波复用**：游戏内部音频引擎（Audio System）直接复用该网格寻路输出的声波路径长度与折角，实时计算低通滤波（Low-Pass Filter）截止频率与混响遮蔽参数，实现了游戏机制与听觉音效表现的严密一致。

```
              +-----------------------------------------------------+
              |                      关卡几何边界                   |
              |                                                     |
              |     Guard A                                         |
              |       (o)                                           |
              |        ^                                            |
              |       / (声波可达)                                  |
              |      /                                              |
              |  [声音源: 奔跑]                                     |
              |       *                                             |
              |       | \                                           |
              |  +----+  \ (实心墙体阻断)     Guard C               |
              |  | 阻 |   x                    (o) 视锥 (Vision Cone)
              |  | 碍 |     Guard B             \      /            |
              |  | 墙 |       (x)                \    /             |
              |  +----+   (拓扑不可达)             \  /              |
              |                                     v               |
              |                              [未察觉但进入视野]     |
              +-----------------------------------------------------+
```

---

## 4. 兴趣源的工业级定义与黑客解法 (Data-Driven Interest Sources)

### 4.1 数据驱动配置解耦

为了避免策划每增加一个游戏事件就需要底层引擎程序员修改 AI 状态感知循环代码，团队将兴趣源抽象为完全数据驱动的模型。全剧约 60 种兴趣源（枪声、脚步、尸体、被破坏的灯具、电击陷阱等）均统一声明于脚本层：

```lua
-- 运行时声音类兴趣源定义（强音奔跑）
CreateInterestSource {
    sense             = "sound",
    priority          = INTEREST_PRIORITY_NOISE_LOUD,
    radius            = RUN_ON_LOUD_RADIUS,
    ttl               = 4 * FRAMES,          -- 存活时间 (Time-to-Live)：极短生命周期
    offset            = {0, 1.5 * TILES},
    forgetlosttarget  = true
}

-- 运行时视觉类兴趣源定义（对阴影中潜伏者的怀疑 - 嗅觉黑客实现）
CreateInterestSource {
    sense             = "sight",
    source            = "suspect",
    priority          = INTEREST_PRIORITY_SUSPECT,
    radius            = INTEREST_RADIUS_SUSPECT,
    ttl               = INTEREST_TTL_FOREVER,
    canberediscovered = true,
    noduplicateradius = 0,
    removeduplicates  = true,
    followowner       = true,
    condition         = HasAttributeTag("dog") -- 仅限警犬响应的特化标签
}
```

### 4.2 极简工程智慧：嗅觉通道的“黑客式”映射 (The "Dog Smell" Hack)

在开发守卫警犬时，玩法机制需要警犬能够在黑暗中“闻”出短距离内的玩家。
- **直觉方案**：为物理感知引擎新增第三种“气味传播与衰减（Smell Test）”管线。该方案开发成本极高，需要引入气味网格扩散模拟。
- **架构妥协方案**：直接复用“视觉（Sight）”管线。声明一个依附在玩家身上的微型视觉兴趣源，通过逻辑条件约束（`condition = HasAttributeTag("dog")`）限制其仅向警犬广播，同时赋予警犬无视光照检测的属性。**通过现有视觉管线的半径截断与标签过滤，零成本达成了“近距离短程嗅觉”的完整拟真**。

### 4.3 空间分布与认知存储 (Separation of Entity & Cognition)

系统在数据架构上严格实施“世界”与“大脑”的分离：
- **Interest Source（兴趣源）**：宿主位于游戏物理世界中。拥有空间坐标、空间判定半径（Radius）、生命周期（TTL）、感知类型（Sense Type）。
- **Interest Record（兴趣记录）**：宿主位于特定 NPC 的私有黑板/大脑（Brain Memory）中。是 NPC 感知到具体源后生成的局部认知拷贝，记录了优先级、事件类型、发现时间戳与在群体中被分配的职责（Role）。

---

## 5. 控制反转：从兴趣源反向驱动与隐式轻量级群体协同

如果由每个 NPC 的每帧决策循环（Update Loop）独立去物理空间探测兴趣，不仅计算复杂度呈 $O(N_{\text{agents}} \times N_{\text{sources}})$ 爆炸，更会导致当出现破损灯泡时，所有守卫一拥而上、齐声抱怨的滑稽场面。

《忍者印记》在此采用了经典的**控制反转架构（Inversion of Control）**：**由兴趣源作为驱动主体，主动检测周边候选 NPC，并直接在局部形成临时隐式群体（Implicit Group）。**

```
                  +-------------------------------+
                  |  SensoryManager: 更新兴趣源   |
                  +-------------------------------+
                                  |
            [空间查询：获取物理范围内潜在候选 NPC 列表]
                                  |
                                  v
                   候选 NPC 数量判定 (Candidates)
                                  |
         +------------------------+------------------------+
         |                                                 |
     [单人感知]                                       [多人群体感知]
         |                                                 |
         v                                                 v
  直接派遣前往调查                                   【仲裁分配局部角色 (Roles)】
 (Direct Investigate)                                      |
                           +-------------------------------+-------------------------------+
                           |                               |                               |
                           v                               v                               v
                    [哨兵/组长 Leader]              [调查员 Investigator]             [旁观者 Bystander]
                   • 播放口头指令音频              • 动态指定 1~2 人               • 保持原地警戒
                   • 悬停原地监督等待              • 前往异常中心实地检索          • 降低该事件在认知中的权值
                                                                                   • 预留注意力捕获新异常
```

### 5.1 隐式群体角色分配模型 (Implicit Group Role Assignment)

全局感知管理器（`SensoryManager`）每帧轮询有效活跃的兴趣源。当一个兴趣源被触发并捕获到多个合法候选人（Candidates）时，系统并不创建显式的“小队（Squad）”数据结构，而是依据几何位置、空间朝向或距离就地分派瞬时角色（Roles）：

1. **组长 / 哨兵（Group Leader / Sentry）**：
   - 算法通常选择距离兴趣源最近或朝向最正的守卫担任。
   - 行为表现：播放特定语音指令（例如：“你去那边看看，我在这盯梢”），保持当前站位，进入警戒观望状态。
2. **调查员（Investigator）**：
   - 系统动态指派 1 至 2 名守卫获得此角色。
   - 行为表现：根据兴趣源的 2D 空间点规划寻路路径，掏出武器逐步接近目标进行现场勘验与搜查动作。
3. **旁观者（Bystander）**：
   - 剩余其他感知到该事件的守卫被分配为此角色。
   - 行为表现：做出轻微的扭头反应确认察觉，随后留在原位。
   - **核心认知降权（Attention De-escalation）**：系统在旁观者大脑中主动**降低该兴趣的局部记忆优先级**，使其心智资源被迅速释放，更容易被玩家在其他方向蓄意制造的新刺激源所调动。

### 5.2 状态无缝解耦机制

角色（Role）的生命周期与具体的兴趣记录（Interest Record）严格绑定。一旦搜查结束、兴趣超时或被新的高优先级事件置换，**角色标记即刻销毁**。AI 底层不需要维护复杂的跨守卫生命周期状态机，避免了守卫死亡、掉出寻路网格后引发的小队逻辑悬挂缺陷。

---

## 6. 认知仲裁：优先级矩阵与时间抢占机制 (Prioritizing Interests)

在复杂的潜行沙盒中，NPC 随时面临多重物理刺激的冲突。

### 6.1 优先级常数表 (Priority Table)

工程实践中，各个层级的优先级数值最终收敛为一套整数仲裁系统（节选自项目最终发售配置）：

```python
# 《忍者印记》发售期最终兴趣源优先级定义
INTEREST_PRIORITY_LOWEST           = 0   # 基础最低环境扰动
INTEREST_PRIORITY_BROKEN           = 1   # 次要环境破坏（如被打破的普通吊灯、花瓶）
INTEREST_PRIORITY_MISSING          = 2   # 巡逻同伴失踪（未按时返回既定路线）
INTEREST_PRIORITY_SUSPECT          = 4   # 怀疑阴影中有异动（可疑目标边缘感知）
INTEREST_PRIORITY_SMOKE            = 4   # 烟雾弹遮蔽
INTEREST_PRIORITY_CORPSE           = 4   # 发现同伴尸体
INTEREST_PRIORITY_NOISE_QUIET      = 4   # 轻微响动（脚步声、道具落地声）
INTEREST_PRIORITY_NOISE_LOUD       = 4   # 巨大轰鸣（爆炸、破门）
INTEREST_PRIORITY_BOX              = 5   # 关键配电箱/发电机被物理损毁
INTEREST_PRIORITY_SPIKEMINE        = 5   # 发现地面穿刺地雷陷阱
INTEREST_PRIORITY_DISTRACTIONFLARE = 10  # 诱饵照明弹（高强度强制干扰源）
INTEREST_PRIORITY_TERROR           = 20  # 极度恐慌事件（如悬挂曝尸引发的神经崩溃）
```

### 6.2 “最新优先（Newest-First）”对决“绝对权重（Absolute Weight）”

在早期的设计假设中，团队认为“发现同伴尸体（`CORPSE`）”应该具备仅次于“直接看见玩家”的最高优先级。但实机对抗测试暴露了严重的逻辑死结：

```
[困境一]：守卫在调查声响途中发现尸体，切换去调查尸体，符合直觉。
[困境二 (反向)]：守卫正在调查尸体，玩家在守卫背后制造了疾步奔跑声。
                 若 CORPSE 优先级高于 NOISE，守卫将对身后的脚步声完全充耳不闻，
                 这在潜行游戏中表现得极为愚蠢且违背常识。
```

**解决方案：平级后入为主（Newest-First Arbitration）**
系统将 `CORPSE`、`NOISE_QUIET`、`NOISE_LOUD`、`SMOKE` 等核心高频交互事件全部赋予**完全相同的优先级数值（Priority = 4）**。

其仲裁数学模型遵循如下抢占规则：

设守卫当前保有的兴趣优先级为 $P_{\text{current}}$，时间戳为 $t_{\text{current}}$；新注入的候选兴趣优先级为 $P_{\text{incoming}}$，时间戳为 $t_{\text{incoming}}$：

$$\text{ShouldReplace} = 
\begin{cases} 
\text{True}, & \text{if } P_{\text{incoming}} > P_{\text{current}} \\
\text{True}, & \text{if } P_{\text{incoming}} = P_{\text{current}} \land t_{\text{incoming}} > t_{\text{current}} \\
\text{False}, & \text{if } P_{\text{incoming}} < P_{\text{current}}
\end{cases}$$

当守卫在勘验尸体（Priority 4）时，哪怕身后传来一声细微的声响（Priority 4），由于满足 $P_{\text{incoming}} \ge P_{\text{current}}$ 且时间最新，守卫会立刻警惕转身。而对于绝对重要性极高（如地雷 `SPIKEMINE`）或极低（如坏灯 `BROKEN`）的事物，则严格执行数值绝对剪枝。

---

## 7. 调查状态机、记忆抑制与再发现机制 (Investigation & Rediscoverability)

NPC 在完成一次调查后，其心理认知模型与物理实体之间必须维持严密的状态流转，防止死循环陷入。

```
              [NPC 发现兴趣源]
                     |
                     v
             [执行调查行为动作]
         (勘验尸体 / 观察破损灯泡)
                     |
                     v
             [调查完成标记判定]
                     |
         +-----------+-----------+
         |                       |
   [常规一次性对象]        [特殊长效对象: 尸体/可疑目标]
         |                       |
         v                       v
【物理世界销毁兴趣源】   【局部记忆抑制 (Suppression)】
(Interest Source 移除,   • 记录进入发现者黑名单 (Discoverer List)
 视觉实体模型保留)       • 单 NPC 终身不再对该兴趣源触发
                         • 禁止维持记忆队列/堆栈 (No Queue)
                         • 直接重置回默认巡逻状态 (Default State)
```

### 7.1 单一兴趣约束与无栈化哲学 (The Single Interest Rule & No-Stack Architecture)

系统做出了一个至关重要的工程裁剪：**每个 NPC 的大脑在同一时刻仅能持有一条兴趣（Single Interest Record），绝不引入兴趣记录队列（Queue）或记忆堆栈（Stack）。**

- **为什么不使用堆栈？**  
  设想若存在调用栈：守卫在巡逻中发现坏灯 $\rightarrow$ 发现尸体入栈 $\rightarrow$ 听到声响入栈 $\rightarrow$ 发现玩家进入交火。一旦玩家利用暗影脱战，如果守卫从栈顶向下回退回溯，他将依次：搜查声响点、再次跑回尸体旁惊呼“天哪，他死了！”、最后再跑回坏掉的灯泡前发呆。这种行为极为机械滑稽。
- **重置为默认状态（Natural Resetting）**：  
  一旦当前最高优先级的兴趣调查完毕或目标丢失，守卫的大脑直接洗净，彻底重置回其默认的行为模式（如原路巡逻）。

### 7.2 调查完成销毁与黑名单记忆机制 (Investigated State & Rediscoverability)

为了避免 NPC 行为陷入重复震荡，系统依据对象类型进行双轨处理：

1. **环境琐事彻底注销（Destroy Interest Source）**：  
   对于坏灯（`BROKEN`）等琐碎物体，一旦被分配的守卫调查完毕，该**“兴趣源（Interest Source）”将直接从物理世界管理器中解注销毁**，但灯泡破碎的模型实体保持原样。此后任何守卫经过此处都不会再触发任何停顿或自言自语。
2. **长效威胁黑名单（Discoverer History）**：  
   对于尸体等不能直接从世界销毁的重要对象，兴趣源自身在内存中维护一个**已发现者列表（Discoverers List）**。
   - 守卫 A 首次发现尸体，尸体将守卫 A 的实体 ID 压入自身的排他名单。
   - 即使此后守卫 A 因中途追击玩家而中断了调查，当他丢失目标返回后，尸体由于黑名单拦截，不会反复引起守卫 A 重新爆发惊呼。
3. **特例：高频再发现标志位（`canberediscovered = true`）**：  
   挂载在玩家身上的视觉可疑兴趣源（`SUSPECT`）是此机制的关键例外。当守卫在远处视野边缘扫过阴影中的玩家时，守卫会被吸引上前查看。这一过程必须允许反复触发，因此该兴趣源被显式标记为 `canberediscovered`，绕过已发现者黑名单机制，保证潜行距离试探机制的持续生效。

---

## 8. 基于兴趣的轻量级代理脚本编制 (Lightweight Agent Scripting)

在关卡叙事与特殊遭遇战制作中，该感知系统被策划团队反向用作了一套极其健壮的**声明式任务脚本工具（Declarative Scripting Framework）**。

```
[传统硬编码脚本 (Imperative)]
守卫.播放动画("Look_Left") ---> 强制沿路标点寻路(PathA) ---> 播放语音("Clear")
(缺陷: 途中遭遇玩家扰动极难恢复，容易产生状态死锁)

[基于兴趣的轻量化脚本 (Interest-Driven)]
策划直接注入兴趣到大脑: Guard.AddInterest(x, y, Priority=SCRIPT)
                 |
                 v
NPC 原生行为树/状态机自动接管：
• 动态避障与开门寻路
• 中途遭遇玩家投掷噪音：根据仲裁矩阵自然打断、警觉
• 恢复后若刺激结束：自动返回既定兴趣目标，无需针对性编写补丁代码
```

- **实现机制**：关卡脚本不需要强行命令 NPC 按指定路径点移动、按特定帧播放开门动画。策划只需直接向 NPC 大脑写入一条高优先级的特定兴趣记录。
- **高鲁棒性容错**：NPC 原生的寻路逻辑会自动解决开门、避障与爬梯。如果在行进途中玩家通过投掷噪音道具进行干扰，NPC 会依据通用的兴趣优先级仲裁体系，平滑地打断当前任务、转向调查噪音，并在警报解除后自主决定行为回退。整个过程无需策划针对各种中断意外编写异常恢复逻辑。
- **边界划分原则**：系统严禁将所有的环境微小交互（如单纯打开一扇门、越过一个障碍）抽象为兴趣。这些属于 NPC 内部**寻路与导向行为（Steering Behaviors）**的局部自洽行为，绝不能污染全局兴趣仲裁黑板。

---

## 9. 工业局限与抖动平滑策略 (Limitations and Improvements)

在实装测试后期，该模型暴露出的核心局限是：**高频交替刺激下的决策抖动（Flickering / Chattering）**。

### 9.1 问题成因

当玩家利用投掷道具在守卫两侧快速交替制造同等优先级的噪音时，或者兴趣源的空间位置在极短时间内微小偏移时，依据“平级后入为主（$P_{\text{incoming}} = P_{\text{current}} \land t_{\text{incoming}} > t_{\text{current}}$）”的规则，NPC 会在左右两侧极度频繁地来回转身、打断搜查动作，产生宛如痉挛的荒诞行为。

### 9.2 生产环境抗抖动平滑算法 (Dither Mitigation Filter)

为解决该边界问题，系统在 NPC 接收新兴趣的入口处追加了**局部相似度滤波器（Locality-based Similarity Filter）**。伪代码实现逻辑如下：

```cpp
bool GuardBrain::ShouldAcceptNewInterest(const InterestRecord& incoming)
{
    if (m_CurrentInterest == nullptr) {
        return true;
    }

    // 1. 严格优先级绝对压制判定
    if (incoming.priority > m_CurrentInterest->priority) {
        return true;
    }
    if (incoming.priority < m_CurrentInterest->priority) {
        return false;
    }

    // 2. 优先级相等情况下的抗抖动平滑检测 (P_incoming == P_current)
    bool isSameSense  = (incoming.senseType == m_CurrentInterest->senseType);
    bool isSameSource = (incoming.sourceType == m_CurrentInterest->sourceType);

    if (isSameSense && isSameSource) {
        // 计算新旧刺激在空间中的欧几里得距离平方
        float distSq = glm::distance2(incoming.position, m_CurrentInterest->position);

        // 如果与当前正在调查的同类兴趣物理空间距离极近，抑制状态重置
        if (distSq < THRESHOLD_REDUNDANT_DISTANCE_SQR) {
            // 吸收新空间坐标，但不打断当前的巡查行为树子节点
            m_CurrentInterest->position = incoming.position;
            m_CurrentInterest->timestamp = incoming.timestamp;
            return false; // 拦截状态打断
        }
    }

    // 3. 超过容差阈值的离散新事件，采纳时间抢占
    return incoming.timestamp > m_CurrentInterest->timestamp;
}
```

通过这一抑制机制，同类、近距离的高频物理扰动被平滑为“局部关注点的平滑位移”，从而彻底消除了潜行游戏中 NPC 频繁原地抽搐的顽疾，保证了工业级潜行 AI 所必需的拟真度与玩家心智模型（Mental Model）的合理性。

---

在 2D/3D 潜行潜伏类游戏（Stealth Action Games，如《忍者之印》（*Mark of the Ninja*））的工业级实现中，守卫 AI 的可信度与响应逻辑并非单纯依赖瞬间的视锥体（Vision Cone）判定，而是建立在**兴趣系统（Interest System）**与**感知率累加架构（Sense Detection Architecture）**之上。本文基于 Klei Entertainment 在《忍者之印》中的实战架构积累，深入剖析连续动态刺激的聚合处理、移动兴趣源的工程权衡、条件感知谓词的架构边界，并对整套数据驱动的兴趣源检测系统进行工业级复盘。

---

## 1. 连续刺激合并与动态兴趣更新机制 (Continuous Stimuli Merging & Dynamic Updates)

### 1.1 连续刺激离散化引发的系统颠簸 (State Thrashing)
在潜行环境中，玩家在地面奔跑会产生持续的脚步声脉冲。若感知系统将每一次脚步声均视作独立的“新兴趣源（New Interest Source）”，会导致以下严重的工程与体验缺陷：
1. **决策颠簸（Decision Flapping）**：AI 代理（Agent）不断被强制重置当前正在执行的“观察/警惕转向”行为，动画和转向插值出现严重抽搐。
2. **黑板（Blackboard）与优先级队列爆炸**：极短时间内生成数十个高优先级的瞬态兴趣对象，导致感知评估管线与空间分配器的内存吞吐骤增。
3. **空间定位漂移**：守卫在追逐连续声音时，由于不同离散事件的微小时间差，导致路径搜索（Pathfinding）目标点频繁失效与重新规划。

```
[ 玩家连续奔跑 Footsteps ]
   │
   ├─ 错误处理（离散事件） ──> [Interest #1] ──> [Interest #2] ──> [Interest #3] （导致状态频繁中断）
   │
   └─ 正确处理（特征融合） ──> [Existing Footstep Interest] ──> 更新空间锚点 p 与 衰减计时器 T
```

### 1.2 动态聚合算法：空间与时间锚点更新
针对连续型刺激源，《忍者之印》感知架构采用**同源复用与动态滑动窗口策略（Temporal/Spatial Merging Strategy）**。

当新的刺激信号传入时，系统在当前的活跃感知列表中按“刺激源类型（Source Category）+ 发起者 ID（Instigator GUID）”进行匹配：
- **命中已有兴趣源**：不派发新事件，直接刷新该兴趣源的生命周期计时器（Timers）并将空间坐标更新至最新刺激位置。
- **空间平滑与距离阈值判定**：若新坐标 $\mathbf{p}_{\text{new}}$ 与已有兴趣源位置 $\mathbf{p}_{\text{curr}}$ 的欧氏距离超出空间重置阈值 $R_{\text{merge}}$，则按加权平滑或分段位移处理；若在阈值内，则执行锚点重置：

$$\mathbf{p}_{\text{interest}}(t) = \mathbf{p}_{\text{stimulus}}(t)$$

$$T_{\text{decay}}(t) = T_{\text{lifespan}}$$

对于正在监听该兴趣源的守卫 AI，其内部的感知累加器（Awareness Accumulator）不会归零，而是继承当前的警觉度蓄积值并向新坐标转向，从而呈现出平滑且自然的“顺声转头追猎”体验。

---

## 2. 移动兴趣源（Moving Interest Sources）的工业困境与权衡

### 2.1 静态世界假说（The Static Position Invariant）
在《忍者之印》的系统底座中，为了保障数以百计的环境刺激能被高效推演，底层感知架构引入了一条核心前置约束：**绝大多数兴趣源在被检出并持久化后，其空间坐标 $\mathbf{p} \in \mathbb{R}^2$ 保持恒定不变**。

该设计决策在开发阶段大幅简化了系统复杂度：
- **调查路径缓存（Investigation Path Caching）**：AI 可以在兴趣源确认的第一时间向导航网格（NavMesh）提交 A* 路径请求，路径终点在整个调查周期内保持有效，无需进行昂贵的动态目标追踪（Moving Target Pursuit）重寻路。
- **群体注视收敛（Group Gaze Convergence）**：多名守卫在协同调查同一具尸体或打碎的电灯时，共享同一个静态空间兴趣锚点，避免了视觉注视方向（Look-at Vector）的不一致。

### 2.2 案例实测：移动纸箱（The Cardboard Box Failure Case）
当玩法设计引入“玩家躲在纸箱内并在移动时发出声响/引起守卫注意”的机制时，静态假设与动态玩法的冲突集中爆发：

```
[玩家在纸箱内移动]
       │
       ▼ (附加 Interest Source 到 Box Entity)
[检测点检测到 Interest] ──> 记录位置 P0 ──> 启动守卫调查脚本/行为树 (NavMesh Target: P0)
       │
       ▼ (箱子继续移动至 P1)
[守卫抵达 P0] ──> 脚本假定目标不变 ──> 调查虚空/未更新的废弃锚点 P0 ──> 产生认知穿帮
```

#### 失效机理剖析
1. **执行脚本（FSM/BT Nodes）解耦断裂**：上层行为树（Behavior Trees）与关卡脚本在 `OnInterestDetected` 触发时，通常将兴趣源的空间坐标深拷贝进本地黑板变量 `TargetLocation`。即使物理实体移动，黑板中的静态向量不会主动跟随。
2. **感知累加率（Detection Rate）空间衰减失真**：反向感知架构依赖兴趣源与 Agent 之间的距离投影计算累加速率 $\Delta A$。若兴趣源持续移动，每帧执行距离衰减重算的计算开销将打破原本为静态点优化的性能边界。

#### 架构权衡反思（Trade-offs）
在潜行游戏研发中，若要彻底支持移动兴趣源，必须引入**实体句柄引用追踪（Entity Handle Tracking）**与**动态追踪转向行为（Dynamic Steering Behaviors）**。然而《忍者之印》团队权衡了性能预算、代码侵入性及项目排期，选择不在底层感知核心中全面支持动态追踪，而是将此类情况作为特例交由高层交互逻辑处理。这清晰表明：**在工业级架构设计中，保持底层核心假设的一致性与低复杂度，往往优于为了少数边缘案例（Edge Cases）而重构整个感知拓扑。**

---

## 3. 基于条件谓词的动态感知过滤（Conditional Detection Predicates）

### 3.1 谓词机制设计原理
在项目开发后期，为了应对复杂多样的关卡特例（如：佩戴防毒面具的守卫忽略毒气陷阱、背对声源且正在操作控制台的守卫免受次级脚步声干扰等），系统在兴趣源定义中增加了条件谓词：

$$\text{CanDetect}(\text{Agent}, \text{InterestSource}) \to \{0, 1\}$$

```
兴趣源广播管线 (Interest Source Pipeline)
                 │
                 ▼
       空间范围粗筛 (Broad-phase AABB/Distance)
                 │
                 ▼
        视线/声音遮挡测试 (Raycast/Acoustic Tracing)
                 │
                 ▼
      [条件谓词求值 (Evaluate Condition)]  <── 注入业务约束
        ├── false ──> 丢弃该 Agent，不产生感知累加
        └── true  ──> 进入 Rate-based 感知蓄能器
```

### 3.2 架构隐患与反模式警示（Anti-patterns）
原书强调，该特性虽然提供了极高的灵活性，但极易沦为**坏味道（Bad Smell）的滋生地**。在生产实践中必须恪守以下边界：

| 维度 | 正确做法（Recommended Practice） | 架构反模式（Anti-Pattern） |
| :--- | :--- | :--- |
| **逻辑职责划分** | 仅用于静态属性过滤（如：守卫职业标签、特殊装备掩码）。 | 在条件谓词中嵌入复杂的状态机查询、历史上下文回溯甚至物理射线检测。 |
| **执行开销** | 复杂度必须控制在 $O(1)$，仅访问黑板或组件缓存位掩码（Bitmask）。 | 触发动态内存分配、字符串匹配或深层递归调用。 |
| **代码可维护性** | 行为树控制主体逻辑，条件谓词仅作为感知的“视神经阻断器”。 | 将本属于决策层（Behavior Layer）的业务分支倒灌进底层感知感知器（Sensory Filters）。 |

---

## 4. 《忍者之印》感知系统完整架构与生产实战

### 4.1 核心架构模式：反向感知源驱动机制 (Source-Centric Detection)
不同于传统感知系统中“每个 Agent 每帧向外发射多条射线检测世界”的 Agent 轮询范式，《忍者之印》采用**兴趣源反向更新各 Agent 蓄积值**的设计：

```
传统轮询模式:
 Agent A ───Raycasts───> [ 遍历世界物体... ]
 Agent B ───Raycasts───> [ 遍历世界物体... ]
 开销 = O(N_agents × N_world_objects)

反向源驱动模式 (Mark of the Ninja):
 [ 活跃兴趣源 (尸体/打碎的灯) ] ───更新───> 计算影响范围内的有限 Agent
                                            └──> 赋予感知率 (Assign Rates)
 开销 = O(M_active_interests × K_nearby_agents)   (其中 M << N)
```

### 4.2 警觉度累加与衰减数学模型
守卫对外界刺激的认知并非布尔状态的突变，而是一个基于连续介质物理直觉的累加过程。

设某兴趣源 $S$ 在 $t$ 时刻对守卫 $i$ 施加的感知影响率为 $R_{\text{detect}}(S, i)$，守卫当前的内部感知蓄积量为 $A_i(t) \in [0, 1.0]$：

$$A_i(t + \Delta t) = \min\left(1.0, \; A_i(t) + R_{\text{detect}}(S, i) \cdot \Delta t\right)$$

当刺激消失或守卫离开影响范围时，感知蓄积量按环境遗忘率 $\lambda_{\text{decay}}$ 指数或线性衰减：

$$A_i(t + \Delta t) = \max\left(0.0, \; A_i(t) - \lambda_{\text{decay}} \cdot \Delta t\right)$$

#### 警戒阶梯响应阈值：
- $A_i < \theta_{\text{curious}}$：无响应，保持巡逻（Patrol）。
- $\theta_{\text{curious}} \le A_i < \theta_{\text{suspicious}}$：起疑（Curious），原地驻留或缓慢转向刺激方向。
- $\theta_{\text{suspicious}} \le A_i < 1.0$：警觉（Suspicious），拔枪并沿导航网格向源位置移动排查。
- $A_i = 1.0$：发现确认（Fully Aware），进入完全战斗/告警状态（Combat/Alarm），向群体广播共享。

```
感知蓄积值 A_i
 1.0 ┌─────────────────────────────── [ 完全告警 (Full Alert) ]
     │                              ▲
     │                             ╱ (感知率 R_detect 高速累加)
 θ_2 ├────────────────────────────┼── [ 警觉排查 (Investigate) ]
     │                           ╱
 θ_1 ├──────────────────────────┼──── [ 起疑转头 (Look at / Curious) ]
     │                         ╱ 
 0.0 └────────────────────────┴────── 刺激结束，按 λ_decay 衰减归零
     0                      t_trigger
```

### 4.3 核心数据结构与检测流程伪代码 (C++ 规范)

```cpp
#include <vector>
#include <memory>
#include <cstdint>

// 空间二维向量
struct Vector2D {
    float x;
    float y;
    float DistanceSquaredTo(const Vector2D& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        return dx * dx + dy * dy;
    }
};

// 兴趣源类型枚举与优先级划分
enum class InterestPriority : uint8_t {
    Low = 0,         // 次级脚步声、微弱环境异响
    Medium = 1,      // 熄灭的灯火、破损的门窗
    High = 2,        // 同伴尸体、剧烈爆炸
    Critical = 3     // 确认目击玩家（Direct Ninja Sight）
};

// 兴趣源核心结构
struct InterestSource {
    uint32_t id;
    uint32_t categoryMask;
    Vector2D position;
    float radius;
    float baseDetectionRate;
    InterestPriority priority;
    float remainingLifespan;
    
    // 条件谓词函数指针（支持极简 Lambda 或静态规则）
    bool (*ConditionPredicate)(const class GuardAgent&, const InterestSource&);
};

// 守卫感知状态
class GuardAgent {
public:
    uint32_t agentId;
    Vector2D position;
    float currentAwareness;    // A_i 蓄积量 [0.0, 1.0]
    InterestPriority currentFocusPriority;

    bool CanProcessSense() const;
    void OnAwarenessThresholdReached(InterestPriority priority, const Vector2D& targetPos);
    void UpdateDecay(float deltaTime, float decayRate);
};

// 兴趣源管理器
class InterestManager {
private:
    std::vector<InterestSource> activeInterests;
    static constexpr float FOOTSTEP_MERGE_RADIUS_SQ = 4.0f; // 2米范围内连续脚步声合并

public:
    // 连续刺激接入与合并
    void PostOrMergeStimulus(uint32_t category, const Vector2D& pos, float rate, InterestPriority priority, float lifespan) {
        for (auto& existing : activeInterests) {
            // 同品类近距离刺激，执行锚点平滑刷新而非新增
            if (existing.categoryMask == category && 
                existing.position.DistanceSquaredTo(pos) < FOOTSTEP_MERGE_RADIUS_SQ) {
                existing.position = pos;                 // 更新空间位置
                existing.remainingLifespan = lifespan;  // 重置存活计时
                return;
            }
        }
        
        // 注册新兴趣源
        activeInterests.push_back(InterestSource{
            GenerateUniqueId(), category, pos, 10.0f, rate, priority, lifespan, nullptr
        });
    }

    // 核心管线：基于兴趣源反向更新全场 Agents 的感知累加率
    void Update(float deltaTime, std::vector<GuardAgent>& guards) {
        for (auto it = activeInterests.begin(); it != activeInterests.end(); ) {
            it->remainingLifespan -= deltaTime;
            if (it->remainingLifespan <= 0.0f) {
                it = activeInterests.erase(it);
                continue;
            }

            for (auto& guard : guards) {
                // 1. 优先级早退裁剪
                if (guard.currentFocusPriority > it->priority) {
                    continue;
                }

                // 2. 空间距离粗筛
                float distSq = guard.position.DistanceSquaredTo(it->position);
                float rangeSq = it->radius * it->radius;
                if (distSq > rangeSq) {
                    continue;
                }

                // 3. 条件谓词评估（边缘业务约束）
                if (it->ConditionPredicate && !it->ConditionPredicate(guard, *it)) {
                    continue;
                }

                // 4. 计算衰减感知率并注入守卫累加器
                float distanceFactor = 1.0f - (distSq / rangeSq);
                float effectiveRate = it->baseDetectionRate * distanceFactor;
                
                guard.currentAwareness += effectiveRate * deltaTime;
                if (guard.currentAwareness >= 1.0f) {
                    guard.currentAwareness = 1.0f;
                    guard.currentFocusPriority = it->priority;
                    guard.OnAwarenessThresholdReached(it->priority, it->position);
                }
            }
            ++it;
        }

        // 统一处理未受刺激守卫的警觉衰减
        for (auto& guard : guards) {
            guard.UpdateDecay(deltaTime, 0.15f);
        }
    }

private:
    uint32_t GenerateUniqueId();
};
```

---

## 5. 架构演化与工程复盘 (Architecture Retrospective)

### 5.1 演化里程碑与价值扩展
《忍者之印》感知系统的演化路径，体现了典型工业级游戏架构的拓展特性：

```
[ 初版立项需求 ]
处理离散静态对象：
- 发现同伴尸体 (Dead Bodies)
- 发现破损路灯 (Broken Lights)
       │
       ▼ (架构正交拓展：引入 Rate-based 与 Priority 分级)
[ 扩展能力实现 ]
- 连续移动声音合并 (Footstep Streams)
- 嗅觉/毒气扩散场响应
- 群体注视协同 (Light-weight Group Behavior)
       │
       ▼ (极大降低生产成本)
[ 成果交付 ]
- 减少关卡脚本特例处理 (Special-case Scripting 归零化)
- 策划数据驱动 (Data-Driven Configuration) 调节守卫感知敏感度
```

### 5.2 核心工业经验与最佳实践（Takeaways）

1. **坚持反向源驱动更新（Source-Centric Updates）**：在宏观世界中，刺激事件的数量在大部分时间帧内远少于场景中的 AI 代理总数。将“代理轮询环境”重构成“刺激源主动向局部范围代理投递感知率”，是保障弱算力平台或高密度同屏战斗流畅运行的核心优化手段。
2. **数据驱动优先级（Data-Driven Priorities）赋能策划**：将兴趣源的感知速率、衰减系数、有效半径及抢占优先级完全资产化（Data-Driven），使得非程序人员无需修改底层 C++ 核心，即可通过修改配置表迅速平衡“苛刻硬核”与“宽容爽快”两种完全不同的潜行难度感受。
3. **轻量级群体协同（Light-weight Emergent Group Awareness）**：系统无需构建重型黑板集群或集中式指挥官模块。一名守卫达到警戒阈值后产生的“高优先级惊呼/搜寻”行为本身，会原地生成一个次级声学兴趣源，自然触发周围其他守卫的感知累加，从而以最低的复杂度实现涌现式（Emergent）的轻量级包抄与协同。
4. **警惕边界修补引发的代码退化**：条件谓词（Condition Predicates）等救火型功能在项目收尾期极易被滥用。必须在代码审查（Code Review）中制定铁律，严禁在感知传感器层编写具体行为逻辑，确保感知（Perception）、推理（Reasoning）与执行（Execution）三层架构的清晰解耦与职责纯粹。
